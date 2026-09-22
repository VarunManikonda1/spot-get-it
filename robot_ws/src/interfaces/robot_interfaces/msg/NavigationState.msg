# NavigationState.msg

std_msgs/Header header

string robot_id

uint8 nav_state

bool path_received
bool path_valid
bool pose_valid
bool perception_valid
bool local_path_required
bool rejoin_required
bool stop_required
bool goal_reached

bool front_blocked
bool left_blocked
bool right_blocked

float32 front_clearance_m
float32 left_clearance_m
float32 right_clearance_m

float32 distance_to_goal_m
float32 distance_to_global_path_m

string reason

uint8 NAV_WAITING_FOR_PATH=0
uint8 NAV_TRACKING_GLOBAL_PATH=1
uint8 NAV_PLANNING_LOCAL_PATH=2
uint8 NAV_FOLLOWING_LOCAL_PATH=3
uint8 NAV_AVOIDING_OBSTACLE=4
uint8 NAV_REJOINING_GLOBAL_PATH=5
uint8 NAV_STOPPED_BY_OBSTACLE=6
uint8 NAV_GOAL_REACHED=7
uint8 NAV_EMERGENCY_STOP=8
uint8 NAV_LOCALIZATION_LOST=9
uint8 NAV_PERCEPTION_STALE=10
