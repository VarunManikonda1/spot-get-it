#ifndef UART_JETSON_H
#define UART_JETSON_H

#include "usart.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define huart_jetson huart1
extern DMA_HandleTypeDef  hdma_usart1_rx;
extern DMA_HandleTypeDef  hdma_usart1_tx;

/*
 * USART1 Jetson 통신 — SPI1 대체
 *
 * 핀:  PB6  (TX, AF7)  →  Jetson RX  (PA9/PA10 USB OTG 충돌 회피)
 *      PB7  (RX, AF7)  ←  Jetson TX
 * 설정: 921600 8N1, no flow control
 * DMA:  RX = DMA2 Stream2 Ch4 (circular)
 *       TX = DMA2 Stream7 Ch4 (normal)
 *
 * RX 수신 전략:
 *   DMA circular 모드로 rx_buf 에 연속 수신.
 *   UART IDLE 인터럽트로 "Jetson 이 보내기 끝남" 감지.
 *   IDLE 콜백에서 DMA NDTR 읽어 head 위치 갱신 → 프로토콜 계층이 파싱.
 */

#define JETSON_UART_BAUDRATE     921600
#define JETSON_UART_RX_BUF_SIZE  512

void MX_USART1_Jetson_Init(void);

void uart_jetson_start_rx(void);

bool uart_jetson_transmit_dma(const uint8_t *data, uint16_t size);
bool uart_jetson_tx_ready(void);

/* IDLE ISR 에서 호출 — rx_idle_flag set + head 위치 snapshot */
void uart_jetson_idle_callback(void);

/* RX 버퍼 접근 (프로토콜 계층용) */
const uint8_t *uart_jetson_rx_buffer(void);
uint16_t       uart_jetson_rx_head(void);
void           uart_jetson_rx_consume(uint16_t new_tail);
uint16_t       uart_jetson_rx_tail(void);
uint16_t       uart_jetson_rx_available(void);

/* IDLE 플래그 — ISR 에서 set, 메인 루프에서 clear */
volatile bool *uart_jetson_idle_flag_ptr(void);

/* === Frame parser (Step B) ===
 *
 * RX circular buffer 에서 magic + CRC 기반 command frame 추출.
 * Jetson actuator_bridge 의 parse_rx_buffer 와 mirror 알고리즘:
 *   1. SPI_MOSI_MAGIC (0xA55A) 위치 탐색
 *   2. MOSI_PAYLOAD_SIZE 모이면 candidate 추출 (wrap-around 처리)
 *   3. CRC 검증 → 성공 시 spi_decode_command() 호출, MOSI 한 프레임 소비
 *   4. 실패 시 1byte 밀고 재동기화
 *
 * Control loop 매 tick (50Hz) 에서 호출. 한 번 호출에 큐에 쌓인 모든
 * frame 을 처리 (backlog drain). 진단 카운터 별도 노출.
 */
typedef enum {
    UART_FRAME_OK = 0,        /* 직전 시도가 frame 한 개 이상 정상 처리 */
    UART_FRAME_NO_DATA,       /* magic 못 찾았거나 MOSI 한 프레임 미달 */
    UART_FRAME_BAD_CRC,       /* CRC 실패 → 1byte 밀고 재시도 중 */
} uart_frame_result_t;

uart_frame_result_t uart_jetson_process_rx(void);

uint32_t uart_jetson_frame_count(void);       /* 누적 정상 frame 수 */
uint32_t uart_jetson_crc_error_count(void);   /* 누적 CRC 실패 수 */
uint32_t uart_jetson_resync_count(void);      /* 누적 magic 재동기화 수 (garbage skip) */

#ifdef __cplusplus
}
#endif

#endif /* UART_JETSON_H */
