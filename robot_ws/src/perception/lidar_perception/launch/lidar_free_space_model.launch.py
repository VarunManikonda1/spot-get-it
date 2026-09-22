from launch import LaunchDescription
from launch.substitutions import PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    # ==========================================================
    # [0] Package share 경로 설정
    # ==========================================================
    lidar_perception_share = FindPackageShare("lidar_perception")

    # ==========================================================
    # [1] Parameter 파일 경로 설정
    # ==========================================================
    local_occupancy_grid_param_file = PathJoinSubstitution([
        lidar_perception_share,
        "config",
        "local_occupancy_grid.param.yaml"
    ])

    free_space_model_param_file = PathJoinSubstitution([
        lidar_perception_share,
        "config",
        "free_space_model.param.yaml"
    ])

    free_space_marker_param_file = PathJoinSubstitution([
        lidar_perception_share,
        "config",
        "free_space_marker.param.yaml"
    ])

    return LaunchDescription([

        # ======================================================
        # [1] Local Occupancy Grid 노드
        # - 입력: /perception/lidar/points_filtered
        # - 출력: /perception/lidar/local_occupancy_grid
        # ======================================================

        Node(
            package="lidar_perception",
            executable="local_occupancy_grid_node",
            name="local_occupancy_grid_node",
            parameters=[local_occupancy_grid_param_file],
            output="screen"
        ),

        # ======================================================
        # [2] Free Space Model 노드
        # - 입력: /perception/lidar/local_occupancy_grid
        # - 출력: /perception/lidar/free_space_model
        # ======================================================

        Node(
            package="lidar_perception",
            executable="free_space_model_node",
            name="free_space_model_node",
            parameters=[free_space_model_param_file],
            output="screen"
        ),

        # [3] Free Space Marker 노드
        Node(
            package="lidar_perception",
            executable="free_space_marker_node",
            name="free_space_marker_node",
            parameters=[free_space_marker_param_file],
            output="screen"
        ),
    ])