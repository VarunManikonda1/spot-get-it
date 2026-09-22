/*
 * Default Pose v3 (Case A) Baseline Measurement
 *
 * v1 측정 결과 (-774, +1338) → foot_x = +1.6mm, knee 토크 부담 큼
 * Case A: foot_x를 정확히 0으로 + 다리 약간 더 펴서 knee 토크 감소
 *
 *   thigh offset = -753  (-66.2°)
 *   knee  offset = +1295 (+113.8°)
 *   foot_x = 0 mm   (hip 정확히 아래)
 *   foot_z = -130 mm  (이전 -123.6mm에서 6.4mm 더 펴짐)
 *
 * v1 대비 변화:
 *   thigh: -774 → -753 (1.8° 더 펴짐)
 *   knee:  +1338 → +1295 (3.8° 더 펴짐)
 *
 * 기대 효과:
 *   - foot_x = 0 → 무게 분배 더 균형 (Front:Rear)
 *   - knee 덜 굽혀짐 → knee 토크 감소
 *   - 본체 6.4mm 높아짐
 *
 * IMUPLUS 모드 (자기 간섭 면역).
 * 정지: ESC
 */

/* ===== ★ NEW Default pose ★ ===== */
#define HIP_OFFSET           0
#define THIGH_OFFSET      (-801)   /* -66.2° (Case A: foot_x=0) */
#define KNEE_OFFSET       (+1292)  /* +113.8° (Case A: foot_x=0) */
#define ZERO_POS           2048

/* ===== 모션 ===== */
#define SPEED_DEG_PER_SEC    20
#define STEP_MS              25
#define MIN_TRANSITION_MS    100

#define COUNTDOWN_SEC          3
#define SETTLE_MS              5000
#define MONITOR_DURATION_MS    10000
#define MONITOR_INTERVAL_MS    1000
#define POLL_PERIOD_MS         10
#define ESC_KEY                0x1B

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
#define BNO055_CALIB_STAT         0x35
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
static const char *joint_names[SERVOS_PER_LEG] = { "hip  ", "thigh", "knee " };

typedef uint16_t pose_t[NUM_LEGS][SERVOS_PER_LEG];

typedef struct {
    bool ok;
    uint8_t err_byte;
    uint16_t position;
    uint16_t speed;
    uint16_t load;
    uint8_t voltage;
    uint8_t temperature;
} read_result_t;

typedef struct { bool ok; uint8_t err_byte; } write_result_t;

typedef struct { float yaw; float pitch; float roll; } body_attitude_t;

typedef struct {
    int knee_sum[NUM_LEGS];
    int knee_max[NUM_LEGS];
    int thigh_sum[NUM_LEGS];
    float yaw_sum;
    float pitch_sum;
    float roll_sum;
    int n_samples;
} stats_t;

static uint8_t bno_addr = BNO055_I2C_ADDR_DEFAULT;

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
/* ESC                                                          */
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

static void bno_read_calib(uint8_t* sys, uint8_t* gyr, uint8_t* acc, uint8_t* mag) {
    uint8_t c = 0;
    bno_read_byte(BNO055_CALIB_STAT, &c);
    *sys = (c >> 6) & 0x03;
    *gyr = (c >> 4) & 0x03;
    *acc = (c >> 2) & 0x03;
    *mag = (c     ) & 0x03;
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

static void apply_pose_all(const pose_t pose) {
    for (int l = 0; l < NUM_LEGS; l++) {
        sts_sync_write_goal(legs[l].huart, legs[l].servo_ids,
                            pose[l], SERVOS_PER_LEG);
    }
}

static void transition_all(const pose_t from, const pose_t to) {
    int max_delta = 0;
    for (int l = 0; l < NUM_LEGS; l++) {
        for (int j = 0; j < SERVOS_PER_LEG; j++) {
            int d = abs_int((int)to[l][j] - (int)from[l][j]);
            if (d > max_delta) max_delta = d;
        }
    }
    if (max_delta == 0) return;

    int speed_unit_per_sec = SPEED_DEG_PER_SEC * 4096 / 360;
    uint32_t total_ms = (uint32_t)max_delta * 1000 / (uint32_t)speed_unit_per_sec;
    if (total_ms < MIN_TRANSITION_MS) total_ms = MIN_TRANSITION_MS;

    int steps = (int)(total_ms / STEP_MS);
    if (steps < 4) steps = 4;

    printf("    transition: max_delta=%d, total=%lu ms, %d steps\r\n",
           max_delta, total_ms, steps);

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
/* 모니터링 + 통계                                               */
/* ============================================================ */

static void sample_loads_and_imu(stats_t* stats) {
    for (int l = 0; l < NUM_LEGS; l++) {
        printf("    %s: ", legs[l].leg_name);
        for (int j = 0; j < SERVOS_PER_LEG; j++) {
            read_result_t r = sts_read_state(legs[l].huart, legs[l].servo_ids[j]);
            if (r.ok) {
                int diff = (int)r.position - ZERO_POS;
                uint16_t lmag = r.load & 0x3FF;
                char ldir = (r.load & 0x400) ? 'C' : 'W';
                printf("[%s %4u(%+5d) %c%-3u] ",
                       joint_names[j], r.position, diff, ldir, lmag);
                if (j == 1) {
                    stats->thigh_sum[l] += lmag;
                } else if (j == 2) {
                    stats->knee_sum[l] += lmag;
                    if (lmag > stats->knee_max[l]) stats->knee_max[l] = lmag;
                }
            }
        }
        printf("\r\n");
    }

    body_attitude_t b;
    if (bno_read_body(&b)) {
        uint8_t s, g, a, m;
        bno_read_calib(&s, &g, &a, &m);
        printf("    BODY: pitch=%+7.2f roll=%+7.2f yaw=%+7.2f | calib G=%d A=%d\r\n",
               b.pitch, b.roll, b.yaw, g, a);
        stats->yaw_sum   += b.yaw;
        stats->pitch_sum += b.pitch;
        stats->roll_sum  += b.roll;
    }
    stats->n_samples++;
}

static void print_analysis(const stats_t* stats) {
    if (stats->n_samples == 0) return;

    int knee_avg[NUM_LEGS];
    int thigh_avg[NUM_LEGS];
    for (int l = 0; l < NUM_LEGS; l++) {
        knee_avg[l]  = stats->knee_sum[l]  / stats->n_samples;
        thigh_avg[l] = stats->thigh_sum[l] / stats->n_samples;
    }
    float pitch_avg = stats->pitch_sum / stats->n_samples;
    float roll_avg  = stats->roll_sum  / stats->n_samples;

    printf("\r\n=================================================\r\n");
    printf("  NEW DEFAULT BASELINE  (n=%d)\r\n", stats->n_samples);
    printf("  thigh offset = %d  (%.1f deg)\r\n",
           THIGH_OFFSET, THIGH_OFFSET * 360.0f / 4096.0f);
    printf("  knee  offset = %d  (%.1f deg)\r\n",
           KNEE_OFFSET, KNEE_OFFSET * 360.0f / 4096.0f);
    printf("=================================================\r\n");

    printf("\r\n  Body attitude (avg):\r\n");
    printf("    Pitch = %+7.2f deg\r\n", pitch_avg);
    printf("    Roll  = %+7.2f deg\r\n", roll_avg);

    printf("\r\n  Knee load per leg:\r\n");
    for (int l = 0; l < NUM_LEGS; l++) {
        printf("    %s : avg=%4d  max=%4d  (thigh avg=%4d)\r\n",
               legs[l].leg_name, knee_avg[l], stats->knee_max[l], thigh_avg[l]);
    }

    int front = knee_avg[IDX_FL] + knee_avg[IDX_FR];
    int rear  = knee_avg[IDX_RL] + knee_avg[IDX_RR];
    int total = front + rear;
    if (total > 0) {
        float front_pct = 100.0f * front / total;
        float rear_pct  = 100.0f * rear  / total;
        float d_over_L  = 0.5f - (float)front / (float)total;
        printf("\r\n  Front : Rear knee load:\r\n");
        printf("    Front (FL+FR) = %4d  (%5.1f %%)\r\n", front, front_pct);
        printf("    Rear  (RL+RR) = %4d  (%5.1f %%)\r\n", rear,  rear_pct);
        printf("    d/L = %+.3f   (Estimated d = %+.1f mm @ L=180)\r\n",
               d_over_L, d_over_L * 180.0f);
    }

    int left  = knee_avg[IDX_FL] + knee_avg[IDX_RL];
    int right = knee_avg[IDX_FR] + knee_avg[IDX_RR];
    int total_lr = left + right;
    if (total_lr > 0) {
        printf("\r\n  Left : Right knee load:\r\n");
        printf("    Left  (FL+RL) = %4d  (%5.1f %%)\r\n",
               left,  100.0f * left  / total_lr);
        printf("    Right (FR+RR) = %4d  (%5.1f %%)\r\n",
               right, 100.0f * right / total_lr);
    }

    int diag1 = knee_avg[IDX_FR] + knee_avg[IDX_RL];
    int diag2 = knee_avg[IDX_FL] + knee_avg[IDX_RR];
    printf("\r\n  Diagonals (trot stance):\r\n");
    printf("    FR+RL = %4d\r\n", diag1);
    printf("    FL+RR = %4d\r\n", diag2);
    if (diag1 + diag2 > 0) {
        printf("    imbalance: %+.1f %%\r\n",
               100.0f * (diag1 - diag2) / (diag1 + diag2));
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
    printf("  NEW Default Pose Baseline\r\n");
    printf("  thigh = %+d (%.1f deg), knee = %+d (%.1f deg)\r\n",
           THIGH_OFFSET, THIGH_OFFSET * 360.0f / 4096.0f,
           KNEE_OFFSET, KNEE_OFFSET * 360.0f / 4096.0f);
    printf("  IMUPLUS mode (mag OFF)\r\n");
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
    printf("    >>> Hold body so legs hang freely <<<\r\n");
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

    printf("\r\n[6] start -> NEW default\r\n");
    transition_all(start_pose, default_pose);

    printf("\r\n[7] Settling %d ms...\r\n", SETTLE_MS);
    delay_with_estop(SETTLE_MS);

    printf("\r\n[8] Monitoring (DO NOT TOUCH the body)...\r\n");
    stats_t stats = {0};
    uint32_t mon_start = HAL_GetTick();
    while (HAL_GetTick() - mon_start < MONITOR_DURATION_MS) {
        if (check_esc()) emergency_stop();
        HAL_Delay(MONITOR_INTERVAL_MS);
        sample_loads_and_imu(&stats);
        printf("\r\n");
    }

    print_analysis(&stats);

    printf("\r\n[9] default -> start\r\n");
    transition_all(default_pose, start_pose);

    printf("\r\n[10] Torque OFF...\r\n");
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
                r.err_byte    = resp[4];
                r.position    = resp[5]  | ((uint16_t)resp[6]  << 8);
                r.speed       = resp[7]  | ((uint16_t)resp[8]  << 8);
                r.load        = resp[9]  | ((uint16_t)resp[10] << 8);
                r.voltage     = resp[11];
                r.temperature = resp[12];
                r.ok          = (resp[4] == 0);
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
                r.err_byte = resp[4];
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
