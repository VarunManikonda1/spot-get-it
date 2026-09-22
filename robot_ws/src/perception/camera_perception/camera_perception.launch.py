from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os


def generate_launch_description():
    param_file = os.path.join(
        get_package_share_directory('camera_perception'),
        'config',
        'camera_perception.param.yaml'
    )

    return LaunchDescription([
        Node(
            package='camera_perception',
            executable='camera_perception_node',
            name='camera_perception_node',
            parameters=[param_file],
            output='screen',
        )
    ])
