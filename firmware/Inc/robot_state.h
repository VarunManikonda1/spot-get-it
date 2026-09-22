#ifndef ROBOT_STATE_H
#define ROBOT_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "imu_bno055.h"

#define NUM_JOINTS 12

typedef enum {
    MODE_IDLE        = 0,
    MODE_POSITION    = 1,
    MODE_CALIBRATION = 2,
    MODE_HOLD        = 3,
} control_mode_t;

typedef enum {
    FAULT_OK            = 0,
    FAULT_IMU_READ_FAIL = 1,
    FAULT_SERVO_TIMEOUT = 2,
    FAULT_CRC_ERROR     = 3,
    FAULT_STALE_COMMAND = 4,
    FAULT_TEMP_HIGH     = 5,
    FAULT_VOLTAGE_LOW   = 6,
    FAULT_SAFETY_LIMIT  = 7,
    FAULT_NAN_IN_TARGET = 8,
} fault_code_t;

#define STATUS_BIT_TORQUE_ON      (1 << 0)
#define STATUS_BIT_IMU_OK         (1 << 1)
#define STATUS_BIT_ALL_SERVOS_OK  (1 << 2)
#define STATUS_BIT_CMD_FRESH      (1 << 3)
#define STATUS_BIT_IN_SAFE_STATE  (1 << 4)
#define STATUS_BIT_CALIBRATING    (1 << 5)

typedef struct {
    /* 서보 측정값 */
    float position_rad[NUM_JOINTS];
    float velocity_rad_s[NUM_JOINTS];
    float load[NUM_JOINTS];
    float temperature[NUM_JOINTS];

    /* IMU */
    body_attitude_t imu;

    /* 명령 (Jetson UART RX에서 갱신) */
    float target_rad[NUM_JOINTS];
    float max_delta_rad[NUM_JOINTS];
    control_mode_t mode;
    uint8_t flags;
    uint16_t cmd_seq;
    uint32_t last_cmd_time_ms;
    float gait_phase;           /* Jetson에서 수신 → echo */
    uint32_t gait_cycle_count;  /* Jetson에서 수신 → echo */
    uint8_t motion_state;       /* Jetson에서 수신 → echo */

    /* 내부 추적 */
    float prev_target_rad[NUM_JOINTS];

    /* 상태 */
    uint8_t status;
    fault_code_t fault_code;
    float bus_voltage;
    bool torque_enabled;

    /* 통신 통계 */
    uint32_t crc_error_count;
    uint32_t spi_timeout_count;
} robot_state_t;

extern volatile robot_state_t g_robot_state;

void robot_state_init(void);

#endif /* ROBOT_STATE_H */
