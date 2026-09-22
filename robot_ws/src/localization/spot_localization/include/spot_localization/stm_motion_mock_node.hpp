#ifndef SPOT_LOCALIZATION__STM_MOTION_MOCK_NODE_HPP_
#define SPOT_LOCALIZATION__STM_MOTION_MOCK_NODE_HPP_

#include <cstdint>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "robot_interfaces/msg/stm_motion.hpp"

class StmMotionMockNode : public rclcpp::Node
{
public:
  StmMotionMockNode();

private:
  static double normalizeAngle(double angle);

  uint8_t getMotionState(double scenario_time_s) const;
  bool isMovingState(uint8_t motion_state) const;

  void updateGaitPhase(uint8_t motion_state, double dt);
  void updateYaw(uint8_t motion_state, double dt);
  void timerCallback();

private:
  rclcpp::Publisher<robot_interfaces::msg::StmMotion>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;

  rclcpp::Time start_time_;
  rclcpp::Time prev_time_;

  std::string topic_name_;

  double publish_rate_hz_{50.0};
  double gait_cycle_hz_{1.0};
  double turn_yaw_rate_rad_s_{0.785398};

  uint32_t seq_{0};

  double gait_total_phase_{0.0};
  double imu_yaw_rad_{0.0};
  double gyro_z_rad_s_{0.0};
};

#endif  // SPOT_LOCALIZATION__STM_MOTION_MOCK_NODE_HPP_