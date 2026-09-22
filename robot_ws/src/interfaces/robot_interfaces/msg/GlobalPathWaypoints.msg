# Global path waypoints for one robot
# Publisher : global_path_manager
# Subscriber: sim_executor, local_path_generator

std_msgs/Header header

string robot_id                          # "spot_01", "spot_02", "spot_03"
LocalizedRobotPose[] waypoints           # 경유지 순서대로 (x_m, y_m, z_m, yaw_rad 사용)
