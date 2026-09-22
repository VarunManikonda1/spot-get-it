"""
경로:
  spot_navigation/launch/mock_navigation_input.launch.py

역할:
  PathProgressTrackerNode 검증에 필요한 mock 입력 2개를 함께 실행한다.

실행 노드:
  - mock_global_path_publisher_node
  - mock_localization_pose_publisher_node

출력 토픽:
  - /planning/mock_global_path/spot_01
  - /localization/pose

사용 목적:
  - 실제 global_path_manager와 localization node 없이
    navigation A 파트의 입력 데이터를 생성한다.
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    pkg_share = get_package_share_directory("spot_navigation")

    mock_global_path_param = os.path.join(
        pkg_share,
        "config",
        "mock_global_path_publisher.param.yaml"
    )

    mock_localization_pose_param = os.path.join(
        pkg_share,
        "config",
        "mock_localization_pose_publisher.param.yaml"
    )

    return LaunchDescription([
        Node(
            package="spot_navigation",
            executable="mock_global_path_publisher_node",
            name="mock_global_path_publisher_node",
            output="screen",
            parameters=[mock_global_path_param],
        ),

        Node(
            package="spot_navigation",
            executable="mock_localization_pose_publisher_node",
            name="mock_localization_pose_publisher_node",
            output="screen",
            parameters=[mock_localization_pose_param],
        ),
    ])