# 하나의 연속된 통과 가능 angular gap 후보
# angle 기준:
# - base_link 기준
# - x+ 전방 = 0 rad
# - y+ 좌측 = positive
# - y- 우측 = negative

uint32 start_bin
uint32 end_bin

float32 start_angle_rad
float32 end_angle_rad
float32 center_angle_rad
float32 width_angle_rad

float32 start_angle_deg
float32 end_angle_deg
float32 center_angle_deg
float32 width_angle_deg

# gap 내부에서 가장 가까운 occupied cell까지의 거리 [m]
# 검사 범위 안에 occupied가 없으면 max_check_range로 clamp
float32 min_clearance

# gap 내부 통계
float32 free_ratio
float32 unknown_ratio
float32 occupied_ratio

# gap 종합 점수
float32 score

# 최종 선택된 gap 여부
bool selected