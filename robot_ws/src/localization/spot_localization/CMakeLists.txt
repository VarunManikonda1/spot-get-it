cmake_minimum_required(VERSION 3.8)
project(spot_localization)

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
# - robot_interfaces: StmMotion.msg 등 커스텀 메시지 인터페이스 패키지
# - nav_msgs: nav_msgs/msg/Odometry publish를 위해 필요
# - geometry_msgs: geometry_msgs/msg/TransformStamped, Quaternion 사용
# ============================================================
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(robot_interfaces REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)

# ============================================================
# stm_motion_mock_node
# ============================================================
# STM이 아직 SPI bridge로 연결되지 않은 상태에서,
# localization 개발을 위해 /localization/stm_motion 더미 토픽을 발행하는 노드
add_executable(stm_motion_mock_node
  src/stm_motion_mock_node.cpp
)

# include/spot_localization/*.hpp 헤더 파일을 사용할 수 있도록 include 경로 등록
# - BUILD_INTERFACE: 워크스페이스에서 빌드할 때 사용할 include 경로
# - INSTALL_INTERFACE: 패키지를 install한 뒤 다른 패키지에서 참조할 때 사용할 include 경로
# stm motion mock 노드
target_include_directories(stm_motion_mock_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# stm_motion_mock_node가 사용하는 ROS 2 의존성 연결
ament_target_dependencies(stm_motion_mock_node
  rclcpp
  robot_interfaces
)

# ============================================================
# gait_odom_estimator_node
# ============================================================
# 역할:
#   /localization/stm_motion을 구독하여
#   /localization/odometry와 odom -> base_link TF를 publish하는 노드
add_executable(gait_odom_estimator_node
  src/gait_odom_estimator_node.cpp
)

# include/spot_localization/gait_odom_estimator_node.hpp 사용을 위한 경로 등록
target_include_directories(gait_odom_estimator_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# gait_odom_estimator_node에서 사용하는 의존성 연결
ament_target_dependencies(gait_odom_estimator_node
  rclcpp
  robot_interfaces
  nav_msgs
  geometry_msgs
  tf2_ros
)

# ============================================================
# mission_tf_node
# ============================================================
# 역할: mission_map -> odom static TF를 publish하는 노드
add_executable(mission_tf_node
  src/mission_tf_node.cpp
)

target_include_directories(mission_tf_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

ament_target_dependencies(mission_tf_node
  rclcpp
  geometry_msgs
  tf2_ros
)

# ============================================================
# localization_pose_node
# ============================================================
# 역할: TF 기반 최종 Localization Pose publish 노드
add_executable(localization_pose_node
  src/localization_pose_node.cpp
)

target_include_directories(localization_pose_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

ament_target_dependencies(localization_pose_node
  rclcpp
  geometry_msgs
  robot_interfaces
  tf2
  tf2_ros
)

# ============================================================
# Install targets
# ============================================================
# 빌드된 실행 파일을 install/spot_localization/lib/spot_localization 아래에 설치
install(TARGETS
  stm_motion_mock_node
  gait_odom_estimator_node
  mission_tf_node
  localization_pose_node
  DESTINATION lib/${PROJECT_NAME}
)

# include 폴더 설치
# 나중에 다른 패키지나 테스트 코드에서 헤더를 참조할 수 있게 하기 위함
install(DIRECTORY
  include/
  DESTINATION include/
)

# config, launch 폴더 설치
# ros2 launch 실행 시 install/share/spot_localization 아래에서 찾을 수 있게 하기 위함
install(DIRECTORY
  config
  launch
  DESTINATION share/${PROJECT_NAME}
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
