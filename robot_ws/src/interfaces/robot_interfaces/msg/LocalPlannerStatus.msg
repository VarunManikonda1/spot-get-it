std_msgs/Header header              # frame_id : mission_map

string robot_id

# =========================
# 입력 상태
# =========================

bool path_progress_received     # PathProgress 메시지 수신 여부
bool path_progress_valid        # PathProgress 입력 유효성
bool pose_received              # 현재 pose 메시지 수신 여부
bool pose_valid                 # 현재 pose 입력 유효성

# =========================
# Local planner 상태
# =========================

uint8 planner_state            # local planner의 현재 상태
bool local_path_available      # 유효한 local path 생성 여부
bool stop_required             # 후속 follower/controller의 정지 여부
bool blocked                   # 장애물 감지 또는 free-space 부재로 안전한 local path를 만들 수 없는 상태

# =========================
# Local target 정보
# =========================

# local path의 최종 target 좌표
# 장애물 회피가 없으면 PathProgress target과 동일할 수 있다.
# 장애물 회피 중이면 새로 계산한 회피 target이 될 수 있다.
float32 local_target_x_m
float32 local_target_y_m
float32 local_target_yaw_rad

# local planner가 최종적으로 선택한 heading
# mission_map 기준 절대 heading
float32 selected_heading_rad

# 현재 pose에서 local target까지의 거리
float32 distance_to_local_target_m

# 현재 yaw와 selected_heading_rad 사이의 오차
float32 local_heading_error_rad

# 현재 yaw와 local_target_yaw_rad 사이의 오차
float32 local_yaw_error_rad

# 현재 pose가 local target에 도달했는지 여부
# global goal 도착 여부가 아니라 local target 도착 여부다.
bool local_target_reached

# =========================
# 생성된 Local path 정보
# =========================

# 생성된 local path의 대략적인 길이 [m]
float32 local_path_length_m

# 생성된 local path의 pose 개수
uint32 local_path_pose_count

# =========================
# 판단 근거
# =========================

# PathProgress의 target_index를 최소 추적용으로 보관
# local planner가 어떤 global sub-goal을 기준으로 local path를 만들었는지 확인하기 위함
uint32 source_target_index

# global path의 lookahead target을 그대로 사용했는지 여부
bool used_global_sub_goal

# free_space_model.best_heading_angle_rad 기반 회피 heading을 사용했는지 여부
bool used_free_space_heading

# 장애물 회피 후 global path 복귀용 local path를 생성했는지 여부
bool used_rejoin_target

# =========================
# Free-space 회피 정보
# =========================

float32 free_space_heading_rad  # base_link 기준 free-space best heading
float32 free_space_clearance_m  # 선택된 free-space 방향의 최소 clearance [m] 
float32 free_space_score        # free_space_model이 계산한 선택 gap의 score

# 디버그용 설명 문자열
string reason

# =========================
# Planner state enum
# =========================

uint8 IDLE=0                    # 초기 상태
uint8 GLOBAL_SUB_GOAL=1         # PathProgress의 lookahead target을 local target으로 사용해 local path를 생성한 상태
uint8 AVOIDANCE=2               # free space heading 기반 local path 생성 상태
uint8 REJOIN=3                  # 회피 후 global path 근처로 복귀하기 위한 local path를 생성한 상태
uint8 BLOCKED=4                 # 장애물 존재 + free space gap X 기반 local path 생성 실패한 상태
uint8 INVALID_INPUT=5           # planning 수행할 수 없는 상태 (ex: pose 없음, path_progress 없음, frame_id 불일치 등)
uint8 GLOBAL_GOAL_REACHED=6     # global path의 최종 goal 도착, 정지 필요