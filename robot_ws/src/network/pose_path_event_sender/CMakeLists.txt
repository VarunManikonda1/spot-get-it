cmake_minimum_required(VERSION 3.8)
project(pose_path_event_sender)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── 의존 패키지 ─────────────────────────────────────────────────
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(robot_interfaces REQUIRED)

# ─── pose_path_event_sender_node ─────────────────────────────────
add_executable(pose_path_event_sender_node
  src/pose_path_event_sender_node.cpp
)

target_include_directories(pose_path_event_sender_node PRIVATE
  include            # include/pose_path_event_sender/proto.h
)

ament_target_dependencies(pose_path_event_sender_node
  rclcpp
  geometry_msgs
  std_msgs
  nav_msgs
  robot_interfaces
)

# ─── 설치 ────────────────────────────────────────────────────────
install(TARGETS pose_path_event_sender_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()