from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os

def generate_launch_description():
    cfg = os.path.join(
        get_package_share_directory('camera_stream_sender'),
        'config', 'camera_stream_sender.param.yaml'
    )
    return LaunchDescription([
        Node(
            package='camera_stream_sender',
            executable='camera_stream_sender_node',
            name='camera_stream_sender_node',
            output='screen',
            parameters=[cfg],
        )
    ])
