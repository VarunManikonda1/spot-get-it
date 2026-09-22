from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument(
            "cmd_vel_topic",
            default_value="/control/cmd_vel/spot_01",
        ),
        DeclareLaunchArgument(
            "mode_topic",
            default_value="/control/behavior/mode",
        ),
        DeclareLaunchArgument(
            "target_topic",
            default_value="/control/classic_control/joint_target",
        ),
        DeclareLaunchArgument("vx_mps", default_value="0.05"),
        DeclareLaunchArgument("vy_mps", default_value="0.0"),
        DeclareLaunchArgument("wz_radps", default_value="0.0"),
        DeclareLaunchArgument("cycles", default_value="1.0"),
        DeclareLaunchArgument("publish_rate_hz", default_value="20.0"),
        DeclareLaunchArgument("walk_start_timeout_sec", default_value="8.0"),
        DeclareLaunchArgument("stop_publish_sec", default_value="3.5"),
        DeclareLaunchArgument("switch_to_stand", default_value="true"),
        Node(
            package="classic_control",
            executable="classic_one_cycle_test_node",
            name="classic_one_cycle_test_node",
            output="screen",
            parameters=[{
                "cmd_vel_topic": LaunchConfiguration("cmd_vel_topic"),
                "mode_topic": LaunchConfiguration("mode_topic"),
                "target_topic": LaunchConfiguration("target_topic"),
                "vx_mps": ParameterValue(
                    LaunchConfiguration("vx_mps"),
                    value_type=float,
                ),
                "vy_mps": ParameterValue(
                    LaunchConfiguration("vy_mps"),
                    value_type=float,
                ),
                "wz_radps": ParameterValue(
                    LaunchConfiguration("wz_radps"),
                    value_type=float,
                ),
                "cycles": ParameterValue(
                    LaunchConfiguration("cycles"),
                    value_type=float,
                ),
                "publish_rate_hz": ParameterValue(
                    LaunchConfiguration("publish_rate_hz"),
                    value_type=float,
                ),
                "walk_start_timeout_sec": ParameterValue(
                    LaunchConfiguration("walk_start_timeout_sec"),
                    value_type=float,
                ),
                "stop_publish_sec": ParameterValue(
                    LaunchConfiguration("stop_publish_sec"),
                    value_type=float,
                ),
                "switch_to_stand": ParameterValue(
                    LaunchConfiguration("switch_to_stand"),
                    value_type=bool,
                ),
            }],
        ),
    ])
