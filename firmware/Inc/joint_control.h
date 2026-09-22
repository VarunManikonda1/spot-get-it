#ifndef JOINT_CONTROL_H
#define JOINT_CONTROL_H

#include <stdint.h>
#include <stdbool.h>
#include "robot_state.h"

bool joint_control_apply_target(void);

bool joint_control_hold(void);

void joint_control_capture_current_as_prev(void);

#endif /* JOINT_CONTROL_H */
