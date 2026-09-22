# 개별 3D 장애물 클러스터 1개를 표현하는 메시지
# 기준 좌표계는 반드시 Spot의 base_link 기준으로 해석한다.

std_msgs/Header header

# 이 슬롯에 실제 클러스터가 존재하는지 여부
# - ObstacleModel 안의 front/left/right 대표 슬롯에서 사용
bool valid

# =========================================================
# 현재 프레임 내 클러스터 식별자
# - cluster ID : 현재 프레임에서 nearest_distance_xy 기준으로 부여되는 ID
# - track ID   : 프레임 간 같은 장애물에 유지되는 ID
# - tracked    : cluster 유지되는 여부
# =========================================================
uint32 cluster_id
uint32 track_id
bool tracked

# 이 클러스터를 구성하는 포인트 개수
uint32 point_count

# =========================================
# Spot base_link 기준 클러스터 중심점 좌표 (m)
# =========================================
float32 centroid_x
float32 centroid_y
float32 centroid_z

# ==================================================
# Spot base_link 기준 클러스터 내 최근접 포인트 좌표 (m)
# - 충돌 위험 판단에는 이 점이 더 직접적일 수 있음
# ==================================================
float32 nearest_x
float32 nearest_y
float32 nearest_z

# ============================
# 대표 거리 정보
# ============================
# 클러스터 중심점까지의 수평 거리
float32 centroid_distance_xy

# 클러스터 내 최근접 포인트까지의 수평 거리
float32 nearest_distance_xy

# ===================================
# 대표 방향 정보
# Spot 정면(+x) 기준 수평 방위각 [rad]
# atan2(centroid_y, centroid_x)
#   0      : 정면
#   +값    : 왼쪽
#   -값    : 오른쪽
# ===================================
float32 azimuth_angle_rad

# Spot 기준 수직 방향각 [rad]
# atan2(centroid_z, sqrt(x^2 + y^2))
float32 elevation_angle_rad

# =========================
# 클러스터 외접 박스 크기 (m)
# =========================
float32 bbox_size_x
float32 bbox_size_y
float32 bbox_size_z

# =========================================================
# Spot 기준 대표 섹터 분류 결과
# - sector는 nearest point 기준 대표 위험 방향 1개를 의미한다.
# - [LEFT][FRONT][RIGHT] = [100][010][001]
# =========================================================
uint8 SECTOR_NONE =0
uint8 SECTOR_RIGHT=1   # 001
uint8 SECTOR_FRONT=2   # 010
uint8 SECTOR_LEFT =4   # 100

# ===================================
# 대표 sector
# - nearest point 기준 대표 위험 방향 1개
# - 반드시 0, 1, 2, 4 중 하나
# ===================================
uint8 sector

# =================================================================================
# Cluster가 실제로 점유하는 sector mask
# sector_mask는 cluster 내부 point들이 걸쳐 있는 모든 sector를 bitmask로 표현한다.
# 예:
# - RIGHT only       = 1  # 001
# - FRONT only       = 2  # 010
# - LEFT only        = 4  # 100
# - FRONT + RIGHT    = 3  # 011
# - LEFT + FRONT     = 6  # 110
# - LEFT + RIGHT     = 5  # 101
# - LEFT+FRONT+RIGHT = 7  # 111
# =================================================================================
uint8 sector_mask