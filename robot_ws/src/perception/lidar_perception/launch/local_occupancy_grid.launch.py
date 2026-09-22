from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory

import os

def generate_launch_description():
    pkg_share = get_package_share_directory('lidar_perception')

    local_occupancy_grid_param = os.path.join(
        pkg_share,
        'config',
        'local_occupancy_grid.param.yaml'
    )

    local_occupancy_grid_node = Node(
        package='lidar_perception',
        executable='local_occupancy_grid_node',
        name='local_occupancy_grid_node',
        output='screen',
        parameters=[local_occupancy_grid_param]
    )

    return LaunchDescription([
        local_occupancy_grid_node
    ])