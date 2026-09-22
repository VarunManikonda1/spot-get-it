#include "telemetry.h"
#include "robot_state.h"
#include "servo_sts3215.h"
#include "robot.h"
#include "imu_bno055.h"
#include "system_hal.h"
#include "config.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/*
 * raw position (0~4095) → rad (canonical convention).
 * JOINT_SIGN/JOINT_ZERO_POS 적용 — target_rad와 같은 부호 체계로 통일.
 * (안 그러면 capture_current_as_prev 시점에 FR/RR (sign=-1) prev가 반대 부호로
 *  잡혀서 첫 명령이 잘못된 방향으로 튐 → "기지개" 현상 발생)
 */
static inline float position_raw_to_rad(uint16_t raw, int joint_idx) {
    return (float)JOINT_SIGN[joint_idx]
         * ((float)raw - (float)JOINT_ZERO_POS[joint_idx])
         * (2.0f * (float)M_PI / 4096.0f);
}

/*
 * STS3215 present speed: sign-magnitude step/s -> rad/s.
 *
 * 0.732 RPM is the value for 50 step/s. Applying 0.732 RPM per raw count
 * over-scales feedback velocity by 50x and drives the RL observation outside
 * the training distribution.
 */
static inline float speed_raw_to_rad_s(int16_t raw, int joint_idx) {
    return (float)JOINT_SIGN[joint_idx]
         * (float)raw * (2.0f * (float)M_PI / 4096.0f);
}

/* load raw → -1.0 ~ +1.0 정규화 */
static inline float load_raw_to_normalized(int16_t raw) {
    float norm = (float)raw / 1000.0f;
    if (norm > 1.0f) norm = 1.0f;
    if (norm < -1.0f) norm = -1.0f;
    return norm;
}

bool telemetry_read_all_servos(void) {
    bool all_ok = true;

    for (int l = 0; l < NUM_LEGS; l++) {
        for (int j = 0; j < SERVOS_PER_LEG; j++) {
            int idx = l * SERVOS_PER_LEG + j;

            sts_full_state_t s = sts_read_full_state(legs[l].huart,
                                                     legs[l].servo_ids[j]);
            if (!s.ok) {
                all_ok = false;
                continue;
            }

            g_robot_state.position_rad[idx]   = position_raw_to_rad(s.position, idx);
            g_robot_state.velocity_rad_s[idx] = speed_raw_to_rad_s(s.speed, idx);
            g_robot_state.load[idx]           = load_raw_to_normalized(s.load);
            g_robot_state.temperature[idx]    = (float)s.temperature_C;

            if (idx == NUM_JOINTS - 1) {
                g_robot_state.bus_voltage = (float)s.voltage_dV * 0.1f;
            }
        }
    }

    /* Hysteresis — 1-cycle glitch (UART byte noise) 무시.
     * 연속 N=3 cycle (60ms) 실패해야 status bit clear. 진짜 서보 disconnect 는
     * 60ms 안에 검출 (느리지 않음). bridge 로 전달되는 status 가 안정됨. */
    static uint8_t servo_fail_streak = 0;
    if (all_ok) {
        servo_fail_streak = 0;
        g_robot_state.status |= STATUS_BIT_ALL_SERVOS_OK;
    } else {
        if (servo_fail_streak < 0xFF) servo_fail_streak++;
        if (servo_fail_streak >= 3) {
            g_robot_state.status &= ~STATUS_BIT_ALL_SERVOS_OK;
        }
    }

    return all_ok;
}

bool telemetry_read_imu(void) {
    body_attitude_t tmp;
    bool ok = bno055_read_body(&hi2c1, &tmp);

    /* Hysteresis — I2C glitch 1-cycle 무시. 3 cycle 연속 실패해야 clear. */
    static uint8_t imu_fail_streak = 0;
    if (ok) {
        g_robot_state.imu = tmp;
        imu_fail_streak = 0;
        g_robot_state.status |= STATUS_BIT_IMU_OK;
    } else {
        if (imu_fail_streak < 0xFF) imu_fail_streak++;
        if (imu_fail_streak >= 3) {
            g_robot_state.status &= ~STATUS_BIT_IMU_OK;
        }
    }

    return ok;
}

bool telemetry_update_all(void) {
    bool servo_ok = telemetry_read_all_servos();
    bool imu_ok = telemetry_read_imu();
    return servo_ok && imu_ok;
}
