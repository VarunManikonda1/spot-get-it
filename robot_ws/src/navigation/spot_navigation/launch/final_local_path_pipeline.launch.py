# ============================================================
# local_path_pipeline.launch.py
#
# 역할:
#   Local Path Planner Pipeline(mock global path,
#   path progress tracker, local path planner)를 실행한다.
#
# 실행 노드:
#   1. mock_global_path_publisher_node
#      - mock global path publish
#
#   2. path_progress_tracker_node
#      - global path와 pose를 기반으로 path progress 계산
#
#   3. local_path_planner_node
#      - PathProgress, pose, ObstacleModel, FreeSpaceModel을 기반으로 local path 생성
#
# 주요 토픽 흐름:
#   /planning/mock_global_path/spot_01
#       → path_progress_tracker_node
#       → /navigation/path_progress/spot_01
#       → local_path_planner_node
#       → /navigation/local_path/spot_01
#       → /navigation/local_planner_status/spot_01
#
# 주의:
#   local_path_planner_node는 아래 perception 토픽도 필요로 한다.
#   따라서 이 launch와 별도로 LiDAR Perception launch가 실행되어 있어야 한다.
#
#   - /perception/lidar/obstacle_model
#   - /perception/lidar/free_space_model
#   - /localization/pose
# ============================================================
from launch import LaunchDescription
from launch.substitutions import PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    spot_navigation_share = FindPackageShare("spot_navigation")

    mock_global_path_param_file = PathJoinSubstitution([
        spot_navigation_share,
        "config",
        "mock_global_path_publisher.param.yaml"
    ])
    
    navigation_debug_visualizer_param_file = PathJoinSubstitution([
        spot_navigation_share,
        "config",
        "navigation_debug_visualizer.param.yaml"
    ])

    path_progress_tracker_param_file = PathJoinSubstitution([
        spot_navigation_share,
        "config",
        "path_progress_tracker.param.yaml"
    ])

    local_path_planner_param_file = PathJoinSubstitution([
        spot_navigation_share,
        "config",
        "local_path_planner.param.yaml"
    ])

    return LaunchDescription([
        # ============================================================
        # mock_global_path_publisher_node
        #
        # 역할:
        #   테스트용 global path를 publish한다.
        #
        # Output:
        #   - /planning/mock_global_path/spot_01
        # ============================================================
        # Node(
        #     package="spot_navigation",
        #     executable="mock_global_path_publisher_node",
        #     name="mock_global_path_publisher_node",
        #     parameters=[mock_global_path_param_file],
        #     output="screen"
        # ),

        Node(
            package="spot_navigation",
            executable="navigation_debug_visualizer_node",
            name="navigation_debug_visualizer_node",
            parameters=[navigation_debug_visualizer_param_file],
            output="screen"
        ),
        # ============================================================
        # path_progress_tracker_node
        #
        # 역할:
        #   global path와 localization pose를 기반으로 현재 진행 상태를 계산한다.
        #
        # Input:
        #   - /planning/mock_global_path/spot_01
        #   - /localization/mock_pose
        #
        # Output:
        #   - /navigation/path_progress/spot_01
        # ============================================================
        Node(
            package="spot_navigation",
            executable="path_progress_tracker_node",
            name="path_progress_tracker_node",
            parameters=[path_progress_tracker_param_file],
            output="screen"
        ),

        # ============================================================
        # local_path_planner_node
        #
        # 역할:
        #   PathProgress, pose, ObstacleModel, FreeSpaceModel을 기반으로
        #   상태별 local path를 생성한다.
        #
        # Input:
        #   - /navigation/path_progress/spot_01
        #   - /localization/mock_pose
        #   - /perception/lidar/obstacle_model
        #   - /perception/lidar/free_space_model
        #
        # Output:
        #   - /navigation/local_path/spot_01
        #   - /navigation/local_planner_status/spot_01
        # ============================================================
        Node(
            package="spot_navigation",
            executable="local_path_planner_node",
            name="local_path_planner_node",
            parameters=[local_path_planner_param_file],
            output="screen"
        ),
    ])