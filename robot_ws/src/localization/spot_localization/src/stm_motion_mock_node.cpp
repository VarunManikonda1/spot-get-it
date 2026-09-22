/*
 * path: spot_localization/src/stm_motion_mock_node.cpp
 * role: Publish mock STM motion data for gait-based localization development.
 * input topics:
 *   - none
 * output topics:
 *   - /localization/stm_motion (robot_interfaces/msg/StmMotion)
 * main functions:
 *   - Simulate STOP, WALK_FORWARD, TURN_LEFT, WALK_FORWARD sequence.
 *   - Publish timestamp_ms, seq, motion_state, gait_phase,
 *     gait_cycle_count, imu_yaw_rad, gyro_z_rad_s.
 */

#include "spot_localization/stm_motion_mock_node.hpp"

#include <chrono>
#include <cmath>
#include <functional>
#include <memory>

using namespace std::chrono_literals;

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
}  // namespace

StmMotionMockNode::StmMotionMockNode()
: Node("stm_motion_mock_node")
{
  publish_rate_hz_ = this->declare_parameter<double>("publish_rate_hz", 50.0);
  gait_cycle_hz_ = this->declare_parameter<double>("gait_cycle_hz", 1.0);
  turn_yaw_rate_rad_s_ = this->declare_parameter<double>(
    "turn_yaw_rate_rad_s", kPi / 4.0);

  topic_name_ = this->declare_parameter<std::string>(
    "stm_motion_topic", "/localization/stm_motion");

  const auto period_ms = static_cast<int>(1000.0 / publish_rate_hz_);

  publisher_ = this->create_publisher<robot_interfaces::msg::StmMotion>(
    topic_name_, rclcpp::QoS(10));

  start_time_ = this->now();
  prev_time_ = start_time_;

  timer_ = this->create_wall_timer(
    std::chrono::milliseconds(period_ms),
    std::bind(&StmMotionMockNode::timerCallback, this));

  RCLCPP_INFO(this->get_logger(), "stm_motion_mock_node started.");
  RCLCPP_INFO(this->get_logger(), "Publishing mock STM motion to: %s", topic_name_.c_str());
}

double StmMotionMockNode::normalizeAngle(double angle)
{
  while (angle > kPi) {
    angle -= kTwoPi;
  }
  while (angle < -kPi) {
    angle += kTwoPi;
  }
  return angle;
}

uint8_t StmMotionMockNode::getMotionState(double scenario_time_s) const
{
  using Msg = robot_interfaces::msg::StmMotion;

  // 0 ~ 2s: STOP
  if (scenario_time_s < 2.0) {
    return Msg::STOP;
  }

  // 2 ~ 8s: WALK_FORWARD
  // 전진 6초
  if (scenario_time_s < 8.0) {
    return Msg::WALK_FORWARD;
  }

  // 8 ~ 10s: TURN_LEFT
  // 기본 turn_yaw_rate_rad_s = pi/4 rad/s 기준
  // 2초 동안 +90도 회전
  if (scenario_time_s < 10.0) {
    return Msg::TURN_LEFT;
  }

  // 10 ~ 13s: WALK_FORWARD
  // 회전한 방향으로 전진 3초
  if (scenario_time_s < 13.0) {
    return Msg::WALK_FORWARD;
  }

  // 13 ~ 14s: TURN_RIGHT
  // 기본 turn_yaw_rate_rad_s = pi/4 rad/s 기준
  // 1초 동안 -45도 회전
  // 90도 방향에서 45도 대각 방향으로 변경
  if (scenario_time_s < 14.0) {
    return Msg::TURN_RIGHT;
  }

  // 14 ~ 20s: WALK_FORWARD
  // 대각 방향으로 전진 6초
  if (scenario_time_s < 20.0) {
    return Msg::WALK_FORWARD;
  }

  // 20 ~ 22s: STOP
  return Msg::STOP;
}

bool StmMotionMockNode::isMovingState(uint8_t motion_state) const
{
  using Msg = robot_interfaces::msg::StmMotion;

  return motion_state == Msg::WALK_FORWARD ||
         motion_state == Msg::WALK_BACKWARD ||
         motion_state == Msg::TURN_LEFT ||
         motion_state == Msg::TURN_RIGHT ||
         motion_state == Msg::STRAFE_LEFT ||
         motion_state == Msg::STRAFE_RIGHT;
}

void StmMotionMockNode::updateGaitPhase(uint8_t motion_state, double dt)
{
  if (!isMovingState(motion_state)) {
    return;
  }

  gait_total_phase_ += gait_cycle_hz_ * dt;

  if (gait_total_phase_ < 0.0) {
    gait_total_phase_ = 0.0;
  }
}

void StmMotionMockNode::updateYaw(uint8_t motion_state, double dt)
{
  using Msg = robot_interfaces::msg::StmMotion;

  gyro_z_rad_s_ = 0.0;

  if (motion_state == Msg::TURN_LEFT) {
    gyro_z_rad_s_ = turn_yaw_rate_rad_s_;
    imu_yaw_rad_ += gyro_z_rad_s_ * dt;
  } else if (motion_state == Msg::TURN_RIGHT) {
    gyro_z_rad_s_ = -turn_yaw_rate_rad_s_;
    imu_yaw_rad_ += gyro_z_rad_s_ * dt;
  }

  imu_yaw_rad_ = normalizeAngle(imu_yaw_rad_);
}

void StmMotionMockNode::timerCallback()
{
  const auto now = this->now();
  const double elapsed_s = (now - start_time_).seconds();
  const double dt = (now - prev_time_).seconds();
  prev_time_ = now;

  if (dt <= 0.0 || dt > 1.0) {
    return;
  }

  // Repeat one scenario every 16 seconds.
  const double scenario_time_s = std::fmod(elapsed_s, 22.0);
  const uint8_t motion_state = getMotionState(scenario_time_s);

  updateGaitPhase(motion_state, dt);
  updateYaw(motion_state, dt);

  const auto gait_cycle_count = static_cast<uint32_t>(std::floor(gait_total_phase_));
  const float gait_phase = static_cast<float>(gait_total_phase_ - gait_cycle_count);

  robot_interfaces::msg::StmMotion msg;
  msg.header.stamp = now;
  msg.header.frame_id = "stm";

  msg.timestamp_ms = static_cast<uint32_t>(elapsed_s * 1000.0);
  msg.seq = seq_++;

  msg.motion_state = motion_state;
  msg.gait_phase = gait_phase;
  msg.gait_cycle_count = gait_cycle_count;

  msg.imu_yaw_rad = static_cast<float>(imu_yaw_rad_);
  msg.gyro_z_rad_s = static_cast<float>(gyro_z_rad_s_);

  publisher_->publish(msg);
}

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<StmMotionMockNode>());
  rclcpp::shutdown();
  return 0;
}