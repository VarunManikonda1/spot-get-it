/*
 * IMU Mapping Verification (IMUPLUS mode) — 6-Step Test
 *
 * IMUPLUS 모드: Accel + Gyro만 사용 (Magnetometer OFF).
 *   - Pitch/Roll: 정확 (중력 기반)
 *   - Yaw: 시작 시점 기준 상대값, 시간 지나면 drift
 *   - 자기장 간섭 완전 면역 ★
 *
 * SpotMicro에서 모터 + 젯슨 + STM32 자기장 노이즈가 강해서
 * NDOF (mag 사용)는 사용 불가. IMUPLUS가 실용적 선택.
 *
 * Steps:
 *   1: BASELINE  — 평평히, 안 건드림
 *   2: FRONT UP  — 앞 들기 (head up)
 *   3: REAR UP   — 뒤 들기 (head down)
 *   4: LEFT UP   — 좌측 들기
 *   5: RIGHT UP  — 우측 들기
 *   6: BASELINE  — 다시 평평히
 *
 * Mapping (검증된 값):
 *   body_pitch = -imu_raw_roll   (head up = +)
 *   body_roll  = -imu_raw_pitch  (left up = +)
 *   body_yaw   = imu_heading (상대값, drift 가능)
 *
 * Calibration:
 *   GYR=3, ACC=3 만 필요 (MAG는 안 씀, MAG=0 정상)
 *
 * 정지: ESC
 */

/* ===== Default pose ===== */
#define HIP_OFFSET           0
#define THIGH_OFFSET      (-512)
#define KNEE_OFFSET       (+1024)
#define ZERO_POS           2048

#define SPEED_DEG_PER_SEC    20
#define STEP_MS              25
#define MIN_TRANSITION_MS    100

#define COUNTDOWN_SEC          3
#define SETTLE_MS              3000
#define STEP_PREP_SEC          3
#define STEP_MEASURE_MS        5000
#define STEP_MEASURE_INTERVAL  500
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

/* ===== BNO055 ===== */
#define BNO055_I2C_ADDR_DEFAULT   0x28
#define BNO055_I2C_ADDR_ALT       0x29
#define BNO055_CHIP_ID            0x00
#define BNO055_EUL_HEADING_LSB    0x1A
#define BNO055_CALIB_STAT         0x35
#define BNO055_OPR_MODE           0x3D
#define BNO055_OPR_CONFIG         0x00
#define BNO055_OPR_IMUPLUS        0x08    /* ★ 6-DOF, no mag ★ */
#define BNO055_OPR_NDOF           0x0C
#define BNO055_CHIP_ID_EXPECTED   0xA0

#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

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

typedef struct {
    float yaw;
    float pitch;
    float roll;
} body_attitude_t;

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
/* BNO055                                                       */
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
        printf("    [FAIL] BNO055 not responding\r\n");
        return false;
    }
    printf("    Found BNO055 at 0x%02X\r\n", bno_addr);

    uint8_t chip_id = 0;
    if (!bno_read_byte(BNO055_CHIP_ID, &chip_id) || chip_id != BNO055_CHIP_ID_EXPECTED) {
        printf("    [FAIL] CHIP_ID = 0x%02X\r\n", chip_id);
        return false;
    }
    printf("    CHIP_ID = 0x%02X OK\r\n", chip_id);

    /* CONFIG → IMUPLUS 모드 (mag OFF) */
    if (!bno_write_byte(BNO055_OPR_MODE, BNO055_OPR_CONFIG)) return false;
    HAL_Delay(25);

    if (!bno_write_byte(BNO055_OPR_MODE, BNO055_OPR_IMUPLUS)) return false;
    HAL_Delay(20);

    /* Verify */
    uint8_t mode = 0;
    bno_read_byte(BNO055_OPR_MODE, &mode);
    printf("    OPR_MODE = 0x%02X  %s\r\n", mode,
           (mode == BNO055_OPR_IMUPLUS) ? "(IMUPLUS, OK)" : "(unexpected)");

    return (mode == BNO055_OPR_IMUPLUS);
}

static bool bno_read_body(body_attitude_t* body) {
    uint8_t buf[6];
    if (!bno_read_bytes(BNO055_EUL_HEADING_LSB, buf, 6)) return false;

    int16_t h_raw = (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
    int16_t r_raw = (int16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
    int16_t p_raw = (int16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8));

    float imu_h = h_raw / 16.0f;
    float imu_r = r_raw / 16.0f;
    float imu_p = p_raw / 16.0f;

    /* IMUPLUS: yaw는 상대값 (시작 시점 기준), pitch/roll은 정확 */
    body->yaw   = imu_h;          /* 그대로 (180° offset 없음, 상대값이니까) */
    body->pitch = -imu_r;         /* head up = + */
    body->roll  = -imu_p;         /* left up = + */

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
/* Step 측정                                                    */
/* ============================================================ */

typedef enum {
    EXPECT_LEVEL,
    EXPECT_PITCH_POS,
    EXPECT_PITCH_NEG,
    EXPECT_ROLL_POS,
    EXPECT_ROLL_NEG
} expectation_t;

static void run_step(int step_num, const char* action, const char* instruction,
                     expectation_t expect) {
    printf("\r\n");
    printf("=================================================\r\n");
    printf(" STEP %d: %s\r\n", step_num, action);
    printf("=================================================\r\n");
    printf(" >>> %s <<<\r\n", instruction);

    for (int i = STEP_PREP_SEC; i > 0; i--) {
        printf("    starting in %d...\r\n", i);
        delay_with_estop(1000);
    }

    printf("    *** GO! Hold position for %d seconds ***\r\n", STEP_MEASURE_MS / 1000);

    int n_samples = STEP_MEASURE_MS / STEP_MEASURE_INTERVAL;
    float pitch_sum = 0.0f, roll_sum = 0.0f, yaw_sum = 0.0f;
    int n_ok = 0;

    for (int s = 0; s < n_samples; s++) {
        if (check_esc()) emergency_stop();
        HAL_Delay(STEP_MEASURE_INTERVAL);

        body_attitude_t b;
        if (bno_read_body(&b)) {
            uint8_t sys_c, gyr_c, acc_c, mag_c;
            bno_read_calib(&sys_c, &gyr_c, &acc_c, &mag_c);
            printf("      sample %2d: pitch=%+7.2f  roll=%+7.2f  yaw=%+7.2f  | calib G=%d A=%d\r\n",
                   s + 1, b.pitch, b.roll, b.yaw, gyr_c, acc_c);
            pitch_sum += b.pitch;
            roll_sum  += b.roll;
            yaw_sum   += b.yaw;
            n_ok++;
        }
    }

    if (n_ok == 0) {
        printf("    [NO DATA]\r\n");
        return;
    }

    float pitch_avg = pitch_sum / n_ok;
    float roll_avg  = roll_sum  / n_ok;
    float yaw_avg   = yaw_sum   / n_ok;

    printf("\r\n   --- AVG (%d samples) ---\r\n", n_ok);
    printf("    pitch = %+7.2f deg\r\n", pitch_avg);
    printf("    roll  = %+7.2f deg\r\n", roll_avg);
    printf("    yaw   = %+7.2f deg (relative, drift possible)\r\n", yaw_avg);

    bool pass = false;
    const char* expected_str = "";
    switch (expect) {
        case EXPECT_LEVEL:
            pass = (pitch_avg > -8.0f && pitch_avg < 12.0f) &&
                   (roll_avg  > -8.0f && roll_avg  < 12.0f);
            expected_str = "pitch & roll near 0 (with small body tilt)";
            break;
        case EXPECT_PITCH_POS:
            pass = (pitch_avg > 10.0f);
            expected_str = "pitch > +10 (head up)";
            break;
        case EXPECT_PITCH_NEG:
            pass = (pitch_avg < -10.0f);
            expected_str = "pitch < -10 (head down)";
            break;
        case EXPECT_ROLL_POS:
            pass = (roll_avg > 10.0f);
            expected_str = "roll > +10 (left up)";
            break;
        case EXPECT_ROLL_NEG:
            pass = (roll_avg < -10.0f);
            expected_str = "roll < -10 (right up)";
            break;
    }

    printf("\r\n    Expected: %s\r\n", expected_str);
    printf("    Result:   %s\r\n", pass ? "*** PASS ***" : "*** FAIL ***");
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
    printf("  IMU Mapping Verification — IMUPLUS mode\r\n");
    printf("  6-DOF (acc + gyr only, mag DISABLED)\r\n");
    printf("  Immune to magnetic interference from motors/Jetson.\r\n");
    printf("  Body axes: head=+x, left=+y, up=+z\r\n");
    printf("  Calib goal: GYR=3, ACC=3 (MAG ignored)\r\n");
    printf("=================================================\r\n");

    /* === 1. PING === */
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

    /* === 2. BNO055 IMUPLUS init === */
    printf("\r\n[2] BNO055 IMUPLUS init...\r\n");
    if (!bno_init_imuplus()) { printf("[ABORT]\r\n"); while (1) {} }
    printf("    OK.\r\n");

    /* === 3. Torque OFF + Countdown === */
    printf("\r\n[3] Torque OFF...\r\n");
    torque_off_all();

    printf("\r\n[4] Starting in %d s — robot on flat surface\r\n", COUNTDOWN_SEC);
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

    for (int l = 0; l < NUM_LEGS; l++) {
        for (int j = 0; j < SERVOS_PER_LEG; j++) {
            sts_write_byte(legs[l].huart, legs[l].servo_ids[j],
                           REG_TORQUE_ENABLE, 1);
        }
    }
    delay_with_estop(300);

    pose_t default_pose;
    compute_default_pose(default_pose);
    printf("\r\n[5] Moving to default pose...\r\n");
    transition_all(start_pose, default_pose);

    printf("\r\n[6] Settling %d ms...\r\n", SETTLE_MS);
    delay_with_estop(SETTLE_MS);

    /* === GUIDED 6 STEPS === */

    run_step(1, "BASELINE",
             "Keep robot still on flat surface, do NOT touch",
             EXPECT_LEVEL);

    run_step(2, "FRONT UP (head up)",
             "Lift FRONT of body (head end UP)",
             EXPECT_PITCH_POS);

    run_step(3, "REAR UP (head down)",
             "Lift REAR of body (tail UP)",
             EXPECT_PITCH_NEG);

    run_step(4, "LEFT UP",
             "Lift LEFT side of body",
             EXPECT_ROLL_POS);

    run_step(5, "RIGHT UP",
             "Lift RIGHT side of body",
             EXPECT_ROLL_NEG);

    run_step(6, "BASELINE (final)",
             "Place robot back flat, do NOT touch",
             EXPECT_LEVEL);

    printf("\r\n=================================================\r\n");
    printf(" Returning to start, torque OFF.\r\n");
    printf("=================================================\r\n");
    transition_all(default_pose, start_pose);
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
