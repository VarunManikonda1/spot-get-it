#include "calibration.h"
#include "robot.h"
#include "servo_sts3215.h"
#include "config.h"
#include <stdio.h>

void calibration_mode(void) {
    robot_torque_off_all();
    printf("\r\n=== CALIBRATION MODE ===\r\n");
    printf("Torque OFF. Move each joint to URDF default pose, then read raw values.\r\n");
    printf("Joint order: FL_s FL_l FL_f | FR_s FR_l FR_f | RL_s RL_l RL_f | RR_s RR_l RR_f\r\n");
    printf("URDF default: shoulder=0.0 rad, leg=-0.6 rad, foot=1.1 rad\r\n");
    printf("ESC to exit.\r\n\r\n");

    while (1) {
        if (check_esc()) emergency_stop();

        printf("RAW: ");
        for (int l = 0; l < NUM_LEGS; l++) {
            for (int j = 0; j < SERVOS_PER_LEG; j++) {
                sts_read_result_t r = sts_read_state(legs[l].huart, legs[l].servo_ids[j]);
                if (r.ok) printf("%4u ", r.position);
                else      printf("---- ");
            }
            printf("| ");
        }
        printf("\r\n");

        HAL_Delay(1000);
    }
}
