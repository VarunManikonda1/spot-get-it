#ifndef SPOT_NAVIGATION__SAFETY_SUPERVISOR_NODE_HPP_
#define SPOT_NAVIGATION__SAFETY_SUPERVISOR_NODE_HPP_

#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <geometry_msgs/msg/twist_stamped.hpp>
#include <std_msgs/msg/string.hpp>

#include <robot_interfaces/msg/navigation_state.hpp>
#include <robot_interfaces/msg/obstacle_model.hpp>
#include <robot_interfaces/msg/free_space_model.hpp>
#include <robot_interfaces/msg/localized_robot_pose.hpp>
#include <robot_interfaces/msg/safety_status.hpp>

namespace spot_navigation
{

class SafetySupervisorNode : public rclcpp::Node
{
public:
  explicit SafetySupervisorNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());

private:
  // ============================================================
  // Subscribers
  // ============================================================
  rclcpp::Subscription<geometry_msgs::msg::TwistStamped>::SharedPtr cmd_vel_raw_sub_;
  rclcpp::Subscription<robot_interfaces::msg::NavigationState>::SharedPtr nav_state_sub_;
  rclcpp::Subscription<robot_interfaces::msg::ObstacleModel>::SharedPtr obstacle_sub_;
  rclcpp::Subscription<robot_interfaces::msg::FreeSpaceModel>::SharedPtr free_space_sub_;
  rclcpp::Subscription<robot_interfaces::msg::LocalizedRobotPose>::SharedPtr pose_sub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr drive_mode_sub_;

  // ============================================================
  // Publishers
  // ============================================================
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_pub_;
  rclcpp::Publisher<robot_interfaces::msg::SafetyStatus>::SharedPtr safety_status_pub_;

  // ============================================================
  // Timer
  // ============================================================
  rclcpp::TimerBase::SharedPtr timer_;

  // ============================================================
  // Latest received data
  // ============================================================
  geometry_msgs::msg::TwistStamped::SharedPtr latest_cmd_vel_raw_;
  robot_interfaces::msg::NavigationState::SharedPtr latest_nav_state_;
  robot_interfaces::msg::ObstacleModel::SharedPtr latest_obstacle_;
  robot_interfaces::msg::FreeSpaceModel::SharedPtr latest_free_space_;
  robot_interfaces::msg::LocalizedRobotPose::SharedPtr latest_pose_;

  // ============================================================
  // Timestamps for stale check
  // ============================================================
  rclcpp::Time last_cmd_time_;
  rclcpp::Time last_pose_time_;
  rclcpp::Time last_perception_time_;

  // ============================================================
  // Internal state
  // ============================================================
  double prev_v_;   // 이전 tick 선속도 (가감속 기준)
  double prev_w_;   // 이전 tick 각속도 (가감속 기준)
  std::string drive_mode_;  // 현재 드라이브 모드 ("auto" / "manual")

  // ============================================================
  // Parameters
  // ============================================================
  std::string robot_id_;

  // Topic names (파라미터로 관리)
  std::string topic_pose_;
  std::string topic_obstacle_;
  std::string topic_free_space_;
  std::string topic_drive_mode_;

  double soft_stop_distance_m_;
  double emergency_stop_distance_m_;
  double max_safe_linear_x_mps_;
  double max_safe_angular_z_radps_;
  double max_linear_accel_mps2_;
  double max_linear_decel_mps2_;
  double max_angular_accel_radps2_;
  double cmd_timeout_sec_;
  double pose_timeout_sec_;
  double perception_timeout_sec_;
  double front_azimuth_limit_rad_;  // 전방 장애물 유효 방위각 한계 (rad)
  double timer_period_sec_;

  // ============================================================
  // Callbacks
  // ============================================================
  void on_cmd_vel_raw(geometry_msgs::msg::TwistStamped::SharedPtr msg);
  void on_nav_state(robot_interfaces::msg::NavigationState::SharedPtr msg);
  void on_obstacle(robot_interfaces::msg::ObstacleModel::SharedPtr msg);
  void on_free_space(robot_interfaces::msg::FreeSpaceModel::SharedPtr msg);
  void on_pose(robot_interfaces::msg::LocalizedRobotPose::SharedPtr msg);
  void on_drive_mode(std_msgs::msg::String::SharedPtr msg);
  void on_timer();

  // ============================================================
  // Safety logic
  // ============================================================

  // 가감속 제한 적용
  double apply_accel_limit(double target, double prev, double accel, double decel, double dt);

  // SafetyStatus 메시지 빌드
  robot_interfaces::msg::SafetyStatus build_safety_status(
    double input_v, double input_w,
    double output_v, double output_w,
    bool emergency_stop, bool soft_stop,
    bool cmd_stale, bool pose_stale, bool perception_stale,
    bool goal_reached, bool obstacle_too_close,
    float front_clearance);
};

}  // namespace spot_navigation

#endif  // SPOT_NAVIGATION__SAFETY_SUPERVISOR_NODE_HPP_
