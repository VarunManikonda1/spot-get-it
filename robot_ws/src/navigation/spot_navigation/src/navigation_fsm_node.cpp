#include "spot_navigation/navigation_fsm_node.hpp"

namespace spot_navigation
{

NavigationFsmNode::NavigationFsmNode(const rclcpp::NodeOptions & options)
: Node("navigation_fsm_node", options),
  current_nav_state_(robot_interfaces::msg::NavigationState::NAV_WAITING_FOR_PATH),
  front_blocked_(false),
  front_clear_timer_active_(false)
{
  // ============================================================
  // Parameters
  // ============================================================
  robot_id_                 = declare_parameter<std::string>("robot_id", "spot_01");
  topic_pose_               = declare_parameter<std::string>("topic_pose", "/localization/pose");
  topic_obstacle_           = declare_parameter<std::string>("topic_obstacle", "/perception/lidar/obstacle_model");
  topic_free_space_         = declare_parameter<std::string>("topic_free_space", "/perception/lidar/free_space_model");
  front_block_distance_m_   = declare_parameter<double>("front_block_distance_m", 0.70);
  side_block_distance_m_    = declare_parameter<double>("side_block_distance_m", 0.50);
  front_clear_distance_m_   = declare_parameter<double>("front_clear_distance_m", 0.85);
  clear_hold_time_sec_      = declare_parameter<double>("clear_hold_time_sec", 0.5);
  rejoin_tolerance_m_       = declare_parameter<double>("rejoin_tolerance_m", 0.30);
  pose_timeout_sec_         = declare_parameter<double>("pose_timeout_sec", 0.5);
  perception_timeout_sec_   = declare_parameter<double>("perception_timeout_sec", 0.5);
  emergency_stop_distance_m_= declare_parameter<double>("emergency_stop_distance_m", 0.30);
  front_azimuth_limit_rad_  = declare_parameter<double>("front_azimuth_limit_rad", 0.5236);
  timer_period_sec_         = declare_parameter<double>("timer_period_sec", 0.02);

  // ============================================================
  // Publishers
  // ============================================================
  nav_state_pub_ = create_publisher<robot_interfaces::msg::NavigationState>(
    "/navigation/state/" + robot_id_, 10);

  // 하위 제어기(safety_supervisor_node) 요구사항에 맞춰 QoS Durability를 Transient Local로 설정
  rclcpp::QoS drive_mode_qos(10);
  drive_mode_qos.transient_local();

  drive_mode_pub_ = create_publisher<std_msgs::msg::String>(
    "/control/drive_mode/" + robot_id_, drive_mode_qos);

  // 노드가 완전히 기동하기 전 AUTO 모드 즉시 발행
  RCLCPP_INFO(get_logger(), "하위 제어기 AUTO 주행 모드 전환 명령 발행 시작...");
  auto mode_msg = std_msgs::msg::String();
  mode_msg.data = "AUTO";

  // 네트워크 탐색(Discovery) 및 초기 동기화를 위해 10Hz로 0.5초간 반복 발행
  rclcpp::Rate ready_rate(10);
  for (int i = 0; i < 5; ++i) {
    drive_mode_pub_->publish(mode_msg);
    rclcpp::spin_some(this->get_node_base_interface());
    ready_rate.sleep();
  }
  RCLCPP_INFO(get_logger(), "AUTO 주행 모드 전환 완료.");

  // ============================================================
  // Subscribers
  // ============================================================
  path_progress_sub_ = create_subscription<robot_interfaces::msg::PathProgress>(
    "/navigation/path_progress/" + robot_id_, 10,
    std::bind(&NavigationFsmNode::on_path_progress, this, std::placeholders::_1));

  pose_sub_ = create_subscription<robot_interfaces::msg::LocalizedRobotPose>(
    topic_pose_, 10,
    std::bind(&NavigationFsmNode::on_pose, this, std::placeholders::_1));

  obstacle_sub_ = create_subscription<robot_interfaces::msg::ObstacleModel>(
    topic_obstacle_, 10,
    std::bind(&NavigationFsmNode::on_obstacle, this, std::placeholders::_1));

  free_space_sub_ = create_subscription<robot_interfaces::msg::FreeSpaceModel>(
    topic_free_space_, 10,
    std::bind(&NavigationFsmNode::on_free_space, this, std::placeholders::_1));

  local_path_sub_ = create_subscription<nav_msgs::msg::Path>(
    "/navigation/local_path/" + robot_id_, 10,
    std::bind(&NavigationFsmNode::on_local_path, this, std::placeholders::_1));

  // ============================================================
  // Timer
  // ============================================================
  timer_ = create_wall_timer(
    std::chrono::duration<double>(timer_period_sec_),
    std::bind(&NavigationFsmNode::on_timer, this));

  last_pose_time_       = now();
  last_perception_time_ = now();

  RCLCPP_INFO(get_logger(), "navigation_fsm_node 시작 [robot_id: %s]", robot_id_.c_str());
}

// ============================================================
// Callbacks
// ============================================================
void NavigationFsmNode::on_path_progress(robot_interfaces::msg::PathProgress::SharedPtr msg)
{ latest_path_progress_ = msg; }

void NavigationFsmNode::on_pose(robot_interfaces::msg::LocalizedRobotPose::SharedPtr msg)
{ latest_pose_ = msg; last_pose_time_ = now(); }

void NavigationFsmNode::on_obstacle(robot_interfaces::msg::ObstacleModel::SharedPtr msg)
{ latest_obstacle_ = msg; last_perception_time_ = now(); }

void NavigationFsmNode::on_free_space(robot_interfaces::msg::FreeSpaceModel::SharedPtr msg)
{ latest_free_space_ = msg; }

void NavigationFsmNode::on_local_path(nav_msgs::msg::Path::SharedPtr msg)
{ latest_local_path_ = msg; }

// ============================================================
// Timer callback : FSM 판단 및 publish
// ============================================================
void NavigationFsmNode::on_timer()
{
  current_nav_state_ = determine_nav_state();
  auto msg = build_nav_state_msg(current_nav_state_);
  nav_state_pub_->publish(msg);
}

// ============================================================
// FSM 판단 (우선순위 순)
// ============================================================
uint8_t NavigationFsmNode::determine_nav_state()
{
  using NS = robot_interfaces::msg::NavigationState;
  const auto now_time = now();

  // 1. NAV_LOCALIZATION_LOST
  const double pose_age = (now_time - last_pose_time_).seconds();
  if (pose_age > pose_timeout_sec_) {
    return NS::NAV_LOCALIZATION_LOST;
  }

  // 3. NAV_WAITING_FOR_PATH
  if (!latest_path_progress_ || !latest_path_progress_->path_valid) {
    return NS::NAV_WAITING_FOR_PATH;
  }

  // 4. NAV_EMERGENCY_STOP
  if (latest_obstacle_ && latest_obstacle_->front.valid) {
    const float azimuth   = latest_obstacle_->front.azimuth_angle_rad;
    const float front_dist = latest_obstacle_->front.nearest_distance_xy;
    if (std::abs(azimuth) <= static_cast<float>(front_azimuth_limit_rad_) &&
        front_dist < static_cast<float>(emergency_stop_distance_m_)) {
      return NS::NAV_EMERGENCY_STOP;
    }
  }

  // 5. NAV_GOAL_REACHED
  if (latest_path_progress_->goal_reached) {
    return NS::NAV_GOAL_REACHED;
  }

  // 6. front_blocked hysteresis 업데이트
  float front_clearance = 9999.0f;
  if (latest_obstacle_ && latest_obstacle_->front.valid) {
    const float azimuth = latest_obstacle_->front.azimuth_angle_rad;
    if (std::abs(azimuth) <= static_cast<float>(front_azimuth_limit_rad_)) {
      front_clearance = latest_obstacle_->front.nearest_distance_xy;
    }
  }
  update_front_blocked(front_clearance);

  // 7. NAV_STOPPED_BY_OBSTACLE
  if (front_blocked_) {
    const bool free_space_available =
      latest_free_space_ && latest_free_space_->path_available;
    if (!free_space_available) {
      return NS::NAV_STOPPED_BY_OBSTACLE;
    }
  }

  // 8. NAV_FOLLOWING_LOCAL_PATH / NAV_PLANNING_LOCAL_PATH
  if (front_blocked_) {
    const bool local_path_available =
      latest_local_path_ && !latest_local_path_->poses.empty();
    if (local_path_available) {
      return NS::NAV_FOLLOWING_LOCAL_PATH;
    }
    return NS::NAV_PLANNING_LOCAL_PATH;
  }

  // 9. NAV_REJOINING_GLOBAL_PATH
  const bool was_avoiding =
    (current_nav_state_ == NS::NAV_FOLLOWING_LOCAL_PATH ||
     current_nav_state_ == NS::NAV_AVOIDING_OBSTACLE ||
     current_nav_state_ == NS::NAV_PLANNING_LOCAL_PATH);

  if (was_avoiding &&
      latest_path_progress_->distance_to_nearest_m < static_cast<float>(rejoin_tolerance_m_))
  {
    return NS::NAV_REJOINING_GLOBAL_PATH;
  }

  // 10. NAV_TRACKING_GLOBAL_PATH
  return NS::NAV_TRACKING_GLOBAL_PATH;
}

// ============================================================
// Hysteresis : front_blocked 업데이트
// ============================================================
void NavigationFsmNode::update_front_blocked(float front_clearance)
{
  if (!front_blocked_) {
    if (front_clearance < static_cast<float>(front_block_distance_m_)) {
      front_blocked_ = true;
      front_clear_timer_active_ = false;
      RCLCPP_WARN(get_logger(), "front_blocked = true (clearance: %.2fm)", front_clearance);
    }
  } else {
    if (front_clearance > static_cast<float>(front_clear_distance_m_)) {
      if (!front_clear_timer_active_) {
        front_clear_start_time_   = now();
        front_clear_timer_active_ = true;
      }
      const double clear_duration = (now() - front_clear_start_time_).seconds();
      if (clear_duration >= clear_hold_time_sec_) {
        front_blocked_            = false;
        front_clear_timer_active_ = false;
        latest_local_path_        = nullptr;
        RCLCPP_INFO(get_logger(), "front_blocked = false (clearance: %.2fm)", front_clearance);
      }
    } else {
      front_clear_timer_active_ = false;
    }
  }
}

// ============================================================
// NavigationState 메시지 빌드
// ============================================================
robot_interfaces::msg::NavigationState
NavigationFsmNode::build_nav_state_msg(uint8_t nav_state)
{
  using NS = robot_interfaces::msg::NavigationState;

  robot_interfaces::msg::NavigationState msg;
  msg.header.stamp    = now();
  msg.header.frame_id = "mission_map";
  msg.robot_id        = robot_id_;
  msg.nav_state       = nav_state;

  msg.path_received    = (latest_path_progress_ != nullptr);
  msg.pose_valid       = (latest_pose_ != nullptr);
  msg.perception_valid = (latest_obstacle_ != nullptr);

  msg.front_blocked = front_blocked_;
  if (latest_obstacle_) {
    msg.left_blocked  = latest_obstacle_->left.valid &&
      (latest_obstacle_->left.nearest_distance_xy < static_cast<float>(side_block_distance_m_));
    msg.right_blocked = latest_obstacle_->right.valid &&
      (latest_obstacle_->right.nearest_distance_xy < static_cast<float>(side_block_distance_m_));

    msg.front_clearance_m = latest_obstacle_->front.valid ? latest_obstacle_->front.nearest_distance_xy : 9999.0f;
    msg.left_clearance_m  = latest_obstacle_->left.valid ? latest_obstacle_->left.nearest_distance_xy : 9999.0f;
    msg.right_clearance_m = latest_obstacle_->right.valid ? latest_obstacle_->right.nearest_distance_xy : 9999.0f;
  }

  if (latest_path_progress_) {
    msg.goal_reached              = latest_path_progress_->goal_reached;
    msg.distance_to_goal_m        = latest_path_progress_->distance_to_goal_m;
    msg.distance_to_global_path_m = latest_path_progress_->distance_to_nearest_m;
  }

  msg.local_path_required = (nav_state == NS::NAV_PLANNING_LOCAL_PATH ||
                             nav_state == NS::NAV_FOLLOWING_LOCAL_PATH ||
                             nav_state == NS::NAV_AVOIDING_OBSTACLE);
  msg.rejoin_required     = (nav_state == NS::NAV_REJOINING_GLOBAL_PATH);
  msg.stop_required       = (nav_state == NS::NAV_EMERGENCY_STOP ||
                             nav_state == NS::NAV_STOPPED_BY_OBSTACLE ||
                             nav_state == NS::NAV_GOAL_REACHED ||
                             nav_state == NS::NAV_LOCALIZATION_LOST ||
                             nav_state == NS::NAV_PERCEPTION_STALE);

  switch (nav_state) {
    case NS::NAV_WAITING_FOR_PATH:      msg.reason = "waiting for global path"; break;
    case NS::NAV_TRACKING_GLOBAL_PATH:  msg.reason = "tracking global path"; break;
    case NS::NAV_PLANNING_LOCAL_PATH:   msg.reason = "front blocked, planning local path"; break;
    case NS::NAV_FOLLOWING_LOCAL_PATH:  msg.reason = "front blocked, following local path"; break;
    case NS::NAV_AVOIDING_OBSTACLE:     msg.reason = "avoiding obstacle"; break;
    case NS::NAV_REJOINING_GLOBAL_PATH: msg.reason = "rejoining global path"; break;
    case NS::NAV_STOPPED_BY_OBSTACLE:   msg.reason = "stopped: no free space"; break;
    case NS::NAV_GOAL_REACHED:          msg.reason = "goal reached"; break;
    case NS::NAV_EMERGENCY_STOP:        msg.reason = "emergency stop: obstacle too close"; break;
    case NS::NAV_LOCALIZATION_LOST:     msg.reason = "localization lost: pose stale"; break;
    case NS::NAV_PERCEPTION_STALE:      msg.reason = "perception stale"; break;
    default:                            msg.reason = "unknown"; break;
  }

  return msg;
}

}  // namespace spot_navigation

// ============================================================
// main
// ============================================================
int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<spot_navigation::NavigationFsmNode>());
  rclcpp::shutdown();
  return 0;
}
