"""
- 경로 : spot_localization/launch/localization_tf_chain.launch.py
- 역할 : mock 기반 localization 전체 파이프라인 실행 launch 파일
- 실행 노드 :
    - stm_motion_mock_node
    - gait_odom_estimator_node
    - mission_tf_node
    - localization_pose_node
- 기능 :
    - STM motion mock 데이터를 /localization/stm_motion으로 publish
    - gait 기반 odometry를 계산하여 /localization/odometry와 odom -> base_link TF publish
    - mission_map -> odom static TF publish
    - TF에서 mission_map -> base_link를 조회하여 /localization/pose publish
- 주의 :
    - localization_pose_node에 별도 delay를 주지 않으므로,
      실행 초기에 TF lookup warning이 1~2회 발생할 수 있음
"""

import os

from ament_index_python.packages import get_package_share_directory

from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    # spot_Localization 패키지의 install/share 경로
    pkg_share = get_package_share_directory("spot_localization")

    # =====================
    # Parameter file paths
    # =====================

    stm_motion_mock_param = os.path.join(
        pkg_share,
        "config",
        "stm_motion_mock.param.yaml"
    )

    gait_odom_param = os.path.join(
        pkg_share,
        "config",
        "gait_odom.param.yaml"
    )

    mission_tf_param = os.path.join(
        pkg_share,
        "config",
        "mission_tf.param.yaml"
    )

    localization_pose_param = os.path.join(
        pkg_share,
        "config",
        "localization_pose.param.yaml"
    )

    # =================
    # Node definitions
    # =================

    stm_motion_mock_node = Node(
        package ="spot_localization",
        executable="stm_motion_mock_node",
        name="stm_motion_mock_node",
        output="screen",
        parameters=[stm_motion_mock_param],
    )

    gait_odom_estimator_node = Node(
        package ="spot_localization",
        executable="gait_odom_estimator_node",
        name="gait_odom_estimator_node",
        output="screen",
        parameters=[gait_odom_param],
    )

    mission_tf_node = Node(
        package ="spot_localization",
        executable="mission_tf_node",
        name="mission_tf_node",
        output="screen",
        parameters=[mission_tf_param],
    )

    localization_pose_node = Node(
        package ="spot_localization",
        executable="localization_pose_node",
        name="localization_pose_node",
        output="screen",
        parameters=[localization_pose_param],
    )

    # ==================
    # Launch description
    # ==================
    return LaunchDescription([
        stm_motion_mock_node,
        gait_odom_estimator_node,
        mission_tf_node,
        localization_pose_node,
    ])    
