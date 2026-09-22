import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    pkg_dir = get_package_share_directory('global_path_manager')
    return LaunchDescription([
        Node(
            package='global_path_manager',
            executable='global_path_manager_node',
            name='global_path_manager_node',
            parameters=[{
                'frame_id': 'mission_map',
                'map_config_path': os.path.join(pkg_dir, 'config', 'map.yaml'),
            }],
            output='screen',
        )
    ])
