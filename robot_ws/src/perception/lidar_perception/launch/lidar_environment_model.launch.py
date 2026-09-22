# 실행 명령어:
#   ros2 launch lidar_perception lidar_environment_model.launch.py
#
# 목적:
#   LiDAR perception 환경 모델 전체를 한 번에 통합 실행한다.
#
# 포함 계층:
#   1. 공통 입력 계층
#      - CygLiDAR driver
#      - base_link -> laser_frame static TF
#      - pointcloud_preprocess_node
#      - obstacle_cluster_node
#
#   2. ObstacleModel 계층
#      - obstacle_model_node
#
#   3. FreeSpaceModel 계층
#      - local_occupancy_grid_node
#      - free_space_model_node
#
# 주요 출력 토픽:
#   /perception/lidar/points_filtered
#   /perception/lidar/obstacle_clusters
#   /perception/lidar/clustered_points_colored
#   /perception/lidar/obstacle_model
#   /perception/lidar/local_occupancy_grid
#   /perception/lidar/free_space_model
#
# 주의:
#   이 launch는 전체 통합 실행용이다.
#   실행 중에는 lidar_preprocess.launch.py,
#   lidar_obstacle_model.launch.py,
#   lidar_free_space_model.launch.py를 동시에 실행하지 않는다.

from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    # ==========================================================
    # [0] Package share 경로 설정
    # ==========================================================
    lidar_perception_share = FindPackageShare("lidar_perception")
    cyglidar_share = FindPackageShare("cyglidar_d1_ros2")

    # ==========================================================
    # [1] Parameter 파일 경로 설정
    # ==========================================================
    preprocess_param_file = PathJoinSubstitution([
        lidar_perception_share,
        "config",
        "pointcloud_preprocess.param.yaml"
    ])

    cluster_param_file = PathJoinSubstitution([
        lidar_perception_share,
        "config",
        "obstacle_cluster.param.yaml"
    ])

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

    # ==========================================================
    # [2] CygLiDAR driver launch 파일 경로
    # ==========================================================
    cyglidar_launch_file = PathJoinSubstitution([
        cyglidar_share,
        "launch",
        "cyglidar.launch.py"
    ])

    return LaunchDescription([

        # ======================================================
        # [1] CygLiDAR D1 driver 실행
        # ======================================================
        # 역할:
        #   실제 LiDAR 센서를 구동하고 raw topic을 publish한다.
        #
        # 출력:
        #   /scan
        #   /scan_2D
        #   /scan_3D
        #   /scan_image
        #
        # 통합 launch에서는 driver를 단 한 번만 실행한다.
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(cyglidar_launch_file)
        ),

        # ======================================================
        # [2] base_link -> laser_frame static TF
        # ======================================================
        # 역할:
        #   LiDAR sensor frame을 robot base frame에 고정한다.
        #
        # parent:
        #   base_link
        #
        # child:
        #   laser_frame
        #
        # 현재 장착 위치:
        #   x = 0.14m
        #   y = 0.00m
        #   z = 0.05m
        Node(
            package="tf2_ros",
            executable="static_transform_publisher",
            name="base_to_laser_static_tf",
            arguments=[
                "0.14", "0.00", "0.05",
                "0", "0", "0",
                "base_link", "laser_frame"
            ],
            output="screen"
        ),

        # ======================================================
        # [3] PointCloud 전처리 노드
        # ======================================================
        # 입력:
        #   /scan_3D
        #
        # 출력:
        #   /perception/lidar/points_filtered
        #
        # 이 토픽은 이후 두 계층에서 사용된다.
        #   1. obstacle_cluster_node
        #   2. local_occupancy_grid_node
        Node(
            package="lidar_perception",
            executable="pointcloud_preprocess_node",
            name="pointcloud_preprocess_node",
            parameters=[preprocess_param_file],
            output="screen"
        ),

        # ======================================================
        # [4] Obstacle Cluster 노드
        # ======================================================
        # 입력:
        #   /perception/lidar/points_filtered
        #
        # 출력:
        #   /perception/lidar/obstacle_clusters
        #   /perception/lidar/clustered_points_colored
        #
        # obstacle_model_node의 입력을 만들고,
        # RViz 시각화용 colored point cloud를 제공한다.
        Node(
            package="lidar_perception",
            executable="obstacle_cluster_node",
            name="obstacle_cluster_node",
            parameters=[cluster_param_file],
            output="screen"
        ),

        # ======================================================
        # [5] Obstacle Model 노드
        # ======================================================
        # 입력:
        #   /perception/lidar/obstacle_clusters
        #
        # 출력:
        #   /perception/lidar/obstacle_model
        #
        # 역할:
        #   front/left/right sector별 대표 장애물 상태를 요약한다.
        Node(
            package="lidar_perception",
            executable="obstacle_model_node",
            name="obstacle_model_node",
            output="screen"
        ),

        # ======================================================
        # [6] Local Occupancy Grid 노드
        # ======================================================
        # 입력:
        #   /perception/lidar/points_filtered
        #
        # 출력:
        #   /perception/lidar/local_occupancy_grid
        #
        # 역할:
        #   base_link 기준 2D local occupancy grid를 생성한다.
        Node(
            package="lidar_perception",
            executable="local_occupancy_grid_node",
            name="local_occupancy_grid_node",
            parameters=[local_occupancy_grid_param_file],
            output="screen"
        ),

        # ======================================================
        # [7] Free Space Model 노드
        # ======================================================
        # 입력:
        #   /perception/lidar/local_occupancy_grid
        #
        # 출력:
        #   /perception/lidar/free_space_model
        #
        # 역할:
        #   local occupancy grid를 front/left/right sector별로 해석하여
        #   free/blocked/clearance 상태를 요약한다.
        Node(
            package="lidar_perception",
            executable="free_space_model_node",
            name="free_space_model_node",
            parameters=[free_space_model_param_file],
            output="screen"
        ),
    ])