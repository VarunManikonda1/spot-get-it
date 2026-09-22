"""
- 경로 : spot_localization/launch/gait_odom_mock.launch.py
- 역할 : STM motion mock node와 gait odom estimator node를 함께 실행
- 실행 노드 :
    - stm_motion_mock_node
    - gait_odom_estimator_node
- 사용 상황 :
    - 실제 STM bridge가 준비되기 전
    - /localization/stm_motion 더미 데이터를 이용해 odometry 계산 검증
"""

import os

from ament_index_python.packages import get_package_share_directory

from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    # spot_localization 패키지의 install/share 경로
    pkg_share = get_package_share_directory("spot_localization")

    # gait odom estimator parameter file
    # 현재 파일명이 gait_odom_param.yaml이므로 이 이름과 정확히 맞춰야 함
    gait_odom_param = os.path.join(
        pkg_share,
        "config",
        "gait_odom.param.yaml"
    )

    return LaunchDescription([
        # STM motion 데이터를 기반으로 odom 계산 노드
        Node(
            package="spot_localization",
            executable="gait_odom_estimator_node",
            name="gait_odom_estimator_node",
            output="screen",
            parameters=[gait_odom_param],
        ),
    ])