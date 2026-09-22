from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os

def generate_launch_description():
    cfg = os.path.join(
        get_package_share_directory('lidar_sparse'),
        'config', 'lidar_sparse.param.yaml'
    )
    return LaunchDescription([
        Node(
            package='lidar_sparse',
            executable='lidar_sparse_node',
            name='lidar_sparse_node',
            output='screen',
            parameters=[cfg],
        )
    ])
