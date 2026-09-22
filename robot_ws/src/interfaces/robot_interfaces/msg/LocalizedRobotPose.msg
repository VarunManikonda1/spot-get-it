# Localization pose of one robot in the common mission frame

std_msgs/Header header

string robot_id         # "spot_01", "spot_02" 로봇 식별자
string base_frame       # 실제 pose가 의미하는 로봇 기준 frame (base_link)

# mission_map 기준 로봇 위치 + heading (x, y, z, yaw)
float32 x_m
float32 y_m
float32 z_m
float32 yaw_rad

# ROS 표준 Pose : position + quaternion orientation
geometry_msgs/Pose pose