from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def package_launch(package_name, launch_file):
    return PythonLaunchDescriptionSource([
        PathJoinSubstitution([
            FindPackageShare(package_name),
            "launch",
            launch_file,
        ])
    ])


def generate_launch_description():
    declare_rpi5_ip = DeclareLaunchArgument(
        "rpi5_ip",
        default_value="192.168.0.13",
        description="BridgeDaemon가 실행 중인 RPi5 IP",
    )
    declare_enable_pose_path_event = DeclareLaunchArgument(
        "enable_pose_path_event",
        default_value="true",
        description="pose_path_event_sender 실행 여부",
    )
    declare_enable_lidar = DeclareLaunchArgument(
        "enable_lidar",
        default_value="true",
        description="lidar_stream_sender 실행 여부",
    )

    pose_path_event_sender = IncludeLaunchDescription(
        package_launch("pose_path_event_sender", "pose_path_event_sender.launch.py"),
        launch_arguments={
            "rpi5_ip": LaunchConfiguration("rpi5_ip"),
        }.items(),
        condition=IfCondition(LaunchConfiguration("enable_pose_path_event")),
    )
    lidar_sender = IncludeLaunchDescription(
        package_launch("lidar_stream_sender", "lidar_stream_sender.launch.py"),
        condition=IfCondition(LaunchConfiguration("enable_lidar")),
    )

    from launch_ros.actions import Node
    cmd_receiver_node = Node(
    package="cmd_receiver",
    executable="cmd_receiver_node",
    name="cmd_receiver_node",
    output="screen",
    )

    return LaunchDescription([
        declare_rpi5_ip,
        declare_enable_pose_path_event,
        declare_enable_lidar,
        pose_path_event_sender,
        lidar_sender,
        cmd_receiver_node,
    ])
