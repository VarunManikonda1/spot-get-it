# 경로 : ~/robot_ws/src/perception/lidar_perception/launch/lidar_memory_fusion.launch.py
# 역할 : memory_fusion_node 단독 실행용 launch 파일
# 실행 노드 :
#   - memory_fusion_node
# 입력 토픽 :
#   - /perception/lidar/local_occupancy_grid
#   - /perception/lidar/obstacle_memory_grid
#   - /localization/pose
# 출력 토픽 :
#   - /perception/lidar/local_occupancy_grid_with_memory
# 사용 목적 :
#   - obstacle_memory_grid_node가 정상 동작하는 상태에서,
#     mission_map 기준 memory obstacle이 base_link 기준 local grid에 재투영되는지 검증한다.

import os

from ament_index_python.packages import get_package_share_directory

from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    package_share_dir = get_package_share_directory("lidar_perception")

    memory_fusion_param_path = os.path.join(
        package_share_dir,
        "config",
        "memory_fusion.param.yaml"
    )

    memory_fusion_node = Node(
        package="lidar_perception",
        executable="memory_fusion_node",
        name="memory_fusion_node",
        output="screen",
        parameters=[
            memory_fusion_param_path
        ]
    )

    return LaunchDescription([
        memory_fusion_node
    ])