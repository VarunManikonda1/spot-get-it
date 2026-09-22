from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    map_arg = DeclareLaunchArgument(
        'map',
        default_value='',
        description='Path to an existing map.yaml (optional)',
    )

    node = Node(
        package='sar_planner',
        executable='sar_planner',
        name='sar_planner',
        output='screen',
        arguments=['--map', LaunchConfiguration('map')],
    )

    return LaunchDescription([map_arg, node])
