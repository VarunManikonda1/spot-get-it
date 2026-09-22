#include "robot.h"
#include "leg_ik.h"
#include "system_hal.h"
#include "config.h"
#include <stdio.h>
#include <math.h>

/* === 다리 구성 (UART 매핑은 system_hal.h의 huart3/4/5/6 참조) === */
leg_t legs[NUM_LEGS] = {
    { &huart6, "FL", { 1,  2,  3}, +1 },
    { &huart4, "FR", { 4,  5,  6}, -1 },
    { &huart5, "RL", { 7,  8,  9}, +1 },
    { &huart3, "RR", {10, 11, 12}, -1 }
};

float foot_pos[NUM_LEGS][2];
volatile bool g_abort = false;

/* === 내부 helper === */
static int abs_int(int x) { return x < 0 ? -x : x; }

static float local_smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

/* === Pose / Transition === */

void robot_update_pose_from_foot(pose_t pose) {
    for (int l = 0; l < NUM_LEGS; l++) {
        int8_t s = legs[l].sign;
        int t_off, k_off;
        ik_2link_to_raw(L1_THIGH_MM, L2_SHIN_MM,
                        foot_pos[l][0], foot_pos[l][1],
                        &t_off, &k_off);
        pose[l][0] = ZERO_POS + s * HIP_OFFSET;
        pose[l][1] = (uint16_t)(ZERO_POS + s * t_off);
        pose[l][2] = (uint16_t)(ZERO_POS + s * k_off);
    }
}

void robot_apply_pose(const pose_t pose) {
    for (int l = 0; l < NUM_LEGS; l++) {
        sts_sync_write_goal(legs[l].huart, legs[l].servo_ids,
                            pose[l], SERVOS_PER_LEG);
    }
}

void robot_transition(const pose_t from, const pose_t to, int speed_dps) {
    int max_delta = 0;
    for (int l = 0; l < NUM_LEGS; l++) {
        for (int j = 0; j < SERVOS_PER_LEG; j++) {
            int d = abs_int((int)to[l][j] - (int)from[l][j]);
            if (d > max_delta) max_delta = d;
        }
    }
    if (max_delta == 0) return;

    int speed_unit_per_sec = speed_dps * 4096 / 360;
    uint32_t total_ms = (uint32_t)max_delta * 1000 / (uint32_t)speed_unit_per_sec;
    if (total_ms < MIN_TRANSITION_MS) total_ms = MIN_TRANSITION_MS;

    int steps = (int)(total_ms / STEP_MS);
    if (steps < 4) steps = 4;

    for (int step = 1; step <= steps; step++) {
        if (check_esc()) emergency_stop();
        float linear_t = (float)step / steps;
        float ratio = local_smoothstep(linear_t);
        pose_t goals;
        for (int l = 0; l < NUM_LEGS; l++) {
            for (int j = 0; j < SERVOS_PER_LEG; j++) {
                int delta = (int)to[l][j] - (int)from[l][j];
                goals[l][j] = (uint16_t)((int)from[l][j] + (int)(delta * ratio));
            }
        }
        robot_apply_pose(goals);
        HAL_Delay(STEP_MS);
    }
}

/* === Torque / Ping === */

void robot_torque_off_all(void) {
    for (int l = 0; l < NUM_LEGS; l++) {
        for (int j = 0; j < SERVOS_PER_LEG; j++) {
            sts_write_byte(legs[l].huart, legs[l].servo_ids[j],
                           STS_REG_TORQUE_ENABLE, 0);
        }
    }
}

void robot_torque_on_all(void) {
    for (int l = 0; l < NUM_LEGS; l++) {
        for (int j = 0; j < SERVOS_PER_LEG; j++) {
            sts_write_byte(legs[l].huart, legs[l].servo_ids[j],
                           STS_REG_TORQUE_ENABLE, 1);
        }
    }
}

int robot_ping_all(void) {
    int alive = 0;
    for (int l = 0; l < NUM_LEGS; l++) {
        for (int j = 0; j < SERVOS_PER_LEG; j++) {
            if (sts_ping(legs[l].huart, legs[l].servo_ids[j])) alive++;
            HAL_Delay(5);
        }
    }
    return alive;
}

bool robot_read_current_pose(pose_t out) {
    for (int l = 0; l < NUM_LEGS; l++) {
        for (int j = 0; j < SERVOS_PER_LEG; j++) {
            sts_read_result_t r = sts_read_state(legs[l].huart, legs[l].servo_ids[j]);
            if (!r.ok) return false;
            out[l][j] = r.position;
        }
    }
    return true;
}

/* === ESC / Safety === */

bool check_esc(void) {
    if (huart2.Instance->SR & USART_SR_RXNE) {
        uint8_t ch = (uint8_t)(huart2.Instance->DR & 0xFF);
        if (ch == ESC_KEY) return true;
    }
    return false;
}

void emergency_stop(void) {
    printf("\r\n*** ESC pressed — torque OFF, halted ***\r\n");
    robot_torque_off_all();
    while (1) {}
}

void delay_with_estop(uint32_t ms) {
    uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < ms) {
        if (check_esc()) emergency_stop();
        HAL_Delay(POLL_PERIOD_MS);
    }
}

bool safety_check(float pitch, float roll) {
    if (fabsf(pitch) > MAX_PITCH_DEG) return false;
    if (fabsf(roll) > MAX_ROLL_DEG) return false;
    return true;
}
