cmake_minimum_required(VERSION 3.8)
project(spot_navigation)

# ============================================================
# Compile options
# ============================================================
# -Wall      : 일반적인 경고 출력
# -Wextra    : 추가 경고 출력
# -Wpedantic : 표준 C++ 관점에서 엄격한 경고 출력
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ============================================================
# Package dependencies
# - ament_cmake: ROS 2 CMake 기반 패키지 빌드 시스템
# - rclcpp: ROS 2 C++ 노드 작성용 클라이언트 라이브러리
# - robot_interfaces: 커스텀 메시지 인터페이스 패키지
# - nav_msgs: local_path를 nav_msgs/msg/Path publish를 위해 필요
# - geometry_msgs: geometry_msgs/msg/TransformStamped, Quaternion 사용
# ============================================================
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(robot_interfaces REQUIRED)
find_package(visualization_msgs REQUIRED)

# ============================================================
# [0] navigation_debug_visualizer_node
# ------------------------------------------------------------
# - 역할:
#   Navigation custom message를 RViz에서 볼 수 있는 표준 메시지로 변환한다.
#
# - Input:
#   - /planning/mock_global_path/spot_01
#   - /localization/mock_pose
#   - /navigation/path_progress/spot_01
#
# - Ouput:
#   - /debug/navigation/global_path
#   - /debug/navigation/mock_pose
#   - /debug/navigation/progress_markers
# ============================================================
add_executable(navigation_debug_visualizer_node
  src/navigation_debug_visualizer_node.cpp
)

target_include_directories(navigation_debug_visualizer_node PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

ament_target_dependencies(navigation_debug_visualizer_node
  rclcpp
  std_msgs
  geometry_msgs
  nav_msgs
  visualization_msgs
  robot_interfaces
)

# ============================================================
# [1] mock_global_path_publisher_node
# ------------------------------------------------------------
# - 역할:
#   실제 planning/global_path_manager가 publish하는
#   /planning/global_path/spot_01 토픽을 mock 데이터로 재현한다.
# - Input: 없음
# - Output: /planning/global_path/spot_01
#     Type: robot_interfaces/msg/GlobalPathWaypoints
# ============================================================
add_executable(mock_global_path_publisher_node
  src/mock_global_path_publisher_node.cpp
)

target_include_directories(mock_global_path_publisher_node PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

ament_target_dependencies(mock_global_path_publisher_node
  rclcpp
  std_msgs
  geometry_msgs
  robot_interfaces
)

# ============================================================
# [2] mock_localization_pose_publisher_node
# ------------------------------------------------------------
# - 역할:
#   mock global path를 입력으로 받아,
#   path 위를 로봇이 이동하는 것처럼 /localization/pose를 publish한다.
#
# - Input:
#   - /planning/mock_global_path/spot_01
#     Type: robot_interfaces/msg/GlobalPathWaypoints
#
# - Output:
#   - /localization/pose
#     Type: robot_interfaces/msg/LocalizedRobotPose
# ============================================================
add_executable(mock_localization_pose_publisher_node
  src/mock_localization_pose_publisher_node.cpp
)

target_include_directories(mock_localization_pose_publisher_node PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

ament_target_dependencies(mock_localization_pose_publisher_node
  rclcpp
  std_msgs
  geometry_msgs
  robot_interfaces
)

# ============================================================
# [3] path_progress_tracker_node
# ------------------------------------------------------------
# - 역할:
#     GlobalPathWaypoints와 LocalizedRobotPose를 입력으로 받아
#     현재 로봇이 global path의 어디쯤 진행 중인지 계산한다.
#
# - Input:
#   - /planning/mock_global_path/spot_01
#   - /localization/mock_pose
#
# - Output:
#   - /navigation/path_progress/spot_01
# ============================================================
add_executable(path_progress_tracker_node
  src/path_progress_tracker_node.cpp
)

target_include_directories(path_progress_tracker_node PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

ament_target_dependencies(path_progress_tracker_node
  rclcpp
  std_msgs
  geometry_msgs
  robot_interfaces
)

# ============================================================
# [4] navigation_fsm_node
# ------------------------------------------------------------
# - 역할:
#   path_progress, obstacle, free_space, pose를 보고
#   현재 자율보행 FSM 상태를 결정하여 publish한다.
# - Input :
#   - /navigation/path_progress/spot_01
#   - /localization/pose
#   - /perception/lidar/obstacle_model
#   - /perception/lidar/free_space_model
# - Output : /navigation/state/spot_01
#     Type: robot_interfaces/msg/NavigationState
# ============================================================
add_executable(navigation_fsm_node
  src/navigation_fsm_node.cpp
)

target_include_directories(navigation_fsm_node PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

ament_target_dependencies(navigation_fsm_node
  rclcpp
  nav_msgs
  std_msgs
  geometry_msgs
  robot_interfaces
)

# ============================================================
# [5] path_follower_node
# ------------------------------------------------------------
# - 역할:
#   local path와 현재 pose를 보고 heading error를 계산하여
#   cmd_vel_raw를 publish한다.
# - Input :
#   - /planning/local_path/spot_01
#   - /localization/pose
#   - /navigation/state/spot_01
# - Output : /control/cmd_vel_raw/spot_01
#     Type: geometry_msgs/msg/TwistStamped
# ============================================================
add_executable(path_follower_node
  src/path_follower_node.cpp
)

target_include_directories(path_follower_node PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

ament_target_dependencies(path_follower_node
  rclcpp
  std_msgs
  geometry_msgs
  nav_msgs
  robot_interfaces
)

# ============================================================
# [6] safety_supervisor_node
# ------------------------------------------------------------
# - 역할:
#   cmd_vel_raw를 받아 안전 조건 검사 후 가감속을 적용하여
#   최종 cmd_vel을 publish한다.
# - Input :
#   - /control/cmd_vel_raw/spot_01
#   - /navigation/state/spot_01
#   - /perception/lidar/obstacle_model
#   - /perception/lidar/free_space_model
#   - /localization/pose
# - Output :
#   - /control/cmd_vel/spot_01
#   - /safety/status/spot_01
# ============================================================
add_executable(safety_supervisor_node
  src/safety_supervisor_node.cpp
)

target_include_directories(safety_supervisor_node PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

ament_target_dependencies(safety_supervisor_node
  rclcpp
  std_msgs
  geometry_msgs
  robot_interfaces
)

# ============================================================
# [7] local_path_planner_node
# ------------------------------------------------------------
# 역할:
#   PathProgress와 현재 localization pose를 기반으로
#   follower/controller가 따라갈 짧은 local path를 생성하는 노드
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
# ============================================================
add_executable(local_path_planner_node
  src/local_path_planner_node.cpp
)

target_include_directories(local_path_planner_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

ament_target_dependencies(local_path_planner_node
  rclcpp
  geometry_msgs
  nav_msgs
  robot_interfaces
)


# 추가 노드 구현 후 add_executable(), ament_target_dependencies() 추가


# ============================================================
# Install targets
# ------------------------------------------------------------
# install/lib/spot_navigation/ 아래에 실행 파일 설치
# 추후 노드 구현 후에 개발한 노드 추가하기
# ============================================================
install(TARGETS
  navigation_debug_visualizer_node
  mock_global_path_publisher_node
  mock_localization_pose_publisher_node
  path_progress_tracker_node
  local_path_planner_node
  navigation_fsm_node
  path_follower_node
  safety_supervisor_node
  DESTINATION lib/${PROJECT_NAME}
)

# ============================================================
# config / launch 디렉토리 설치
# ------------------------------------------------------------
# launch 파일에서 FindPackageShare("spot_navigation")로
# config/*.yaml 파일을 찾기 위해 반드시 설치해야 한다.
# ============================================================
install(DIRECTORY
  config
  launch
  DESTINATION share/${PROJECT_NAME}
)

# include 폴더 설치
# 나중에 다른 패키지나 테스트 코드에서 헤더를 참조할 수 있게 하기 위함
install(DIRECTORY
  include/
  DESTINATION include/
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  # the following line skips the linter which checks for copyrights
  # comment the line when a copyright and license is added to all source files
  set(ament_cmake_copyright_FOUND TRUE)
  # the following line skips cpplint (only works in a git repo)
  # comment the line when this package is in a git repo and when
  # a copyright and license is added to all source files
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
