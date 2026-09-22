#ifndef SPOT_NAVIGATION__PATH_FOLLOWER_NODE_HPP_
#define SPOT_NAVIGATION__PATH_FOLLOWER_NODE_HPP_

#include <rclcpp/rclcpp.hpp>
#include <nav_msgs/msg/path.hpp>
#include <geometry_msgs/msg/twist_stamped.hpp>

#include <robot_interfaces/msg/localized_robot_pose.hpp>
#include <robot_interfaces/msg/navigation_state.hpp>

namespace spot_navigation
{

class PathFollowerNode : public rclcpp::Node
{
public:
  explicit PathFollowerNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());

private:
  // ============================================================
  // Subscribers
  // ============================================================
  rclcpp::Subscription<nav_msgs::msg::Path>::SharedPtr local_path_sub_;
  rclcpp::Subscription<robot_interfaces::msg::LocalizedRobotPose>::SharedPtr pose_sub_;
  rclcpp::Subscription<robot_interfaces::msg::NavigationState>::SharedPtr nav_state_sub_;

  // ============================================================
  // Publisher
  // ============================================================
  rclcpp::Publisher<geometry_msgs::msg::TwistStamped>::SharedPtr cmd_vel_raw_pub_;

  // ============================================================
  // Timer
  // ============================================================
  rclcpp::TimerBase::SharedPtr timer_;

  // ============================================================
  // Latest received data
  // ============================================================
  nav_msgs::msg::Path::SharedPtr latest_local_path_;
  robot_interfaces::msg::LocalizedRobotPose::SharedPtr latest_pose_;
  robot_interfaces::msg::NavigationState::SharedPtr latest_nav_state_;

  // ============================================================
  // Internal state
  // ============================================================
  size_t current_waypoint_index_;   // 현재 추종 중인 waypoint index
  double prev_heading_error_;       // 이전 tick heading_error (rate limiter용)

  // ============================================================
  // Parameters
  // ============================================================
  std::string robot_id_;

  // Topic names (파라미터로 관리)
  std::string topic_pose_;

  double max_linear_x_mps_;
  double min_linear_x_mps_;
  double max_angular_z_radps_;
  double k_yaw_;
  double heading_tolerance_rad_;
  double turn_in_place_threshold_rad_;
  double slow_down_angle_rad_;
  double waypoint_reach_tolerance_m_;
  double heading_error_rate_limit_rad_;  // heading_error 틱당 최대 변화량 (보행 노이즈 차단)
  double timer_period_sec_;

  // ============================================================
  // Callbacks
  // ============================================================
  void on_local_path(nav_msgs::msg::Path::SharedPtr msg);
  void on_pose(robot_interfaces::msg::LocalizedRobotPose::SharedPtr msg);
  void on_nav_state(robot_interfaces::msg::NavigationState::SharedPtr msg);
  void on_timer();

  // ============================================================
  // Control logic
  // ============================================================

  // 현재 위치와 waypoint 간 거리 계산
  double distance_to_waypoint(size_t index);

  // heading error 계산 (-π ~ π wrap)
  double compute_heading_error(double target_x, double target_y);

  // heading error 기반 v 계산
  double compute_linear_velocity(double heading_error);

  // heading error 기반 w 계산
  double compute_angular_velocity(double heading_error);
};

}  // namespace spot_navigation

#endif  // SPOT_NAVIGATION__PATH_FOLLOWER_NODE_HPP_
