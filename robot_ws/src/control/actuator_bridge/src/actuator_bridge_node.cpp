#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "geometry_msgs/msg/twist.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/imu.hpp"

#include "robot_interfaces/msg/joint_feedback.hpp"
#include "robot_interfaces/msg/joint_target.hpp"
#include "robot_interfaces/msg/robot_status.hpp"
#include "robot_interfaces/msg/stm_motion.hpp"

#include "actuator_bridge/packet_codec.hpp"
#include "actuator_bridge/uart_transport.hpp"

namespace
{

constexpr uint8_t MODE_DISABLE = 0;

constexpr uint8_t STATUS_OK = 0;
constexpr uint8_t STATUS_WARN = 1;
constexpr uint8_t STATUS_FAULT = 2;

constexpr uint8_t STM_STATUS_TORQUE_ON = (1U << 0U);
constexpr uint8_t STM_STATUS_IMU_OK = (1U << 1U);
constexpr uint8_t STM_STATUS_ALL_SERVOS_OK = (1U << 2U);
constexpr uint8_t STM_STATUS_CMD_FRESH = (1U << 3U);
constexpr uint8_t STM_STATUS_IN_SAFE_STATE = (1U << 4U);
constexpr uint8_t STM_STATUS_CALIBRATING = (1U << 5U);

constexpr uint8_t FAULT_NONE = 0;
constexpr uint8_t FAULT_STALE_TARGET = 1;
constexpr uint8_t FAULT_LINK_OPEN_FAILED = 10;
constexpr uint8_t FAULT_LINK_WRITE_FAILED = 11;
constexpr uint8_t FAULT_FEEDBACK_DECODE_FAILED = 12;
constexpr uint8_t FAULT_SEQ_MISMATCH = 13;
constexpr uint8_t FAULT_LINK_TIMEOUT = 14;

constexpr uint8_t STM_FAULT_STALE_COMMAND = 4;

// ROS JointTarget semantic mode
constexpr uint8_t JT_MODE_DISABLE = 0;
constexpr uint8_t JT_MODE_STAND = 1;
constexpr uint8_t JT_MODE_RL = 2;
constexpr uint8_t JT_MODE_CROUCH = 3;
constexpr uint8_t JT_MODE_E_STOP = 4;
constexpr uint8_t JT_MODE_CLASSIC = 5;
constexpr uint8_t JT_MODE_SIT = 6;

// Wire mode: STM firmware가 실제로 이해하는 최소 mode
constexpr uint8_t WIRE_MODE_DISABLE = 0;
constexpr uint8_t WIRE_MODE_OPERATE = 1;

// Wire flags
constexpr uint8_t WIRE_FLAG_E_STOP = 0x08;

uint8_t mapRosModeToWireMode(uint8_t ros_mode, uint8_t ros_flags)
{
  // E-STOP flag가 이미 들어와 있으면 mode와 무관하게 disable
  if ((ros_flags & WIRE_FLAG_E_STOP) != 0U) {
    return WIRE_MODE_DISABLE;
  }

  switch (ros_mode) {
    case JT_MODE_STAND:
    case JT_MODE_RL:
    case JT_MODE_CROUCH:
    case JT_MODE_CLASSIC:
    case JT_MODE_SIT:
      return WIRE_MODE_OPERATE;

    case JT_MODE_DISABLE:
    case JT_MODE_E_STOP:
    default:
      return WIRE_MODE_DISABLE;
  }
}

uint8_t mapRosModeToWireFlags(uint8_t ros_mode, uint8_t ros_flags)
{
  uint8_t wire_flags = ros_flags;

  if (ros_mode == JT_MODE_E_STOP) {
    wire_flags |= WIRE_FLAG_E_STOP;
  }

  return wire_flags;
}

rclcpp::QoS control_qos()
{
  return rclcpp::QoS(rclcpp::KeepLast(1)).best_effort().durability_volatile();
}

double applyDeadband(double value, double deadband)
{
  if (!std::isfinite(value)) {
    return value;
  }

  return std::fabs(value) <= deadband ? 0.0 : value;
}

uint8_t classifyMotionStateFromTwist(
  const geometry_msgs::msg::Twist & twist,
  double deadband)
{
  const double vx = applyDeadband(twist.linear.x, deadband);
  const double vy = applyDeadband(twist.linear.y, deadband);
  const double wz = applyDeadband(twist.angular.z, deadband);

  if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(wz)) {
    return robot_interfaces::msg::StmMotion::UNKNOWN;
  }

  const double abs_vx = std::fabs(vx);
  const double abs_vy = std::fabs(vy);
  const double abs_wz = std::fabs(wz);

  if (abs_vx == 0.0 && abs_vy == 0.0 && abs_wz == 0.0) {
    return robot_interfaces::msg::StmMotion::STOP;
  }

  if (abs_vx != 0.0 && abs_wz == 0.0) {
    return vx > 0.0 ?
           robot_interfaces::msg::StmMotion::WALK_FORWARD :
           robot_interfaces::msg::StmMotion::WALK_BACKWARD;
  }

  if (abs_wz != 0.0 && abs_vx == 0.0) {
    return wz > 0.0 ?
           robot_interfaces::msg::StmMotion::TURN_LEFT :
           robot_interfaces::msg::StmMotion::TURN_RIGHT;
  }

  if (abs_wz != 0.0 && abs_vx != 0.0) {
    return wz > 0.0 ?
           robot_interfaces::msg::StmMotion::WALK_FORWARD_TURN_LEFT :
           robot_interfaces::msg::StmMotion::WALK_FORWARD_TURN_RIGHT;
  }
  return vy > 0.0 ?
         robot_interfaces::msg::StmMotion::STRAFE_LEFT :
         robot_interfaces::msg::StmMotion::STRAFE_RIGHT;
}

uint8_t effectiveMotionStateForMode(uint8_t ros_mode, uint8_t requested_motion_state)
{
  switch (ros_mode) {
    case JT_MODE_RL:
    case JT_MODE_CLASSIC:
      return requested_motion_state;

    case JT_MODE_DISABLE:
    case JT_MODE_STAND:
    case JT_MODE_CROUCH:
    case JT_MODE_E_STOP:
    case JT_MODE_SIT:
    default:
      return robot_interfaces::msg::StmMotion::STOP;
  }
}

}  // namespace

class ActuatorBridgeNode : public rclcpp::Node
{
public:
  ActuatorBridgeNode()
  : Node("actuator_bridge_node")
  {
    control_rate_hz_ = this->declare_parameter("control_rate_hz", 50.0);
    max_target_age_ms_ = this->declare_parameter("max_target_age_ms", 1000.0);
    stale_disable_after_ms_ = this->declare_parameter("stale_disable_after_ms", 5000.0);
    freeze_seq_when_stale_ = this->declare_parameter("freeze_seq_when_stale", true);
    if (stale_disable_after_ms_ < max_target_age_ms_) {
      throw std::runtime_error("stale_disable_after_ms must be >= max_target_age_ms");
    }

    uart_device_ = this->declare_parameter("uart_device", "/dev/ttyTHS1");
    uart_baudrate_ = this->declare_parameter("uart_baudrate", 921600);
    feedback_timeout_ms_ = this->declare_parameter("feedback_timeout_ms", 100.0);
    max_rx_buffer_size_ = this->declare_parameter("max_rx_buffer_size", 4096);
    status_transient_fail_limit_ = this->declare_parameter("status_transient_fail_limit", 15);
    if (status_transient_fail_limit_ < 1) {
      throw std::runtime_error("status_transient_fail_limit must be >= 1");
    }

    const std::vector<double> default_joint_angles = this->declare_parameter<std::vector<double>>(
      "default_joint_angles",
      {0.0, -0.9921237899157832, 1.4907337340120823,
        0.0, -0.9921237899157832, 1.4907337340120823,
        0.0, -0.9921237899157832, 1.4907337340120823,
        0.0, -0.9921237899157832, 1.4907337340120823});

    const std::vector<double> default_max_delta_rad = this->declare_parameter<std::vector<double>>(
      "default_max_delta_rad",
      {0.03, 0.03, 0.03,
        0.03, 0.03, 0.03,
        0.03, 0.03, 0.03,
        0.03, 0.03, 0.03});

    if (default_joint_angles.size() != actuator_bridge::NUM_JOINTS) {
      throw std::runtime_error("default_joint_angles must have 12 elements");
    }
    if (default_max_delta_rad.size() != actuator_bridge::NUM_JOINTS) {
      throw std::runtime_error("default_max_delta_rad must have 12 elements");
    }

    for (std::size_t i = 0; i < actuator_bridge::NUM_JOINTS; ++i) {
      default_target_rad_[i] = static_cast<float>(default_joint_angles[i]);
      latest_target_rad_[i] = default_target_rad_[i];
      default_max_delta_rad_[i] = static_cast<float>(default_max_delta_rad[i]);
      latest_max_delta_rad_[i] = default_max_delta_rad_[i];
    }

    target_topic_ = this->declare_parameter(
      "target_topic", "/control/selected/joint_target");
    cmd_vel_topic_ = this->declare_parameter(
      "cmd_vel_topic", "/control/cmd_vel/spot_01");
    motion_state_deadband_ = this->declare_parameter("motion_state_deadband", 0.05);

    target_sub_ = this->create_subscription<robot_interfaces::msg::JointTarget>(
      target_topic_,
      control_qos(),
      std::bind(&ActuatorBridgeNode::targetCallback, this, std::placeholders::_1));
    cmd_vel_sub_ = this->create_subscription<geometry_msgs::msg::Twist>(
      cmd_vel_topic_,
      control_qos(),
      std::bind(&ActuatorBridgeNode::cmdVelCallback, this, std::placeholders::_1));

    joint_feedback_pub_ = this->create_publisher<robot_interfaces::msg::JointFeedback>(
      "/control/actuator/joint_feedback", control_qos());
    imu_pub_ = this->create_publisher<sensor_msgs::msg::Imu>(
      "/control/actuator/imu", control_qos());
    status_pub_ = this->create_publisher<robot_interfaces::msg::RobotStatus>(
      "/control/actuator/status", control_qos());
    odom_source_pub_ = this->create_publisher<robot_interfaces::msg::StmMotion>(
      "/localization/spot_motion",
      rclcpp::QoS(rclcpp::KeepLast(1)).best_effort().durability_volatile());

    const bool uart_ok = uart_.open_device(
      uart_device_,
      static_cast<int>(uart_baudrate_),
      static_cast<std::size_t>(max_rx_buffer_size_));

    if (!uart_ok) {
      link_open_failed_ = true;
      RCLCPP_ERROR(
        this->get_logger(),
        "UART open failed: %s",
        uart_.last_error().c_str());
    } else {
      RCLCPP_INFO(
        this->get_logger(),
        "UART opened: device=%s, baudrate=%d, command=%zu bytes, feedback=%zu bytes",
        uart_device_.c_str(),
        uart_baudrate_,
        actuator_bridge::COMMAND_PACKET_SIZE,
        actuator_bridge::FEEDBACK_PACKET_SIZE);
    }

    last_target_time_ = this->now();

    const auto period = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<double>(1.0 / control_rate_hz_));
    timer_ = this->create_wall_timer(period, std::bind(&ActuatorBridgeNode::controlLoop, this));

    RCLCPP_INFO(
      this->get_logger(),
      "actuator_bridge_node started with UART transport: rate=%.1f Hz, target_topic=%s, cmd_vel_topic=%s, motion_state_deadband=%.3f",
      control_rate_hz_,
      target_topic_.c_str(),
      cmd_vel_topic_.c_str(),
      motion_state_deadband_);
  }

private:
  void cmdVelCallback(const geometry_msgs::msg::Twist::SharedPtr msg)
  {
    const uint8_t motion_state =
      classifyMotionStateFromTwist(*msg, motion_state_deadband_);

    std::lock_guard<std::mutex> lock(mutex_);
    latest_motion_state_ = motion_state;
  }

  void targetCallback(const robot_interfaces::msg::JointTarget::SharedPtr msg)
  {
    if (msg->target_rad.size() != actuator_bridge::NUM_JOINTS ||
      msg->max_delta_rad.size() != actuator_bridge::NUM_JOINTS)
    {
      RCLCPP_WARN_THROTTLE(
        this->get_logger(), *this->get_clock(), 1000,
        "invalid JointTarget array size");
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    latest_seq_ = msg->seq;
    latest_mode_ = msg->mode;
    latest_flags_ = msg->flags;
    last_target_time_ = this->now();
    have_target_ = true;

    for (std::size_t i = 0; i < actuator_bridge::NUM_JOINTS; ++i) {
      latest_target_rad_[i] = msg->target_rad[i];
      latest_max_delta_rad_[i] = msg->max_delta_rad[i];
    }

    latest_gait_phase_ = msg->gait_phase;
    latest_gait_cycle_count_ = msg->gait_cycle_count;
  }

  void controlLoop()
  {
    const auto loop_start = std::chrono::steady_clock::now();

    if (link_open_failed_ || !uart_.is_open()) {
      publishBridgeFaultStatus(
        last_sent_seq_,
        FAULT_LINK_OPEN_FAILED,
        0.0F,
        loop_start);
      return;
    }

    actuator_bridge::CommandPacket command;
    const bool stale = fillCommandPacket(command);

    const std::vector<uint8_t> tx = actuator_bridge::encode_command_packet(command);

    const auto write_start = std::chrono::steady_clock::now();
    const bool write_ok = uart_.write_packet(tx);
    const auto write_end = std::chrono::steady_clock::now();
    const float write_latency_ms = static_cast<float>(
      std::chrono::duration<double, std::milli>(write_end - write_start).count());

    if (!write_ok) {
      packet_drop_count_++;
      RCLCPP_WARN_THROTTLE(
        this->get_logger(), *this->get_clock(), 1000,
        "UART write failed: %s", uart_.last_error().c_str());
      publishBridgeFaultStatus(
        command.seq,
        FAULT_LINK_WRITE_FAILED,
        write_latency_ms,
        loop_start);
      return;
    }

    actuator_bridge::FeedbackPacket feedback;
    std::chrono::steady_clock::time_point feedback_stamp;
    const bool have_feedback = uart_.get_latest_feedback(feedback, feedback_stamp);

    const auto now = std::chrono::steady_clock::now();
    const float feedback_age_ms = have_feedback ?
      static_cast<float>(std::chrono::duration<double, std::milli>(now - feedback_stamp).count()) :
      9999.0F;

    if (!have_feedback || feedback_age_ms > feedback_timeout_ms_) {
      packet_drop_count_++;
      publishBridgeFaultStatus(
        command.seq,
        FAULT_LINK_TIMEOUT,
        feedback_age_ms,
        loop_start);
      return;
    }

    checkSequence(command.seq, feedback.seq_echo);
    publishJointFeedbackFromPacket(feedback);
    publishImuFromPacket(feedback);
    publishStatusFromPacket(feedback, stale, loop_start, feedback_age_ms);
    publishOdomSourceFromPacket(feedback);
  }

  bool fillCommandPacket(actuator_bridge::CommandPacket & command)
  {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto now = this->now();
    const double target_age_ms = (now - last_target_time_).seconds() * 1000.0;
    const bool stale = (!have_target_) || (target_age_ms > max_target_age_ms_);

    if (stale) {
      const bool hard_stale = (!have_target_) || (target_age_ms > stale_disable_after_ms_);
      command.seq = freeze_seq_when_stale_ ? last_sent_seq_ : latest_seq_;
      command.mode = hard_stale ?
        WIRE_MODE_DISABLE :
        mapRosModeToWireMode(latest_mode_, latest_flags_);
      command.flags = hard_stale ? 0U : mapRosModeToWireFlags(latest_mode_, latest_flags_);
      command.gait_phase = latest_gait_phase_;
      command.gait_cycle_count = latest_gait_cycle_count_;
      command.motion_state = robot_interfaces::msg::StmMotion::STOP;

      for (std::size_t i = 0; i < actuator_bridge::NUM_JOINTS; ++i) {
        command.target_rad[i] = hard_stale ? default_target_rad_[i] : latest_target_rad_[i];
        command.max_delta_rad[i] = hard_stale ? default_max_delta_rad_[i] : latest_max_delta_rad_[i];
      }
    } else {
      command.seq = latest_seq_;
      command.mode = mapRosModeToWireMode(latest_mode_, latest_flags_);
      command.flags = mapRosModeToWireFlags(latest_mode_, latest_flags_);
      command.gait_phase = latest_gait_phase_;
      command.gait_cycle_count = latest_gait_cycle_count_;
      command.motion_state = effectiveMotionStateForMode(
        latest_mode_,
        latest_motion_state_);

      for (std::size_t i = 0; i < actuator_bridge::NUM_JOINTS; ++i) {
        command.target_rad[i] = latest_target_rad_[i];
        command.max_delta_rad[i] = latest_max_delta_rad_[i];
      }
    }

    command.timestamp_us = static_cast<uint32_t>(
      static_cast<uint64_t>(now.nanoseconds()) / 1000ULL);

    last_sent_seq_ = command.seq;
    return stale;
  }

  void checkSequence(uint16_t sent_seq, uint16_t seq_echo)
  {
    constexpr uint16_t kAllowedSeqLag = 5;
    const uint16_t lag = static_cast<uint16_t>(sent_seq - seq_echo);

    if (lag > kAllowedSeqLag) {
      RCLCPP_WARN_THROTTLE(
        this->get_logger(), *this->get_clock(), 1000,
        "seq lag too large: sent=%u, echo=%u, lag=%u",
        sent_seq, seq_echo, lag);
    }
  }

  void publishJointFeedbackFromPacket(const actuator_bridge::FeedbackPacket & feedback)
  {
    robot_interfaces::msg::JointFeedback msg;
    msg.header.stamp = this->now();
    msg.header.frame_id = "base_link";
    msg.seq_echo = feedback.seq_echo;

    for (std::size_t i = 0; i < actuator_bridge::NUM_JOINTS; ++i) {
      msg.position_rad[i] = feedback.position_rad[i];
      msg.velocity_rad_s[i] = feedback.velocity_rad_s[i];
      msg.load[i] = feedback.load_or_current[i];
      msg.temperature[i] = feedback.temperature[i];
    }

    joint_feedback_pub_->publish(msg);
  }

  void publishImuFromPacket(const actuator_bridge::FeedbackPacket & feedback)
  {
    sensor_msgs::msg::Imu msg;
    msg.header.stamp = this->now();
    msg.header.frame_id = "imu_link";

    // FeedbackPacket: wxyz, ROS sensor_msgs/Imu: xyzw
    msg.orientation.w = feedback.quat_wxyz[0];
    msg.orientation.x = feedback.quat_wxyz[1];
    msg.orientation.y = feedback.quat_wxyz[2];
    msg.orientation.z = feedback.quat_wxyz[3];

    msg.angular_velocity.x = feedback.gyro_rad_s[0];
    msg.angular_velocity.y = feedback.gyro_rad_s[1];
    msg.angular_velocity.z = feedback.gyro_rad_s[2];

    msg.linear_acceleration.x = feedback.accel_m_s2[0];
    msg.linear_acceleration.y = feedback.accel_m_s2[1];
    msg.linear_acceleration.z = feedback.accel_m_s2[2];

    imu_pub_->publish(msg);
  }

  void publishStatusFromPacket(
    const actuator_bridge::FeedbackPacket & feedback,
    bool stale,
    const std::chrono::steady_clock::time_point & loop_start,
    float feedback_age_ms)
  {
    const auto loop_end = std::chrono::steady_clock::now();
    const float loop_time_ms = static_cast<float>(
      std::chrono::duration<double, std::milli>(loop_end - loop_start).count());

    const bool torque_on = (feedback.status & STM_STATUS_TORQUE_ON) != 0U;
    const bool imu_ok = (feedback.status & STM_STATUS_IMU_OK) != 0U;
    const bool servos_ok = (feedback.status & STM_STATUS_ALL_SERVOS_OK) != 0U;
    const bool cmd_fresh = (feedback.status & STM_STATUS_CMD_FRESH) != 0U;
    const bool in_safe_state = (feedback.status & STM_STATUS_IN_SAFE_STATE) != 0U;
    const bool calibrating = (feedback.status & STM_STATUS_CALIBRATING) != 0U;
    const bool fault_ok = (feedback.fault_code == FAULT_NONE);
    const bool stm_stale_command =
      (feedback.fault_code == STM_FAULT_STALE_COMMAND) && !cmd_fresh;
    const bool raw_ok =
      fault_ok && !in_safe_state && !calibrating && imu_ok && servos_ok && cmd_fresh && torque_on;
    const bool transient_status_candidate =
      !in_safe_state && !calibrating && imu_ok && servos_ok &&
      ((fault_ok && (!cmd_fresh || !torque_on)) || (stm_stale_command && torque_on));

    bool effective_ok = raw_ok;
    bool effective_torque_on = torque_on;
    bool suppress_transient_warn = false;

    if (raw_ok) {
      status_transient_fail_streak_ = 0;
    } else if (transient_status_candidate) {
      status_transient_fail_streak_++;
      if (status_transient_fail_streak_ <=
        static_cast<uint32_t>(status_transient_fail_limit_))
      {
        effective_ok = true;
        effective_torque_on = true;
        suppress_transient_warn = true;
      }
    } else {
      status_transient_fail_streak_ = status_transient_fail_limit_;
    }

    robot_interfaces::msg::RobotStatus msg;
    msg.header.stamp = this->now();
    msg.header.frame_id = "base_link";
    msg.seq_echo = feedback.seq_echo;

    if (stale) {
      msg.status = STATUS_WARN;
      msg.fault_code = FAULT_STALE_TARGET;
      msg.torque_enabled = effective_torque_on;
      msg.servo_connected = servos_ok;
    } else {
      msg.fault_code = feedback.fault_code;
      msg.torque_enabled = effective_torque_on;
      msg.servo_connected = servos_ok;

      if (!fault_ok && !stm_stale_command) {
        msg.status = STATUS_FAULT;
      } else if (stm_stale_command && !suppress_transient_warn) {
        msg.status = STATUS_WARN;
      } else if (in_safe_state || calibrating) {
        msg.status = STATUS_WARN;
      } else if (effective_ok) {
        msg.status = STATUS_OK;
      } else {
        msg.status = STATUS_WARN;
      }
    }

    if (msg.status != STATUS_OK && !suppress_transient_warn) {
      RCLCPP_WARN_THROTTLE(
        this->get_logger(), *this->get_clock(), 1000,
        "actuator status not OK: ros_status=%u raw_stm_status=0x%02x "
        "fault_code=%u stale=%d torque_on=%d imu_ok=%d servos_ok=%d cmd_fresh=%d "
        "in_safe_state=%d calibrating=%d feedback_age_ms=%.3f seq_echo=%u",
        msg.status,
        feedback.status,
        msg.fault_code,
        stale ? 1 : 0,
        torque_on ? 1 : 0,
        imu_ok ? 1 : 0,
        servos_ok ? 1 : 0,
        cmd_fresh ? 1 : 0,
        in_safe_state ? 1 : 0,
        calibrating ? 1 : 0,
        feedback_age_ms,
        msg.seq_echo);
    }

    msg.bus_voltage = feedback.bus_voltage;
    msg.loop_time_ms = loop_time_ms;

    // RobotStatus.msg의 기존 필드명을 유지한다.
    // UART 최종 구조에서는 spi_latency_ms = feedback_age_ms,
    // spi_connected = UART link open 상태로 해석한다.
    msg.spi_latency_ms = feedback_age_ms;
    msg.spi_connected = uart_.is_open();

    msg.packet_drop_count = packet_drop_count_;
    msg.crc_error_count = totalCrcErrorCount();
    msg.missed_deadline_count = missed_deadline_count_;

    if (loop_time_ms > (1000.0 / control_rate_hz_)) {
      missed_deadline_count_++;
    }

    status_pub_->publish(msg);
  }

  void publishOdomSourceFromPacket(const actuator_bridge::FeedbackPacket & feedback)
  {
    robot_interfaces::msg::StmMotion msg;
    msg.header.stamp = this->now();
    msg.header.frame_id = "base_link";
    msg.timestamp_ms = feedback.timestamp_us / 1000U;
    msg.seq = feedback.seq_echo;
    msg.motion_state = feedback.motion_state;
    msg.gait_phase = feedback.gait_phase;
    msg.gait_cycle_count = feedback.gait_cycle_count;
    msg.imu_yaw_rad = feedback.imu_yaw_rad;
    msg.gyro_z_rad_s = feedback.gyro_rad_s[2];

    odom_source_pub_->publish(msg);
  }

  void publishBridgeFaultStatus(
    uint16_t seq_echo,
    uint8_t fault_code,
    float feedback_age_or_latency_ms,
    const std::chrono::steady_clock::time_point & loop_start)
  {
    const auto loop_end = std::chrono::steady_clock::now();
    const float loop_time_ms = static_cast<float>(
      std::chrono::duration<double, std::milli>(loop_end - loop_start).count());

    robot_interfaces::msg::RobotStatus msg;
    msg.header.stamp = this->now();
    msg.header.frame_id = "base_link";
    msg.seq_echo = seq_echo;
    msg.status = STATUS_FAULT;
    msg.fault_code = fault_code;
    msg.bus_voltage = 0.0F;
    msg.loop_time_ms = loop_time_ms;
    msg.spi_latency_ms = feedback_age_or_latency_ms;
    msg.packet_drop_count = packet_drop_count_;
    msg.crc_error_count = totalCrcErrorCount();
    msg.missed_deadline_count = missed_deadline_count_;
    msg.torque_enabled = false;
    msg.spi_connected = uart_.is_open();
    msg.servo_connected = false;

    if (loop_time_ms > (1000.0 / control_rate_hz_)) {
      missed_deadline_count_++;
    }

    status_pub_->publish(msg);
  }

  uint32_t totalCrcErrorCount() const
  {
    return crc_error_count_ + uart_.crc_error_count();
  }

private:
  double control_rate_hz_{50.0};
  double max_target_age_ms_{1000.0};
  double stale_disable_after_ms_{5000.0};
  bool freeze_seq_when_stale_{true};

  std::string uart_device_{"/dev/ttyTHS1"};
  int uart_baudrate_{921600};
  double feedback_timeout_ms_{100.0};
  int max_rx_buffer_size_{4096};
  double motion_state_deadband_{0.05};
  int status_transient_fail_limit_{15};
  uint32_t status_transient_fail_streak_{0};

  actuator_bridge::UartTransport uart_;
  bool link_open_failed_{false};

  std::mutex mutex_;
  bool have_target_{false};
  uint16_t latest_seq_{0};
  uint16_t last_sent_seq_{0};
  uint8_t latest_mode_{MODE_DISABLE};
  uint8_t latest_flags_{0};
  rclcpp::Time last_target_time_;

  std::string target_topic_{"/control/selected/joint_target"};
  std::string cmd_vel_topic_{"/control/cmd_vel/spot_01"};

  std::array<float, actuator_bridge::NUM_JOINTS> default_target_rad_{};
  std::array<float, actuator_bridge::NUM_JOINTS> default_max_delta_rad_{};
  std::array<float, actuator_bridge::NUM_JOINTS> latest_target_rad_{};
  std::array<float, actuator_bridge::NUM_JOINTS> latest_max_delta_rad_{};

  float latest_gait_phase_{0.0F};
  uint32_t latest_gait_cycle_count_{0};
  uint8_t latest_motion_state_{robot_interfaces::msg::StmMotion::STOP};

  uint32_t packet_drop_count_{0};
  uint32_t crc_error_count_{0};
  uint32_t missed_deadline_count_{0};

  rclcpp::Subscription<robot_interfaces::msg::JointTarget>::SharedPtr target_sub_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_sub_;
  rclcpp::Publisher<robot_interfaces::msg::JointFeedback>::SharedPtr joint_feedback_pub_;
  rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr imu_pub_;
  rclcpp::Publisher<robot_interfaces::msg::RobotStatus>::SharedPtr status_pub_;
  rclcpp::Publisher<robot_interfaces::msg::StmMotion>::SharedPtr odom_source_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<ActuatorBridgeNode>());
  rclcpp::shutdown();
  return 0;
}
