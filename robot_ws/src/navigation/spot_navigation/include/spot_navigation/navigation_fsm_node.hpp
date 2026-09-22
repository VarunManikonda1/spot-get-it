#ifndef SPOT_NAVIGATION__NAVIGATION_FSM_NODE_HPP_
#define SPOT_NAVIGATION__NAVIGATION_FSM_NODE_HPP_

#include <rclcpp/rclcpp.hpp>
#include <nav_msgs/msg/path.hpp>
#include <std_msgs/msg/string.hpp>

#include <robot_interfaces/msg/path_progress.hpp>
#include <robot_interfaces/msg/localized_robot_pose.hpp>
#include <robot_interfaces/msg/obstacle_model.hpp>
#include <robot_interfaces/msg/free_space_model.hpp>
#include <robot_interfaces/msg/navigation_state.hpp>

namespace spot_navigation
{

class NavigationFsmNode : public rclcpp::Node
{
public:
  explicit NavigationFsmNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());

private:
  // ============================================================
  // Subscribers
  // ============================================================
  rclcpp::Subscription<robot_interfaces::msg::PathProgress>::SharedPtr path_progress_sub_;
  rclcpp::Subscription<robot_interfaces::msg::LocalizedRobotPose>::SharedPtr pose_sub_;
  rclcpp::Subscription<robot_interfaces::msg::ObstacleModel>::SharedPtr obstacle_sub_;
  rclcpp::Subscription<robot_interfaces::msg::FreeSpaceModel>::SharedPtr free_space_sub_;
  rclcpp::Subscription<nav_msgs::msg::Path>::SharedPtr local_path_sub_;

  // ============================================================
  // Publisher
  // ============================================================
  rclcpp::Publisher<robot_interfaces::msg::NavigationState>::SharedPtr nav_state_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr drive_mode_pub_;

  // ============================================================
  // Timer
  // ============================================================
  rclcpp::TimerBase::SharedPtr timer_;

  // ============================================================
  // Latest received data
  // ============================================================
  robot_interfaces::msg::PathProgress::SharedPtr latest_path_progress_;
  robot_interfaces::msg::LocalizedRobotPose::SharedPtr latest_pose_;
  robot_interfaces::msg::ObstacleModel::SharedPtr latest_obstacle_;
  robot_interfaces::msg::FreeSpaceModel::SharedPtr latest_free_space_;
  nav_msgs::msg::Path::SharedPtr latest_local_path_;

  // ============================================================
  // Timestamps for stale check
  // ============================================================
  rclcpp::Time last_pose_time_;
  rclcpp::Time last_perception_time_;

  // ============================================================
  // Internal FSM state
  // ============================================================
  uint8_t current_nav_state_;

  bool front_blocked_;
  rclcpp::Time front_clear_start_time_;
  bool front_clear_timer_active_;

  // ============================================================
  // Parameters
  // ============================================================
  std::string robot_id_;
  std::string topic_pose_;
  std::string topic_obstacle_;
  std::string topic_free_space_;

  double front_block_distance_m_;
  double side_block_distance_m_;
  double front_clear_distance_m_;
  double clear_hold_time_sec_;
  double rejoin_tolerance_m_;
  double pose_timeout_sec_;
  double perception_timeout_sec_;
  double emergency_stop_distance_m_;
  double front_azimuth_limit_rad_;
  double timer_period_sec_;

  // ============================================================
  // Callbacks
  // ============================================================
  void on_path_progress(robot_interfaces::msg::PathProgress::SharedPtr msg);
  void on_pose(robot_interfaces::msg::LocalizedRobotPose::SharedPtr msg);
  void on_obstacle(robot_interfaces::msg::ObstacleModel::SharedPtr msg);
  void on_free_space(robot_interfaces::msg::FreeSpaceModel::SharedPtr msg);
  void on_local_path(nav_msgs::msg::Path::SharedPtr msg);
  void on_timer();

  // ============================================================
  // FSM logic
  // ============================================================
  uint8_t determine_nav_state();
  void update_front_blocked(float front_clearance);
  robot_interfaces::msg::NavigationState build_nav_state_msg(uint8_t nav_state);
};

}  // namespace spot_navigation

#endif  // SPOT_NAVIGATION__NAVIGATION_FSM_NODE_HPP_
