std_msgs/Header header

# ============================
# Risk level enum
# ============================
uint8 RISK_UNKNOWN=0
uint8 RISK_SAFE=1
uint8 RISK_CAUTION=2
uint8 RISK_BLOCKED=3

# ============================
# Main free-space result
# ============================

# 통과 가능한 후보 gap이 하나 이상 있는지
bool path_available

# 최종 위험도
uint8 risk_level

# 선택된 대표 진행 방향
# base_link 기준, x+ 전방 = 0 rad
# 좌측 +, 우측 -
float32 best_heading_angle_rad
float32 best_heading_angle_deg

# 선택된 gap의 대표 정보
float32 best_clearance          # 그 방향으로 갔을 때 확보된 최소 장애물 여유 거리
float32 best_score
float32 best_free_ratio
float32 best_unknown_ratio
float32 best_occupied_ratio

# 후보 gap 개수
uint32 candidate_gap_count

# 선택된 gap 정보
robot_interfaces/FreeSpaceGap selected_gap

# 디버깅/후속 planning 후보 목록
robot_interfaces/FreeSpaceGap[] candidate_gaps

# ============================
# Debug summary
# ============================

# 전체 angular bin 개수
uint32 bin_count

# candidate로 인정된 bin 개수
uint32 candidate_bin_count

# blocked로 판정된 bin 개수
uint32 blocked_bin_count

# 전체 분석 cell 수
uint32 analyzed_cell_count

# 분석 범위 안의 cell 통계
uint32 total_unknown_count
uint32 total_free_count
uint32 total_occupied_count

float32 total_unknown_ratio
float32 total_free_ratio
float32 total_occupied_ratio