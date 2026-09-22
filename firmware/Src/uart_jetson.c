#include "uart_jetson.h"
#include "system_hal.h"      /* Error_Handler() */
#include "spi_protocol.h"    /* MOSI_PAYLOAD_SIZE, SPI_MOSI_MAGIC, spi_decode_command, crc16_ccitt_false */

/* === Peripheral handles === */
extern DMA_HandleTypeDef  hdma_usart1_rx;
extern DMA_HandleTypeDef  hdma_usart1_tx;

/* === RX circular buffer + tracking === */
static uint8_t         rx_buf[JETSON_UART_RX_BUF_SIZE];
static volatile uint16_t rx_head = 0;   /* DMA 가 현재 쓰고 있는 위치 */
static uint16_t          rx_tail = 0;   /* 프로토콜 계층이 읽은 위치 */
static volatile bool     rx_idle = false;

/* === USART1 초기화: 921600 8N1, full-duplex, DMA === */
void MX_USART1_Jetson_Init(void)
{
    MX_USART1_UART_Init();
}

/* === RX DMA circular 시작 + IDLE 인터럽트 활성화 === */
void uart_jetson_start_rx(void)
{
    rx_head = 0;
    rx_tail = 0;
    rx_idle = false;

    HAL_UART_Receive_DMA(&huart_jetson, rx_buf, JETSON_UART_RX_BUF_SIZE);

    /* IDLE 인터럽트 활성화 (HAL 이 자동으로 안 켜줌) */
    __HAL_UART_ENABLE_IT(&huart_jetson, UART_IT_IDLE);
}

/* === TX: feedback DMA 전송 ===
 * 주의: HAL_UART_GetState() 는 gState(TX) | RxState 의 OR 값을 반환.
 *       RX DMA circular 모드가 활성이면 RxState=BUSY_RX 라서
 *       HAL_UART_STATE_BUSY_TX 비트와 우연히 겹쳐 항상 busy 로 오판됨.
 *       반드시 gState 만 직접 확인해야 함.
 */
bool uart_jetson_transmit_dma(const uint8_t *data, uint16_t size)
{
    if (huart_jetson.gState != HAL_UART_STATE_READY) {
        return false;
    }
    return HAL_UART_Transmit_DMA(&huart_jetson, (uint8_t *)data, size) == HAL_OK;
}

bool uart_jetson_tx_ready(void)
{
    return huart_jetson.gState == HAL_UART_STATE_READY;
}

/* === IDLE 콜백 — USART1_IRQHandler 에서 호출 === */
void uart_jetson_idle_callback(void)
{
    uint16_t ndtr = __HAL_DMA_GET_COUNTER(huart_jetson.hdmarx);
    rx_head = JETSON_UART_RX_BUF_SIZE - ndtr;
    if (rx_head >= JETSON_UART_RX_BUF_SIZE) {
        rx_head = 0;
    }
    rx_idle = true;
}

/* === RX 버퍼 접근 함수들 === */
const uint8_t *uart_jetson_rx_buffer(void)   { return rx_buf; }
uint16_t       uart_jetson_rx_tail(void)     { return rx_tail; }

/* rx_head 는 IDLE ISR 에서도 갱신되지만, 신뢰성을 위해 항상 DMA NDTR 에서
 * 직접 계산. IDLE 인터럽트가 어떤 이유로 안 와도 데이터 위치 즉시 반영. */
uint16_t uart_jetson_rx_head(void)
{
    uint16_t ndtr = (uint16_t)__HAL_DMA_GET_COUNTER(huart_jetson.hdmarx);
    uint16_t h = JETSON_UART_RX_BUF_SIZE - ndtr;
    if (h >= JETSON_UART_RX_BUF_SIZE) h = 0;
    return h;
}

void uart_jetson_rx_consume(uint16_t new_tail)
{
    rx_tail = new_tail % JETSON_UART_RX_BUF_SIZE;
}

uint16_t uart_jetson_rx_available(void)
{
    /* head 는 IDLE 인터럽트와 무관하게 항상 DMA NDTR 에서 최신 위치 가져옴 */
    uint16_t h = uart_jetson_rx_head();
    uint16_t t = rx_tail;
    if (h >= t) return h - t;
    return JETSON_UART_RX_BUF_SIZE - t + h;
}

volatile bool *uart_jetson_idle_flag_ptr(void) { return &rx_idle; }

/* ============================================================================
 * Frame parser (Step B)
 *
 * Magic + CRC 기반 command frame extraction.
 * Jetson actuator_bridge 의 parse_rx_buffer 와 mirror.
 *
 * 호출 정책: control_loop 매 tick 에서 한 번. 백로그가 있으면 한 번에 다 처리
 * (DMA 가 50Hz 보다 빠르게 burst 로 받았을 경우 대비).
 * ============================================================================ */

static volatile uint32_t frame_count_ok      = 0;
static volatile uint32_t parser_crc_errors   = 0;
static volatile uint32_t parser_resync_count = 0;

uart_frame_result_t uart_jetson_process_rx(void)
{
    uart_frame_result_t last_result = UART_FRAME_NO_DATA;

    /* 큐에 MOSI 한 프레임 이상 누적되어 있는 동안 반복 — backlog drain */
    while (uart_jetson_rx_available() >= MOSI_PAYLOAD_SIZE) {
        uint16_t avail = uart_jetson_rx_available();

        /* 1) magic 0xA55A 위치 탐색 (tail 부터, 16-bit LE) */
        bool     found = false;
        uint16_t magic_off = 0;
        for (uint16_t off = 0; off + 1 < avail; off++) {
            uint16_t p0 = (uint16_t)((rx_tail + off    ) % JETSON_UART_RX_BUF_SIZE);
            uint16_t p1 = (uint16_t)((rx_tail + off + 1) % JETSON_UART_RX_BUF_SIZE);
            uint16_t m  = (uint16_t)rx_buf[p0] | ((uint16_t)rx_buf[p1] << 8);
            if (m == SPI_MOSI_MAGIC) {
                magic_off = off;
                found = true;
                break;
            }
        }

        if (!found) {
            /* magic 없음 — 마지막 1byte 만 남기고 폐기.
             * (그 byte 가 다음 frame magic 의 첫 byte 일 수 있어서 보존) */
            uint16_t h = uart_jetson_rx_head();
            rx_tail = (uint16_t)((h + JETSON_UART_RX_BUF_SIZE - 1) % JETSON_UART_RX_BUF_SIZE);
            return last_result;
        }

        /* 2) magic 앞 garbage 가 있으면 tail 점프 */
        if (magic_off > 0) {
            rx_tail = (uint16_t)((rx_tail + magic_off) % JETSON_UART_RX_BUF_SIZE);
            parser_resync_count++;
        }

        /* 3) magic 위치부터 MOSI 한 프레임 확보됐는지 재확인 */
        if (uart_jetson_rx_available() < MOSI_PAYLOAD_SIZE) {
            return last_result;
        }

        /* 4) MOSI candidate 를 선형 버퍼로 복사 (wrap-around 처리) */
        static uint8_t candidate[MOSI_PAYLOAD_SIZE];
        for (uint16_t i = 0; i < MOSI_PAYLOAD_SIZE; i++) {
            uint16_t p = (uint16_t)((rx_tail + i) % JETSON_UART_RX_BUF_SIZE);
            candidate[i] = rx_buf[p];
        }

        /* 5) decode — spi_decode_command 가 CRC 검증 + g_robot_state 갱신 */
        decode_result_t r = spi_decode_command(candidate);
        if (r == DECODE_OK) {
            /* 정상 처리 — MOSI 한 프레임 소비 */
            rx_tail = (uint16_t)((rx_tail + MOSI_PAYLOAD_SIZE) % JETSON_UART_RX_BUF_SIZE);
            frame_count_ok++;
            last_result = UART_FRAME_OK;
            /* 다음 frame 도 큐에 있을 수 있으니 계속 */
        } else {
            /* CRC 또는 magic 실패 — 1byte 밀고 재시도.
             * spi_decode_command 가 g_robot_state.crc_error_count 도 증가시킴. */
            parser_crc_errors++;
            rx_tail = (uint16_t)((rx_tail + 1) % JETSON_UART_RX_BUF_SIZE);
            last_result = UART_FRAME_BAD_CRC;
            /* 루프 계속 — 같은 시도가 다시 일어날 수 있지만 1byte씩 밀려서 결국 빠져나옴 */
        }
    }

    return last_result;
}

uint32_t uart_jetson_frame_count(void)       { return frame_count_ok; }
uint32_t uart_jetson_crc_error_count(void)   { return parser_crc_errors; }
uint32_t uart_jetson_resync_count(void)      { return parser_resync_count; }
