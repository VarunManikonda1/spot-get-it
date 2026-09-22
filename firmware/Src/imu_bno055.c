#include "imu_bno055.h"
#include <math.h>
#include <stdio.h>

#define BNO055_I2C_ADDR_DEFAULT   0x28
#define BNO055_I2C_ADDR_ALT       0x29
#define BNO055_CHIP_ID            0x00
#define BNO055_ACC_DATA_X_LSB     0x08
#define BNO055_GYR_DATA_X_LSB     0x14
#define BNO055_EUL_HEADING_LSB    0x1A
#define BNO055_QUA_DATA_W_LSB     0x20
#define BNO055_OPR_MODE           0x3D
#define BNO055_OPR_CONFIG         0x00
#define BNO055_OPR_IMUPLUS        0x08
#define BNO055_CHIP_ID_EXPECTED   0xA0

/* === Axis remap registers (BNO055 datasheet 4.4.1) ===
 * 목표: chip output 을 RL 학습 컨벤션 (FLU body frame + 표준 proper accel)
 *       으로 출력. STM/ROS 측 변환 모두 identity 유지.
 *
 * 학습 컨벤션 (legged_gym/legged_robot.py:518, :533):
 *   - body frame: X=forward, Y=left, Z=up
 *   - 정자세 IMU accel = (0, 0, +9.8)   (proper accel, 중력 반대 방향)
 *   - projected_gravity = -accel / |accel| = (0, 0, -1)
 *
 * 사용자 마운트 확정 (0x24+0x00 P1 default 빌드의 3자세 측정):
 *   정자세    -> az ≈ +9.6  (chip out_Z 가 robot +Z up 과 정렬)
 *   왼다리 밑 -> ay ≈ +9.4  (chip out_Y 가 robot -Y right 와 정렬)
 *   머리 위   -> ax ≈ -9.7  (chip out_X 가 robot -X backward 와 정렬)
 *
 * 즉 chip 의 P1 default output 이 robot FLU 와 비교해 X, Y 만 부호 반대.
 * Z 는 정렬. 따라서 CONFIG 는 P1 default 그대로 두고 SIGN 만 X, Y negate.
 *
 *   AXIS_MAP_CONFIG = 0x24  (P1 default — datasheet table 3-7)
 *   AXIS_MAP_SIGN bit2=X_neg, bit1=Y_neg, bit0=Z_neg
 *     -> X, Y negate, Z positive = 0b00000_110 = 0x06
 *
 * 효과: chip 이 학습 frame 으로 직접 출력 → SW remap 불필요.
 */
#define BNO055_AXIS_MAP_CONFIG    0x41
#define BNO055_AXIS_MAP_SIGN      0x42
#define BNO055_AXIS_CONFIG        0x24
#define BNO055_AXIS_SIGN          0x06

#define BNO055_ACCEL_SCALE        (1.0f / 100.0f)
#define BNO055_GYRO_SCALE         (1.0f / 16.0f * ((float)M_PI / 180.0f))
#define BNO055_QUAT_SCALE         (1.0f / 16384.0f)
#define BNO055_EULER_SCALE        (1.0f / 16.0f)
#define QUAT_NORM_MIN             0.5f

/* init에서 결정된 주소를 read_body가 사용 */
static uint8_t bno_addr = BNO055_I2C_ADDR_DEFAULT;

static bool bno_read_byte(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t* val) {
    return (HAL_I2C_Mem_Read(hi2c, bno_addr << 1, reg,
                             I2C_MEMADD_SIZE_8BIT, val, 1, 100) == HAL_OK);
}

static bool bno_write_byte(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t val) {
    return (HAL_I2C_Mem_Write(hi2c, bno_addr << 1, reg,
                              I2C_MEMADD_SIZE_8BIT, &val, 1, 100) == HAL_OK);
}

static bool bno_read_bytes(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t* buf, uint8_t len) {
    return (HAL_I2C_Mem_Read(hi2c, bno_addr << 1, reg,
                             I2C_MEMADD_SIZE_8BIT, buf, len, 100) == HAL_OK);
}

static const char *hal_status_name(HAL_StatusTypeDef status) {
    switch (status) {
        case HAL_OK:      return "HAL_OK";
        case HAL_ERROR:   return "HAL_ERROR";
        case HAL_BUSY:    return "HAL_BUSY";
        case HAL_TIMEOUT: return "HAL_TIMEOUT";
        default:          return "HAL_UNKNOWN";
    }
}

static void bno_print_i2c_diag(I2C_HandleTypeDef *hi2c, const char *tag) {
    printf("[I2C %s] State=0x%02X Lock=0x%02X Error=0x%08lX BUSY=%lu SR1=0x%04lX SR2=0x%04lX\r\n",
           tag,
           (unsigned int)hi2c->State,
           (unsigned int)hi2c->Lock,
           (unsigned long)HAL_I2C_GetError(hi2c),
           (unsigned long)(__HAL_I2C_GET_FLAG(hi2c, I2C_FLAG_BUSY) ? 1UL : 0UL),
           (unsigned long)hi2c->Instance->SR1,
           (unsigned long)hi2c->Instance->SR2);
}

static void bno055_i2c_bus_recover(I2C_HandleTypeDef *hi2c) {
    printf("[I2C recover] start\r\n");

    HAL_I2C_DeInit(hi2c);
    if (hi2c->Instance == I2C1) {
        __HAL_RCC_I2C1_FORCE_RESET();
        __HAL_RCC_I2C1_RELEASE_RESET();
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gp = {0};
    gp.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    gp.Mode = GPIO_MODE_OUTPUT_OD;
    gp.Pull = GPIO_PULLUP;
    gp.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gp);

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_SET);
    HAL_Delay(2);
    printf("[I2C recover] idle pins SCL=%d SDA=%d\r\n",
           (int)HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8),
           (int)HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9));

    for (int i = 0; i < 9; i++) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
        HAL_Delay(1);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
        HAL_Delay(1);
    }

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);
    HAL_Delay(2);

    printf("[I2C recover] after clocks SCL=%d SDA=%d\r\n",
           (int)HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8),
           (int)HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9));

    if (HAL_I2C_Init(hi2c) != HAL_OK) {
        printf("[I2C recover] HAL_I2C_Init failed, ErrorCode=0x%08lX\r\n",
               (unsigned long)HAL_I2C_GetError(hi2c));
    }
}

bool bno055_init_imuplus(I2C_HandleTypeDef *hi2c) {
    HAL_Delay(700);
    bno_print_i2c_diag(hi2c, "before");

    if (__HAL_I2C_GET_FLAG(hi2c, I2C_FLAG_BUSY)) {
        bno055_i2c_bus_recover(hi2c);
        bno_print_i2c_diag(hi2c, "after recovery");
    }

    HAL_StatusTypeDef ready28 =
        HAL_I2C_IsDeviceReady(hi2c, BNO055_I2C_ADDR_DEFAULT << 1, 3, 50);
    uint32_t err28 = HAL_I2C_GetError(hi2c);
    bno_print_i2c_diag(hi2c, "after 0x28");
    printf("[BNO055] IsDeviceReady 0x28 -> %s (%d), ErrorCode=0x%08lX\r\n",
           hal_status_name(ready28), (int)ready28, (unsigned long)err28);

    if (ready28 == HAL_OK) {
        bno_addr = BNO055_I2C_ADDR_DEFAULT;
        printf("[BNO055] addr=0x28 ready\r\n");
    } else {
        HAL_StatusTypeDef ready29 =
            HAL_I2C_IsDeviceReady(hi2c, BNO055_I2C_ADDR_ALT << 1, 3, 50);
        uint32_t err29 = HAL_I2C_GetError(hi2c);
        bno_print_i2c_diag(hi2c, "after 0x29");
        printf("[BNO055] IsDeviceReady 0x29 -> %s (%d), ErrorCode=0x%08lX\r\n",
               hal_status_name(ready29), (int)ready29, (unsigned long)err29);

        if (ready29 != HAL_OK) {
            printf("[BNO055] no ACK at 0x28/0x29\r\n");
            return false;
        }

        bno_addr = BNO055_I2C_ADDR_ALT;
        printf("[BNO055] addr=0x29 ready\r\n");
    }

    uint8_t chip_id = 0;
    if (!bno_read_byte(hi2c, BNO055_CHIP_ID, &chip_id)) {
        printf("[BNO055] CHIP_ID read failed\r\n");
        return false;
    }
    if (chip_id != BNO055_CHIP_ID_EXPECTED) {
        printf("[BNO055] bad CHIP_ID=0x%02X expected=0x%02X\r\n",
               chip_id, BNO055_CHIP_ID_EXPECTED);
        return false;
    }
    printf("[BNO055] CHIP_ID OK\r\n");

    if (!bno_write_byte(hi2c, BNO055_OPR_MODE, BNO055_OPR_CONFIG)) {
        printf("[BNO055] CONFIG mode write failed\r\n");
        return false;
    }
    HAL_Delay(25);

    /* Axis remap — CONFIG 모드에서만 변경 가능.
     * P6 placement: chip 출력이 robot body frame 으로 통일됨 (모든 downstream 일관). */
    if (!bno_write_byte(hi2c, BNO055_AXIS_MAP_CONFIG, BNO055_AXIS_CONFIG)) {
        printf("[BNO055] axis config write failed\r\n");
        return false;
    }
    HAL_Delay(10);
    if (!bno_write_byte(hi2c, BNO055_AXIS_MAP_SIGN, BNO055_AXIS_SIGN)) {
        printf("[BNO055] axis sign write failed\r\n");
        return false;
    }
    HAL_Delay(10);

    if (!bno_write_byte(hi2c, BNO055_OPR_MODE, BNO055_OPR_IMUPLUS)) {
        printf("[BNO055] IMUPLUS mode write failed\r\n");
        return false;
    }
    HAL_Delay(20);
    return true;
}

bool bno055_read_body(I2C_HandleTypeDef *hi2c, body_attitude_t *body) {
    /* 0x08~0x27: accel(6) + mag(6,skip) + gyro(6) + euler(6) + quat(8) = 32 byte burst */
    uint8_t buf[32];
    if (!bno_read_bytes(hi2c, BNO055_ACC_DATA_X_LSB, buf, 32)) {
        body->data_valid = false;
        return false;
    }

    /* --- Accel (0x08~0x0D) → buf[0..5] --- */
    int16_t ax_raw = (int16_t)((uint16_t)buf[0]  | ((uint16_t)buf[1]  << 8));
    int16_t ay_raw = (int16_t)((uint16_t)buf[2]  | ((uint16_t)buf[3]  << 8));
    int16_t az_raw = (int16_t)((uint16_t)buf[4]  | ((uint16_t)buf[5]  << 8));
    /* buf[6..11]: MAG — skip */

    /* --- Gyro (0x14~0x19) → buf[12..17] --- */
    int16_t gx_raw = (int16_t)((uint16_t)buf[12] | ((uint16_t)buf[13] << 8));
    int16_t gy_raw = (int16_t)((uint16_t)buf[14] | ((uint16_t)buf[15] << 8));
    int16_t gz_raw = (int16_t)((uint16_t)buf[16] | ((uint16_t)buf[17] << 8));

    /* --- Euler (0x1A~0x1F) → buf[18..23] --- */
    int16_t h_raw  = (int16_t)((uint16_t)buf[18] | ((uint16_t)buf[19] << 8));
    int16_t r_raw  = (int16_t)((uint16_t)buf[20] | ((uint16_t)buf[21] << 8));
    int16_t p_raw  = (int16_t)((uint16_t)buf[22] | ((uint16_t)buf[23] << 8));

    /* --- Quaternion (0x20~0x27) → buf[24..31] --- */
    int16_t qw_raw = (int16_t)((uint16_t)buf[24] | ((uint16_t)buf[25] << 8));
    int16_t qx_raw = (int16_t)((uint16_t)buf[26] | ((uint16_t)buf[27] << 8));
    int16_t qy_raw = (int16_t)((uint16_t)buf[28] | ((uint16_t)buf[29] << 8));
    int16_t qz_raw = (int16_t)((uint16_t)buf[30] | ((uint16_t)buf[31] << 8));

    /* Euler — chip 출력이 ROS REP-103 과 부호 반대 (3축 모두 검증 완료).
     * SW 에서 sign flip 으로 정합. register naming swap 은 유지:
     *   BNO055 EUL_ROLL  register = standard pitch (rotation around Y, ±90°)
     *   BNO055 EUL_PITCH register = standard roll  (rotation around X, ±180°)
     */
    body->yaw   = -(h_raw * BNO055_EULER_SCALE);
    body->pitch =  r_raw * BNO055_EULER_SCALE;
    body->roll  =  p_raw * BNO055_EULER_SCALE;

    /* yaw wrap to ±180° — chip HEADING register 가 0~360° unsigned 라
     * sign flip 결과 -360~0 범위. ROS 표준 ±π 형태로 정리. */
    if (body->yaw < -180.0f) body->yaw += 360.0f;

    /* Gyro → rad/s. axis_map (0x24+0x06) 적용 후 output frame 이 left-handed
     * (det(T)=-1) 라 axial vector 부호가 vector 변환과 차이 발생.
     * 측정 검증 결과 gy 만 REP-103 과 부호 반대 → SW flip. gx, gz 는 일치. */
    float gx = gx_raw * BNO055_GYRO_SCALE;
    float gy = gy_raw * BNO055_GYRO_SCALE;
    float gz = gz_raw * BNO055_GYRO_SCALE;
    body->gyro[0] =  gx;
    body->gyro[1] = -gy;   /* sign flip — REP-103 정합 */
    body->gyro[2] =  gz;

    /* Accel → m/s². Chip P6 remap 후 출력이 이미 body frame.
     * 정자세 검증: az ≈ -9.8 (Z up → 중력은 -Z 방향). */
    float ax = ax_raw * BNO055_ACCEL_SCALE;
    float ay = ay_raw * BNO055_ACCEL_SCALE;
    float az = az_raw * BNO055_ACCEL_SCALE;

    body->accel[0] = ax;
    body->accel[1] = ay;
    body->accel[2] = az;

    /* Quaternion → normalized. Chip P6 remap 후 출력이 이미 body frame. */
    float qw = qw_raw * BNO055_QUAT_SCALE;
    float qx = qx_raw * BNO055_QUAT_SCALE;
    float qy = qy_raw * BNO055_QUAT_SCALE;
    float qz = qz_raw * BNO055_QUAT_SCALE;

    float norm = sqrtf(qw * qw + qx * qx + qy * qy + qz * qz);
    if (norm < QUAT_NORM_MIN) {
        body->quat[0] = 1.0f;
        body->quat[1] = 0.0f;
        body->quat[2] = 0.0f;
        body->quat[3] = 0.0f;
    } else {
        float inv = 1.0f / norm;
        body->quat[0] = qw * inv;
        body->quat[1] = qx * inv;
        body->quat[2] = qy * inv;
        body->quat[3] = qz * inv;
    }

    body->data_valid = true;
    return true;
}
