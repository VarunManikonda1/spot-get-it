cmake_minimum_required(VERSION 3.8)
project(lidar_perception)              # package 이름 확인

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
# - sensor_msgs: PointCloud2 입력/출력을 위해 필요
# - std_msgs: ROS 2 표준 메시지 사용을 위해 필요
# - nav_msgs: OccupancyGrid 메시지 사용을 위해 필요
# - geometry_msgs: RViz marker 및 TF 관련 geometry 메시지 사용
# - visualization_msgs: RViz 시각화 marker publish를 위해 필요
# - robot_interfaces: LiDAR perception custom message 사용
# - tf2 / tf2_ros / tf2_sensor_msgs: LiDAR frame 변환 처리
# - pcl_conversions: ROS PointCloud2와 PCL PointCloud 변환
# - PCL: PointCloud filtering, KD-Tree, Euclidean clustering 처리
# ============================================================
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(visualization_msgs REQUIRED)
find_package(robot_interfaces REQUIRED)

find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_sensor_msgs REQUIRED)

find_package(pcl_conversions REQUIRED)
find_package(PCL REQUIRED COMPONENTS common filters search segmentation)

add_definitions(${PCL_DEFINITIONS})

# ============================================================
# [0] pointcloud_preprocess_node
# ------------------------------------------------------------
# - 역할:
#   CygLiDAR raw PointCloud2를 base_link 기준의 전처리된 점군으로 변환한다.
#
# - Input:
#   - /scan_3D
#     Type: sensor_msgs/msg/PointCloud2
#
# - Output:
#   - /perception/lidar/points_filtered
#     Type: sensor_msgs/msg/PointCloud2
# ============================================================
add_executable(pointcloud_preprocess_node
  src/pointcloud_preprocess_node.cpp
)

target_include_directories(pointcloud_preprocess_node PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  ${PCL_INCLUDE_DIRS}
)

ament_target_dependencies(pointcloud_preprocess_node
  rclcpp
  sensor_msgs
  std_msgs
  robot_interfaces
  tf2
  tf2_ros
  tf2_sensor_msgs
  pcl_conversions
)

target_link_libraries(pointcloud_preprocess_node
  ${PCL_LIBRARIES}
)

# ============================================================
# [1] obstacle_cluster_node
# ------------------------------------------------------------
# - 역할:
#   전처리된 PointCloud를 클러스터링하여 장애물 후보 묶음과
#   RViz 디버깅용 colored point cloud를 publish한다.
#
# - Input:
#   - /perception/lidar/points_filtered
#     Type: sensor_msgs/msg/PointCloud2
#
# - Output:
#   - /perception/lidar/obstacle_clusters
#     Type: robot_interfaces/msg/ObstacleClusters
#   - /perception/lidar/clustered_points_colored
#     Type: sensor_msgs/msg/PointCloud2
# ============================================================
add_executable(obstacle_cluster_node
  src/obstacle_cluster_node.cpp
)

target_include_directories(obstacle_cluster_node PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  ${PCL_INCLUDE_DIRS}
)

ament_target_dependencies(obstacle_cluster_node
  rclcpp
  sensor_msgs
  std_msgs
  robot_interfaces
  pcl_conversions
)

target_link_libraries(obstacle_cluster_node
  ${PCL_LIBRARIES}
)

# ============================================================
# [2] obstacle_model_node
# ------------------------------------------------------------
# - 역할:
#   obstacle_clusters를 Decision/Planning 계층에서 사용하기 쉬운
#   front/left/right 대표 장애물 모델로 요약한다.
#
# - Input:
#   - /perception/lidar/obstacle_clusters
#     Type: robot_interfaces/msg/ObstacleClusters
#
# - Output:
#   - /perception/lidar/obstacle_model
#     Type: robot_interfaces/msg/ObstacleModel
# ============================================================
add_executable(obstacle_model_node
  src/obstacle_model_node.cpp
)

target_include_directories(obstacle_model_node PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

ament_target_dependencies(obstacle_model_node
  rclcpp
  robot_interfaces
)

# ============================================================
# [3] local_occupancy_grid_node
# ------------------------------------------------------------
# - 역할:
#   전처리된 LiDAR PointCloud를 base_link 기준 local OccupancyGrid로 변환한다.
#
# - Input:
#   - /perception/lidar/points_filtered
#     Type: sensor_msgs/msg/PointCloud2
#
# - Output:
#   - /perception/lidar/local_occupancy_grid
#     Type: nav_msgs/msg/OccupancyGrid
# ============================================================
add_executable(local_occupancy_grid_node 
  src/local_occupancy_grid_node.cpp
)

target_include_directories(local_occupancy_grid_node PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  ${PCL_INCLUDE_DIRS}
)

ament_target_dependencies(local_occupancy_grid_node
  rclcpp
  sensor_msgs
  nav_msgs
  pcl_conversions
)

target_link_libraries(local_occupancy_grid_node
  ${PCL_LIBRARIES}
)

# ============================================================
# [4] free_space_model_node
# ------------------------------------------------------------
# - 역할:
#   local OccupancyGrid를 angular bin 단위로 분석하여
#   주행 가능한 free-space gap과 best heading을 계산한다.
#
# - Input:
#   - /perception/lidar/local_occupancy_grid
#     Type: nav_msgs/msg/OccupancyGrid
#
# - Output:
#   - /perception/lidar/free_space_model
#     Type: robot_interfaces/msg/FreeSpaceModel
# ============================================================
add_executable(free_space_model_node 
  src/free_space_model_node.cpp
)

target_include_directories(free_space_model_node PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

ament_target_dependencies(free_space_model_node
  rclcpp
  nav_msgs
  robot_interfaces
)

# ============================================================
# [5] free_space_marker_node
# ------------------------------------------------------------
# - 역할:
#   FreeSpaceModel을 RViz에서 확인할 수 있는 marker 형태로 변환한다.
#
# - Input:
#   - /perception/lidar/free_space_model
#     Type: robot_interfaces/msg/FreeSpaceModel
#
# - Output:
#   - RViz visualization marker topic
#   - /perception/lidar/free_space_markers
#     Type: visualization_msgs/msg/MarkerArray
# ============================================================
add_executable(free_space_marker_node 
  src/free_space_marker_node.cpp
)

target_include_directories(free_space_marker_node PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

ament_target_dependencies(free_space_marker_node
  rclcpp
  visualization_msgs
  geometry_msgs
  robot_interfaces
)

# ============================================================
# [6] obstacle_memory_grid_node
# ------------------------------------------------------------
# - 역할:
#   현재 LiDAR local occupancy grid를 localization pose 기준으로
#   mission_map 좌표계에 누적하고, TTL 동안 최근 장애물 cell을 기억한다.
#
# - Input:
#   - /perception/lidar/local_occupancy_grid
#     Type: nav_msgs/msg/OccupancyGrid
#     Frame: base_link
#
#   - /localization/pose
#     Type: robot_interfaces/msg/LocalizedRobotPose
#     Frame: mission_map
#
# - Output:
#   - /perception/lidar/obstacle_memory_grid
#     Type: nav_msgs/msg/OccupancyGrid
#     Frame: mission_map
# ============================================================
add_executable(obstacle_memory_grid_node
  src/obstacle_memory_grid_node.cpp
)

target_include_directories(obstacle_memory_grid_node PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

ament_target_dependencies(obstacle_memory_grid_node
  rclcpp
  nav_msgs
  robot_interfaces
)

# ============================================================
# [7] memory_fusion_node
# ------------------------------------------------------------
# - 역할:
#   현재 LiDAR 기반 local occupancy grid와
#   mission_map 기준 obstacle memory grid를 현재 localization pose 기준으로 융합하여
#   local planner가 사용할 수 있는 memory 포함 local occupancy grid를 생성한다.
#
# - Input:
#   - /perception/lidar/local_occupancy_grid
#     Type: nav_msgs/msg/OccupancyGrid
#
#   - /perception/lidar/obstacle_memory_grid
#     Type: nav_msgs/msg/OccupancyGrid
#
#   - /localization/pose
#     Type: robot_interfaces/msg/LocalizedRobotPose
#
# - Output:
#   - /perception/lidar/local_occupancy_grid_with_memory
#     Type: nav_msgs/msg/OccupancyGrid
# ============================================================
add_executable(memory_fusion_node
  src/memory_fusion_node.cpp
)

target_include_directories(memory_fusion_node PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

ament_target_dependencies(memory_fusion_node
  rclcpp
  nav_msgs
  robot_interfaces
)

# ============================================================
# Install targets
# ------------------------------------------------------------
# install/lib/lidar_perception/ 아래에 실행 파일 설치
# 추후 노드 구현 후에 개발한 노드 추가하기
# ============================================================
install(TARGETS
  pointcloud_preprocess_node
  obstacle_cluster_node
  obstacle_model_node
  local_occupancy_grid_node
  free_space_model_node
  free_space_marker_node
  obstacle_memory_grid_node
  memory_fusion_node
  DESTINATION lib/${PROJECT_NAME}
)

# ============================================================
# config / launch 디렉토리 설치
# ------------------------------------------------------------
# launch 파일에서 FindPackageShare("lidar_perception")로
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