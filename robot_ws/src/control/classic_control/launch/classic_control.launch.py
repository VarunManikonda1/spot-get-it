from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    pkg_share = FindPackageShare("classic_control")

    default_config = PathJoinSubstitution([
        pkg_share,
        "config",
        "classic_control.yaml",
    ])

    config_arg = DeclareLaunchArgument(
        "config",
        default_value=default_config,
        description="Path to classic control YAML",
    )

    return LaunchDescription([
        config_arg,
        Node(
            package="classic_control",
            executable="classic_control_node",
            name="classic_control_node",
            output="screen",
            parameters=[LaunchConfiguration("config")],
        ),
    ])
