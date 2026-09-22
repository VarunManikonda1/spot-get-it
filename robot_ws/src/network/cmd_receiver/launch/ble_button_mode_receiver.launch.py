from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument(
            "device_name",
            default_value="UNO_R4_Button",
            description="BLE advertise device name",
        ),
        DeclareLaunchArgument(
            "mode_topic",
            default_value="/control/behavior/mode",
            description="Behavior mode topic",
        ),
        DeclareLaunchArgument(
            "mode_for_a",
            default_value="CLASSIC",
            description="Mode string published when BLE value A is received",
        ),
        DeclareLaunchArgument(
            "mode_for_b",
            default_value="SIT",
            description="Mode string published when BLE value B is received",
        ),
        DeclareLaunchArgument(
            "mode_for_c",
            default_value="DETECT",
            description="Mode string published when BLE value C is received",
        ),
        Node(
            package="cmd_receiver",
            executable="ble_button_mode_receiver",
            name="ble_button_mode_receiver",
            output="screen",
            parameters=[{
                "device_name": LaunchConfiguration("device_name"),
                "mode_topic": LaunchConfiguration("mode_topic"),
                "mode_for_a": LaunchConfiguration("mode_for_a"),
                "mode_for_b": LaunchConfiguration("mode_for_b"),
                "mode_for_c": LaunchConfiguration("mode_for_c"),
            }],
        ),
    ])
