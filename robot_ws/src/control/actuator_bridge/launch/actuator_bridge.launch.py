from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os


def generate_launch_description():
    pkg_share = get_package_share_directory("actuator_bridge")
    param_file = os.path.join(pkg_share, "config", "uart_bridge.param.yaml")

    return LaunchDescription(
        [
            Node(
                package="actuator_bridge",
                executable="actuator_bridge_node",
                name="actuator_bridge_node",
                output="screen",
                parameters=[param_file],
            )
        ]
    )