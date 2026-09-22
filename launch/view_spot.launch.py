"""RViz2 viewer for the Spot Micro URDF.

Loads urdf/spot_micro_view.urdf, resolves the @REPO_ROOT@ mesh placeholder
against this repository, and brings up robot_state_publisher,
joint_state_publisher_gui and rviz2 so the 12 joints can be dragged by hand.

    ros2 launch launch/view_spot.launch.py

Override the repository location with SPOT_REPO_ROOT if the launch file is
copied elsewhere.
"""

import os

from launch import LaunchDescription
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue

REPO_ROOT = os.environ.get(
    'SPOT_REPO_ROOT',
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
)


def generate_launch_description():

    urdf_path = os.path.join(REPO_ROOT, 'urdf', 'spot_micro_view.urdf')

    # Read the URDF as plain text in Python, NOT through the command line,
    # and point the mesh URIs at this checkout.
    with open(urdf_path, 'r') as f:
        urdf_text = f.read().replace('@REPO_ROOT@', REPO_ROOT)

    robot_description = ParameterValue(urdf_text, value_type=str)

    rsp = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        parameters=[{'robot_description': robot_description}],
        output='screen'
    )

    jsp_gui = Node(
        package='joint_state_publisher_gui',
        executable='joint_state_publisher_gui',
        output='screen'
    )

    rviz = Node(
        package='rviz2',
        executable='rviz2',
        output='screen'
    )

    return LaunchDescription([rsp, jsp_gui, rviz])
