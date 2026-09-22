"""
- 경로 : spot_localization/launch/localization_pose.launch.py
- 역할 : localization_pose_node 단독 실행 launch 파일
- 실행 노드 :
    - localization_pose_node
- 사용 상황 :
    - mission_tf_node와 gait_odom_estimator_node가 이미 실행 중일 때
    - TF tree에서 mission_map -> base_link를 조회하여 /localization/pose만 publish하고 싶을 때
"""

import os

from ament_index_python.packages import get_package_share_directory

from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    # spot_localization 패키지의 install/share 경로
    pkg_share = get_package_share_directory("spot_localization")

    # localization_pose_node parameter file
    localization_pose_param = os.path.join(
        pkg_share,
        "config",
        "localization_pose.param.yaml"
    )

    return LaunchDescription([
        Node(
            package="spot_localization",
            executable="localization_pose_node",
            name="localization_pose_node",
            output="screen",
            parameters=[localization_pose_param],
        ),
    ])