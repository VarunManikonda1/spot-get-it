/*
 * Static Crawl Gait — Single Leg Cycle (RR)
 *
 * 한 다리 (RR) 한 cycle 시연:
 *
 *   Phase 0: baseline (모든 발 -10, -130)
 *   Phase 1: body shift (모든 발 +20, -130) — 본체 30mm 뒤로
 *   Phase 2: RR lift (RR만 +20, -100) — 발 30mm 위로
 *   Phase 3: RR swing (RR만 +50, -100) — 발 본체 frame +30mm 앞으로
 *   Phase 4: RR place (RR만 +50, -130) — 발 내려놓음
 *   Phase 5: body advance (모든 발 -30 shift) — 본체 30mm 앞으로
 *
 * 결과: 본체 30mm 절대 전진. RR은 본체 frame +20 (default보다 +30 앞).
 *
 * 좌표계: hip frame, x=forward(+), z=up(+)
 *
 * Safety: pitch 35°, roll 40° abort.
 * 정지: ESC.
 */

#define L1_THIGH_MM    105.0f
#define L2_SHIN_MM     130.0f

#define DEFAULT_FOOT_X    -10.0f
#define DEFAULT_FOOT_Z   -130.0f
#define BODY_SHIFT_X       30.0f
#define LEG_LIFT_Z         30.0f
#define STRIDE_X           30.0f

/* 보행 다리 */
#define WALKING_LEG_IDX    3   /* RR */

/* ===== 모션 ===== */
#define ZERO_POS                2048
#define HIP_OFFSET              0
#define SPEED_DEG_PER_SEC       25
#define LIFT_SPEED_DEG_PER_SEC  20
#define STEP_MS                 25
#define MIN_TRANSITION_MS       100

#define COUNTDOWN_SEC          3
#define INITIAL_SETTLE_MS      4000
#define PHASE_SETTLE_MS        1500
#define PHASE_MEASURE_MS       1000
#define MEASURE_INTERVAL_MS    500
#define POLL_PERIOD_MS         10
#define ESC_KEY                0x1B

#define MAX_PITCH_DEG    35.0f
#define MAX_ROLL_DEG     40.0f

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

#define M_PI_F  3.14159265358979f

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

/* === 발 위치 상태 (hip frame) === */
static float foot_pos[NUM_LEGS][2];   /* [0]=x, [1]=z */

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
    if (fabsf(pitch) > MAX_PITCH_DEG) return false;
    if (fabsf(roll) > MAX_ROLL_DEG) return false;
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
/* IK                                                           */
/* ============================================================ */

static void ik_to_raw_offsets(float foot_x, float foot_z,
                              int* thigh_offset, int* knee_offset) {
    float r2 = foot_x * foot_x + foot_z * foot_z;
    float cos_k = (r2 - L1_THIGH_MM*L1_THIGH_MM - L2_SHIN_MM*L2_SHIN_MM)
                  / (2.0f * L1_THIGH_MM * L2_SHIN_MM);
    if (cos_k > 1.0f) cos_k = 1.0f;
    if (cos_k < -1.0f) cos_k = -1.0f;
    float sin_k = sqrtf(1.0f - cos_k * cos_k);
    float theta_k = atan2f(sin_k, cos_k);

    float A = L1_THIGH_MM + L2_SHIN_MM * cos_k;
    float B = L2_SHIN_MM * sin_k;
    float det = A*A + B*B;
    float foot_z_neg = -foot_z;
    float sin_t = (A * foot_x - B * foot_z_neg) / det;
    float cos_t = (B * foot_x + A * foot_z_neg) / det;
    float theta_t = atan2f(sin_t, cos_t);

    float t_deg = theta_t * 180.0f / M_PI_F;
    float k_deg = theta_k * 180.0f / M_PI_F;

    *thigh_offset = (int)roundf(t_deg * 4096.0f / 360.0f);
    *knee_offset  = (int)roundf(k_deg * 4096.0f / 360.0f);
}

/* foot_pos[][] → pose */
static void update_pose_from_foot(pose_t pose) {
    for (int l = 0; l < NUM_LEGS; l++) {
        int8_t s = legs[l].sign;
        int t_off, k_off;
        ik_to_raw_offsets(foot_pos[l][0], foot_pos[l][1], &t_off, &k_off);
        pose[l][0] = ZERO_POS + s * HIP_OFFSET;
        pose[l][1] = (uint16_t)(ZERO_POS + s * t_off);
        pose[l][2] = (uint16_t)(ZERO_POS + s * k_off);
    }
}

/* ============================================================ */
/* Pose / Transition                                            */
/* ============================================================ */

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
/* Phase 측정                                                   */
/* ============================================================ */

static void measure_phase(const char* name, uint32_t duration_ms) {
    int n = duration_ms / MEASURE_INTERVAL_MS;
    float pitch_sum = 0.0f, roll_sum = 0.0f;
    int n_ok = 0;

    for (int s = 0; s < n; s++) {
        if (check_esc()) emergency_stop();
        HAL_Delay(MEASURE_INTERVAL_MS);

        body_attitude_t b;
        bool imu_ok = bno_read_body(&b);
        if (imu_ok) {
            pitch_sum += b.pitch;
            roll_sum  += b.roll;
            n_ok++;
            if (!safety_check(b.pitch, b.roll)) {
                g_abort = true;
                printf("    !! ABORT pitch=%+.1f roll=%+.1f !!\r\n", b.pitch, b.roll);
                return;
            }
        }

        /* Read RR knee load */
        read_result_t r = sts_read_state(legs[WALKING_LEG_IDX].huart,
                                         legs[WALKING_LEG_IDX].servo_ids[2]);
        uint16_t rr_load = r.ok ? (r.load & 0x3FF) : 0;

        printf("    [%s] s%d: pitch=%+.2f roll=%+.2f  RR_knee=%u\r\n",
               name, s+1,
               imu_ok ? b.pitch : 0, imu_ok ? b.roll : 0,
               rr_load);
    }
}

/* ============================================================ */
/* Phase Helpers                                                */
/* ============================================================ */

static void apply_and_log(const char* phase_name, pose_t prev_pose, pose_t target_pose,
                          int speed_dps, uint32_t settle_ms, uint32_t measure_ms) {
    printf("\r\n--- %s ---\r\n", phase_name);
    printf("    Target foot positions:\r\n");
    for (int l = 0; l < NUM_LEGS; l++) {
        printf("      %s: (x=%+.0f, z=%+.0f)\r\n",
               legs[l].leg_name, foot_pos[l][0], foot_pos[l][1]);
    }

    transition_all(prev_pose, target_pose, speed_dps);
    delay_with_estop(settle_ms);

    if (measure_ms > 0) {
        measure_phase(phase_name, measure_ms);
    }
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
    printf("  Static Crawl Gait — Single Leg Cycle (RR)\r\n");
    printf("\r\n");
    printf("  Phase 0: baseline (-10, -130)\r\n");
    printf("  Phase 1: body shift (+20, -130)   <- body 30mm BACK\r\n");
    printf("  Phase 2: RR lift   (+20, -100)    <- foot 30mm UP\r\n");
    printf("  Phase 3: RR swing  (+50, -100)    <- foot 30mm FORWARD\r\n");
    printf("  Phase 4: RR place  (+50, -130)    <- foot DOWN\r\n");
    printf("  Phase 5: body advance             <- body 30mm FORWARD\r\n");
    printf("\r\n");
    printf("  Expected: body advances 30mm absolute.\r\n");
    printf("  Stride: %.0f mm,  Lift: %.0f mm,  Shift: %.0f mm\r\n",
           STRIDE_X, LEG_LIFT_Z, BODY_SHIFT_X);
    printf("\r\n");
    printf("  >>> Robot on flat surface, do NOT touch <<<\r\n");
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

    /* === Phase 0: baseline === */
    for (int l = 0; l < NUM_LEGS; l++) {
        foot_pos[l][0] = DEFAULT_FOOT_X;
        foot_pos[l][1] = DEFAULT_FOOT_Z;
    }
    pose_t default_pose;
    update_pose_from_foot(default_pose);

    printf("\r\n[6] start -> default\r\n");
    transition_all(start_pose, default_pose, SPEED_DEG_PER_SEC);

    printf("\r\n[7] Initial settle %d ms...\r\n", INITIAL_SETTLE_MS);
    delay_with_estop(INITIAL_SETTLE_MS);

    printf("\r\n=================================================\r\n");
    printf(" CRAWL CYCLE START\r\n");
    printf("=================================================\r\n");

    pose_t prev_pose, target_pose;
    memcpy(prev_pose, default_pose, sizeof(pose_t));

    /* === Phase 1: body shift (모든 발 +30) === */
    if (g_abort) goto done;
    for (int l = 0; l < NUM_LEGS; l++) foot_pos[l][0] += BODY_SHIFT_X;
    update_pose_from_foot(target_pose);
    apply_and_log("PHASE 1: body shift", prev_pose, target_pose,
                  SPEED_DEG_PER_SEC, PHASE_SETTLE_MS, PHASE_MEASURE_MS);
    memcpy(prev_pose, target_pose, sizeof(pose_t));

    /* === Phase 2: RR lift === */
    if (g_abort) goto done;
    foot_pos[WALKING_LEG_IDX][1] += LEG_LIFT_Z;   /* z 위로 */
    update_pose_from_foot(target_pose);
    apply_and_log("PHASE 2: RR lift", prev_pose, target_pose,
                  LIFT_SPEED_DEG_PER_SEC, PHASE_SETTLE_MS, PHASE_MEASURE_MS);
    memcpy(prev_pose, target_pose, sizeof(pose_t));

    /* === Phase 3: RR swing forward === */
    if (g_abort) goto done;
    foot_pos[WALKING_LEG_IDX][0] += STRIDE_X;     /* x 앞으로 */
    update_pose_from_foot(target_pose);
    apply_and_log("PHASE 3: RR swing forward", prev_pose, target_pose,
                  LIFT_SPEED_DEG_PER_SEC, PHASE_SETTLE_MS, PHASE_MEASURE_MS);
    memcpy(prev_pose, target_pose, sizeof(pose_t));

    /* === Phase 4: RR place down === */
    if (g_abort) goto done;
    foot_pos[WALKING_LEG_IDX][1] -= LEG_LIFT_Z;   /* z 내림 */
    update_pose_from_foot(target_pose);
    apply_and_log("PHASE 4: RR place down", prev_pose, target_pose,
                  LIFT_SPEED_DEG_PER_SEC, PHASE_SETTLE_MS, PHASE_MEASURE_MS);
    memcpy(prev_pose, target_pose, sizeof(pose_t));

    /* === Phase 5: body advance (모든 발 -30, 본체 30mm 앞으로) === */
    if (g_abort) goto done;
    for (int l = 0; l < NUM_LEGS; l++) foot_pos[l][0] -= BODY_SHIFT_X;
    update_pose_from_foot(target_pose);
    apply_and_log("PHASE 5: body advance", prev_pose, target_pose,
                  SPEED_DEG_PER_SEC, PHASE_SETTLE_MS, PHASE_MEASURE_MS);
    memcpy(prev_pose, target_pose, sizeof(pose_t));

    printf("\r\n=================================================\r\n");
    printf(" CYCLE COMPLETE\r\n");
    printf("\r\n");
    printf(" Body advanced approximately %d mm.\r\n", (int)BODY_SHIFT_X);
    printf(" Final foot positions (hip frame):\r\n");
    for (int l = 0; l < NUM_LEGS; l++) {
        printf("   %s: (x=%+.0f, z=%+.0f)\r\n",
               legs[l].leg_name, foot_pos[l][0], foot_pos[l][1]);
    }
    printf("=================================================\r\n");

done:
    /* === Return: foot all default → start === */
    printf("\r\n[Final] Returning all feet to default...\r\n");
    for (int l = 0; l < NUM_LEGS; l++) {
        foot_pos[l][0] = DEFAULT_FOOT_X;
        foot_pos[l][1] = DEFAULT_FOOT_Z;
    }
    update_pose_from_foot(target_pose);
    transition_all(prev_pose, target_pose, SPEED_DEG_PER_SEC);
    delay_with_estop(1000);

    printf("\r\n[Final] default -> start\r\n");
    transition_all(target_pose, start_pose, SPEED_DEG_PER_SEC);

    printf("\r\n[Final] Torque OFF.\r\n");
    torque_off_all();

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
