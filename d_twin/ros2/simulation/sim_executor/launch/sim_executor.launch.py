import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    pkg_dir    = get_package_share_directory('sim_executor')
    param_path = os.path.join(pkg_dir, 'config', 'sim_executor.param.yaml')

    sim_executor = Node(
        package='sim_executor',
        executable='sim_executor_node',
        name='sim_executor_node',
        parameters=[param_path],
        output='screen',
        emulate_tty=True,   # 키보드 stdin 활성화
    )

    return LaunchDescription([sim_executor])
