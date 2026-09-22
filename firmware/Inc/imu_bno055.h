#ifndef IMU_BNO055_H
#define IMU_BNO055_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>

/*
 * BNO055 IMU 드라이버 (IMUPLUS 모드: gyro + accel only, mag 안 씀).
 * I2C handle은 외부에서 초기화 후 핸들 포인터로 전달.
 */

typedef struct {
    /* 기존 — safety_check, debug print용 (deg) */
    float yaw;
    float pitch;
    float roll;

    /* SPI 패킷용 — gyro rad/s, body frame, ROS 컨벤션 (+CCW) */
    float gyro[3];   /* [0]=X(forward), [1]=Y(left), [2]=Z(up) */

    /* SPI 패킷용 — accel m/s², body frame */
    float accel[3];  /* [0]=X(forward), [1]=Y(left), [2]=Z(up) */

    /* SPI 패킷용 — quaternion wxyz, normalized, body frame */
    float quat[4];   /* [0]=w, [1]=x, [2]=y, [3]=z */

    bool data_valid;
} body_attitude_t;

/*
 * 자동으로 0x28 / 0x29 주소 스캔 후 IMUPLUS 모드 진입.
 * 첫 응답한 주소는 모듈 내부 static에 저장 — 이후 read_body는 그 주소 사용.
 */
bool bno055_init_imuplus(I2C_HandleTypeDef *hi2c);

/*
 * EUL 레지스터에서 yaw/pitch/roll 읽음.
 * 본체 마운팅 기준 좌표 변환 적용:
 *   body.yaw   = +(BNO heading)
 *   body.pitch = -(BNO roll register)
 *   body.roll  = -(BNO pitch register)
 */
bool bno055_read_body(I2C_HandleTypeDef *hi2c, body_attitude_t *body);

#endif /* IMU_BNO055_H */
