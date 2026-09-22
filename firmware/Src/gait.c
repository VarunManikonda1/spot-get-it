#include "gait.h"
#include "robot.h"
#include "imu_bno055.h"
#include "servo_sts3215.h"
#include "system_hal.h"
#include "config.h"
#include <stdio.h>

float gait_smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

/* Trot diagonal pair phase offsets (배열 순서: FL, FR, RL, RR) */
static const float trot_phase_offset[NUM_LEGS] = {
    0.5f,   /* FL — Pair B */
    0.0f,   /* FR — Pair A */
    0.0f,   /* RL — Pair A */
    0.5f,   /* RR — Pair B */
};

bool stand_at_height(float height_mm) {
    if (height_mm < BODY_HEIGHT_MIN_MM || height_mm > BODY_HEIGHT_MAX_MM) {
        printf("!! height %.0f mm out of [%.0f, %.0f] — skipped\r\n",
               (double)height_mm,
               (double)BODY_HEIGHT_MIN_MM, (double)BODY_HEIGHT_MAX_MM);
        return false;
    }

    pose_t from_pose;
    robot_update_pose_from_foot(from_pose);

    for (int l = 0; l < NUM_LEGS; l++) {
        foot_pos[l][0] = DEFAULT_FOOT_X;
        foot_pos[l][1] = -height_mm;
    }

    pose_t to_pose;
    robot_update_pose_from_foot(to_pose);

    robot_transition(from_pose, to_pose, SPEED_DEG_PER_SEC);
    return true;
}

/*
 * leg_phase ∈ [0, 1) → hip frame에서의 발끝 offset (default 기준)
 *
 * Stance (leg_phase < DUTY_FACTOR):
 *   x_off: +stride/2 → -stride/2 선형 sweep
 *   z_off: 0
 *
 * Swing (leg_phase ≥ DUTY_FACTOR):
 *   1) s를 smoothstep으로 warp → 양 끝(liftoff/touchdown) 속도 0
 *   2) Cubic Bezier 대칭 arch:
 *        P0=(-stride/2, 0)
 *        P1=(0, 4/3 × LIFT_Z)
 *        P2=(0, 4/3 × LIFT_Z)
 *        P3=(+stride/2, 0)
 *      → x 궤적 대칭 (back dwell 제거)
 *      → z peak가 정확히 LIFT_Z (4/3 보정으로 cubic Bezier의 0.75배 효과 상쇄)
 */
static void compute_trot_offset(float leg_phase, float* fx_off, float* fz_off) {
    if (leg_phase < DUTY_FACTOR) {
        float s = leg_phase / DUTY_FACTOR;
        *fx_off = STRIDE_X * 0.5f - s * STRIDE_X;
        *fz_off = 0.0f;
        return;
    }

    float s = (leg_phase - DUTY_FACTOR) / (1.0f - DUTY_FACTOR);
    s = gait_smoothstep(s);

    float oms = 1.0f - s;
    float B0 = oms * oms * oms;
    float B1 = 3.0f * oms * oms * s;
    float B2 = 3.0f * oms * s * s;
    float B3 = s * s * s;

    const float xP0 = -STRIDE_X * 0.5f;
    const float xP1 =  0.0f;
    const float xP2 =  0.0f;
    const float xP3 =  STRIDE_X * 0.5f;
    const float zP0 = 0.0f;
    const float zP1 = (4.0f / 3.0f) * LIFT_Z;
    const float zP2 = (4.0f / 3.0f) * LIFT_Z;
    const float zP3 = 0.0f;

    *fx_off = B0*xP0 + B1*xP1 + B2*xP2 + B3*xP3;
    *fz_off = B0*zP0 + B1*zP1 + B2*zP2 + B3*zP3;
}

void run_trot(int n_cycles) {
    uint32_t loop_origin = HAL_GetTick();
    uint32_t total_duration_ms = (uint32_t)n_cycles * GAIT_PERIOD_MS;
    int last_logged_cycle = -1;

#if ENABLE_MONITOR
    uint32_t last_monitor = 0;
    char leg_phase_marker[NUM_LEGS] = {'?', '?', '?', '?'};
    int servo_rr_idx = 0;
    uint16_t latest_pres[NUM_LEGS][SERVOS_PER_LEG] = {{0}};
    uint16_t latest_load[NUM_LEGS][SERVOS_PER_LEG] = {{0}};
#endif

    while (1) {
        uint32_t loop_start = HAL_GetTick();
        uint32_t elapsed_ms = loop_start - loop_origin;

        if (elapsed_ms >= total_duration_ms) break;
        if (g_abort) break;
        if (check_esc()) emergency_stop();

        float phase = (float)(elapsed_ms % GAIT_PERIOD_MS) / (float)GAIT_PERIOD_MS;
        int current_cycle = (int)(elapsed_ms / GAIT_PERIOD_MS);

        for (int l = 0; l < NUM_LEGS; l++) {
            float lp = phase + trot_phase_offset[l];
            if (lp >= 1.0f) lp -= 1.0f;

            float fx_off, fz_off;
            compute_trot_offset(lp, &fx_off, &fz_off);

            foot_pos[l][0] = DEFAULT_FOOT_X + fx_off;
            foot_pos[l][1] = DEFAULT_FOOT_Z + fz_off;

#if ENABLE_MONITOR
            leg_phase_marker[l] = (lp < DUTY_FACTOR) ? 'S' : 'W';
#endif
        }

        pose_t goals;
        robot_update_pose_from_foot(goals);
        robot_apply_pose(goals);

#if ENABLE_MONITOR
        /* Round-robin: 한 iteration에 1서보씩 present_pos/load read */
        int rr_leg = servo_rr_idx / SERVOS_PER_LEG;
        int rr_joint = servo_rr_idx % SERVOS_PER_LEG;
        sts_read_result_t rr = sts_read_state(legs[rr_leg].huart,
                                              legs[rr_leg].servo_ids[rr_joint]);
        if (rr.ok) {
            latest_pres[rr_leg][rr_joint] = rr.position;
            latest_load[rr_leg][rr_joint] = rr.load;
        }
        int rr_just = servo_rr_idx;
        servo_rr_idx = (servo_rr_idx + 1) % NUM_SERVOS;
#endif

        body_attitude_t b = {0};
        bool imu_ok = bno055_read_body(&hi2c1, &b);
        if (imu_ok && !safety_check(b.pitch, b.roll)) {
            printf("\r\n!! ABORT pitch=%+.1f roll=%+.1f !!\r\n",
                   (double)b.pitch, (double)b.roll);
            g_abort = true;
            break;
        }

        if (current_cycle != last_logged_cycle) {
            printf("\r\n=== Cycle %d / %d ===\r\n", current_cycle + 1, n_cycles);
            last_logged_cycle = current_cycle;
        }

#if ENABLE_MONITOR
        if (loop_start - last_monitor >= MONITOR_PERIOD_MS) {
            last_monitor = loop_start;

            int rr_l = rr_just / SERVOS_PER_LEG;
            int rr_j = rr_just % SERVOS_PER_LEG;
            uint8_t rr_id = legs[rr_l].servo_ids[rr_j];

            printf("[ph=%.2f t=%4lums] y=%+6.1f p=%+6.1f r=%+6.1f  rr=ID%u\r\n",
                   (double)phase, (unsigned long)elapsed_ms,
                   (double)b.yaw, (double)b.pitch, (double)b.roll,
                   (unsigned)rr_id);

            for (int l = 0; l < NUM_LEGS; l++) {
                int16_t l0 = sts_load_to_signed(latest_load[l][0]);
                int16_t l1 = sts_load_to_signed(latest_load[l][1]);
                int16_t l2 = sts_load_to_signed(latest_load[l][2]);
                printf(" %s[%c] foot=(%+7.1f,%+7.1f) cmd=(%4u,%4u,%4u) "
                       "pres=(%4u,%4u,%4u) load=(%+5d,%+5d,%+5d)\r\n",
                       legs[l].leg_name, leg_phase_marker[l],
                       (double)foot_pos[l][0], (double)foot_pos[l][1],
                       (unsigned)goals[l][0], (unsigned)goals[l][1], (unsigned)goals[l][2],
                       (unsigned)latest_pres[l][0], (unsigned)latest_pres[l][1], (unsigned)latest_pres[l][2],
                       (int)l0, (int)l1, (int)l2);
            }
        }
#endif

        uint32_t elapsed = HAL_GetTick() - loop_start;
        if (elapsed < LOOP_PERIOD_MS) {
            HAL_Delay(LOOP_PERIOD_MS - elapsed);
        }
    }
}
