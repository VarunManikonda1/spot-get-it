#include "control_loop.h"
#include "robot_state.h"
#include "spi_protocol.h"
#include "joint_control.h"
#include "telemetry.h"
#include "robot.h"
#include "system_hal.h"
#include "config.h"
#include "uart_jetson.h"
#include <stdio.h>
#include <math.h>

/* Stale 처리 — Jetson 끊김 시 단계별 degradation
 *   0   ~ WARN_MS : 정상 (fresh bit set, mode 그대로)
 *   WARN_MS ~ SAFE_MS : warn (fresh bit clear, FAULT_STALE_COMMAND set, mode 유지)
 *   SAFE_MS 이상     : safe (mode 강제 IDLE → torque off)
 *
 * 핵심: WARN ~ SAFE 사이에는 mode 강제 변경 안 함. Bridge 가 명시적으로 보낸
 * 마지막 wire mode (보통 DISABLE 또는 OPERATE) 그대로 dispatch. Bridge 자체 stale
 * 시 DISABLE 보내면 STM 의 mode 도 IDLE 자연스러움. STM 이 mode HOLD 로 강제하면
 * mode oscillation 발생 → torque 토글.
 */
#define STALE_WARN_MS          200     /* fresh bit clear, FAULT_STALE_COMMAND */
#define STALE_SAFE_MS          1000    /* mode 강제 IDLE (safe state) */
#define TEMP_LIMIT_C           70.0f   /* 서보 온도 한계 */
#define VOLTAGE_LOW_V          10.0f   /* 3S LiPo 저전압 한계 */
#define VOLTAGE_VALID_V        0.1f    /* ADC 유효 판별 최소값 */
#define DEBUG_PRINT_MS         1000    /* 디버그 출력 주기 */

/* UART feedback TX buffer (MISO 261B, DMA용 — 스택 X) */
static uint8_t uart_tx_buffer[MISO_PAYLOAD_SIZE];

/* === Helper: torque 자동 ON ===
 * Jetson 측에서 flag 를 사용하지 않기로 결정 (RL/stand 모두 flags=0 송신).
 * Wire mode 가 OPERATE 면 dispatch 가 알아서 torque on 한다.
 * Torque off 는 MODE_IDLE / MODE_CALIBRATION dispatch 에서 처리.
 * E-STOP 은 STM 내부 트리거 (UART ESC / safety_check / 향후 sit-down) 로만 발동.
 */
static void ensure_torque_on(void) {
    if (!g_robot_state.torque_enabled) {
        robot_torque_on_all();
        g_robot_state.torque_enabled = true;
        g_robot_state.status |= STATUS_BIT_TORQUE_ON;
        /* 토크 ON 시점에 현재 position을 prev_target으로 캡처 → 점프 방지 */
        joint_control_capture_current_as_prev();
    }
}

/* === Helper: stale 체크 ===
 * 2단계 degradation. mode oscillation 방지 위해 WARN 단계엔 mode 유지.
 * SAFE 단계에서만 강제 IDLE → torque off.
 */
static void check_stale(uint32_t now_ms) {
    uint32_t age = now_ms - g_robot_state.last_cmd_time_ms;

    if (age > STALE_WARN_MS) {
        /* 200ms+ : fresh bit clear + fault 표시. mode 는 그대로 유지. */
        g_robot_state.status &= ~STATUS_BIT_CMD_FRESH;
        if (g_robot_state.fault_code == FAULT_OK) {
            g_robot_state.fault_code = FAULT_STALE_COMMAND;
        }
    }

    if (age > STALE_SAFE_MS) {
        /* 1s+ : 명백한 disconnect. 강제 안전 상태 (mode IDLE → torque off). */
        g_robot_state.mode = MODE_IDLE;
    }
}

/* === Helper: 안전 검사 ===
 *
 * Fault 처리 정책:
 *  - Transient fault (SAFETY, TEMP, VOLTAGE, IMU, STALE, SERVO_TIMEOUT):
 *    매 cycle 재평가. 조건 해소되면 자동 FAULT_OK로 복귀.
 *  - Sticky fault (CRC_ERROR, NAN_IN_TARGET): 한 번 set 되면 유지.
 *    명시적 reset 또는 다음 transient fault 가 더 우선시되면 변경 가능.
 *
 * Priority (높은 게 우선): SAFETY > NAN > TEMP > VOLTAGE > IMU > STALE > SERVO_TMO > CRC
 */
static void check_safety(void) {
    fault_code_t new_fault = FAULT_OK;

    /* 1. 자세 한계 (pitch/roll) — 가장 위험, 즉시 torque OFF */
    if (!safety_check(g_robot_state.imu.pitch, g_robot_state.imu.roll)) {
        new_fault = FAULT_SAFETY_LIMIT;
        g_robot_state.status |= STATUS_BIT_IN_SAFE_STATE;
        robot_torque_off_all();
        g_robot_state.torque_enabled = false;
        g_robot_state.status &= ~STATUS_BIT_TORQUE_ON;
        g_robot_state.mode = MODE_IDLE;
        g_robot_state.fault_code = new_fault;
        return;
    } else {
        g_robot_state.status &= ~STATUS_BIT_IN_SAFE_STATE;
    }

    /* 2. 서보 온도 한계 — hysteresis 적용 (연속 N cycle 임계치 초과해야 fault).
     *   서보 telemetry UART 가 가끔 1 byte 노이즈로 가짜 high temp 읽음.
     *   실제 과열은 초 단위로 천천히 올라가니까 N=3 cycle (60ms) 으로 충분히 잡힘. */
    #define TEMP_HIGH_HYSTERESIS_CYCLES  3
    static uint8_t temp_high_streak = 0;
    if (new_fault == FAULT_OK) {
        bool any_too_hot = false;
        for (int i = 0; i < NUM_JOINTS; i++) {
            if (g_robot_state.temperature[i] > TEMP_LIMIT_C) {
                any_too_hot = true;
                break;
            }
        }
        if (any_too_hot) {
            if (temp_high_streak < 0xFF) temp_high_streak++;
            if (temp_high_streak >= TEMP_HIGH_HYSTERESIS_CYCLES) {
                new_fault = FAULT_TEMP_HIGH;
            }
        } else {
            temp_high_streak = 0;
        }
    }

    /* 3. 전압 한계 (ADC가 유효한 경우만) — 동일 hysteresis 패턴 */
    #define VOLTAGE_LOW_HYSTERESIS_CYCLES  3
    static uint8_t volt_low_streak = 0;
    if (new_fault == FAULT_OK
        && g_robot_state.bus_voltage > VOLTAGE_VALID_V
        && g_robot_state.bus_voltage < VOLTAGE_LOW_V) {
        if (volt_low_streak < 0xFF) volt_low_streak++;
        if (volt_low_streak >= VOLTAGE_LOW_HYSTERESIS_CYCLES) {
            new_fault = FAULT_VOLTAGE_LOW;
        }
    } else {
        volt_low_streak = 0;
    }

    /* Sticky fault (CRC, NAN) 는 보존 — transient 조건 다 해소돼도 OK로 안 돌림.
     * 다만 새 transient fault 가 생기면 그게 우선. */
    fault_code_t cur = g_robot_state.fault_code;
    bool cur_is_sticky = (cur == FAULT_CRC_ERROR || cur == FAULT_NAN_IN_TARGET);
    if (cur_is_sticky && new_fault == FAULT_OK) {
        /* sticky 유지 */
    } else {
        g_robot_state.fault_code = new_fault;
    }
}

/* === Helper: 모드 디스패치 === */
static void dispatch_mode(void) {
    switch (g_robot_state.mode) {
        case MODE_IDLE:
            /* 토크 OFF 유지 */
            if (g_robot_state.torque_enabled) {
                robot_torque_off_all();
                g_robot_state.torque_enabled = false;
                g_robot_state.status &= ~STATUS_BIT_TORQUE_ON;
            }
            break;

        case MODE_POSITION:
            /* Jetson 이 OPERATE 명령 보냄 → 자동 torque on */
            ensure_torque_on();
            if (!joint_control_apply_target()) {
                /* NaN 또는 servo timeout → hold fallback */
                joint_control_hold();
            }
            break;

        case MODE_HOLD:
            /* stale fallback — Jetson 짧게 끊겨도 마지막 자세 유지하면서 자세 안 무너지게 */
            ensure_torque_on();
            joint_control_hold();
            break;

        case MODE_CALIBRATION:
            /* 토크 OFF + 측정만 (telemetry는 계속 갱신) */
            g_robot_state.status |= STATUS_BIT_CALIBRATING;
            if (g_robot_state.torque_enabled) {
                robot_torque_off_all();
                g_robot_state.torque_enabled = false;
                g_robot_state.status &= ~STATUS_BIT_TORQUE_ON;
            }
            break;

        default:
            /* 알 수 없는 모드 → IDLE 강제 */
            g_robot_state.mode = MODE_IDLE;
            break;
    }
}

/* === 50Hz 메인 루프 === */
void control_loop_run(void) {
    /* 초기화 */
    robot_state_init();

    /* 부팅 직후: torque OFF, IDLE 모드 */
    robot_torque_off_all();
    g_robot_state.torque_enabled = false;
    g_robot_state.mode = MODE_IDLE;

    /* 첫 telemetry read → prev_target_rad 초기화 */
    telemetry_update_all();
    joint_control_capture_current_as_prev();

    /* last_cmd_time_ms 초기화 — 부팅 직후 즉시 stale 발동 방지 */
    g_robot_state.last_cmd_time_ms = HAL_GetTick();

    printf("\r\n=== Control loop started (50Hz, UART) ===\r\n");

    /* UART RX 시작 — DMA circular + IDLE 인터럽트 */
    uart_jetson_start_rx();

    /* 첫 feedback 한 번 송신 (Jetson 이 통신 시작 인지하기 쉽게) */
    spi_encode_feedback(uart_tx_buffer);
    uart_jetson_transmit_dma(uart_tx_buffer, MISO_PAYLOAD_SIZE);

    /* 50Hz 루프 */
    uint32_t next_tick = HAL_GetTick();
    uint32_t last_print = 0;

    /* === 진단: cycle time 측정 ===
     * 20ms 초과한 cycle 수 + 최대 cycle 시간 1초마다 reset 후 출력.
     */
    uint32_t worst_cycle_ms = 0;
    uint32_t cycle_over_20 = 0;
    uint32_t cycle_count   = 0;

    /* === 진단: TX 실패 카운터 === */
    uint32_t tx_busy_count = 0;

    while (1) {
        uint32_t t_start = HAL_GetTick();

        /* ESC 체크 (USART2 ST-Link VCP 콘솔) */
        if (check_esc()) emergency_stop();

        /* 1. UART RX 처리 — 큐에 쌓인 command frame 모두 디코드 */
        (void)uart_jetson_process_rx();

        /* 2. Stale check */
        check_stale(t_start);

        /* 3. Telemetry (서보 12ch + IMU) */
        telemetry_update_all();

        /* 4. Safety (자세/온도/전압) */
        check_safety();

        /* 5. Mode dispatch */
        dispatch_mode();

        /* 6. Feedback encode + UART TX */
        spi_encode_feedback(uart_tx_buffer);
        if (!uart_jetson_transmit_dma(uart_tx_buffer, MISO_PAYLOAD_SIZE)) {
            /* TX DMA busy — 이전 송신 아직 진행 중. 이번 cycle skip.
             * 50Hz × 261B @ 921600 = 약 14% bus utilization → 거의 발생 안 함. */
            tx_busy_count++;
        }

        /* 7. 디버그 출력 (1초마다) */
        if (t_start - last_print >= DEBUG_PRINT_MS) {
            last_print = t_start;

            float max_temp = 0.0f;
            int   max_temp_idx = 0;
            for (int i = 0; i < NUM_JOINTS; i++) {
                if (g_robot_state.temperature[i] > max_temp) {
                    max_temp = g_robot_state.temperature[i];
                    max_temp_idx = i;
                }
            }

            /* UART 카운터 — 1초간 delta 계산 */
            static uint32_t prev_frame = 0, prev_crc = 0, prev_resync = 0, prev_tx_busy = 0;
            uint32_t cur_frame  = uart_jetson_frame_count();
            uint32_t cur_crc    = uart_jetson_crc_error_count();
            uint32_t cur_resync = uart_jetson_resync_count();
            uint32_t d_frame    = cur_frame  - prev_frame;
            uint32_t d_crc      = cur_crc    - prev_crc;
            uint32_t d_resync   = cur_resync - prev_resync;
            uint32_t d_tx_busy  = tx_busy_count - prev_tx_busy;
            prev_frame   = cur_frame;
            prev_crc     = cur_crc;
            prev_resync  = cur_resync;
            prev_tx_busy = tx_busy_count;

            printf("[%lu] mode=%u st=0x%02X fault=%u torque=%u seq=%u "
                   "rx_frames=%lu/s crc_err=%lu/s resync=%lu/s tx_busy=%lu/s "
                   "maxT=%.0fC(j%d) Vbus=%.1fV "
                   "worst_cyc=%lums over20=%lu/%lu\r\n",
                   (unsigned long)t_start,
                   (unsigned)g_robot_state.mode,
                   (unsigned)g_robot_state.status,
                   (unsigned)g_robot_state.fault_code,
                   (unsigned)g_robot_state.torque_enabled,
                   (unsigned)g_robot_state.cmd_seq,
                   (unsigned long)d_frame,
                   (unsigned long)d_crc,
                   (unsigned long)d_resync,
                   (unsigned long)d_tx_busy,
                   (double)max_temp, max_temp_idx,
                   (double)g_robot_state.bus_voltage,
                   (unsigned long)worst_cycle_ms,
                   (unsigned long)cycle_over_20,
                   (unsigned long)cycle_count);

            /* 1초마다 리셋 (직전 1초 통계) */
            worst_cycle_ms = 0;
            cycle_over_20 = 0;
            cycle_count = 0;
        }

        /* 8. 주기 유지 (50Hz = 20ms) — cycle time 측정 후 delay */
        uint32_t cycle_body_ms = HAL_GetTick() - t_start;
        if (cycle_body_ms > worst_cycle_ms) worst_cycle_ms = cycle_body_ms;
        if (cycle_body_ms > 20) cycle_over_20++;
        cycle_count++;

        next_tick += LOOP_PERIOD_MS;
        uint32_t now = HAL_GetTick();
        if (now < next_tick) {
            HAL_Delay(next_tick - now);
        } else {
            /* deadline miss — 다음 tick 재정렬 */
            next_tick = now;
        }
    }
}
