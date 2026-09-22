"""
pose_path_event_sender.launch.py

사용법:
  # 기본 실행 (yaml 파라미터 사용)
  ros2 launch pose_path_event_sender pose_path_event_sender.launch.py

  # RPi5 IP 변경
  ros2 launch pose_path_event_sender pose_path_event_sender.launch.py rpi5_ip:=192.168.0.100
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():

    declare_rpi5_ip = DeclareLaunchArgument(
        "rpi5_ip",
        default_value="192.168.0.13",
        description="BridgeDaemon가 실행 중인 RPi5 IP",
    )

    params_file = PathJoinSubstitution(
        [FindPackageShare("pose_path_event_sender"), "config",
         "pose_path_event_sender.yaml"]
    )

    pose_path_event_sender_node = Node(
        package="pose_path_event_sender",
        executable="pose_path_event_sender_node",
        name="pose_path_event_sender",
        output="screen",
        parameters=[
            params_file,
            {"rpi5_ip": LaunchConfiguration("rpi5_ip")},
        ],
    )

    return LaunchDescription([
        declare_rpi5_ip,
        pose_path_event_sender_node,
    ])
