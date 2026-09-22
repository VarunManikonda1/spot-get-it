#include "joint_control.h"
#include "robot_state.h"
#include "servo_sts3215.h"
#include "robot.h"
#include "config.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define RAW_PER_RAD (4096.0f / (2.0f * (float)M_PI))

static uint16_t joint_rad_to_raw(float rad, int joint_idx) {
    float raw_offset = rad * RAW_PER_RAD;
    int raw = (int)JOINT_ZERO_POS[joint_idx]
            + (int)((float)JOINT_SIGN[joint_idx] * raw_offset);

    if (raw < 0) raw = 0;
    if (raw > 4095) raw = 4095;

    return (uint16_t)raw;
}

static bool has_nan_in_targets(void) {
    for (int i = 0; i < NUM_JOINTS; i++) {
        if (!isfinite(g_robot_state.target_rad[i])) return true;
    }
    return false;
}

static void apply_limits(float *applied) {
    for (int i = 0; i < NUM_JOINTS; i++) {
        float target = g_robot_state.target_rad[i];
        float prev = g_robot_state.prev_target_rad[i];
        float max_delta = g_robot_state.max_delta_rad[i];

        float delta = target - prev;
        if (delta > max_delta) delta = max_delta;
        if (delta < -max_delta) delta = -max_delta;
        float limited = prev + delta;

        if (limited < JOINT_MIN_RAD[i]) limited = JOINT_MIN_RAD[i];
        if (limited > JOINT_MAX_RAD[i]) limited = JOINT_MAX_RAD[i];

        applied[i] = limited;
    }
}

bool joint_control_apply_target(void) {
    if (has_nan_in_targets()) {
        g_robot_state.fault_code = FAULT_NAN_IN_TARGET;
        return false;
    }

    float applied_rad[NUM_JOINTS];
    apply_limits(applied_rad);

    for (int l = 0; l < NUM_LEGS; l++) {
        uint16_t goals[SERVOS_PER_LEG];
        for (int j = 0; j < SERVOS_PER_LEG; j++) {
            int joint_idx = l * SERVOS_PER_LEG + j;
            goals[j] = joint_rad_to_raw(applied_rad[joint_idx], joint_idx);
        }

        bool ok = sts_sync_write_goal(legs[l].huart,
                                      legs[l].servo_ids,
                                      goals,
                                      SERVOS_PER_LEG);
        if (!ok) {
            g_robot_state.fault_code = FAULT_SERVO_TIMEOUT;
        }
    }

    for (int i = 0; i < NUM_JOINTS; i++) {
        g_robot_state.prev_target_rad[i] = applied_rad[i];
    }

    return true;
}

bool joint_control_hold(void) {
    for (int i = 0; i < NUM_JOINTS; i++) {
        g_robot_state.target_rad[i] = g_robot_state.prev_target_rad[i];
    }
    return joint_control_apply_target();
}

void joint_control_capture_current_as_prev(void) {
    for (int i = 0; i < NUM_JOINTS; i++) {
        g_robot_state.prev_target_rad[i] = g_robot_state.position_rad[i];
    }
}
