# Planning / Decision이 바로 사용하는 요약 장애물 모델
# front / left / right별 대표 장애물만 담는다.

std_msgs/Header header

# front / left / right 중 하나라도 대표 클러스터가 존재하면 true
bool obstacle_detected

# ==================================================
# 섹터별 대표 장애물
# - valid=false 이면 해당 섹터에 대표 장애물이 없다는 뜻
# ==================================================
ObstacleCluster front
ObstacleCluster left
ObstacleCluster right