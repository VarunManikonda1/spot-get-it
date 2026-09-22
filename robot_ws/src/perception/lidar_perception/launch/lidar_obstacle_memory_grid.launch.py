# 경로 : ~/robot_ws/src/perception/lidar_perception/launch/lidar_obstacle_memory_grid.launch.py
# 역할 : LiDAR obstacle memory grid 검증용 단독 launch 파일
# 실행 노드 :
#   - obstacle_memory_grid_node
# 입력 토픽 :
#   - /perception/lidar/local_occupancy_grid
#   - /localization/pose
# 출력 토픽 :
#   - /perception/lidar/obstacle_memory_grid
# 주요 기능 :
#   - base_link 기준 local occupancy grid의 occupied cell을 mission_map 기준 obstacle memory grid로 변환
#   - 최근 관측된 장애물을 TTL 동안 유지하는 memory grid 발행
# 사용 목적 :
#   - local_occupancy_grid_node와 localization pose가 이미 실행 중인 상태에서
#     obstacle_memory_grid_node만 단독으로 실행해 memory grid 생성 여부를 검증한다.

import os

from ament_index_python.packages import get_package_share_directory

from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    # lidar_perception 패키지의 share 디렉토리 경로를 가져온다.
    #
    # colcon build 후 install/share/lidar_perception 아래에
    # config, launch 파일들이 설치되므로,
    # launch 실행 시 parameter yaml 파일을 안정적으로 찾기 위해 사용한다.
    package_share_dir = get_package_share_directory("lidar_perception")

    # obstacle_memory_grid_node에서 사용할 parameter yaml 파일 경로.
    #
    # 이 파일에는 다음과 같은 설정이 들어간다.
    # - input_local_grid_topic
    # - localization_pose_topic
    # - output_memory_grid_topic
    # - memory_frame
    # - memory_resolution
    # - obstacle_memory_ttl_sec
    # - occupied_threshold
    obstacle_memory_grid_param_path = os.path.join(
        package_share_dir,
        "config",
        "obstacle_memory_grid.param.yaml"
    )

    # obstacle_memory_grid_node 실행 설정.
    #
    # 이 노드는 다음 입력이 이미 살아있어야 정상 동작한다.
    # - /perception/lidar/local_occupancy_grid
    # - /localization/pose
    #
    # local_occupancy_grid 또는 localization pose가 없으면
    # obstacle memory update가 수행되지 않는다.
    obstacle_memory_grid_node = Node(
        package="lidar_perception",
        executable="obstacle_memory_grid_node",
        name="obstacle_memory_grid_node",
        output="screen",
        parameters=[
            obstacle_memory_grid_param_path
        ]
    )

    return LaunchDescription([
        obstacle_memory_grid_node
    ])