# frame_id  : mission_map
# timestamp : path progress 계산 시각
std_msgs/Header header     

string robot_id

bool path_received              # global path 메시지 수신 여부
bool pose_received              # localization pose 메시지 수신 여부
bool path_valid                 # 수신한 global path 유효 여부
bool pose_valid                 # 수신한 localization pose 유효 여부

uint32 total_waypoints          # global path 전체 waypoint 개수

# nearest point 정보
uint32 nearest_index            # 가장 가까운 global path waypoint index
float32 distance_to_nearest_m   # 현재 로봇 위치와 nearest waypoint 사이의 거리
float32 nearest_x_m             # 가장 가까운 global path waypoint의 x 좌표
float32 nearest_y_m             # 가장 가까운 global path waypoint의 y 좌표

# target point 정보
uint32 target_index             # nearest_index보다 앞쪽에 있는 lookahead target waypoint index
float32 distance_to_target_m    # 현재 로봇 위치와 target waypoint 사이의 거리
float32 target_x_m              # lookahead target waypoint의 x 좌표
float32 target_y_m              # lookahead target waypoint의 y 좌표
float32 target_yaw_rad          # target_index에 해당하는 global path waypoint의 yaw_rad 값

float32 target_heading_rad      # 현재 로봇 위치에서 target waypoint를 바라보는 방향각
float32 heading_error_rad       # target waypoint 향하기 위한 회전 오차
float32 yaw_error_rad           # target waypoint의 yaw_rad 자세에 맞추기 위한 회전 오차

float32 progress_ratio          # global path 진행률 (0.0 ~ 1.0)
float32 distance_to_goal_m      # 현재 로봇 위치와 global path의 마지막 waypoint 사이의 거리

bool goal_reached               # goal에 도달했는지 여부