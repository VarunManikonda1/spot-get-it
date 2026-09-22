#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "robot_interfaces/msg/joint_feedback.hpp"
#include "robot_interfaces/msg/joint_target.hpp"

namespace
{

using JointFeedback = robot_interfaces::msg::JointFeedback;
using JointTarget = robot_interfaces::msg::JointTarget;

constexpr std::size_t NUM_JOINTS = 12;
constexpr uint8_t MODE_CROUCH = 3;

std::array<float, NUM_JOINTS> toPoseArray(
  const std::vector<double> & values,
  const std::string & param_name)
{
  if (values.size() != NUM_JOINTS) {
    throw std::runtime_error(param_name + " must have 12 elements");
  }

  std::array<float, NUM_JOINTS> out{};
  for (std::size_t i = 0; i < NUM_JOINTS; ++i) {
    out[i] = static_cast<float>(values[i]);
  }
  return out;
}

std::string toUpper(std::string value)
{
  std::transform(
    value.begin(),
    value.end(),
    value.begin(),
    [](unsigned char c) {
      return static_cast<char>(std::toupper(c));
    });
  return value;
}

double smoothstep(double x)
{
  const double s = std::clamp(x, 0.0, 1.0);
  return s * s * (3.0 - 2.0 * s);
}

std::array<float, NUM_JOINTS> blendPose(
  const std::array<float, NUM_JOINTS> & from,
  const std::array<float, NUM_JOINTS> & to,
  double ratio)
{
  const double s = smoothstep(ratio);
  std::array<float, NUM_JOINTS> out{};
  for (std::size_t i = 0; i < NUM_JOINTS; ++i) {
    out[i] = static_cast<float>(from[i] + (to[i] - from[i]) * s);
  }
  return out;
}

}  // namespace

class DetectMotionNode : public rclcpp::Node
{
public:
  DetectMotionNode()
  : Node("detect_motion_node")
  {
    rate_hz_ = this->declare_parameter<double>("rate_hz", 50.0);
    sit_transition_sec_ = this->declare_parameter<double>("sit_transition_sec", 3.0);
    sit_skip_tolerance_rad_ = this->declare_parameter<double>("sit_skip_tolerance_rad", 0.08);
    raise_transition_sec_ = this->declare_parameter<double>("raise_transition_sec", 1.2);
    wave_segment_sec_ = this->declare_parameter<double>("wave_segment_sec", 0.75);
    wave_cycles_ = this->declare_parameter<int>("wave_cycles", 3);
    sit_max_delta_rad_ = this->declare_parameter<double>("sit_max_delta_rad", 0.015);
    raise_max_delta_rad_ = this->declare_parameter<double>("raise_max_delta_rad", 0.02);
    wave_max_delta_rad_ = this->declare_parameter<double>("wave_max_delta_rad", 0.02);

    sit_pose_ = toPoseArray(
      this->declare_parameter<std::vector<double>>(
        "sit_pose",
    {
      0.0, -0.700000, 2.250000,
      0.0, -0.700000, 2.250000,
      0.0, -0.700000, 2.250000,
      0.0, -0.700000, 2.250000
    }),
      "sit_pose");

    raised_pose_ = toPoseArray(
      this->declare_parameter<std::vector<double>>(
        "right_front_raised_pose",
    {
      -0.120000, -0.720000, 1.050000,
      0.0, 1.100000, 1.550000,
      0.120000, -1.500000, 2.250000,
      0.120000, -1.500000, 2.250000
    }),
      "right_front_raised_pose");

    calf_wave_pose_ = toPoseArray(
      this->declare_parameter<std::vector<double>>(
        "right_front_calf_wave_pose",
    {
      -0.120000, -0.720000, 1.050000,
      0.0, 1.100000, 2.150000,
      0.120000, -1.500000, 2.250000,
      0.120000, -1.500000, 2.250000
    }),
      "right_front_calf_wave_pose");

    transition_from_pose_ = sit_pose_;
    last_target_pose_ = sit_pose_;

    mode_sub_ = this->create_subscription<std_msgs::msg::String>(
      "/control/behavior/active_mode",
      rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local(),
      std::bind(&DetectMotionNode::modeCallback, this, std::placeholders::_1));

    feedback_sub_ = this->create_subscription<JointFeedback>(
      "/control/actuator/joint_feedback",
      rclcpp::QoS(rclcpp::KeepLast(1)).best_effort(),
      std::bind(&DetectMotionNode::feedbackCallback, this, std::placeholders::_1));

    target_pub_ = this->create_publisher<JointTarget>(
      "/control/detect/joint_target",
      rclcpp::QoS(rclcpp::KeepLast(1)).best_effort());

    const auto period = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<double>(1.0 / rate_hz_));

    timer_ = this->create_wall_timer(
      period,
      std::bind(&DetectMotionNode::timerCallback, this));

    RCLCPP_INFO(
      this->get_logger(),
      "detect_motion_node started. rate=%.1f Hz, wave_cycles=%d",
      rate_hz_,
      wave_cycles_);
  }

private:
  enum class Phase
  {
    Idle,
    SitDown,
    RaiseFront,
    Wave,
    HoldRaised
  };

  void modeCallback(const std_msgs::msg::String::SharedPtr msg)
  {
    const std::string mode = toUpper(msg->data);
    if (mode == "DETECT") {
      if (!active_) {
        startSequence();
        RCLCPP_INFO(this->get_logger(), "DETECT sequence started");
      } else if (phase_ == Phase::HoldRaised || phase_ == Phase::Idle) {
        startSequence();
        RCLCPP_INFO(this->get_logger(), "DETECT sequence restarted");
      }
      return;
    }

    if (active_) {
      active_ = false;
      setPhase(Phase::Idle);
      RCLCPP_INFO(this->get_logger(), "DETECT sequence reset by mode=%s", mode.c_str());
    }
  }

  void feedbackCallback(const JointFeedback::SharedPtr msg)
  {
    std::lock_guard<std::mutex> lock(feedback_mutex_);
    latest_feedback_.emplace();
    for (std::size_t i = 0; i < NUM_JOINTS; ++i) {
      latest_feedback_.value()[i] = msg->position_rad[i];
    }
    latest_feedback_time_ = this->now();
  }

  void timerCallback()
  {
    std::array<float, NUM_JOINTS> target = sit_pose_;
    uint8_t mode = MODE_CROUCH;
    double max_delta = sit_max_delta_rad_;

    if (active_) {
      updateActiveTarget(target, mode, max_delta);
    }

    JointTarget msg;
    msg.header.stamp = this->now();
    msg.header.frame_id = "base_link";
    msg.seq = static_cast<uint16_t>(seq_ & 0xFFFFU);
    msg.mode = mode;
    msg.flags = 0;
    msg.gait_phase = 0.0F;
    msg.gait_cycle_count = 0U;

    for (std::size_t i = 0; i < NUM_JOINTS; ++i) {
      msg.target_rad[i] = target[i];
      msg.max_delta_rad[i] = static_cast<float>(max_delta);
    }

    target_pub_->publish(msg);
    last_target_pose_ = target;
    seq_++;
  }

  void updateActiveTarget(
    std::array<float, NUM_JOINTS> & target,
    uint8_t & mode,
    double & max_delta)
  {
    const double elapsed = phaseElapsedSec();

    if (phase_ == Phase::SitDown) {
      target = blendPose(transition_from_pose_, sit_pose_, elapsed / sit_transition_sec_);
      mode = MODE_CROUCH;
      max_delta = sit_max_delta_rad_;
      if (elapsed >= sit_transition_sec_) {
        setPhase(Phase::RaiseFront);
      }
      return;
    }

    if (phase_ == Phase::RaiseFront) {
      target = blendPose(sit_pose_, raised_pose_, elapsed / raise_transition_sec_);
      mode = MODE_CROUCH;
      max_delta = raise_max_delta_rad_;
      if (elapsed >= raise_transition_sec_) {
        setPhase(Phase::Wave);
      }
      return;
    }

    if (phase_ == Phase::Wave) {
      const int half_cycles = std::max(1, wave_cycles_ * 2);
      const int segment = static_cast<int>(std::floor(elapsed / wave_segment_sec_));
      if (segment >= half_cycles) {
        setPhase(Phase::HoldRaised);
        target = raised_pose_;
      } else {
        const double segment_elapsed = elapsed - static_cast<double>(segment) * wave_segment_sec_;
        const double local = segment_elapsed / wave_segment_sec_;
        const double from = (segment % 2 == 0) ? 0.0 : 1.0;
        const double to = 1.0 - from;
        const double lift_ratio = from + (to - from) * smoothstep(local);
        target = blendPose(raised_pose_, calf_wave_pose_, lift_ratio);
      }
      mode = MODE_CROUCH;
      max_delta = wave_max_delta_rad_;
      return;
    }

    if (phase_ == Phase::HoldRaised) {
      target = raised_pose_;
      mode = MODE_CROUCH;
      max_delta = sit_max_delta_rad_;
      return;
    }

    target = sit_pose_;
    mode = MODE_CROUCH;
    max_delta = sit_max_delta_rad_;
  }

  void startSequence()
  {
    active_ = true;

    const auto feedback = latestFeedback();
    if (feedback.has_value()) {
      transition_from_pose_ = feedback.value();
    } else {
      transition_from_pose_ = last_target_pose_;
      RCLCPP_WARN(
        this->get_logger(),
        "fresh joint feedback unavailable; starting DETECT sit transition from last target");
    }

    if (maxPoseError(transition_from_pose_, sit_pose_) <= sit_skip_tolerance_rad_) {
      RCLCPP_INFO(
        this->get_logger(),
        "current pose is already near sit pose; skipping SitDown phase");
      setPhase(Phase::RaiseFront);
      return;
    }

    setPhase(Phase::SitDown);
  }

  static double maxPoseError(
    const std::array<float, NUM_JOINTS> & lhs,
    const std::array<float, NUM_JOINTS> & rhs)
  {
    double max_error = 0.0;
    for (std::size_t i = 0; i < NUM_JOINTS; ++i) {
      max_error = std::max(
        max_error,
        std::fabs(static_cast<double>(lhs[i] - rhs[i])));
    }
    return max_error;
  }

  std::optional<std::array<float, NUM_JOINTS>> latestFeedback() const
  {
    std::lock_guard<std::mutex> lock(feedback_mutex_);
    if (!latest_feedback_.has_value()) {
      return std::nullopt;
    }

    const double age_sec = (this->now() - latest_feedback_time_).seconds();
    if (age_sec > 0.5) {
      return std::nullopt;
    }
    return latest_feedback_;
  }

  void setPhase(Phase phase)
  {
    phase_ = phase;
    phase_start_time_ = this->now();
    RCLCPP_INFO(this->get_logger(), "DETECT phase -> %s", phaseName(phase_));
  }

  double phaseElapsedSec() const
  {
    return (this->now() - phase_start_time_).seconds();
  }

  const char * phaseName(Phase phase) const
  {
    switch (phase) {
      case Phase::Idle:
        return "Idle";
      case Phase::SitDown:
        return "SitDown";
      case Phase::RaiseFront:
        return "RaiseFront";
      case Phase::Wave:
        return "Wave";
      case Phase::HoldRaised:
        return "HoldRaised";
      default:
        return "Unknown";
    }
  }

private:
  double rate_hz_{50.0};
  double sit_transition_sec_{3.0};
  double sit_skip_tolerance_rad_{0.08};
  double raise_transition_sec_{1.2};
  double wave_segment_sec_{0.75};
  int wave_cycles_{3};
  double sit_max_delta_rad_{0.015};
  double raise_max_delta_rad_{0.02};
  double wave_max_delta_rad_{0.02};

  bool active_{false};
  Phase phase_{Phase::Idle};
  rclcpp::Time phase_start_time_{0, 0, RCL_ROS_TIME};
  uint32_t seq_{0};

  std::array<float, NUM_JOINTS> sit_pose_{};
  std::array<float, NUM_JOINTS> raised_pose_{};
  std::array<float, NUM_JOINTS> calf_wave_pose_{};
  std::array<float, NUM_JOINTS> transition_from_pose_{};
  std::array<float, NUM_JOINTS> last_target_pose_{};

  mutable std::mutex feedback_mutex_;
  std::optional<std::array<float, NUM_JOINTS>> latest_feedback_;
  rclcpp::Time latest_feedback_time_{0, 0, RCL_ROS_TIME};

  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr mode_sub_;
  rclcpp::Subscription<JointFeedback>::SharedPtr feedback_sub_;
  rclcpp::Publisher<JointTarget>::SharedPtr target_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<DetectMotionNode>());
  rclcpp::shutdown();
  return 0;
}
