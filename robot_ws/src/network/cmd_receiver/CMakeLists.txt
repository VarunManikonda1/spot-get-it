cmake_minimum_required(VERSION 3.5)
project(cmd_receiver)

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)

ament_python_install_package(${PROJECT_NAME})

install(PROGRAMS
  cmd_receiver/cmd_receiver_node.py
  DESTINATION lib/${PROJECT_NAME}
  RENAME cmd_receiver_node
)

install(PROGRAMS
  cmd_receiver/ble_button_mode_receiver.py
  DESTINATION lib/${PROJECT_NAME}
  RENAME ble_button_mode_receiver
)

install(DIRECTORY
  launch
  DESTINATION share/${PROJECT_NAME}
)

ament_package()
