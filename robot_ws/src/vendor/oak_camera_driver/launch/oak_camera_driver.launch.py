from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os


def generate_launch_description():
    param_file = os.path.join(
        get_package_share_directory('oak_camera_driver'),
        'config',
        'oak_camera_driver.param.yaml'
    )

    return LaunchDescription([
        Node(
            package='oak_camera_driver',
            executable='oak_camera_driver_node',
            name='oak_camera_driver_node',
            parameters=[param_file],
            output='screen',
        )
    ])
