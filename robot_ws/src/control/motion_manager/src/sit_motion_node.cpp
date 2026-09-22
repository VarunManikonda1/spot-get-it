#include <algorithm>
#include <array>
#include <cstdint>
#include <cctype>
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

using JointTarget = robot_interfaces::msg::JointTarget;
using JointFeedback = robot_interfaces::msg::JointFeedback;

constexpr std::size_t NUM_JOINTS = 12;

// robot_interfaces/JointTarget.msg의 mode 값과 맞춰야 한다.
constexpr uint8_t MODE_SIT = 6;

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

double smoothstep(double ratio)
{
  ratio = std::clamp(ratio, 0.0, 1.0);
  return ratio * ratio * (3.0 - 2.0 * ratio);
}

}  // namespace

class SitMotionNode : public rclcpp::Node
{
public:
  SitMotionNode()
  : Node("sit_motion_node")
  {
    rate_hz_ = this->declare_parameter<double>("rate_hz", 50.0);
    max_delta_rad_ = this->declare_parameter<double>("max_delta_rad", 0.02);
    transition_sec_ = this->declare_parameter<double>("transition_sec", 2.0);
    feedback_timeout_sec_ = this->declare_parameter<double>("feedback_timeout_sec", 0.5);

    const std::vector<double> stand_pose_param =
      this->declare_parameter<std::vector<double>>(
      "stand_pose",
    {
      0.0, -0.9921237899157832, 1.4907337340120823,
      0.0, -0.9921237899157832, 1.4907337340120823,
      0.0, -0.9921237899157832, 1.4907337340120823,
      0.0, -0.9921237899157832, 1.4907337340120823
    });

    const std::vector<double> sit_pose_param =
      this->declare_parameter<std::vector<double>>(
      "sit_pose",
    {
      0.0, -0.720000, 1.050000,
      0.0, -0.720000, 1.050000,
      0.0, -1.500000, 2.250000,
      0.0, -1.500000, 2.250000
    });

    stand_pose_ = toPoseArray(stand_pose_param, "stand_pose");
    sit_pose_ = toPoseArray(sit_pose_param, "sit_pose");
    transition_from_ = sit_pose_;
    last_published_pose_ = sit_pose_;

    target_pub_ = this->create_publisher<JointTarget>(
      "/control/sit/joint_target",
      rclcpp::QoS(10));

    active_mode_sub_ = this->create_subscription<std_msgs::msg::String>(
      "/control/behavior/active_mode",
      rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local(),
      std::bind(&SitMotionNode::activeModeCallback, this, std::placeholders::_1));

    feedback_sub_ = this->create_subscription<JointFeedback>(
      "/control/actuator/joint_feedback",
      rclcpp::QoS(rclcpp::KeepLast(1)).best_effort(),
      std::bind(&SitMotionNode::feedbackCallback, this, std::placeholders::_1));

    const auto period = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<double>(1.0 / rate_hz_));

    timer_ = this->create_wall_timer(
      period,
      std::bind(&SitMotionNode::timerCallback, this));

    RCLCPP_INFO(
      this->get_logger(),
      "sit_motion_node started. rate=%.1f Hz, transition=%.2fs, max_delta=%.4f rad/step",
      rate_hz_,
      transition_sec_,
      max_delta_rad_);
  }

private:
  void activeModeCallback(const std_msgs::msg::String::SharedPtr msg)
  {
    const bool new_active = toUpper(msg->data) == "SIT";

    if (new_active && !sit_active_) {
      sit_active_ = true;
      startSitTransition();
      return;
    }

    if (!new_active && sit_active_) {
      sit_active_ = false;
      transitioning_ = false;
    }
  }

  void feedbackCallback(const JointFeedback::SharedPtr msg)
  {
    latest_feedback_.emplace();
    for (std::size_t i = 0; i < NUM_JOINTS; ++i) {
      latest_feedback_.value()[i] = msg->position_rad[i];
    }
    latest_feedback_time_ = this->now();
  }

  void timerCallback()
  {
    const std::array<float, NUM_JOINTS> target = activeTarget();

    JointTarget msg;
    msg.seq = static_cast<uint16_t>(seq_ & 0xFFFFU);
    msg.mode = MODE_SIT;
    msg.flags = 0;

    for (std::size_t i = 0; i < NUM_JOINTS; ++i) {
      msg.target_rad[i] = target[i];
      msg.max_delta_rad[i] = static_cast<float>(max_delta_rad_);
    }

    target_pub_->publish(msg);
    last_published_pose_ = target;
    seq_++;
  }

  std::array<float, NUM_JOINTS> activeTarget()
  {
    if (!sit_active_) {
      return transitionSource();
    }

    if (!transitioning_) {
      return sit_pose_;
    }

    const double elapsed = (this->now() - transition_start_time_).seconds();
    const double ratio = transition_sec_ > 0.0 ? elapsed / transition_sec_ : 1.0;
    const double s = smoothstep(ratio);

    std::array<float, NUM_JOINTS> target{};
    for (std::size_t i = 0; i < NUM_JOINTS; ++i) {
      target[i] = static_cast<float>(
        static_cast<double>(transition_from_[i]) +
        (static_cast<double>(sit_pose_[i]) - static_cast<double>(transition_from_[i])) * s);
    }

    if (ratio >= 1.0) {
      transitioning_ = false;
      return sit_pose_;
    }

    return target;
  }

  void startSitTransition()
  {
    transition_from_ = transitionSource();
    transition_start_time_ = this->now();
    transitioning_ = true;

    RCLCPP_INFO(
      this->get_logger(),
      "SIT transition started: %.2fs",
      transition_sec_);
  }

  std::array<float, NUM_JOINTS> transitionSource() const
  {
    if (isFeedbackFresh()) {
      return latest_feedback_.value();
    }
    return last_published_pose_;
  }

  bool isFeedbackFresh() const
  {
    if (!latest_feedback_.has_value()) {
      return false;
    }

    return (this->now() - latest_feedback_time_).seconds() <= feedback_timeout_sec_;
  }

private:
  double rate_hz_{50.0};
  double max_delta_rad_{0.02};
  double transition_sec_{2.0};
  double feedback_timeout_sec_{0.5};

  std::array<float, NUM_JOINTS> stand_pose_{};
  std::array<float, NUM_JOINTS> sit_pose_{};
  std::array<float, NUM_JOINTS> transition_from_{};
  std::array<float, NUM_JOINTS> last_published_pose_{};
  std::optional<std::array<float, NUM_JOINTS>> latest_feedback_;
  rclcpp::Time latest_feedback_time_{0, 0, RCL_ROS_TIME};
  rclcpp::Time transition_start_time_{0, 0, RCL_ROS_TIME};

  bool sit_active_{false};
  bool transitioning_{false};
  uint32_t seq_{0};

  rclcpp::Publisher<JointTarget>::SharedPtr target_pub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr active_mode_sub_;
  rclcpp::Subscription<JointFeedback>::SharedPtr feedback_sub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<SitMotionNode>());
  rclcpp::shutdown();
  return 0;
}
