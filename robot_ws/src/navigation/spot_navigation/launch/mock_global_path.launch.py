"""
경로:
  spot_navigation/launch/mock_global_path.launch.py

역할:
  mock_global_path_publisher_node 단독 실행용 launch 파일.

실행 노드:
  - mock_global_path_publisher_node

출력 토픽:
  - /planning/global_path/spot_01
    Type: robot_interfaces/msg/GlobalPathWaypoints

사용 목적:
  - 실제 global_path_manager 없이도 빨간색 global path 더미 데이터를 publish한다.
  - path_progress_tracker_node 개발 전에 global path 입력 토픽을 단독 검증한다.

주의:
  - 실제 팀원의 global_path_manager와 동시에 실행하면
    /planning/global_path/spot_01 토픽에 publisher가 2개 생길 수 있다.
  - mock 검증 중에는 실제 global_path_manager를 끄고 실행하는 것을 권장한다.
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    # spot_navigation 패키지의 install/share 경로를 가져온다.
    # colcon build 이후 config 파일은 install/spot_navigation/share/spot_navigation/config 아래에서 찾게 된다.
    pkg_share = get_package_share_directory("spot_navigation")

    # mock_global_path_publisher_node가 사용할 YAML 파라미터 파일 경로.
    # robot_id, topic 이름, frame_id, waypoint 배열 등이 정의되어 있다.
    mock_param = os.path.join(
        pkg_share,
        "config",
        "mock_global_path_publisher.param.yaml"
    )

    return LaunchDescription([
        Node(
            package="spot_navigation",
            executable="mock_global_path_publisher_node",
            name="mock_global_path_publisher_node",
            output="screen",
            parameters=[mock_param],
        )
    ])