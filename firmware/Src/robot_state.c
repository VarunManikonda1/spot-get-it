#include "robot_state.h"
#include <string.h>

volatile robot_state_t g_robot_state;

void robot_state_init(void) {
    memset((void *)&g_robot_state, 0, sizeof(g_robot_state));
    g_robot_state.mode = MODE_IDLE;
    g_robot_state.fault_code = FAULT_OK;
    g_robot_state.imu.quat[0] = 1.0f;
}
