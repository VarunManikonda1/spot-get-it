# ============================================================
# lidar_environment_pipeline.launch.py
#
# 역할: LiDAR Perception 전체 파이프라인을 실행한다.
#
# 실행 구성:
#   1. lidar_base.launch.py
#      - CygLiDAR driver 실행
#      - base_link -> laser_frame static TF publish
#      - pointcloud_preprocess_node 실행
#
#   2. lidar_obstacle_model.launch.py
#      - obstacle_cluster_node 실행
#      - obstacle_model_node 실행
#
#   3. lidar_free_space_model.launch.py
#      - local_occupancy_grid_node 실행
#      - free_space_model_node 실행
#      - free_space_marker_node 실행
#
# 주요 토픽 흐름:
#   /scan_3D
#     -> /perception/lidar/points_filtered
#     -> /perception/lidar/obstacle_clusters
#     -> /perception/lidar/obstacle_model
#
#   /perception/lidar/points_filtered
#     -> /perception/lidar/local_occupancy_grid
#     -> /perception/lidar/free_space_model
#
# Navigation 2차 MVP에서 필요한 최종 출력:
#   - /perception/lidar/obstacle_model
#   - /perception/lidar/free_space_model
#
# 주의:
#   이 launch를 실행할 때는 lidar_base.launch.py를 별도로 중복 실행하지 않는다.
#   static TF와 CygLiDAR driver가 중복 실행될 수 있기 때문이다.
# ============================================================

from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription, TimerAction
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    lidar_perception_share = FindPackageShare("lidar_perception")

    lidar_base_launch_file = PathJoinSubstitution([
        lidar_perception_share,
        "launch",
        "lidar_base.launch.py"
    ])

    lidar_obstacle_model_launch_file = PathJoinSubstitution([
        lidar_perception_share,
        "launch",
        "lidar_obstacle_model.launch.py"
    ])

    lidar_free_space_model_launch_file = PathJoinSubstitution([
        lidar_perception_share,
        "launch",
        "lidar_free_space_model.launch.py"
    ])

    return LaunchDescription([
        # ============================================================
        # [1] LiDAR base pipeline 먼저 실행
        #
        # 구성:
        #   - CygLiDAR driver
        #   - base_link -> laser_frame static TF
        #   - pointcloud_preprocess_node
        #
        # 출력:
        #   - /scan_3D
        #   - /perception/lidar/points_filtered
        # ============================================================
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(lidar_base_launch_file)
        ),

        # ============================================================
        # [2] obstacle model pipeline 지연 실행
        #
        # 이유:
        #   pointcloud_preprocess_node와 static TF가 먼저 올라온 뒤
        #   /perception/lidar/points_filtered를 안정적으로 받기 위함.
        # ============================================================
        TimerAction(
            period=2.0,
            actions=[
                IncludeLaunchDescription(
                    PythonLaunchDescriptionSource(lidar_obstacle_model_launch_file)
                )
            ]
        ),

        # ============================================================
        # [3] free-space model pipeline 지연 실행
        #
        # 이유:
        #   /perception/lidar/points_filtered가 생성된 뒤
        #   local_occupancy_grid_node와 free_space_model_node를 실행하기 위함.
        # ============================================================
        TimerAction(
            period=2.5,
            actions=[
                IncludeLaunchDescription(
                    PythonLaunchDescriptionSource(lidar_free_space_model_launch_file)
                )
            ]
        ),
    ])