#!/bin/bash
# App Selector를 우회하여 현재 터미널에서 바로 실행 (bashrc 재실행 원천 차단)
unset AMENT_PREFIX_PATH
unset COLCON_PREFIX_PATH
unset ROS_PYTHON_VERSION
unset ROS_VERSION
unset PYTHONPATH
unset OLD_PYTHONPATH

export ROS_DISTRO=humble
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
export LD_LIBRARY_PATH=/home/ubuntu/isaac-sim/exts/isaacsim.ros2.bridge/humble/lib

cd /home/ubuntu/isaac-sim
./isaac-sim.sh
