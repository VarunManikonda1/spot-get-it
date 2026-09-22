/*
 * Three-Leg Standing Test
 *
 * Default pose v4 Case A2: thigh -801 (-70.4°), knee +1292 (+113.6°)
 * 무게 중심 d ≈ 0 mm 확인 후 3-leg 안정성 테스트.
 *
 * 시나리오:
 *   1. 4-leg default standing → baseline 측정 (3초)
 *   2. FR 들기 → 5초 측정 → 내림
 *   3. RL 들기 → 5초 측정 → 내림  (FR과 같은 대각선)
 *   4. FL 들기 → 5초 측정 → 내림
 *   5. RR 들기 → 5초 측정 → 내림  (FL과 같은 대각선)
 *   6. 비교 표 출력
 *
 * 들기 방식: 해당 다리만 knee +30° 추가 굽힘 (발이 위로 올라감)
 *
 * 측정 항목 (각 단계):
 *   - 본체 IMU pitch / roll
 *   - 4 다리 knee load (들린 다리는 ≈ 0)
 *   - 나머지 3 다리 토크 재분배
 *
 * Safety: pitch/roll 절댓값 35° 초과 시 자동 abort.
 * 정지: ESC
 */

/* ===== Default pose (v4 Case A2, 균형점 확인됨) ===== */
#define HIP_OFFSET           0
#define THIGH_OFFSET      (-801)    /* -70.4° */
#define KNEE_OFFSET       (+1292)   /* +113.6° */
#define ZERO_POS           2048

/* ===== 들기 ===== */
#define LIFT_KNEE_DEG    30          /* 발 들기 위해 knee 추가 굽힘 */
#define LIFT_KNEE_UNIT   ((int)(LIFT_KNEE_DEG * 4096 / 360))

/* ===== 모션 ===== */
#define SPEED_DEG_PER_SEC    20
#define LIFT_SPEED_DEG_PER_SEC  15   /* 들 때 더 천천히 */
#define STEP_MS              25
#define MIN_TRANSITION_MS    100

#define COUNTDOWN_SEC          3
#define INITIAL_SETTLE_MS      5000
#define BASELINE_MEASURE_MS    3000
#define LIFT_SETTLE_MS         2000   /* 들고 안정화 */
#define LIFT_MEASURE_MS        5000
#define LOWER_SETTLE_MS        2000   /* 내리고 안정화 */
#define MEASURE_INTERVAL_MS    1000
#define POLL_PERIOD_MS         10
#define ESC_KEY                0x1B

/* Safety */
#define MAX_PITCH_DEG    35.0f
#define MAX_ROLL_DEG     40.0f   /* default roll +5° 감안 */

/* ===== STS 프로토콜 ===== */
#define HEADER1                 0xFF
#define HEADER2                 0xFF
#define BROADCAST_ID            0xFE
#define INST_PING               0x01
#define INST_READ               0x02
#define INST_WRITE              0x03
#define INST_SYNC_WRITE         0x83
#define REG_TORQUE_ENABLE       0x28
#define REG_GOAL_POSITION       0x2A
#define REG_PRESENT_POSITION    0x38
#define READ_BYTES              8
#define READ_RESP_LEN_BYTE      (READ_BYTES + 2)
#define ACK_LEN_BYTE            2
#define TIMEOUT_MS              30

#define NUM_LEGS         4
#define SERVOS_PER_LEG   3
#define NUM_SERVOS       (NUM_LEGS * SERVOS_PER_LEG)

#define IDX_FL  0
#define IDX_FR  1
#define IDX_RL  2
#define IDX_RR  3

/* ===== BNO055 ===== */
#define BNO055_I2C_ADDR_DEFAULT   0x28
#define BNO055_I2C_ADDR_ALT       0x29
#define BNO055_CHIP_ID            0x00
#define BNO055_EUL_HEADING_LSB    0x1A
#define BNO055_OPR_MODE           0x3D
#define BNO055_OPR_CONFIG         0x00
#define BNO055_OPR_IMUPLUS        0x08
#define BNO055_CHIP_ID_EXPECTED   0xA0

#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

typedef struct {
    UART_HandleTypeDef *huart;
    const char *leg_name;
    uint8_t servo_ids[SERVOS_PER_LEG];
    int8_t sign;
} leg_t;

UART_HandleTypeDef huart3, huart4, huart5, huart6;
UART_HandleTypeDef huart2;
I2C_HandleTypeDef hi2c1;

static leg_t legs[NUM_LEGS] = {
    { &huart6, "FL", { 1,  2,  3}, +1 },
    { &huart4, "FR", { 4,  5,  6}, -1 },
    { &huart5, "RL", { 7,  8,  9}, +1 },
    { &huart3, "RR", {10, 11, 12}, -1 }
};

typedef uint16_t pose_t[NUM_LEGS][SERVOS_PER_LEG];

typedef struct {
    bool ok;
    uint16_t position;
    uint16_t load;
} read_result_t;

typedef struct { bool ok; } write_result_t;

typedef struct { float yaw; float pitch; float roll; } body_attitude_t;

/* === 측정 결과 === */
typedef struct {
    const char* label;       /* "BASELINE", "FR", ... */
    int lifted_idx;          /* -1 = baseline */
    float pitch_avg;
    float roll_avg;
    int knee_load[NUM_LEGS]; /* avg knee load for each leg */
    int n_samples;
    bool aborted;
} trial_result_t;

/* 들기 순서: 대각선 (trot stance 의미) */
static const int lift_order[] = { IDX_FR, IDX_RL, IDX_FL, IDX_RR };
#define NUM_LIFTS  (sizeof(lift_order) / sizeof(lift_order[0]))

#define NUM_TRIALS  (1 + NUM_LIFTS)   /* baseline + 4 lifts */
static trial_result_t trials[NUM_TRIALS];

static uint8_t bno_addr = BNO055_I2C_ADDR_DEFAULT;
static volatile bool g_abort = false;

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_UART4_HDSEL_Init(void);
static void MX_USART3_HDSEL_Init(void);
static void MX_USART6_HDSEL_Init(void);
static void MX_UART5_HDSEL_Init(void);
static void MX_USART2_Init(void);
static void MX_I2C1_Init(void);
void Error_Handler(void);

static uint8_t calc_checksum(const uint8_t *buf, uint8_t len);
static bool sts_ping(UART_HandleTypeDef *huart, uint8_t id);
static read_result_t sts_read_state(UART_HandleTypeDef *huart, uint8_t id);
static write_result_t sts_write_byte(UART_HandleTypeDef *huart, uint8_t id, uint8_t addr, uint8_t val);
static bool sts_sync_write_goal(UART_HandleTypeDef *huart,
                                const uint8_t *ids, const uint16_t *goals, uint8_t count);

int _write(int file, char *ptr, int len) {
    (void)file;
    HAL_UART_Transmit(&huart2, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    return len;
}

static int abs_int(int x) { return x < 0 ? -x : x; }

static float smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

/* ============================================================ */
/* ESC + Safety                                                 */
/* ============================================================ */

static bool check_esc(void) {
    if (huart2.Instance->SR & USART_SR_RXNE) {
        uint8_t ch = (uint8_t)(huart2.Instance->DR & 0xFF);
        if (ch == ESC_KEY) return true;
    }
    return false;
}

static void torque_off_all(void) {
    for (int l = 0; l < NUM_LEGS; l++) {
        for (int j = 0; j < SERVOS_PER_LEG; j++) {
            sts_write_byte(legs[l].huart, legs[l].servo_ids[j],
                           REG_TORQUE_ENABLE, 0);
        }
    }
}

static void emergency_stop(void) {
    printf("\r\n*** ESC pressed — torque OFF, halted ***\r\n");
    torque_off_all();
    while (1) {}
}

static void delay_with_estop(uint32_t ms) {
    uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < ms) {
        if (check_esc()) emergency_stop();
        HAL_Delay(POLL_PERIOD_MS);
    }
}

static bool safety_check(float pitch, float roll) {
    if (fabsf(pitch) > MAX_PITCH_DEG) {
        printf("    !! SAFETY: pitch %+.1f exceeds %.0f !!\r\n", pitch, MAX_PITCH_DEG);
        return false;
    }
    if (fabsf(roll) > MAX_ROLL_DEG) {
        printf("    !! SAFETY: roll %+.1f exceeds %.0f !!\r\n", roll, MAX_ROLL_DEG);
        return false;
    }
    return true;
}

/* ============================================================ */
/* BNO055 IMUPLUS                                               */
/* ============================================================ */

static bool bno_read_byte(uint8_t reg, uint8_t* val) {
    return (HAL_I2C_Mem_Read(&hi2c1, bno_addr << 1, reg,
                             I2C_MEMADD_SIZE_8BIT, val, 1, 100) == HAL_OK);
}

static bool bno_write_byte(uint8_t reg, uint8_t val) {
    return (HAL_I2C_Mem_Write(&hi2c1, bno_addr << 1, reg,
                              I2C_MEMADD_SIZE_8BIT, &val, 1, 100) == HAL_OK);
}

static bool bno_read_bytes(uint8_t reg, uint8_t* buf, uint8_t len) {
    return (HAL_I2C_Mem_Read(&hi2c1, bno_addr << 1, reg,
                             I2C_MEMADD_SIZE_8BIT, buf, len, 100) == HAL_OK);
}

static bool bno_init_imuplus(void) {
    HAL_Delay(700);
    if (HAL_I2C_IsDeviceReady(&hi2c1, BNO055_I2C_ADDR_DEFAULT << 1, 3, 50) == HAL_OK) {
        bno_addr = BNO055_I2C_ADDR_DEFAULT;
    } else if (HAL_I2C_IsDeviceReady(&hi2c1, BNO055_I2C_ADDR_ALT << 1, 3, 50) == HAL_OK) {
        bno_addr = BNO055_I2C_ADDR_ALT;
    } else {
        return false;
    }
    uint8_t chip_id = 0;
    if (!bno_read_byte(BNO055_CHIP_ID, &chip_id) || chip_id != BNO055_CHIP_ID_EXPECTED) {
        return false;
    }
    bno_write_byte(BNO055_OPR_MODE, BNO055_OPR_CONFIG);
    HAL_Delay(25);
    bno_write_byte(BNO055_OPR_MODE, BNO055_OPR_IMUPLUS);
    HAL_Delay(20);
    return true;
}

static bool bno_read_body(body_attitude_t* body) {
    uint8_t buf[6];
    if (!bno_read_bytes(BNO055_EUL_HEADING_LSB, buf, 6)) return false;
    int16_t h_raw = (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
    int16_t r_raw = (int16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
    int16_t p_raw = (int16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8));
    body->yaw   = h_raw / 16.0f;
    body->pitch = -(r_raw / 16.0f);
    body->roll  = -(p_raw / 16.0f);
    return true;
}

/* ============================================================ */
/* Pose                                                         */
/* ============================================================ */

static void compute_default_pose(pose_t pose) {
    for (int l = 0; l < NUM_LEGS; l++) {
        int8_t s = legs[l].sign;
        pose[l][0] = ZERO_POS + s * HIP_OFFSET;
        pose[l][1] = ZERO_POS + s * THIGH_OFFSET;
        pose[l][2] = ZERO_POS + s * KNEE_OFFSET;
    }
}

/* lift_idx 다리만 knee를 +LIFT_KNEE_DEG 추가 굽힘 */
static void compute_lifted_pose(pose_t pose, int lift_idx) {
    compute_default_pose(pose);
    int8_t s = legs[lift_idx].sign;
    pose[lift_idx][2] = ZERO_POS + s * (KNEE_OFFSET + LIFT_KNEE_UNIT);
}

static void apply_pose_all(const pose_t pose) {
    for (int l = 0; l < NUM_LEGS; l++) {
        sts_sync_write_goal(legs[l].huart, legs[l].servo_ids,
                            pose[l], SERVOS_PER_LEG);
    }
}

static void transition_all(const pose_t from, const pose_t to, int speed_dps) {
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
        float ratio = smoothstep(linear_t);
        pose_t goals;
        for (int l = 0; l < NUM_LEGS; l++) {
            for (int j = 0; j < SERVOS_PER_LEG; j++) {
                int delta = (int)to[l][j] - (int)from[l][j];
                goals[l][j] = (uint16_t)((int)from[l][j] + (int)(delta * ratio));
            }
        }
        apply_pose_all(goals);
        HAL_Delay(STEP_MS);
    }
}

/* ============================================================ */
/* 측정                                                         */
/* ============================================================ */

static void measure_trial(trial_result_t* result, uint32_t duration_ms) {
    int n_samples = duration_ms / MEASURE_INTERVAL_MS;
    int knee_sum[NUM_LEGS] = {0};
    float pitch_sum = 0.0f, roll_sum = 0.0f;
    int n_ok = 0;

    for (int s = 0; s < n_samples; s++) {
        if (check_esc()) emergency_stop();
        HAL_Delay(MEASURE_INTERVAL_MS);

        /* Read 4 knee servos */
        for (int l = 0; l < NUM_LEGS; l++) {
            read_result_t r = sts_read_state(legs[l].huart, legs[l].servo_ids[2]);
            if (r.ok) {
                knee_sum[l] += (r.load & 0x3FF);
            }
        }

        /* IMU */
        body_attitude_t b;
        if (bno_read_body(&b)) {
            pitch_sum += b.pitch;
            roll_sum  += b.roll;
            n_ok++;

            /* Safety abort */
            if (!safety_check(b.pitch, b.roll)) {
                result->aborted = true;
                g_abort = true;
                printf("    *** ABORT: tilt exceeded ***\r\n");
                return;
            }
        }

        printf("      sample %d/%d  pitch=%+.2f roll=%+.2f  knee[FL=%4u FR=%4u RL=%4u RR=%4u]\r\n",
               s + 1, n_samples,
               (n_ok > 0) ? pitch_sum/n_ok : 0.0f,
               (n_ok > 0) ? roll_sum/n_ok  : 0.0f,
               knee_sum[IDX_FL]/(s+1), knee_sum[IDX_FR]/(s+1),
               knee_sum[IDX_RL]/(s+1), knee_sum[IDX_RR]/(s+1));
    }

    if (n_ok == 0) return;

    for (int l = 0; l < NUM_LEGS; l++) {
        result->knee_load[l] = knee_sum[l] / n_samples;
    }
    result->pitch_avg = pitch_sum / n_ok;
    result->roll_avg  = roll_sum  / n_ok;
    result->n_samples = n_samples;
}

/* ============================================================ */
/* Trial 단계                                                   */
/* ============================================================ */

static void run_baseline_trial(trial_result_t* r) {
    r->label = "BASELINE";
    r->lifted_idx = -1;
    r->aborted = false;

    printf("\r\n=================================================\r\n");
    printf(" BASELINE (4-leg standing)\r\n");
    printf("=================================================\r\n");
    printf("    Measuring %d ms...\r\n", BASELINE_MEASURE_MS);
    measure_trial(r, BASELINE_MEASURE_MS);

    printf("\r\n    -- baseline --\r\n");
    printf("    pitch=%+.2f  roll=%+.2f\r\n", r->pitch_avg, r->roll_avg);
    printf("    knee load FL=%d FR=%d RL=%d RR=%d\r\n",
           r->knee_load[IDX_FL], r->knee_load[IDX_FR],
           r->knee_load[IDX_RL], r->knee_load[IDX_RR]);
}

static void run_lift_trial(trial_result_t* r, int lift_idx, const pose_t default_pose) {
    r->label = legs[lift_idx].leg_name;
    r->lifted_idx = lift_idx;
    r->aborted = false;

    printf("\r\n=================================================\r\n");
    printf(" LIFT %s (knee +%d deg)\r\n", legs[lift_idx].leg_name, LIFT_KNEE_DEG);
    printf("=================================================\r\n");

    /* 들기 transition */
    pose_t lifted;
    compute_lifted_pose(lifted, lift_idx);
    printf("    Lifting %s...\r\n", legs[lift_idx].leg_name);
    transition_all(default_pose, lifted, LIFT_SPEED_DEG_PER_SEC);

    /* Settle */
    printf("    Settling %d ms...\r\n", LIFT_SETTLE_MS);
    delay_with_estop(LIFT_SETTLE_MS);

    /* Measure */
    printf("    Measuring %d ms...\r\n", LIFT_MEASURE_MS);
    measure_trial(r, LIFT_MEASURE_MS);

    /* Lower transition (if not aborted) */
    if (!r->aborted) {
        printf("    Lowering %s...\r\n", legs[lift_idx].leg_name);
        transition_all(lifted, default_pose, LIFT_SPEED_DEG_PER_SEC);
        delay_with_estop(LOWER_SETTLE_MS);
    } else {
        /* 즉시 default 복귀 */
        printf("    !! ABORT: returning to default !!\r\n");
        transition_all(lifted, default_pose, SPEED_DEG_PER_SEC);
    }

    printf("\r\n    -- lift %s --\r\n", legs[lift_idx].leg_name);
    if (!r->aborted) {
        printf("    pitch=%+.2f  roll=%+.2f\r\n", r->pitch_avg, r->roll_avg);
        printf("    knee load FL=%d FR=%d RL=%d RR=%d\r\n",
               r->knee_load[IDX_FL], r->knee_load[IDX_FR],
               r->knee_load[IDX_RL], r->knee_load[IDX_RR]);
    } else {
        printf("    *** ABORTED ***\r\n");
    }
}

/* ============================================================ */
/* 최종 비교 표                                                 */
/* ============================================================ */

static void print_summary(void) {
    printf("\r\n");
    printf("=================================================\r\n");
    printf("  THREE-LEG STANDING TEST SUMMARY\r\n");
    printf("=================================================\r\n");
    printf("\r\n");
    printf("  Trial    | pitch    roll    | FL    FR    RL    RR  | (lifted)\r\n");
    printf("  ---------+------------------+----------------------+---------\r\n");

    for (int i = 0; i < NUM_TRIALS; i++) {
        trial_result_t* t = &trials[i];
        if (t->aborted) {
            printf("  %-8s | (ABORTED)\r\n", t->label);
            continue;
        }
        printf("  %-8s | %+6.2f   %+6.2f   | %3d   %3d   %3d   %3d  | ",
               t->label, t->pitch_avg, t->roll_avg,
               t->knee_load[IDX_FL], t->knee_load[IDX_FR],
               t->knee_load[IDX_RL], t->knee_load[IDX_RR]);
        if (t->lifted_idx >= 0) {
            printf("%s lifted\r\n", legs[t->lifted_idx].leg_name);
        } else {
            printf("baseline\r\n");
        }
    }

    /* 분석 */
    printf("\r\n  Analysis (vs baseline):\r\n");
    if (trials[0].aborted) {
        printf("    Baseline aborted, no comparison.\r\n");
        return;
    }
    for (int i = 1; i < NUM_TRIALS; i++) {
        trial_result_t* t = &trials[i];
        if (t->aborted) continue;
        float pitch_delta = t->pitch_avg - trials[0].pitch_avg;
        float roll_delta  = t->roll_avg  - trials[0].roll_avg;
        printf("    Lift %s: pitch %+5.2f -> %+5.2f (Δ%+5.2f)  roll %+5.2f -> %+5.2f (Δ%+5.2f)\r\n",
               t->label,
               trials[0].pitch_avg, t->pitch_avg, pitch_delta,
               trials[0].roll_avg,  t->roll_avg,  roll_delta);
    }

    /* 안정성 평가 */
    printf("\r\n  Stability assessment:\r\n");
    printf("    Δtilt < 3 deg = stable, 3-10 = marginal, >10 = unstable\r\n");
    for (int i = 1; i < NUM_TRIALS; i++) {
        trial_result_t* t = &trials[i];
        if (t->aborted) continue;
        float total_delta = sqrtf(
            (t->pitch_avg - trials[0].pitch_avg) * (t->pitch_avg - trials[0].pitch_avg) +
            (t->roll_avg  - trials[0].roll_avg)  * (t->roll_avg  - trials[0].roll_avg)
        );
        const char* eval;
        if (total_delta < 3.0f) eval = "STABLE   ";
        else if (total_delta < 10.0f) eval = "MARGINAL ";
        else eval = "UNSTABLE ";
        printf("    Lift %s: Δtilt = %.2f deg  [%s]\r\n", t->label, total_delta, eval);
    }
    printf("=================================================\r\n");
}

/* ============================================================ */
/* MAIN                                                         */
/* ============================================================ */

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART2_Init();
    MX_UART4_HDSEL_Init();
    MX_USART6_HDSEL_Init();
    MX_USART3_HDSEL_Init();
    MX_UART5_HDSEL_Init();
    MX_I2C1_Init();
    HAL_Delay(200);

    printf("\r\n=== STM32F446RE Boot ===\r\n");
    printf("\r\n=================================================\r\n");
    printf("  Three-Leg Standing Test\r\n");
    printf("  Default: thigh=%d (%.1f), knee=%d (%.1f)\r\n",
           THIGH_OFFSET, THIGH_OFFSET * 360.0f / 4096.0f,
           KNEE_OFFSET, KNEE_OFFSET * 360.0f / 4096.0f);
    printf("\r\n");
    printf("  Trials: BASELINE, then lift each leg (FR, RL, FL, RR).\r\n");
    printf("  Lift = knee +%d deg.\r\n", LIFT_KNEE_DEG);
    printf("  Total time: ~60 sec\r\n");
    printf("\r\n");
    printf("  >>> Robot on flat surface, do NOT touch <<<\r\n");
    printf("  >>> Be ready to catch if it tips over <<<\r\n");
    printf("=================================================\r\n");

    printf("\r\n[1] Pinging 12 servos...\r\n");
    int alive = 0;
    for (int l = 0; l < NUM_LEGS; l++) {
        for (int j = 0; j < SERVOS_PER_LEG; j++) {
            if (sts_ping(legs[l].huart, legs[l].servo_ids[j])) alive++;
            HAL_Delay(5);
        }
    }
    printf("    %d / %d alive\r\n", alive, NUM_SERVOS);
    if (alive < NUM_SERVOS) { while (1) {} }

    printf("\r\n[2] BNO055 IMUPLUS init...\r\n");
    if (!bno_init_imuplus()) { printf("[ABORT]\r\n"); while (1) {} }
    printf("    OK.\r\n");

    printf("\r\n[3] Torque OFF...\r\n");
    torque_off_all();

    printf("\r\n[4] Starting in %d s...\r\n", COUNTDOWN_SEC);
    for (int i = COUNTDOWN_SEC; i > 0; i--) {
        printf("    %d...\r\n", i);
        delay_with_estop(1000);
    }

    pose_t start_pose;
    for (int l = 0; l < NUM_LEGS; l++) {
        for (int j = 0; j < SERVOS_PER_LEG; j++) {
            read_result_t r = sts_read_state(legs[l].huart, legs[l].servo_ids[j]);
            if (!r.ok) { while (1) {} }
            start_pose[l][j] = r.position;
        }
    }
    apply_pose_all(start_pose);
    delay_with_estop(50);

    printf("\r\n[5] Torque ON 12 servos...\r\n");
    for (int l = 0; l < NUM_LEGS; l++) {
        for (int j = 0; j < SERVOS_PER_LEG; j++) {
            sts_write_byte(legs[l].huart, legs[l].servo_ids[j],
                           REG_TORQUE_ENABLE, 1);
        }
    }
    delay_with_estop(300);

    pose_t default_pose;
    compute_default_pose(default_pose);

    printf("\r\n[6] start -> default 4-leg standing\r\n");
    transition_all(start_pose, default_pose, SPEED_DEG_PER_SEC);

    printf("\r\n[7] Initial settle %d ms...\r\n", INITIAL_SETTLE_MS);
    delay_with_estop(INITIAL_SETTLE_MS);

    /* === Baseline === */
    run_baseline_trial(&trials[0]);

    /* === Each leg lift === */
    for (int i = 0; i < (int)NUM_LIFTS; i++) {
        if (g_abort) {
            printf("\r\n*** Global abort active, skipping remaining lifts ***\r\n");
            trials[1 + i].aborted = true;
            trials[1 + i].label = legs[lift_order[i]].leg_name;
            continue;
        }
        run_lift_trial(&trials[1 + i], lift_order[i], default_pose);
    }

    /* === Summary === */
    print_summary();

    /* === Return === */
    printf("\r\n[Final] default -> start\r\n");
    transition_all(default_pose, start_pose, SPEED_DEG_PER_SEC);

    printf("\r\n[Final] Torque OFF.\r\n");
    torque_off_all();
    printf("    OK.\r\n");
    printf("\r\n=== TEST COMPLETE ===\r\n");

    while (1) {
        if (check_esc()) emergency_stop();
        HAL_Delay(POLL_PERIOD_MS);
    }
}

/* ============================================================ */
/* STS 프로토콜                                                 */
/* ============================================================ */

static uint8_t calc_checksum(const uint8_t *buf, uint8_t len) {
    uint16_t sum = 0;
    for (uint8_t i = 2; i < len - 1; i++) sum += buf[i];
    return (uint8_t)(~sum & 0xFF);
}

static bool sts_ping(UART_HandleTypeDef *huart, uint8_t id) {
    uint8_t pkt[6];
    pkt[0] = HEADER1; pkt[1] = HEADER2; pkt[2] = id;
    pkt[3] = 2; pkt[4] = INST_PING;
    pkt[5] = calc_checksum(pkt, 6);
    if (HAL_UART_Transmit(huart, pkt, 6, 50) != HAL_OK) return false;

    uint8_t raw[7] = {0};
    HAL_UART_Receive(huart, raw, 7, TIMEOUT_MS);
    uint16_t received = 7 - huart->RxXferCount;
    if (received < 6) return false;

    for (int off = 0; off + 5 < received; off++) {
        if (raw[off] == HEADER1 && raw[off+1] == HEADER2
            && raw[off+2] == id && raw[off+3] == 2) {
            uint8_t resp[6];
            memcpy(resp, &raw[off], 6);
            if (resp[5] == calc_checksum(resp, 6) && resp[4] == 0) return true;
        }
    }
    return false;
}

static read_result_t sts_read_state(UART_HandleTypeDef *huart, uint8_t id) {
    read_result_t r = {0};
    uint8_t req[8];
    req[0] = HEADER1; req[1] = HEADER2; req[2] = id;
    req[3] = 4; req[4] = INST_READ;
    req[5] = REG_PRESENT_POSITION; req[6] = READ_BYTES;
    req[7] = calc_checksum(req, 8);

    if (HAL_UART_Transmit(huart, req, 8, 50) != HAL_OK) return r;

    uint8_t raw[15] = {0};
    HAL_UART_Receive(huart, raw, 15, TIMEOUT_MS);
    uint16_t received = 15 - huart->RxXferCount;
    if (received < 14) return r;

    for (int off = 0; off + 13 < received; off++) {
        if (raw[off] == HEADER1 && raw[off+1] == HEADER2
            && raw[off+2] == id && raw[off+3] == READ_RESP_LEN_BYTE) {
            uint8_t resp[14];
            memcpy(resp, &raw[off], 14);
            if (resp[13] == calc_checksum(resp, 14)) {
                r.position = resp[5]  | ((uint16_t)resp[6]  << 8);
                r.load     = resp[9]  | ((uint16_t)resp[10] << 8);
                r.ok       = (resp[4] == 0);
                return r;
            }
        }
    }
    return r;
}

static write_result_t sts_write_byte(UART_HandleTypeDef *huart, uint8_t id, uint8_t addr, uint8_t val) {
    write_result_t r = {0};
    uint8_t req[8];
    req[0] = HEADER1; req[1] = HEADER2; req[2] = id;
    req[3] = 4; req[4] = INST_WRITE;
    req[5] = addr; req[6] = val;
    req[7] = calc_checksum(req, 8);

    if (HAL_UART_Transmit(huart, req, 8, 50) != HAL_OK) return r;

    uint8_t raw[7] = {0};
    HAL_UART_Receive(huart, raw, 7, TIMEOUT_MS);
    uint16_t received = 7 - huart->RxXferCount;
    if (received < 6) return r;

    for (int off = 0; off + 5 < received; off++) {
        if (raw[off] == HEADER1 && raw[off+1] == HEADER2
            && raw[off+2] == id && raw[off+3] == ACK_LEN_BYTE) {
            uint8_t resp[6];
            memcpy(resp, &raw[off], 6);
            if (resp[5] == calc_checksum(resp, 6)) {
                r.ok = (resp[4] == 0);
                return r;
            }
        }
    }
    return r;
}

static bool sts_sync_write_goal(UART_HandleTypeDef *huart,
                                const uint8_t *ids, const uint16_t *goals, uint8_t count) {
    if (count == 0 || count > 12) return false;

    uint8_t pkt[8 + 12 * 3];
    uint8_t len_field = (1 + 2) * count + 4;

    pkt[0] = HEADER1; pkt[1] = HEADER2; pkt[2] = BROADCAST_ID;
    pkt[3] = len_field; pkt[4] = INST_SYNC_WRITE;
    pkt[5] = REG_GOAL_POSITION; pkt[6] = 2;

    for (uint8_t i = 0; i < count; i++) {
        pkt[7 + i*3 + 0] = ids[i];
        pkt[7 + i*3 + 1] = goals[i] & 0xFF;
        pkt[7 + i*3 + 2] = (goals[i] >> 8) & 0xFF;
    }

    uint8_t total_len = 8 + count * 3;
    pkt[total_len - 1] = calc_checksum(pkt, total_len);

    return (HAL_UART_Transmit(huart, pkt, total_len, 50) == HAL_OK);
}

/* ============================================================ */
/* 시스템 / GPIO / I2C / UART 초기화                            */
/* ============================================================ */

static void SystemClock_Config(void) {
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM = 8; osc.PLL.PLLN = 180;
    osc.PLL.PLLP = RCC_PLLP_DIV2; osc.PLL.PLLQ = 4; osc.PLL.PLLR = 2;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();
    if (HAL_PWREx_EnableOverDrive() != HAL_OK) Error_Handler();
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) Error_Handler();
}

static void MX_GPIO_Init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
}

static void MX_I2C1_Init(void) {
    __HAL_RCC_I2C1_CLK_ENABLE();
    GPIO_InitTypeDef gp = {0};
    gp.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gp.Mode = GPIO_MODE_AF_OD;
    gp.Pull = GPIO_PULLUP;
    gp.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gp.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gp);

    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 400000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) Error_Handler();
}

static void MX_UART4_HDSEL_Init(void) {
    __HAL_RCC_UART4_CLK_ENABLE();
    GPIO_InitTypeDef gp = {0};
    gp.Pin = GPIO_PIN_0; gp.Mode = GPIO_MODE_AF_OD; gp.Pull = GPIO_NOPULL;
    gp.Speed = GPIO_SPEED_FREQ_VERY_HIGH; gp.Alternate = GPIO_AF8_UART4;
    HAL_GPIO_Init(GPIOA, &gp);
    huart4.Instance = UART4;
    huart4.Init.BaudRate = 1000000; huart4.Init.WordLength = UART_WORDLENGTH_8B;
    huart4.Init.StopBits = UART_STOPBITS_1; huart4.Init.Parity = UART_PARITY_NONE;
    huart4.Init.Mode = UART_MODE_TX_RX; huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart4.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_HalfDuplex_Init(&huart4) != HAL_OK) Error_Handler();
}

static void MX_USART6_HDSEL_Init(void) {
    __HAL_RCC_USART6_CLK_ENABLE();
    GPIO_InitTypeDef gp = {0};
    gp.Pin = GPIO_PIN_6; gp.Mode = GPIO_MODE_AF_OD; gp.Pull = GPIO_NOPULL;
    gp.Speed = GPIO_SPEED_FREQ_VERY_HIGH; gp.Alternate = GPIO_AF8_USART6;
    HAL_GPIO_Init(GPIOC, &gp);
    huart6.Instance = USART6;
    huart6.Init.BaudRate = 1000000; huart6.Init.WordLength = UART_WORDLENGTH_8B;
    huart6.Init.StopBits = UART_STOPBITS_1; huart6.Init.Parity = UART_PARITY_NONE;
    huart6.Init.Mode = UART_MODE_TX_RX; huart6.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart6.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_HalfDuplex_Init(&huart6) != HAL_OK) Error_Handler();
}

static void MX_USART3_HDSEL_Init(void) {
    __HAL_RCC_USART3_CLK_ENABLE();
    GPIO_InitTypeDef gp = {0};
    gp.Pin = GPIO_PIN_10; gp.Mode = GPIO_MODE_AF_OD; gp.Pull = GPIO_NOPULL;
    gp.Speed = GPIO_SPEED_FREQ_VERY_HIGH; gp.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOB, &gp);
    huart3.Instance = USART3;
    huart3.Init.BaudRate = 1000000; huart3.Init.WordLength = UART_WORDLENGTH_8B;
    huart3.Init.StopBits = UART_STOPBITS_1; huart3.Init.Parity = UART_PARITY_NONE;
    huart3.Init.Mode = UART_MODE_TX_RX; huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_HalfDuplex_Init(&huart3) != HAL_OK) Error_Handler();
}

static void MX_UART5_HDSEL_Init(void) {
    __HAL_RCC_UART5_CLK_ENABLE();
    GPIO_InitTypeDef gp = {0};
    gp.Pin = GPIO_PIN_12; gp.Mode = GPIO_MODE_AF_OD; gp.Pull = GPIO_NOPULL;
    gp.Speed = GPIO_SPEED_FREQ_VERY_HIGH; gp.Alternate = GPIO_AF8_UART5;
    HAL_GPIO_Init(GPIOC, &gp);
    huart5.Instance = UART5;
    huart5.Init.BaudRate = 1000000; huart5.Init.WordLength = UART_WORDLENGTH_8B;
    huart5.Init.StopBits = UART_STOPBITS_1; huart5.Init.Parity = UART_PARITY_NONE;
    huart5.Init.Mode = UART_MODE_TX_RX; huart5.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart5.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_HalfDuplex_Init(&huart5) != HAL_OK) Error_Handler();
}

static void MX_USART2_Init(void) {
    __HAL_RCC_USART2_CLK_ENABLE();
    GPIO_InitTypeDef gp = {0};
    gp.Pin = GPIO_PIN_2 | GPIO_PIN_3; gp.Mode = GPIO_MODE_AF_PP; gp.Pull = GPIO_NOPULL;
    gp.Speed = GPIO_SPEED_FREQ_VERY_HIGH; gp.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gp);
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200; huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1; huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX; huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK) Error_Handler();
}

void Error_Handler(void) {
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {
    (void)file; (void)line;
}
#endif
