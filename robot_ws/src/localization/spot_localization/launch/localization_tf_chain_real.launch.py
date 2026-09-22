"""
- 경로 : spot_localization/launch/localization_tf_chain_real.launch.py
- 역할 : 실제 STM bridge 기반 localization 전체 파이프라인 실행 launch 파일
- 실행 노드 :
    - gait_odom_estimator_node
    - mission_tf_node
    - localization_pose_node
- 전제 :
    - 팀원 bridge node가 /localization/spot_motion 토픽으로
      robot_interfaces/msg/StmMotion 데이터를 publish 중이어야 함
- 기능 :
    - /localization/spot_motion을 구독하여 gait 기반 odometry 계산
    - /localization/odometry publish
    - odom -> base_link TF publish
    - mission_map -> odom static TF publish
    - mission_map 기준 최종 로봇 pose를 /localization/pose로 publish
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    pkg_share = get_package_share_directory("spot_localization")

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

    gait_odom_estimator_node = Node(
        package="spot_localization",
        executable="gait_odom_estimator_node",
        name="gait_odom_estimator_node",
        output="screen",
        parameters=[
            gait_odom_param,
            {"stm_motion_topic": "/localization/spot_motion"},
        ],
    )

    mission_tf_node = Node(
        package="spot_localization",
        executable="mission_tf_node",
        name="mission_tf_node",
        output="screen",
        parameters=[mission_tf_param],
    )

    localization_pose_node = Node(
        package="spot_localization",
        executable="localization_pose_node",
        name="localization_pose_node",
        output="screen",
        parameters=[localization_pose_param],
    )

    return LaunchDescription([
        gait_odom_estimator_node,
        mission_tf_node,
        localization_pose_node,
    ])