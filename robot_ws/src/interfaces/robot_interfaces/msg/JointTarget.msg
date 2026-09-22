std_msgs/Header header

uint8 MODE_DISABLE=0
uint8 MODE_STAND=1
uint8 MODE_RL=2
uint8 MODE_CROUCH=3
uint8 MODE_E_STOP=4
uint8 MODE_CLASSIC=5
uint8 MODE_CLASSIC_CONTROL=5
uint8 MODE_SIT=6

uint16 seq
uint8 mode
uint8 flags

float32[12] target_rad
float32[12] max_delta_rad

float32 gait_phase
uint32 gait_cycle_count
