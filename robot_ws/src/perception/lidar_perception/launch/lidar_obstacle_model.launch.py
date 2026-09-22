# 실행 명령어 : ros2 launch lidar_perception lidar_obstacle_model.launch.py

from launch import LaunchDescription
from launch.substitutions import PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare

def generate_launch_description():
    lidar_perception_share = FindPackageShare("lidar_perception")

    cluster_param_file = PathJoinSubstitution([
        lidar_perception_share, "config", "obstacle_cluster.param.yaml"
    ])

    return LaunchDescription([

        # obstacle_cluster_node
        Node(
            package="lidar_perception",
            executable="obstacle_cluster_node",
            name="obstacle_cluster_node",
            parameters=[cluster_param_file],
            output="screen"
        ),

        # ======================================================
        # [1] Obstacle Model 노드
        # - 입력: /perception/lidar/obstacle_clusters
        # - 출력: /perception/lidar/obstacle_model
        # ======================================================
        Node(
            package="lidar_perception",
            executable="obstacle_model_node",
            name="obstacle_model_node",
            output="screen"
        ),
    ])