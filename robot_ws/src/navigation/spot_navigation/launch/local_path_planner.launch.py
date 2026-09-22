# ============================================================
# local_path_planner.launch.py
#
# 역할:
#   local_path_planner_node만 단독 실행한다.
#   PathProgress와 현재 localization pose를 입력으로 받아,
#   follower/controller가 따라갈 짧은 local path를 생성한다.
#
# Input:
#   - /navigation/path_progress/spot_01
#     Type: robot_interfaces/msg/PathProgress
#
#   - /localization/mock_pose
#     Type: robot_interfaces/msg/LocalizedRobotPose
#
# Output:
#   - /navigation/local_path/spot_01
#     Type: nav_msgs/msg/Path
#
#   - /navigation/local_planner_status/spot_01
#     Type: robot_interfaces/msg/LocalPlannerStatus
#
# 실행 노드:
#   - local_path_planner_node
#
# 주의:
#   이 launch는 local_path_planner_node만 실행한다.
#   따라서 path_progress_tracker_node와 mock/localization pose publisher는
#   별도로 실행되어 있어야 GLOBAL_SUB_GOAL 상태로 local path가 생성된다.
# ============================================================

from launch import LaunchDescription
from launch.substitutions import PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    spot_navigation_share = FindPackageShare("spot_navigation")

    local_path_planner_param_file = PathJoinSubstitution([
        spot_navigation_share,
        "config",
        "local_path_planner.param.yaml"
    ])

    return LaunchDescription([
        # ============================================================
        # local_path_planner_node
        #
        # 역할:
        #   PathProgress와 current pose를 기반으로 local path 생성
        #
        # Input:
        #   - /navigation/path_progress/spot_01
        #   - /localization/mock_pose
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