# ============================================================
# final_lidar_environment_pipeline.launch.py
#
# 역할:
#   LiDAR Perception 전체 파이프라인을 실행한다.
#
# 실행 구성:
#   1. lidar_base.launch.py
#      - CygLiDAR driver 실행
#      - base_link -> laser_frame static TF publish
#      - pointcloud_preprocess_node 실행
#
#   2. Obstacle Model branch
#      - obstacle_cluster_node 실행
#      - obstacle_model_node 실행
#
#   3. Occupancy + Memory + FreeSpace branch
#      - local_occupancy_grid_node 실행
#      - obstacle_memory_grid_node 실행
#      - memory_fusion_node 실행
#      - free_space_model_node 실행
#      - free_space_marker_node 실행
#
# 주요 토픽 흐름:
#   /scan_3D
#     -> /perception/lidar/points_filtered
#
#   Obstacle branch:
#   /perception/lidar/points_filtered
#     -> /perception/lidar/obstacle_clusters
#     -> /perception/lidar/obstacle_model
#
#   Free-space + memory branch:
#   /perception/lidar/points_filtered
#     -> /perception/lidar/local_occupancy_grid
#     -> /perception/lidar/obstacle_memory_grid
#     -> /perception/lidar/local_occupancy_grid_with_memory
#     -> /perception/lidar/free_space_model
#
# Navigation에서 사용하는 최종 출력:
#   - /perception/lidar/obstacle_model
#   - /perception/lidar/free_space_model
#
# 주의:
#   obstacle_memory_grid_node와 memory_fusion_node는 /localization/pose가 필요하다.
#   따라서 이 launch를 실행하기 전에 localization pipeline 또는 mock localization pose publisher가
#   /localization/pose를 발행하고 있어야 한다.
# ============================================================

from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription, TimerAction
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    # ==========================================================
    # [0] Package share 경로 설정
    # ==========================================================
    lidar_perception_share = FindPackageShare("lidar_perception")

    # ==========================================================
    # [1] Include할 base launch 파일 경로
    # ==========================================================
    lidar_base_launch_file = PathJoinSubstitution([
        lidar_perception_share,
        "launch",
        "lidar_base.launch.py"
    ])

    # ==========================================================
    # [2] Parameter 파일 경로 설정
    # ==========================================================

    # obstacle_cluster_node parameter
    obstacle_cluster_param_file = PathJoinSubstitution([
        lidar_perception_share,
        "config",
        "obstacle_cluster.param.yaml"
    ])

    # local_occupancy_grid_node parameter
    local_occupancy_grid_param_file = PathJoinSubstitution([
        lidar_perception_share,
        "config",
        "local_occupancy_grid.param.yaml"
    ])

    # obstacle_memory_grid_node parameter
    obstacle_memory_grid_param_file = PathJoinSubstitution([
        lidar_perception_share,
        "config",
        "obstacle_memory_grid.param.yaml"
    ])

    # memory_fusion_node parameter
    memory_fusion_param_file = PathJoinSubstitution([
        lidar_perception_share,
        "config",
        "memory_fusion.param.yaml"
    ])

    # free_space_model_node parameter
    free_space_model_param_file = PathJoinSubstitution([
        lidar_perception_share,
        "config",
        "free_space_model.param.yaml"
    ])

    # free_space_marker_node parameter
    free_space_marker_param_file = PathJoinSubstitution([
        lidar_perception_share,
        "config",
        "free_space_marker.param.yaml"
    ])

    return LaunchDescription([

        # ======================================================
        # [1] LiDAR base pipeline
        #
        # 구성:
        #   - CygLiDAR driver
        #   - base_link -> laser_frame static TF
        #   - pointcloud_preprocess_node
        #
        # 출력:
        #   - /scan_3D
        #   - /perception/lidar/points_filtered
        #
        # 주의:
        #   lidar_base.launch.py는 전체 파이프라인에서 한 번만 실행한다.
        #   중복 실행하면 LiDAR driver나 static TF가 중복될 수 있다.
        # ======================================================
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(lidar_base_launch_file)
        ),

        # ======================================================
        # [2] Obstacle Model branch
        #
        # 입력:
        #   - /perception/lidar/points_filtered
        #
        # 출력:
        #   - /perception/lidar/obstacle_clusters
        #   - /perception/lidar/obstacle_model
        #
        # 실행 지연 이유:
        #   pointcloud_preprocess_node가 먼저 올라와
        #   /perception/lidar/points_filtered를 발행할 시간을 준다.
        # ======================================================
        TimerAction(
            period=2.0,
            actions=[
                Node(
                    package="lidar_perception",
                    executable="obstacle_cluster_node",
                    name="obstacle_cluster_node",
                    parameters=[obstacle_cluster_param_file],
                    output="screen"
                ),

                Node(
                    package="lidar_perception",
                    executable="obstacle_model_node",
                    name="obstacle_model_node",
                    output="screen"
                ),
            ]
        ),

        # ======================================================
        # [3] Local Occupancy Grid node
        #
        # 입력:
        #   - /perception/lidar/points_filtered
        #
        # 출력:
        #   - /perception/lidar/local_occupancy_grid
        #
        # 역할:
        #   현재 LiDAR frame에서 얻은 points_filtered를
        #   base_link 기준 local occupancy grid로 변환한다.
        # ======================================================
        TimerAction(
            period=2.5,
            actions=[
                Node(
                    package="lidar_perception",
                    executable="local_occupancy_grid_node",
                    name="local_occupancy_grid_node",
                    parameters=[local_occupancy_grid_param_file],
                    output="screen"
                ),
            ]
        ),

        # ======================================================
        # [4] Obstacle Memory Grid node
        #
        # 입력:
        #   - /perception/lidar/local_occupancy_grid
        #   - /localization/pose
        #
        # 출력:
        #   - /perception/lidar/obstacle_memory_grid
        #
        # 역할:
        #   base_link 기준 local occupancy grid의 occupied cell을
        #   mission_map 기준 memory grid로 변환하고 TTL 동안 유지한다.
        #
        # 주의:
        #   /localization/pose가 발행되지 않으면 memory update가 수행되지 않는다.
        # ======================================================
        TimerAction(
            period=3.0,
            actions=[
                Node(
                    package="lidar_perception",
                    executable="obstacle_memory_grid_node",
                    name="obstacle_memory_grid_node",
                    parameters=[obstacle_memory_grid_param_file],
                    output="screen"
                ),
            ]
        ),

        # ======================================================
        # [5] Memory Fusion node
        #
        # 입력:
        #   - /perception/lidar/local_occupancy_grid
        #   - /perception/lidar/obstacle_memory_grid
        #   - /localization/pose
        #
        # 출력:
        #   - /perception/lidar/local_occupancy_grid_with_memory
        #
        # 역할:
        #   mission_map 기준 obstacle memory를 현재 base_link 기준
        #   local occupancy grid 위에 다시 투영한다.
        #
        # 결과:
        #   FreeSpaceModel이 사용할 수 있는 base_link 기준 fused grid를 만든다.
        # ======================================================
        TimerAction(
            period=3.5,
            actions=[
                Node(
                    package="lidar_perception",
                    executable="memory_fusion_node",
                    name="memory_fusion_node",
                    parameters=[memory_fusion_param_file],
                    output="screen"
                ),
            ]
        ),

        # ======================================================
        # [6] Free Space Model node
        #
        # 입력:
        #   - /perception/lidar/local_occupancy_grid_with_memory
        #
        # 출력:
        #   - /perception/lidar/free_space_model
        #
        # 역할:
        #   현재 LiDAR grid와 obstacle memory가 합쳐진 fused grid를 기반으로
        #   주행 가능 방향, gap, clearance, risk level을 계산한다.
        #
        # 필수 설정:
        #   free_space_model.param.yaml의 input_topic이 반드시
        #   /perception/lidar/local_occupancy_grid_with_memory
        #   로 설정되어 있어야 한다.
        # ======================================================
        TimerAction(
            period=4.0,
            actions=[
                Node(
                    package="lidar_perception",
                    executable="free_space_model_node",
                    name="free_space_model_node",
                    parameters=[free_space_model_param_file],
                    output="screen"
                ),
            ]
        ),

        # ======================================================
        # [7] Free Space Marker node
        #
        # 입력:
        #   - /perception/lidar/free_space_model
        #
        # 출력:
        #   - RViz marker topic
        #
        # 역할:
        #   FreeSpaceModel 결과를 RViz에서 시각화한다.
        # ======================================================
        TimerAction(
            period=4.2,
            actions=[
                Node(
                    package="lidar_perception",
                    executable="free_space_marker_node",
                    name="free_space_marker_node",
                    parameters=[free_space_marker_param_file],
                    output="screen"
                ),
            ]
        ),
    ])