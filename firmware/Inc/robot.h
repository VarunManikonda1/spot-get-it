#ifndef ROBOT_H
#define ROBOT_H

#include "stm32f4xx_hal.h"
#include "servo_sts3215.h"
#include <stdint.h>
#include <stdbool.h>

/* === 다리 구성 === */
#define NUM_LEGS         4
#define SERVOS_PER_LEG   3
#define NUM_SERVOS       (NUM_LEGS * SERVOS_PER_LEG)

#define IDX_FL  0
#define IDX_FR  1
#define IDX_RL  2
#define IDX_RR  3

typedef struct {
    UART_HandleTypeDef *huart;
    const char *leg_name;
    uint8_t servo_ids[SERVOS_PER_LEG];
    int8_t sign;          /* +1 or -1 — IK 출력값에 곱함 (좌우 반전) */
} leg_t;

typedef uint16_t pose_t[NUM_LEGS][SERVOS_PER_LEG];

/* === 글로벌 상태 === */
extern leg_t legs[NUM_LEGS];
extern float foot_pos[NUM_LEGS][2];      /* [0]=x, [1]=z (hip frame, mm) */
extern volatile bool g_abort;            /* safety abort flag */

/* === Pose / Transition === */
void robot_update_pose_from_foot(pose_t pose);
void robot_apply_pose(const pose_t pose);
void robot_transition(const pose_t from, const pose_t to, int speed_dps);

/* === Torque / Ping === */
void robot_torque_off_all(void);
void robot_torque_on_all(void);
int  robot_ping_all(void);                 /* alive 서보 개수 반환 */
bool robot_read_current_pose(pose_t out);  /* 모든 서보 present_position 읽어옴 */

/* === ESC / Safety === */
bool check_esc(void);
void emergency_stop(void);                 /* torque off + halt forever */
void delay_with_estop(uint32_t ms);        /* HAL_Delay + ESC 폴링 */
bool safety_check(float pitch, float roll);

#endif /* ROBOT_H */
