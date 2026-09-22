#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "robot_interfaces/msg/joint_target.hpp"

namespace
{

using JointTarget = robot_interfaces::msg::JointTarget;

constexpr uint8_t MODE_STAND = 1;
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

std::string normalizeMode(std::string value)
{
  value = toUpper(value);
  if (value == "CLASSIC_CONTROL") {
    return "CLASSIC";
  }
  return value;
}

}  // namespace

class JointTargetMuxNode : public rclcpp::Node
{
public:
  JointTargetMuxNode()
  : Node("joint_target_mux_node")
  {
    rate_hz_ = this->declare_parameter<double>("rate_hz", 50.0);
    rl_timeout_ms_ = this->declare_parameter<double>("rl_timeout_ms", 100.0);
    classic_timeout_ms_ = this->declare_parameter<double>("classic_timeout_ms", 100.0);
    stand_timeout_ms_ = this->declare_parameter<double>("stand_timeout_ms", 100.0);
    sit_timeout_ms_ = this->declare_parameter<double>("sit_timeout_ms", 100.0);
    detect_timeout_ms_ = this->declare_parameter<double>("detect_timeout_ms", 100.0);
    behavior_mode_ = normalizeMode(this->declare_parameter<std::string>("default_mode", "SIT"));

    mode_sub_ = this->create_subscription<std_msgs::msg::String>(
      "/control/behavior/mode",
      rclcpp::QoS(10),
      std::bind(&JointTargetMuxNode::modeCallback, this, std::placeholders::_1));

    active_mode_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/control/behavior/active_mode",
      rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local());

    rl_sub_ = this->create_subscription<JointTarget>(
      "/control/rl/joint_target",
      rclcpp::QoS(rclcpp::KeepLast(1)).best_effort(),
      std::bind(&JointTargetMuxNode::rlCallback, this, std::placeholders::_1));

    classic_sub_ = this->create_subscription<JointTarget>(
      "/control/classic_control/joint_target",
      rclcpp::QoS(rclcpp::KeepLast(1)).best_effort(),
      std::bind(&JointTargetMuxNode::classicCallback, this, std::placeholders::_1));

    stand_sub_ = this->create_subscription<JointTarget>(
      "/control/stand/joint_target",
      rclcpp::QoS(rclcpp::KeepLast(1)).best_effort(),
      std::bind(&JointTargetMuxNode::standCallback, this, std::placeholders::_1));

    sit_sub_ = this->create_subscription<JointTarget>(
      "/control/sit/joint_target",
      rclcpp::QoS(rclcpp::KeepLast(1)).best_effort(),
      std::bind(&JointTargetMuxNode::sitCallback, this, std::placeholders::_1));

    detect_sub_ = this->create_subscription<JointTarget>(
      "/control/detect/joint_target",
      rclcpp::QoS(rclcpp::KeepLast(1)).best_effort(),
      std::bind(&JointTargetMuxNode::detectCallback, this, std::placeholders::_1));

    selected_pub_ = this->create_publisher<JointTarget>(
      "/control/selected/joint_target",
      rclcpp::QoS(rclcpp::KeepLast(1)).best_effort());

    publishActiveMode();

    const auto period = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<double>(1.0 / rate_hz_));

    timer_ = this->create_wall_timer(
      period,
      std::bind(&JointTargetMuxNode::timerCallback, this));

    RCLCPP_INFO(
      this->get_logger(),
      "joint_target_mux_node started. rate=%.1f Hz, default_mode=%s",
      rate_hz_,
      behavior_mode_.c_str());
  }

private:
  void modeCallback(const std_msgs::msg::String::SharedPtr msg)
  {
    const std::string new_mode = normalizeMode(msg->data);

    if (new_mode.empty()) {
      return;
    }

    if (
      new_mode != "RL" && new_mode != "CLASSIC" && new_mode != "STAND" &&
      new_mode != "SIT" && new_mode != "DETECT")
    {
      RCLCPP_WARN(
        this->get_logger(),
        "unsupported mode '%s'. Allowed modes: RL, CLASSIC, STAND, SIT, DETECT. Keeping current mode=%s",
        msg->data.c_str(),
        behavior_mode_.c_str());
      return;
    }

    if (new_mode != behavior_mode_) {
      RCLCPP_INFO(
        this->get_logger(),
        "behavior mode changed: %s -> %s",
        behavior_mode_.c_str(),
        new_mode.c_str());
      behavior_mode_ = new_mode;
    }

    publishActiveMode();
  }

  void publishActiveMode()
  {
    std_msgs::msg::String msg;
    msg.data = behavior_mode_;
    active_mode_pub_->publish(msg);
  }

  void rlCallback(const JointTarget::SharedPtr msg)
  {
    latest_rl_ = *msg;
    latest_rl_time_ = this->now();
  }

  void classicCallback(const JointTarget::SharedPtr msg)
  {
    latest_classic_ = *msg;
    latest_classic_time_ = this->now();
  }

  void standCallback(const JointTarget::SharedPtr msg)
  {
    latest_stand_ = *msg;
    latest_stand_time_ = this->now();
  }

  void sitCallback(const JointTarget::SharedPtr msg)
  {
    latest_sit_ = *msg;
    latest_sit_time_ = this->now();
  }

  void detectCallback(const JointTarget::SharedPtr msg)
  {
    latest_detect_ = *msg;
    latest_detect_time_ = this->now();
  }

  void timerCallback()
  {
    JointTarget selected;

    if (!selectTarget(selected)) {
      return;
    }

    // 최종 seq는 mux가 다시 매긴다.
    // actuator_bridge와 STM32는 selected target 기준 seq만 보면 된다.
    selected.seq = static_cast<uint16_t>(seq_ & 0xFFFFU);

    selected_pub_->publish(selected);
    seq_++;
  }

  bool selectTarget(JointTarget & selected)
  {
    if (behavior_mode_ == "RL") {
      if (isRlFresh()) {
        selected = latest_rl_.value();
        // mode 는 RL 이 명시한 값 그대로 사용 (덮어쓰지 않음).
        // RL 이 정상 inference 시 MODE_RL, safe_target 시 MODE_DISABLE/E_STOP
        // 등을 보내는데, 이전엔 mux 가 강제로 MODE_RL 로 덮어써서 RL 의
        // 안전 의도가 STM 에 전달되지 못했음 (RL disabled 인데도 STM 이
        // RL 모드로 처리 → 속도 명령 0 에도 robot 안 멈춤).
        return true;
      }

      RCLCPP_WARN_THROTTLE(
        this->get_logger(),
        *this->get_clock(),
        1000,
        "RL target stale. Falling back to SIT.");

      if (isSitFresh()) {
        selected = latest_sit_.value();
        selected.mode = MODE_SIT;
        return true;
      }

      if (isStandFresh()) {
        selected = latest_stand_.value();
        selected.mode = MODE_STAND;
        return true;
      }

      return false;
    }

    if (behavior_mode_ == "CLASSIC") {
      if (isClassicFresh()) {
        selected = latest_classic_.value();
        return true;
      }

      RCLCPP_WARN_THROTTLE(
        this->get_logger(),
        *this->get_clock(),
        1000,
        "CLASSIC target stale. Falling back to SIT.");

      if (isSitFresh()) {
        selected = latest_sit_.value();
        selected.mode = MODE_SIT;
        return true;
      }

      if (isStandFresh()) {
        selected = latest_stand_.value();
        selected.mode = MODE_STAND;
        return true;
      }

      return false;
    }

    if (behavior_mode_ == "DETECT") {
      if (isDetectFresh()) {
        selected = latest_detect_.value();
        return true;
      }

      RCLCPP_WARN_THROTTLE(
        this->get_logger(),
        *this->get_clock(),
        1000,
        "DETECT target stale. Falling back to SIT.");

      if (isSitFresh()) {
        selected = latest_sit_.value();
        selected.mode = MODE_SIT;
        return true;
      }

      if (isStandFresh()) {
        selected = latest_stand_.value();
        selected.mode = MODE_STAND;
        return true;
      }

      return false;
    }

    if (behavior_mode_ == "STAND") {
      if (isStandFresh()) {
        selected = latest_stand_.value();
        selected.mode = MODE_STAND;
        return true;
      }

      RCLCPP_WARN_THROTTLE(
        this->get_logger(),
        *this->get_clock(),
        1000,
        "STAND target stale. No selected target published.");

      return false;
    }

    // default: SIT
    if (isSitFresh()) {
      selected = latest_sit_.value();
      selected.mode = MODE_SIT;
      return true;
    }

    RCLCPP_WARN_THROTTLE(
      this->get_logger(),
      *this->get_clock(),
      1000,
      "SIT target stale. Falling back to STAND.");

    if (isStandFresh()) {
      selected = latest_stand_.value();
      selected.mode = MODE_STAND;
      return true;
    }

    RCLCPP_WARN_THROTTLE(
      this->get_logger(),
      *this->get_clock(),
      1000,
      "SIT and STAND targets stale. No selected target published.");

    return false;
  }

  bool isRlFresh() const
  {
    if (!latest_rl_.has_value()) {
      return false;
    }

    const double age_ms = (this->now() - latest_rl_time_).seconds() * 1000.0;
    return age_ms <= rl_timeout_ms_;
  }

  bool isClassicFresh() const
  {
    if (!latest_classic_.has_value()) {
      return false;
    }

    const double age_ms = (this->now() - latest_classic_time_).seconds() * 1000.0;
    return age_ms <= classic_timeout_ms_;
  }

  bool isStandFresh() const
  {
    if (!latest_stand_.has_value()) {
      return false;
    }

    const double age_ms = (this->now() - latest_stand_time_).seconds() * 1000.0;
    return age_ms <= stand_timeout_ms_;
  }

  bool isSitFresh() const
  {
    if (!latest_sit_.has_value()) {
      return false;
    }

    const double age_ms = (this->now() - latest_sit_time_).seconds() * 1000.0;
    return age_ms <= sit_timeout_ms_;
  }

  bool isDetectFresh() const
  {
    if (!latest_detect_.has_value()) {
      return false;
    }

    const double age_ms = (this->now() - latest_detect_time_).seconds() * 1000.0;
    return age_ms <= detect_timeout_ms_;
  }

private:
  double rate_hz_{50.0};
  double rl_timeout_ms_{100.0};
  double classic_timeout_ms_{100.0};
  double stand_timeout_ms_{100.0};
  double sit_timeout_ms_{100.0};
  double detect_timeout_ms_{100.0};

  std::string behavior_mode_{"SIT"};

  std::optional<JointTarget> latest_rl_;
  std::optional<JointTarget> latest_classic_;
  std::optional<JointTarget> latest_stand_;
  std::optional<JointTarget> latest_sit_;
  std::optional<JointTarget> latest_detect_;

  rclcpp::Time latest_rl_time_{0, 0, RCL_ROS_TIME};
  rclcpp::Time latest_classic_time_{0, 0, RCL_ROS_TIME};
  rclcpp::Time latest_stand_time_{0, 0, RCL_ROS_TIME};
  rclcpp::Time latest_sit_time_{0, 0, RCL_ROS_TIME};
  rclcpp::Time latest_detect_time_{0, 0, RCL_ROS_TIME};

  uint32_t seq_{0};

  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr mode_sub_;
  rclcpp::Subscription<JointTarget>::SharedPtr rl_sub_;
  rclcpp::Subscription<JointTarget>::SharedPtr classic_sub_;
  rclcpp::Subscription<JointTarget>::SharedPtr stand_sub_;
  rclcpp::Subscription<JointTarget>::SharedPtr sit_sub_;
  rclcpp::Subscription<JointTarget>::SharedPtr detect_sub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr active_mode_pub_;
  rclcpp::Publisher<JointTarget>::SharedPtr selected_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<JointTargetMuxNode>());
  rclcpp::shutdown();
  return 0;
}
