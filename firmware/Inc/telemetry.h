#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdbool.h>

/* 12 서보 일괄 read → g_robot_state에 raw → rad 변환해서 저장 */
bool telemetry_read_all_servos(void);

/* BNO055 read → g_robot_state.imu에 저장 */
bool telemetry_read_imu(void);

/* 둘 다 호출 (control_loop가 매 tick 사용) */
bool telemetry_update_all(void);

#endif /* TELEMETRY_H */
