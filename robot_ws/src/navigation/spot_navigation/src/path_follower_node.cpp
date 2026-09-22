#include "spot_navigation/path_follower_node.hpp"

#include <cmath>
#include <limits>

namespace spot_navigation
{

PathFollowerNode::PathFollowerNode(const rclcpp::NodeOptions & options)
: Node("path_follower_node", options),
  current_waypoint_index_(0),
  prev_heading_error_(0.0)
{
  // ============================================================
  // Parameters
  // ============================================================
  robot_id_                    = declare_parameter<std::string>("robot_id", "spot_01");
  topic_pose_                  = declare_parameter<std::string>("topic_pose", "/localization/pose");
  max_linear_x_mps_            = declare_parameter<double>("max_linear_x_mps", 0.10);
  min_linear_x_mps_            = declare_parameter<double>("min_linear_x_mps", 0.05);
  max_angular_z_radps_         = declare_parameter<double>("max_angular_z_radps", 0.40);
  k_yaw_                       = declare_parameter<double>("k_yaw", 1.0);
  heading_tolerance_rad_       = declare_parameter<double>("heading_tolerance_rad", 0.15);
  turn_in_place_threshold_rad_ = declare_parameter<double>("turn_in_place_threshold_rad", 0.7);
  slow_down_angle_rad_         = declare_parameter<double>("slow_down_angle_rad", 0.5);
  waypoint_reach_tolerance_m_  = declare_parameter<double>("waypoint_reach_tolerance_m", 0.2);
  heading_error_rate_limit_rad_= declare_parameter<double>("heading_error_rate_limit_rad", 0.04);
  timer_period_sec_            = declare_parameter<double>("timer_period_sec", 0.02);

  // ============================================================
  // Subscribers
  // ============================================================
  local_path_sub_ = create_subscription<nav_msgs::msg::Path>(
    "/navigation/local_path/" + robot_id_, 10,
    std::bind(&PathFollowerNode::on_local_path, this, std::placeholders::_1));

  pose_sub_ = create_subscription<robot_interfaces::msg::LocalizedRobotPose>(
    topic_pose_, 10,
    std::bind(&PathFollowerNode::on_pose, this, std::placeholders::_1));

  nav_state_sub_ = create_subscription<robot_interfaces::msg::NavigationState>(
    "/navigation/state/" + robot_id_, 10,
    std::bind(&PathFollowerNode::on_nav_state, this, std::placeholders::_1));

  // ============================================================
  // Publisher
  // ============================================================
  cmd_vel_raw_pub_ = create_publisher<geometry_msgs::msg::TwistStamped>(
    "/control/cmd_vel_raw/" + robot_id_, 10);

  // ============================================================
  // Timer
  // ============================================================
  timer_ = create_wall_timer(
    std::chrono::duration<double>(timer_period_sec_),
    std::bind(&PathFollowerNode::on_timer, this));

  RCLCPP_INFO(get_logger(), "path_follower_node 시작 [robot_id: %s]", robot_id_.c_str());
}

// ============================================================
// Callbacks
// ============================================================

void PathFollowerNode::on_local_path(nav_msgs::msg::Path::SharedPtr msg)
{
  latest_local_path_ = msg;
  RCLCPP_DEBUG(get_logger(), "local path 수신 : %zu waypoints", msg->poses.size());
}
void PathFollowerNode::on_pose(robot_interfaces::msg::LocalizedRobotPose::SharedPtr msg)
{
  latest_pose_ = msg;
}

void PathFollowerNode::on_nav_state(robot_interfaces::msg::NavigationState::SharedPtr msg)
{
  latest_nav_state_ = msg;
}

// ============================================================
// Timer callback
// ============================================================

void PathFollowerNode::on_timer()
{
  using NS = robot_interfaces::msg::NavigationState;

  geometry_msgs::msg::TwistStamped cmd;
  cmd.header.stamp    = now();
  cmd.header.frame_id = "base_link";

  // ----------------------------------------------------------
  // 데이터 유효성 검사
  // ----------------------------------------------------------
  if (!latest_nav_state_ || !latest_pose_ || !latest_local_path_) {
    cmd_vel_raw_pub_->publish(cmd);  // v=0, w=0
    return;
  }

  // ----------------------------------------------------------
  // FSM state 확인
  // ----------------------------------------------------------
  const uint8_t nav_state = latest_nav_state_->nav_state;
  if (nav_state != NS::NAV_TRACKING_GLOBAL_PATH &&
      nav_state != NS::NAV_FOLLOWING_LOCAL_PATH)
  {
    cmd_vel_raw_pub_->publish(cmd);  // v=0, w=0
    return;
  }

  // ----------------------------------------------------------
  // local path 유효성 검사
  // ----------------------------------------------------------
  if (latest_local_path_->poses.empty()) {
    cmd_vel_raw_pub_->publish(cmd);  // v=0, w=0
    return;
  }

  const size_t path_size = latest_local_path_->poses.size();

  // ----------------------------------------------------------
  // 현재 위치와 가장 가까운 waypoint 찾기 (nearest)
  // nearest + 1을 target으로 설정
  // ----------------------------------------------------------
  size_t nearest = 0;
  double min_dist = std::numeric_limits<double>::max();
  for (size_t i = 0; i < path_size; ++i) {
    const double dx = latest_local_path_->poses[i].pose.position.x - latest_pose_->x_m;
    const double dy = latest_local_path_->poses[i].pose.position.y - latest_pose_->y_m;
    const double d  = std::sqrt(dx * dx + dy * dy);
    if (d < min_dist) {
      min_dist = d;
      nearest  = i;
    }
  }

  // nearest가 마지막이면 마지막 waypoint가 target
  current_waypoint_index_ = (nearest + 1 < path_size) ? nearest + 1 : path_size - 1;

  // ----------------------------------------------------------
  // target waypoint 선택
  // ----------------------------------------------------------
  const auto & target_pose = latest_local_path_->poses[current_waypoint_index_];
  const double target_x = target_pose.pose.position.x;
  const double target_y = target_pose.pose.position.y;

  // ----------------------------------------------------------
  // heading error 계산
  // ----------------------------------------------------------
  const double raw_heading_error = compute_heading_error(target_x, target_y);

  // ----------------------------------------------------------
  // heading_error rate limiter : 보행 노이즈 차단
  // 이전 tick 대비 변화량을 heading_error_rate_limit_rad_ 이내로 제한
  // ----------------------------------------------------------
  const double delta = raw_heading_error - prev_heading_error_;
  const double clamped_delta = std::max(-heading_error_rate_limit_rad_,
                                        std::min(heading_error_rate_limit_rad_, delta));
  const double heading_error = prev_heading_error_ + clamped_delta;
  prev_heading_error_ = heading_error;

  // ----------------------------------------------------------
  // v, w 계산
  // ----------------------------------------------------------
  const double v_cmd = compute_linear_velocity(heading_error);
  const double w_cmd = compute_angular_velocity(heading_error);

  cmd.twist.linear.x  = v_cmd;
  cmd.twist.linear.y  = 0.0;
  cmd.twist.angular.z = w_cmd;

  cmd_vel_raw_pub_->publish(cmd);
}

// ============================================================
// 현재 위치와 waypoint 간 거리 계산
// ============================================================

double PathFollowerNode::distance_to_waypoint(size_t index)
{
  const double cx = latest_pose_->x_m;
  const double cy = latest_pose_->y_m;
  const double wx = latest_local_path_->poses[index].pose.position.x;
  const double wy = latest_local_path_->poses[index].pose.position.y;
  const double dx = wx - cx;
  const double dy = wy - cy;
  return std::sqrt(dx * dx + dy * dy);
}

// ============================================================
// heading error 계산 (-π ~ π wrap)
// ============================================================

double PathFollowerNode::compute_heading_error(double target_x, double target_y)
{
  const double cx = latest_pose_->x_m;
  const double cy = latest_pose_->y_m;
  const double current_yaw = latest_pose_->yaw_rad;

  const double target_heading = std::atan2(target_y - cy, target_x - cx);
  double error = target_heading - current_yaw;

  // -π ~ π wrap
  while (error >  M_PI) { error -= 2.0 * M_PI; }
  while (error < -M_PI) { error += 2.0 * M_PI; }

  return error;
}

// ============================================================
// v 계산 (heading_error 기반 감속)
// ============================================================

double PathFollowerNode::compute_linear_velocity(double heading_error)
{
  // 제자리 회전 구간
  if (std::abs(heading_error) > turn_in_place_threshold_rad_) {
    return 0.0;
  }

  // heading_error 기반 감속
  // 0.15 ~ 0.50 구간에서 선형 감속, min_v 보장
  // 0.50 ~ 0.70 구간에서 v=0 (방향 많이 틀어짐, 전진 안 함)
  double v_cmd = max_linear_x_mps_;
  if (std::abs(heading_error) > heading_tolerance_rad_) {
    const double ratio = 1.0 - std::abs(heading_error) / slow_down_angle_rad_;
    v_cmd = max_linear_x_mps_ * std::max(ratio, 0.0);
    if (v_cmd > 0.0) {
      v_cmd = std::max(v_cmd, min_linear_x_mps_);
    }
  }

  return v_cmd;
}

// ============================================================
// w 계산
// ============================================================

double PathFollowerNode::compute_angular_velocity(double heading_error)
{
  double w_cmd = k_yaw_ * heading_error;

  if (w_cmd >  max_angular_z_radps_) { w_cmd =  max_angular_z_radps_; }
  if (w_cmd < -max_angular_z_radps_) { w_cmd = -max_angular_z_radps_; }

  return w_cmd;
}

}  // namespace spot_navigation

// ============================================================
// main
// ============================================================
int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<spot_navigation::PathFollowerNode>());
  rclcpp::shutdown();
  return 0;
}
