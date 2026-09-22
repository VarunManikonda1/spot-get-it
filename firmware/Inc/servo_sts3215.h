#ifndef SERVO_STS3215_H
#define SERVO_STS3215_H

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/*
 * Feetech STS3215 servo bus protocol.
 * 가정: HDSEL UART 1Mbps, 1 wire half-duplex. UART은 미리 HAL_HalfDuplex_Init되어 있어야 함.
 * 모든 함수는 blocking. 응답 검증 후 결과 반환.
 */

typedef struct {
    bool ok;
    uint16_t position;
    uint16_t load;
} sts_read_result_t;

typedef struct {
    bool ok;
} sts_write_result_t;

/* 외부에서 sts_write_byte로 직접 register 쓰기 위해 노출 */
#define STS_REG_TORQUE_ENABLE       0x28
#define STS_REG_GOAL_POSITION       0x2A
#define STS_REG_PRESENT_POSITION    0x38

/* 0x38부터 8 byte read: position, speed, load, voltage, temperature */
typedef struct {
    bool ok;
    uint16_t position;        /* raw 0~4095 */
    int16_t  speed;           /* raw, signed */
    int16_t  load;            /* signed (sts_load_to_signed 적용 완료) */
    uint8_t  voltage_dV;      /* 0.1V 단위 */
    uint8_t  temperature_C;   /* 섭씨 */
} sts_full_state_t;

bool                  sts_ping(UART_HandleTypeDef *huart, uint8_t id);
sts_read_result_t     sts_read_state(UART_HandleTypeDef *huart, uint8_t id);
sts_full_state_t      sts_read_full_state(UART_HandleTypeDef *huart, uint8_t id);
sts_write_result_t    sts_write_byte(UART_HandleTypeDef *huart, uint8_t id, uint8_t addr, uint8_t val);
bool                  sts_sync_write_goal(UART_HandleTypeDef *huart,
                                          const uint8_t *ids, const uint16_t *goals, uint8_t count);

/* speed 레지스터: bit15=방향, bit14~0=크기 → 부호 있는 값으로 변환 */
int16_t               sts_speed_to_signed(uint16_t raw);

/* load 레지스터: bit10=방향, bit9~0=크기 → 부호 있는 값으로 변환 */
int16_t               sts_load_to_signed(uint16_t raw);

#endif /* SERVO_STS3215_H */
