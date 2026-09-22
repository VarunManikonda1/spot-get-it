"""
- 경로 : spot_localization/launch/localization_mock.launch.py
- 역할 : localization mock pipeline 전체 실행
- 실행 노드 :
    - stm_motion_mock_node
    - gait_odom_estimator_node
    - mission_tf_node
- 사용 상황 :
    - 실제 STM bridge가 준비되기 전
    - 더미 STM motion 데이터를 이용해 mission_map -> odom -> base_link 전체 TF 체인 검증
"""

import os

from ament_index_python.packages import get_package_share_directory

from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    pkg_share = get_package_share_directory("spot_localization")

    # stm_motion_mock_param = os.path.join(
    #     pkg_share,
    #     "config",
    #     "stm_motion_mock.param.yaml"
    # )

    # gait_odom_param = os.path.join(
    #     pkg_share,
    #     "config",
    #     "gait_odom.param.yaml"
    # )

    mission_tf_param = os.path.join(
        pkg_share,
        "config",
        "mission_tf.param.yaml"
    )

    return LaunchDescription([
        # Node(
        #     package="spot_localization",
        #     executable="stm_motion_mock_node",
        #     name="stm_motion_mock_node",
        #     output="screen",
        #     parameters=[stm_motion_mock_param],
        # ),

        # Node(
        #     package="spot_localization",
        #     executable="gait_odom_estimator_node",
        #     name="gait_odom_estimator_node",
        #     output="screen",
        #     parameters=[gait_odom_param],
        # ),

        Node(
            package="spot_localization",
            executable="mission_tf_node",
            name="mission_tf_node",
            output="screen",
            parameters=[mission_tf_param],
        ),
    ])