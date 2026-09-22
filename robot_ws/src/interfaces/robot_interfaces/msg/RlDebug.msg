std_msgs/Header header

float32[47] observation
float32[12] raw_action
float32[12] ik_ref_rad
float32[12] target_rad

float32 gait_phase
float32 cmd_vx
float32 cmd_vy
float32 cmd_wz

float32 feedback_age_ms
float32 obs_build_ms
float32 inference_ms
float32 postprocess_ms
float32 total_loop_ms
float32 timer_elapsed_ms
float32 timer_jitter_ms

uint32 missed_deadline_count
bool emergency_stop
