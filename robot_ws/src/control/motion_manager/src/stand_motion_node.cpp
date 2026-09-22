#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "robot_interfaces/msg/joint_target.hpp"

namespace
{

using JointTarget = robot_interfaces::msg::JointTarget;

constexpr std::size_t NUM_JOINTS = 12;

// robot_interfaces/JointTarget.msg의 mode 값과 맞춰야 한다.
// 현재 사용 기준:
// 0: DISABLE
// 1: STAND
// 2: RL
// 5: CLASSIC
constexpr uint8_t MODE_STAND = 1;

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

}  // namespace

class StandMotionNode : public rclcpp::Node
{
public:
  StandMotionNode()
  : Node("stand_motion_node")
  {
    rate_hz_ = this->declare_parameter<double>("rate_hz", 50.0);
    max_delta_rad_ = this->declare_parameter<double>("max_delta_rad", 0.03);

    const std::vector<double> stand_pose_param =
      this->declare_parameter<std::vector<double>>(
      "stand_pose",
    {
      0.0, -0.9921237899157832, 1.4907337340120823,
      0.0, -0.9921237899157832, 1.4907337340120823,
      0.0, -0.9921237899157832, 1.4907337340120823,
      0.0, -0.9921237899157832, 1.4907337340120823
    });

    stand_pose_ = toPoseArray(stand_pose_param, "stand_pose");

    target_pub_ = this->create_publisher<JointTarget>(
      "/control/stand/joint_target",
      rclcpp::QoS(10));

    const auto period = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<double>(1.0 / rate_hz_));

    timer_ = this->create_wall_timer(
      period,
      std::bind(&StandMotionNode::timerCallback, this));

    RCLCPP_INFO(
      this->get_logger(),
      "stand_motion_node started. rate=%.1f Hz, max_delta=%.4f rad/step",
      rate_hz_,
      max_delta_rad_);
  }

private:
  void timerCallback()
  {
    JointTarget msg;
    msg.seq = static_cast<uint16_t>(seq_ & 0xFFFFU);
    msg.mode = MODE_STAND;
    msg.flags = 0;

    for (std::size_t i = 0; i < NUM_JOINTS; ++i) {
      msg.target_rad[i] = stand_pose_[i];
      msg.max_delta_rad[i] = static_cast<float>(max_delta_rad_);
    }

    target_pub_->publish(msg);
    seq_++;
  }

private:
  double rate_hz_{50.0};
  double max_delta_rad_{0.03};

  std::array<float, NUM_JOINTS> stand_pose_{};
  uint32_t seq_{0};

  rclcpp::Publisher<JointTarget>::SharedPtr target_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<StandMotionNode>());
  rclcpp::shutdown();
  return 0;
}
