# robot_ws/src/control/rl_locomotion/launch/rl_locomotion.launch.py

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch.substitutions import PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    pkg_share = FindPackageShare("rl_locomotion")

    default_common_config = PathJoinSubstitution([
        pkg_share,
        "config",
        "common_policy_config.yaml",
    ])

    default_policy_config = PathJoinSubstitution([
        pkg_share,
        "config",
        "policy_classic_ik_flat.yaml",
    ])

    common_config_arg = DeclareLaunchArgument(
        "common_config",
        default_value=default_common_config,
        description="Path to common RL locomotion YAML",
    )

    policy_config_arg = DeclareLaunchArgument(
        "policy_config",
        default_value=default_policy_config,
        description="Path to model/profile-specific policy YAML",
    )

    common_config = LaunchConfiguration("common_config")
    policy_config = LaunchConfiguration("policy_config")

    return LaunchDescription([
        common_config_arg,
        policy_config_arg,

        Node(
            package="rl_locomotion",
            executable="rl_locomotion_node",
            name="rl_locomotion_node",
            output="screen",
            # 뒤에 있는 YAML이 앞의 값을 override할 수 있으므로
            # 모델별 policy_config를 마지막에 둠
            parameters=[
                common_config,
                policy_config,
            ],
        )
    ])
