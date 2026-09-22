from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    # ==========================================================
    # [0] Package share 경로 설정
    # FindPackageShare는 install/share/<package_name> 경로를 찾아준다.
    # launch 파일 안에서 config yaml이나 다른 패키지 launch 파일을 참조할 때 사용한다.
    # ==========================================================
    lidar_perception_share = FindPackageShare("lidar_perception")
    cyglidar_share = FindPackageShare("cyglidar_d1_ros2")

    # ==========================================================================
    # [1] pointcloud_preprocess_node, obstacle_cluster_node parameter 파일 경로
    # ==========================================================================
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
        # - 주요 출력: /scan, /scan_2D, /scan_3D, /scan_image
        # - 주의: 실제 USB LiDAR 장치를 점유하므로, 다른 launch에서 driver 동시 실행 X
        # ======================================================
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(cyglidar_launch_file)
        ),

        # ======================================================
        # [2] base_link -> laser_frame static TF publish
        # - LiDAR 위치: (x, y, z) = (0.14m, 0.00m, z = 0.05m)
        # - 회전: (roll, pitch, yaw) = (0, 0, 0)
        # - TF:
        #   parent: base_link
        #   child : laser_frame
        # ======================================================
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
        # [3] PointCloud 전처리 노드 실행
        # - 입력: /scan_3D
        # - 출력: /perception/lidar/points_filtered
        # ======================================================
        Node(
            package="lidar_perception",
            executable="pointcloud_preprocess_node",
            name="pointcloud_preprocess_node",
            parameters=[preprocess_param_file],
            output="screen"
        ),

        # ======================================================
        # [4] Obstacle Cluster 노드
        # - 입력: /perception/lidar/points_filtered
        # - 출력: /perception/lidar/obstacle_clusters
        #        /perception/lidar/clustered_points_colored
        # ======================================================

        Node(
            package="lidar_perception",
            executable="obstacle_cluster_node",
            name="obstacle_cluster_node",
            parameters=[cluster_param_file],
            output="screen"
        ),
    ])