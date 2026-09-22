#include "spot_navigation/safety_supervisor_node.hpp"

#include <cmath>
#include <algorithm>

namespace spot_navigation
{

SafetySupervisorNode::SafetySupervisorNode(const rclcpp::NodeOptions & options)
: Node("safety_supervisor_node", options),
  prev_v_(0.0),
  prev_w_(0.0),
  drive_mode_("manual")
{
  // ============================================================
  // Parameters
  // ============================================================
  robot_id_                   = declare_parameter<std::string>("robot_id", "spot_01");
  topic_pose_                 = declare_parameter<std::string>("topic_pose", "/localization/pose");
  topic_obstacle_             = declare_parameter<std::string>("topic_obstacle", "/perception/lidar/obstacle_model");
  topic_free_space_           = declare_parameter<std::string>("topic_free_space", "/perception/lidar/free_space_model");
  topic_drive_mode_           = declare_parameter<std::string>("topic_drive_mode", "/control/drive_mode/spot_01");
  soft_stop_distance_m_       = declare_parameter<double>("soft_stop_distance_m", 0.65);
  emergency_stop_distance_m_  = declare_parameter<double>("emergency_stop_distance_m", 0.30);
  max_safe_linear_x_mps_      = declare_parameter<double>("max_safe_linear_x_mps", 0.10);
  max_safe_angular_z_radps_   = declare_parameter<double>("max_safe_angular_z_radps", 0.40);
  max_linear_accel_mps2_      = declare_parameter<double>("max_linear_accel_mps2", 1.65);
  max_linear_decel_mps2_      = declare_parameter<double>("max_linear_decel_mps2", 0.20);
  max_angular_accel_radps2_   = declare_parameter<double>("max_angular_accel_radps2", 1.0);
  cmd_timeout_sec_            = declare_parameter<double>("cmd_timeout_sec", 0.30);
  pose_timeout_sec_           = declare_parameter<double>("pose_timeout_sec", 0.50);
  perception_timeout_sec_     = declare_parameter<double>("perception_timeout_sec", 0.50);
  front_azimuth_limit_rad_    = declare_parameter<double>("front_azimuth_limit_rad", 0.5236);  // 30도
  timer_period_sec_           = declare_parameter<double>("timer_period_sec", 0.02);

  // ============================================================
  // Subscribers
  // ============================================================
  cmd_vel_raw_sub_ = create_subscription<geometry_msgs::msg::TwistStamped>(
    "/control/cmd_vel_raw/" + robot_id_, 10,
    std::bind(&SafetySupervisorNode::on_cmd_vel_raw, this, std::placeholders::_1));

  nav_state_sub_ = create_subscription<robot_interfaces::msg::NavigationState>(
    "/navigation/state/" + robot_id_, 10,
    std::bind(&SafetySupervisorNode::on_nav_state, this, std::placeholders::_1));

  obstacle_sub_ = create_subscription<robot_interfaces::msg::ObstacleModel>(
    topic_obstacle_, 10,
    std::bind(&SafetySupervisorNode::on_obstacle, this, std::placeholders::_1));

  free_space_sub_ = create_subscription<robot_interfaces::msg::FreeSpaceModel>(
    topic_free_space_, 10,
    std::bind(&SafetySupervisorNode::on_free_space, this, std::placeholders::_1));

  pose_sub_ = create_subscription<robot_interfaces::msg::LocalizedRobotPose>(
    topic_pose_, 10,
    std::bind(&SafetySupervisorNode::on_pose, this, std::placeholders::_1));

  rclcpp::QoS drive_mode_qos(10);
  drive_mode_qos.reliable();
  drive_mode_qos.transient_local();

  drive_mode_sub_ = create_subscription<std_msgs::msg::String>(
    topic_drive_mode_, drive_mode_qos,
    std::bind(&SafetySupervisorNode::on_drive_mode, this, std::placeholders::_1));

  // ============================================================
  // Publishers
  // ============================================================
  cmd_vel_pub_ = create_publisher<geometry_msgs::msg::Twist>(
    "/control/cmd_vel/" + robot_id_, 10);

  safety_status_pub_ = create_publisher<robot_interfaces::msg::SafetyStatus>(
    "/safety/status/" + robot_id_, 10);

  // ============================================================
  // Timer
  // ============================================================
  timer_ = create_wall_timer(
    std::chrono::duration<double>(timer_period_sec_),
    std::bind(&SafetySupervisorNode::on_timer, this));

  // 초기 시간 설정
  last_cmd_time_        = now();
  last_pose_time_       = now();
  last_perception_time_ = now();

  RCLCPP_INFO(get_logger(), "safety_supervisor_node 시작 [robot_id: %s]", robot_id_.c_str());
}

// ============================================================
// Callbacks
// ============================================================

void SafetySupervisorNode::on_cmd_vel_raw(
  geometry_msgs::msg::TwistStamped::SharedPtr msg)
{
  latest_cmd_vel_raw_ = msg;
  last_cmd_time_      = now();
}

void SafetySupervisorNode::on_nav_state(
  robot_interfaces::msg::NavigationState::SharedPtr msg)
{
  latest_nav_state_ = msg;
}

void SafetySupervisorNode::on_obstacle(
  robot_interfaces::msg::ObstacleModel::SharedPtr msg)
{
  latest_obstacle_      = msg;
  last_perception_time_ = now();
}

void SafetySupervisorNode::on_free_space(
  robot_interfaces::msg::FreeSpaceModel::SharedPtr msg)
{
  latest_free_space_ = msg;
}

void SafetySupervisorNode::on_pose(
  robot_interfaces::msg::LocalizedRobotPose::SharedPtr msg)
{
  latest_pose_    = msg;
  last_pose_time_ = now();
}

void SafetySupervisorNode::on_drive_mode(
  std_msgs::msg::String::SharedPtr msg)
{
  drive_mode_ = msg->data;
}

// ============================================================
// Timer callback
// ============================================================

void SafetySupervisorNode::on_timer()
{
  using NS = robot_interfaces::msg::NavigationState;
  // drive_mode가 AUTO일 때만 publish
  if (drive_mode_ != "AUTO") {
    return;
  }

  const double dt = timer_period_sec_;
  const auto now_time = now();

  // 입력값
  const double input_v = latest_cmd_vel_raw_ ?
    latest_cmd_vel_raw_->twist.linear.x : 0.0;
  const double input_w = latest_cmd_vel_raw_ ?
    latest_cmd_vel_raw_->twist.angular.z : 0.0;

  // ----------------------------------------------------------
  // stale 검사
  // ----------------------------------------------------------
  const bool cmd_stale =
    (now_time - last_cmd_time_).seconds() > cmd_timeout_sec_;
  const bool pose_stale =
    (now_time - last_pose_time_).seconds() > pose_timeout_sec_;
  // perception_stale 비활성화
  // obstacle_model/free_space_model이 장애물 없을 때 미발행되는 구조라
  // lidar 동작 여부와 무관하게 stale이 뜨는 문제 → 향후 별도 토픽으로 대체 필요
  const bool perception_stale = false;
  // const bool perception_stale =
  //   (now_time - last_perception_time_).seconds() > perception_timeout_sec_;

  // ----------------------------------------------------------
  // goal_reached 검사
  // ----------------------------------------------------------
  const bool goal_reached = latest_nav_state_ &&
    (latest_nav_state_->nav_state == NS::NAV_GOAL_REACHED);

  // ----------------------------------------------------------
  // 전방 장애물 거리
  // azimuth_angle_rad 기준으로 front_azimuth_limit_rad 이내인 경우만 유효
  // ----------------------------------------------------------
  float front_clearance = 9999.0f;
  if (latest_obstacle_ && latest_obstacle_->front.valid) {
    const float azimuth = latest_obstacle_->front.azimuth_angle_rad;
    if (std::abs(azimuth) <= static_cast<float>(front_azimuth_limit_rad_)) {
      front_clearance = latest_obstacle_->front.nearest_distance_xy;
    }
  }

  const bool emergency_stop =
    front_clearance < static_cast<float>(emergency_stop_distance_m_);
  const bool soft_stop =
    !emergency_stop &&
    front_clearance < static_cast<float>(soft_stop_distance_m_);
  const bool obstacle_too_close = emergency_stop || soft_stop;

  // ----------------------------------------------------------
  // 목표 v, w 결정
  // ----------------------------------------------------------
  double target_v = input_v;
  double target_w = input_w;

  // 1, 2, 3, 5 : 감속 후 정지
  if (cmd_stale || pose_stale || perception_stale || goal_reached) {
    target_v = 0.0;
    target_w = 0.0;
    // 가감속 limit에서 점진적으로 0에 수렴
  }
  // 4 : emergency stop → 즉시 정지
  else if (emergency_stop) {
    prev_v_ = 0.0;
    prev_w_ = 0.0;
    // 바로 publish 후 종료
    geometry_msgs::msg::Twist cmd;
    cmd.linear.x  = 0.0;
    cmd.angular.z = 0.0;
    cmd_vel_pub_->publish(cmd);

    auto status = build_safety_status(
      input_v, input_w, 0.0, 0.0,
      true, false, cmd_stale, pose_stale, perception_stale,
      goal_reached, obstacle_too_close, front_clearance);
    safety_status_pub_->publish(status);
    return;
  }
  // 6 : soft stop → v 거리 비율 감속, w 통과
  else if (soft_stop) {
    const double ratio =
      static_cast<double>(front_clearance) / soft_stop_distance_m_;
    target_v = input_v * ratio;
    target_w = input_w;
  }

  // ----------------------------------------------------------
  // 7 : max speed 제한
  // ----------------------------------------------------------
  target_v = std::clamp(target_v, 0.0, max_safe_linear_x_mps_);
  target_w = std::clamp(target_w, -max_safe_angular_z_radps_, max_safe_angular_z_radps_);

  // ----------------------------------------------------------
  // 8 : acceleration limit 적용
  // ----------------------------------------------------------
  const double out_v = apply_accel_limit(
    target_v, prev_v_, max_linear_accel_mps2_, max_linear_decel_mps2_, dt);
  const double out_w = apply_accel_limit(
    target_w, prev_w_, max_angular_accel_radps2_, max_angular_accel_radps2_, dt);

  prev_v_ = out_v;
  prev_w_ = out_w;

  // ----------------------------------------------------------
  // 9 : publish
  // ----------------------------------------------------------
  geometry_msgs::msg::Twist cmd;
  cmd.linear.x  = out_v;
  cmd.angular.z = out_w;
  cmd_vel_pub_->publish(cmd);

  auto status = build_safety_status(
    input_v, input_w, out_v, out_w,
    emergency_stop, soft_stop,
    cmd_stale, pose_stale, perception_stale,
    goal_reached, obstacle_too_close, front_clearance);
  safety_status_pub_->publish(status);
}

// ============================================================
// 가감속 제한 적용
// ============================================================

double SafetySupervisorNode::apply_accel_limit(
  double target, double prev, double accel, double decel, double dt)
{
  if (target > prev) {
    // 가속
    return std::min(prev + accel * dt, target);
  } else if (target < prev) {
    // 감속
    return std::max(prev - decel * dt, target);
  }
  return target;
}

// ============================================================
// SafetyStatus 메시지 빌드
// ============================================================

robot_interfaces::msg::SafetyStatus
SafetySupervisorNode::build_safety_status(
  double input_v, double input_w,
  double output_v, double output_w,
  bool emergency_stop, bool soft_stop,
  bool cmd_stale, bool pose_stale, bool perception_stale,
  bool goal_reached, bool obstacle_too_close,
  float front_clearance)
{
  robot_interfaces::msg::SafetyStatus msg;
  msg.header.stamp    = now();
  msg.header.frame_id = "base_link";
  msg.robot_id        = robot_id_;

  msg.safe_to_move            = !emergency_stop && !cmd_stale &&
                                !pose_stale && !perception_stale;
  msg.soft_stop_required      = soft_stop;
  msg.emergency_stop_required = emergency_stop;
  msg.pose_stale              = pose_stale;
  msg.perception_stale        = perception_stale;
  msg.cmd_stale               = cmd_stale;
  msg.obstacle_too_close      = obstacle_too_close;
  msg.goal_reached            = goal_reached;
  msg.local_path_invalid      = false;  // local_path_planner 연동 후 갱신

  msg.nearest_front_obstacle_m  = front_clearance;
  msg.soft_stop_distance_m      = static_cast<float>(soft_stop_distance_m_);
  msg.emergency_stop_distance_m = static_cast<float>(emergency_stop_distance_m_);

  msg.input_linear_x_mps    = static_cast<float>(input_v);
  msg.input_angular_z_radps = static_cast<float>(input_w);
  msg.output_linear_x_mps   = static_cast<float>(output_v);
  msg.output_angular_z_radps= static_cast<float>(output_w);

  // reason
  if (emergency_stop)       msg.reason = "emergency stop: obstacle too close";
  else if (cmd_stale)       msg.reason = "cmd_vel_raw stale: decelerating";
  else if (pose_stale)      msg.reason = "pose stale: decelerating";
  else if (perception_stale)msg.reason = "perception stale: decelerating";
  else if (goal_reached)    msg.reason = "goal reached: decelerating";
  else if (soft_stop)       msg.reason = "soft stop: obstacle approaching";
  else                      msg.reason = "normal";

  return msg;
}

}  // namespace spot_navigation

// ============================================================
// main
// ============================================================
int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<spot_navigation::SafetySupervisorNode>());
  rclcpp::shutdown();
  return 0;
}
