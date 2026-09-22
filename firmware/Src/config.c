#include "config.h"
#include "robot_state.h"

/* Joint 순서: FL_s, FL_l, FL_f, FR_s, FR_l, FR_f,
 *             RL_s, RL_l, RL_f, RR_s, RR_l, RR_f
 * A4 캘리브레이션 후 실측 값으로 교체 */

const uint16_t JOINT_ZERO_POS[NUM_JOINTS] = {
    2048, 2048, 2048,
    2048, 2048, 2048,
    2048, 2048, 2048,
    2048, 2048, 2048,
};

const int8_t JOINT_SIGN[NUM_JOINTS] = {
    +1, +1, +1,  /* FL: sign=+1 (robot.c legs[0]) */
    -1, -1, -1,  /* FR: sign=-1 (robot.c legs[1]) */
    +1, +1, +1,  /* RL: sign=+1 (robot.c legs[2]) */
    -1, -1, -1,  /* RR: sign=-1 (robot.c legs[3]) */
};

const float JOINT_MIN_RAD[NUM_JOINTS] = {
    -0.548f, -2.666f, -0.100f,
    -0.548f, -2.666f, -0.100f,
    -0.548f, -2.666f, -0.100f,
    -0.548f, -2.666f, -0.100f,
};

const float JOINT_MAX_RAD[NUM_JOINTS] = {
    +0.548f, +1.548f, +2.590f,
    +0.548f, +1.548f, +2.590f,
    +0.548f, +1.548f, +2.590f,
    +0.548f, +1.548f, +2.590f,
};
