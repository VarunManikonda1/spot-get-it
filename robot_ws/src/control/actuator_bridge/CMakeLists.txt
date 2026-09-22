cmake_minimum_required(VERSION 3.8)
project(actuator_bridge)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(robot_interfaces REQUIRED)
find_package(Threads REQUIRED)

add_library(packet_codec
  src/packet_codec.cpp
)
target_compile_features(packet_codec PUBLIC cxx_std_17)
target_include_directories(packet_codec PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

add_library(uart_transport
  src/uart_transport.cpp
)
target_compile_features(uart_transport PUBLIC cxx_std_17)
target_include_directories(uart_transport PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(uart_transport
  packet_codec
  Threads::Threads
)

add_executable(actuator_bridge_node
  src/actuator_bridge_node.cpp
)
target_compile_features(actuator_bridge_node PUBLIC cxx_std_17)
target_include_directories(actuator_bridge_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(actuator_bridge_node
  packet_codec
  uart_transport
)
ament_target_dependencies(actuator_bridge_node
  rclcpp
  geometry_msgs
  sensor_msgs
  robot_interfaces
)

install(TARGETS
  packet_codec
  uart_transport
  actuator_bridge_node
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY include/
  DESTINATION include
)

install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
