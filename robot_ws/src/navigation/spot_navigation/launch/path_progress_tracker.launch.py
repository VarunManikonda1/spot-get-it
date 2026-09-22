"""
경로:
  spot_navigation/launch/path_progress_tracker_test.launch.py

역할:
  mock global path, mock localization pose, path progress tracker를 함께 실행하는 테스트 launch 파일.

실행 노드:
  - mock_global_path_publisher_node
  - mock_localization_pose_publisher_node
  - path_progress_tracker_node

출력 토픽:
  - /planning/mock_global_path/spot_01
  - /localization/mock_pose
  - /navigation/path_progress/spot_01

사용 목적:
  - 실제 global_path_manager, 실제 localization node 없이
    PathProgressTrackerNode의 nearest_index, target_index, progress_ratio 계산을 검증한다.
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    pkg_share = get_package_share_directory("spot_navigation")

    path_progress_tracker_param = os.path.join(
        pkg_share,
        "config",
        "path_progress_tracker.param.yaml"
    )

    return LaunchDescription([
        Node(
            package="spot_navigation",
            executable="path_progress_tracker_node",
            name="path_progress_tracker_node",
            output="screen",
            parameters=[path_progress_tracker_param],
        ),
    ])