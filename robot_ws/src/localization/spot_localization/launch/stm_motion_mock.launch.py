from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory

import os

def generate_launch_description():
    pkg_share = get_package_share_directory("spot_localization")

    mock_param = os.path.join(
        pkg_share,
        "config",
        "stm_motion_mock.param.yaml"
    )

    return LaunchDescription([
        Node(
            package="spot_localization",
            executable="stm_motion_mock_node",
            name="stm_motion_mock_node",
            output="screen",
            parameters=[mock_param],
        )
    ])