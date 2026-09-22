from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from ament_index_python.packages import get_package_share_directory
import os


def generate_launch_description():
    pkg_share = get_package_share_directory('lidar_stream_sender')
    default_param = os.path.join(
        pkg_share, 'config', 'lidar_stream_sender.param.yaml'
    )

    return LaunchDescription([
        DeclareLaunchArgument(
            'params_file',
            default_value=default_param,
            description='파라미터 yaml 파일 경로',
        ),

        Node(
            package='lidar_stream_sender',
            executable='lidar_stream_sender_node',
            name='lidar_stream_sender_node',
            output='screen',
            parameters=[LaunchConfiguration('params_file')],
        ),
    ])
