from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare

def generate_launch_description():
    lidar_perception_share = FindPackageShare("lidar_perception")
    cyglidar_share = FindPackageShare("cyglidar_d1_ros2")

    preprocess_param_file = PathJoinSubstitution([
        lidar_perception_share, 
        "config", 
        "pointcloud_preprocess.param.yaml"
    ])

    cyglidar_launch_file = PathJoinSubstitution([
        cyglidar_share, 
        "launch", 
        "cyglidar.launch.py"
    ])

    return LaunchDescription([

        # [1] CygLiDAR 드라이버
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(cyglidar_launch_file)
        ),

        # [2] LiDAR Static TF
        Node(
            package="tf2_ros",
            executable="static_transform_publisher",
            name="base_to_laser_static_tf",
            arguments=["0.14", "0.00", "0.05", "0", "0", "0", "base_link", "laser_frame"],
            output="screen"
        ),

        # [3] 공통 전처리
        Node(
            package="lidar_perception",
            executable="pointcloud_preprocess_node",
            name="pointcloud_preprocess_node",
            parameters=[preprocess_param_file],
            output="screen"
        ),
    ])