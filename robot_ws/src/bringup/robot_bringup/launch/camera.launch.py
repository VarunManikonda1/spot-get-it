from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from ament_index_python.packages import get_package_share_directory
import os


def generate_launch_description():

    oak = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            os.path.join(
                get_package_share_directory('oak_camera_driver'),
                'launch', 'oak_camera_driver.launch.py'
            )
        )
    )

    perception = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            os.path.join(
                get_package_share_directory('camera_perception'),
                'launch', 'camera_perception.launch.py'
            )
        )
    )

    return LaunchDescription([oak, perception])
