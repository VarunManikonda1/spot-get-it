from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os


def generate_launch_description():
    pkg_share = get_package_share_directory('motion_manager')
    param_file = os.path.join(pkg_share, 'config', 'motion_manager.param.yaml')

    stand_motion_node = Node(
        package='motion_manager',
        executable='stand_motion_node',
        name='stand_motion_node',
        output='screen',
        parameters=[param_file],
    )

    sit_motion_node = Node(
        package='motion_manager',
        executable='sit_motion_node',
        name='sit_motion_node',
        output='screen',
        parameters=[param_file],
    )

    detect_motion_node = Node(
        package='motion_manager',
        executable='detect_motion_node',
        name='detect_motion_node',
        output='screen',
        parameters=[param_file],
    )

    joint_target_mux_node = Node(
        package='motion_manager',
        executable='joint_target_mux_node',
        name='joint_target_mux_node',
        output='screen',
        parameters=[param_file],
    )

    return LaunchDescription([
        stand_motion_node,
        sit_motion_node,
        detect_motion_node,
        joint_target_mux_node,
    ])
