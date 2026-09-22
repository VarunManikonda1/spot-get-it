/*
 * 경로: spot_navigation/src/mock_localization_pose_publisher_node.cpp
 * 역할:
 *   mock global path를 따라 Spot pose가 실제로 이동하는 것처럼
 *   /localization/pose를 연속적으로 publish한다.
 *
 * 입력 토픽:
 *   - /planning/mock_global_path/spot_01
 *     Type: robot_interfaces/msg/GlobalPathWaypoints
 *
 * 출력 토픽:
 *   - /localization/pose
 *     Type: robot_interfaces/msg/LocalizedRobotPose
 *
 * 주요 기능:
 *   - GlobalPathWaypoints를 수신하면 waypoint 목록을 저장한다.
 *   - 현재 segment index와 segment 내 이동 거리를 관리한다.
 *   - linear_speed_mps와 publish_rate_hz를 기반으로 이동 거리를 누적한다.
 *   - waypoint 사이를 선형 보간하여 촘촘하게 움직이는 pose를 생성한다.
 *   - yaw는 이동 segment 방향 또는 waypoint yaw 보간 방식으로 계산한다.
 *
 * 좌표계:
 *   - global path와 localization pose는 모두 global_frame 기준이다.
 *   - 현재 프로젝트에서는 global_frame = "mission_map"으로 사용한다.
 */

#include "spot_navigation/mock_localization_pose_publisher_node.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <random>

using namespace std::chrono_literals;

namespace spot_navigation
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
}  // namespace

MockLocalizationPosePublisherNode::MockLocalizationPosePublisherNode()
: Node("mock_localization_pose_publisher_node"),
  default_z_m_(0.05),
  publish_rate_hz_(20.0),
  linear_speed_mps_(0.2),
  loop_path_(false),
  hold_at_goal_(true),
  use_segment_yaw_(true),
  reset_on_new_path_(true),
  path_received_(false),
  path_valid_(false),
  goal_reached_(false),
  current_segment_index_(0),
  distance_along_segment_m_(0.0),
  last_update_time_initialized_(false),
  noise_enabled_(false),
  position_noise_std_m_(0.0),
  yaw_noise_std_rad_(0.0),
  max_position_noise_m_(0.10),
  max_yaw_noise_rad_(0.10),
  noise_seed_(-1),
  normal_distribution_(0.0, 1.0)
{
  loadParameters();
  initializeNoiseGenerator();

  /*
   * mock_global_path_publisher_node가 transient_local QoS로 path를 publish하므로,
   * subscriber도 transient_local로 맞추면 나중에 실행되어도 마지막 path를 받을 수 있다.
   */
  auto path_qos = rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local();

  global_path_sub_ =
    this->create_subscription<robot_interfaces::msg::GlobalPathWaypoints>(
      global_path_topic_,
      path_qos,
      std::bind(&MockLocalizationPosePublisherNode::globalPathCallback, this, std::placeholders::_1)
    );

  /*
   * localization pose는 계속 갱신되는 실시간 상태값이므로 일반 reliable QoS를 사용한다.
   */
  auto pose_qos = rclcpp::QoS(rclcpp::KeepLast(10)).reliable();

  pose_pub_ =
    this->create_publisher<robot_interfaces::msg::LocalizedRobotPose>(
      localization_pose_topic_,
      pose_qos
    );

  const auto publish_period =
    std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::duration<double>(1.0 / publish_rate_hz_)
    );

  publish_timer_ = this->create_wall_timer(
    publish_period,
    std::bind(&MockLocalizationPosePublisherNode::publishMockPose, this)
  );

  RCLCPP_INFO(
    this->get_logger(),
    "mock_localization_pose_publisher_node started. input=%s, output=%s, robot_id=%s, frame=%s",
    global_path_topic_.c_str(),
    localization_pose_topic_.c_str(),
    robot_id_.c_str(),
    global_frame_.c_str()
  );
}

void MockLocalizationPosePublisherNode::loadParameters()
{
  this->declare_parameter<std::string>("robot_id", "spot_01");
  this->declare_parameter<std::string>("global_path_topic", "/planning/mock_global_path/spot_01");
  this->declare_parameter<std::string>("localization_pose_topic", "/localization/mock_pose");
  this->declare_parameter<std::string>("global_frame", "mission_map");
  this->declare_parameter<std::string>("base_frame", "base_link");

  this->declare_parameter<double>("default_z_m", 0.05);
  this->declare_parameter<double>("publish_rate_hz", 20.0);
  this->declare_parameter<double>("linear_speed_mps", 0.2);

  this->declare_parameter<bool>("loop_path", false);
  this->declare_parameter<bool>("hold_at_goal", true);
  this->declare_parameter<bool>("use_segment_yaw", true);
  this->declare_parameter<bool>("reset_on_new_path", true);
  
  this->declare_parameter<bool>("noise_enabled", false);
  this->declare_parameter<double>("position_noise_std_m", 0.0);
  this->declare_parameter<double>("yaw_noise_std_rad", 0.0);
  this->declare_parameter<double>("max_position_noise_m", 0.10);
  this->declare_parameter<double>("max_yaw_noise_rad", 0.10);
  this->declare_parameter<int>("noise_seed", -1);

  robot_id_ = this->get_parameter("robot_id").as_string();
  global_path_topic_ = this->get_parameter("global_path_topic").as_string();
  localization_pose_topic_ = this->get_parameter("localization_pose_topic").as_string();
  global_frame_ = this->get_parameter("global_frame").as_string();
  base_frame_ = this->get_parameter("base_frame").as_string();

  default_z_m_ = this->get_parameter("default_z_m").as_double();
  publish_rate_hz_ = this->get_parameter("publish_rate_hz").as_double();
  linear_speed_mps_ = this->get_parameter("linear_speed_mps").as_double();

  loop_path_ = this->get_parameter("loop_path").as_bool();
  hold_at_goal_ = this->get_parameter("hold_at_goal").as_bool();
  use_segment_yaw_ = this->get_parameter("use_segment_yaw").as_bool();
  reset_on_new_path_ = this->get_parameter("reset_on_new_path").as_bool();

  noise_enabled_ = this->get_parameter("noise_enabled").as_bool();
  position_noise_std_m_ = this->get_parameter("position_noise_std_m").as_double();
  yaw_noise_std_rad_ = this->get_parameter("yaw_noise_std_rad").as_double();
  max_position_noise_m_ = this->get_parameter("max_position_noise_m").as_double();
  max_yaw_noise_rad_ = this->get_parameter("max_yaw_noise_rad").as_double();
  noise_seed_ = this->get_parameter("noise_seed").as_int();

  if (publish_rate_hz_ <= 0.0) {
    RCLCPP_WARN(
      this->get_logger(),
      "publish_rate_hz must be positive. Reset to 20.0 Hz."
    );
    publish_rate_hz_ = 20.0;
  }

  if (linear_speed_mps_ < 0.0) {
    RCLCPP_WARN(
      this->get_logger(),
      "linear_speed_mps must be non-negative. Reset to 0.2 m/s."
    );
    linear_speed_mps_ = 0.2;
  }

  if (position_noise_std_m_ < 0.0) {
    RCLCPP_WARN(
      this->get_logger(),
      "position_noise_std_m must be non-negative. Reset to 0.0 m."
    );
    position_noise_std_m_ = 0.0;
  }

  if (yaw_noise_std_rad_ < 0.0) {
    RCLCPP_WARN(
      this->get_logger(),
      "yaw_noise_std_rad must be non-negative. Reset to 0.0 rad."
    );
    yaw_noise_std_rad_ = 0.0;
  }

  if (max_position_noise_m_ < 0.0) {
    RCLCPP_WARN(
      this->get_logger(),
      "max_position_noise_m must be non-negative. Reset to 0.10 m."
    );
    max_position_noise_m_ = 0.10;
  }

  if (max_yaw_noise_rad_ < 0.0) {
    RCLCPP_WARN(
      this->get_logger(),
      "max_yaw_noise_rad must be non-negative. Reset to 0.10 rad."
    );
    max_yaw_noise_rad_ = 0.10;
  }
}

void MockLocalizationPosePublisherNode::initializeNoiseGenerator()
{
  /*
  noise_seed_가 0 이상이면 고정 seed를 사용한다.
  이 경우 노드를 재실행해도 같은 noise 패턴이 생성되어 디버깅 재현성이 좋아진다.

  noise_seed_가 -1이면 std::random_device를 사용한다.
  이 경우 실행할 때마다 다른 noise 패턴이 생성된다.
  */
  if (noise_seed_ >= 0) {
    random_engine_.seed(static_cast<std::mt19937::result_type>(noise_seed_));
  } else {
    std::random_device random_device;
    random_engine_.seed(random_device());
  }

  RCLCPP_INFO(
    this->get_logger(),
    "mock pose noise: enabled=%s, pos_std=%.3f m, yaw_std=%.3f rad, pos_max=%.3f m, yaw_max=%.3f rad, seed=%d",
    noise_enabled_ ? "true" : "false",
    position_noise_std_m_,
    yaw_noise_std_rad_,
    max_position_noise_m_,
    max_yaw_noise_rad_,
    noise_seed_
  );
}

double MockLocalizationPosePublisherNode::sampleGaussianNoise(
  double stddev,
  double max_abs_value)
{
  /*
  noise가 꺼져 있거나 표준편차가 0이면 노이즈를 추가하지 않는다.
  */
  if (!noise_enabled_ || stddev <= 0.0) {
    return 0.0;
  }

  /*
  normal_distribution_은 평균 0, 표준편차 1인 표준정규분포다.
  여기에 stddev를 곱해 원하는 표준편차의 Gaussian noise를 만든다.
  */
  double noise = stddev * normal_distribution_(random_engine_);

  /*
  max_abs_value가 0보다 크면 noise를 [-max_abs_value, +max_abs_value] 범위로 제한한다.
  큰 outlier가 한 번 튀어서 path progress가 과하게 흔들리는 것을 막기 위함이다.
  */
  if (max_abs_value > 0.0) {
    noise = std::clamp(noise, -max_abs_value, max_abs_value);
  }

  return noise;
}

void MockLocalizationPosePublisherNode::globalPathCallback(
  const robot_interfaces::msg::GlobalPathWaypoints::SharedPtr msg)
{
  path_received_ = true;

  /*
  [1] robot_id 검사.

  다중 로봇 환경에서 다른 로봇의 global path가 들어오면
  현재 mock localization pose 생성에 사용하지 않는다.
  */
  if (msg->robot_id != robot_id_) {
    RCLCPP_WARN_THROTTLE(
      this->get_logger(),
      *this->get_clock(),
      2000,
      "Ignore global path for another robot. received=%s, expected=%s",
      msg->robot_id.c_str(),
      robot_id_.c_str()
    );
    return;
  }

  /*
  [2] frame_id 검사.

  mock pose는 global_frame 기준으로 publish되므로,
  global path도 같은 global_frame 기준이어야 한다.
  */
  if (msg->header.frame_id != global_frame_) {
    path_valid_ = false;

    RCLCPP_WARN(
      this->get_logger(),
      "Global path frame mismatch. received=%s, expected=%s",
      msg->header.frame_id.c_str(),
      global_frame_.c_str()
    );
    return;
  }

  /*
  [3] waypoint empty 검사.

  waypoint가 없으면 segment 이동, 보간 pose 생성, goal pose 생성이 모두 불가능하다.
  */
  if (msg->waypoints.empty()) {
    path_valid_ = false;

    RCLCPP_WARN(
      this->get_logger(),
      "Received global path is empty. Cannot generate mock localization pose."
    );
    return;
  }

  /*
  [4] GlobalPathWaypoints 메시지를 내부 Waypoint2D vector로 변환한다.

  내부 계산에서는 x, y, z, yaw만 필요하므로,
  custom msg 전체를 그대로 들고 있지 않고 계산용 구조체로 변환한다.
  */
  std::vector<Waypoint2D> new_waypoints;
  new_waypoints.reserve(msg->waypoints.size());

  bool waypoint_frame_valid = true;

  for (const auto & waypoint_msg : msg->waypoints) {
    if (!waypoint_msg.header.frame_id.empty() &&
      waypoint_msg.header.frame_id != global_frame_)
    {
      waypoint_frame_valid = false;
    }

    Waypoint2D waypoint;
    waypoint.x_m = static_cast<double>(waypoint_msg.x_m);
    waypoint.y_m = static_cast<double>(waypoint_msg.y_m);

    /*
    z 값이 NaN/inf이면 mock pose의 z가 깨질 수 있으므로 default_z_m_로 대체한다.
    */
    waypoint.z_m = std::isfinite(static_cast<double>(waypoint_msg.z_m)) ?
      static_cast<double>(waypoint_msg.z_m) : default_z_m_;

    waypoint.yaw_rad = static_cast<double>(waypoint_msg.yaw_rad);

    new_waypoints.push_back(waypoint);
  }

  if (!waypoint_frame_valid) {
    path_valid_ = false;

    RCLCPP_WARN(
      this->get_logger(),
      "At least one waypoint frame_id does not match global_frame=%s.",
      global_frame_.c_str()
    );
    return;
  }

  /*
  [5] 같은 path 반복 수신인지, 실제 새 path인지 판단한다.

  기존 path가 유효하고 waypoint 개수와 각 waypoint의 x, y, z, yaw가 모두 같으면
  같은 path가 반복 publish된 것으로 본다.

  반대로 기존 path가 없거나, 개수/좌표/yaw 중 하나라도 다르면 새 path로 본다.
  */
  bool path_changed = true;

  if (path_valid_ && waypoints_.size() == new_waypoints.size()) {
    path_changed = false;

    for (std::size_t i = 0; i < new_waypoints.size(); ++i) {
      const auto & old_wp = waypoints_[i];
      const auto & new_wp = new_waypoints[i];

      const bool waypoint_changed =
        std::fabs(old_wp.x_m - new_wp.x_m) > 1.0e-6 ||
        std::fabs(old_wp.y_m - new_wp.y_m) > 1.0e-6 ||
        std::fabs(old_wp.z_m - new_wp.z_m) > 1.0e-6 ||
        std::fabs(old_wp.yaw_rad - new_wp.yaw_rad) > 1.0e-6;

      if (waypoint_changed) {
        path_changed = true;
        break;
      }
    }
  }

  /*
  [6] 최신 path 저장.

  같은 path여도 저장 자체는 해둔다.
  단, 같은 path이면 아래 진행 상태 reset은 하지 않는다.
  */
  waypoints_ = new_waypoints;
  path_valid_ = true;

  /*
  [7] 진행 상태 reset 조건.

  reset_on_new_path_가 true여도 "진짜 새 path"일 때만 reset한다.
  같은 path가 반복 publish된 경우에는 current_segment_index_와
  distance_along_segment_m_를 유지해야 pose가 연속적으로 전진한다.
  */
  if (reset_on_new_path_ && path_changed) {
    current_segment_index_ = 0;
    distance_along_segment_m_ = 0.0;
    goal_reached_ = false;
    last_update_time_initialized_ = false;

    RCLCPP_INFO(
      this->get_logger(),
      "New global path detected. Reset mock localization motion state. waypoints=%zu",
      waypoints_.size()
    );
  } else {
    RCLCPP_DEBUG(
      this->get_logger(),
      "Same global path received. Keep mock localization motion state. segment=%zu, distance=%.3f",
      current_segment_index_,
      distance_along_segment_m_
    );
  }

  RCLCPP_INFO_THROTTLE(
    this->get_logger(),
    *this->get_clock(),
    3000,
    "Received valid global path. robot_id=%s, frame=%s, waypoints=%zu, path_changed=%s",
    msg->robot_id.c_str(),
    msg->header.frame_id.c_str(),
    waypoints_.size(),
    path_changed ? "true" : "false"
  );
}

void MockLocalizationPosePublisherNode::publishMockPose()
{
  const auto now = this->now();

  if (!hasValidPath()) {
    RCLCPP_WARN_THROTTLE(
      this->get_logger(),
      *this->get_clock(),
      3000,
      "Waiting for valid global path. path_received=%s, path_valid=%s",
      path_received_ ? "true" : "false",
      path_valid_ ? "true" : "false"
    );
    return;
  }

  /*
   * 첫 publish 때는 이동량 계산 없이 첫 pose를 바로 publish한다.
   * 이후 timer tick부터 실제 경과 시간 dt를 기반으로 이동 거리를 누적한다.
   */
  if (!last_update_time_initialized_) {
    last_update_time_ = now;
    last_update_time_initialized_ = true;

    pose_pub_->publish(makeCurrentPoseMsg(now));
    return;
  }

  const double dt_sec = std::max(0.0, (now - last_update_time_).seconds());
  last_update_time_ = now;

  const double delta_distance_m = linear_speed_mps_ * dt_sec;

  if (!goal_reached_ || loop_path_) {
    advanceAlongPath(delta_distance_m);
  }

  if (goal_reached_ && !hold_at_goal_ && !loop_path_) {
    RCLCPP_INFO_THROTTLE(
      this->get_logger(),
      *this->get_clock(),
      3000,
      "Mock robot reached goal. hold_at_goal=false, so pose publishing is skipped."
    );
    return;
  }

  pose_pub_->publish(makeCurrentPoseMsg(now));

  RCLCPP_INFO_THROTTLE(
    this->get_logger(),
    *this->get_clock(),
    2000,
    "mock pose publishing. segment=%zu, distance_along_segment=%.3f m, goal_reached=%s",
    current_segment_index_,
    distance_along_segment_m_,
    goal_reached_ ? "true" : "false"
  );
}

void MockLocalizationPosePublisherNode::advanceAlongPath(double delta_distance_m)
{
  if (waypoints_.size() <= 1) {
    goal_reached_ = true;
    current_segment_index_ = 0;
    distance_along_segment_m_ = 0.0;
    return;
  }

  double remaining_distance_m = delta_distance_m;

  while (remaining_distance_m > 0.0) {
    if (current_segment_index_ >= waypoints_.size() - 1) {
      goal_reached_ = true;

      if (loop_path_) {
        current_segment_index_ = 0;
        distance_along_segment_m_ = 0.0;
        goal_reached_ = false;
      }

      return;
    }

    const auto & start = waypoints_[current_segment_index_];
    const auto & end = waypoints_[current_segment_index_ + 1];

    const double segment_length_m =
      calculateDistance2D(start.x_m, start.y_m, end.x_m, end.y_m);

    /*
     * waypoint가 중복되어 segment 길이가 거의 0인 경우,
     * 해당 segment는 이동할 수 없으므로 다음 segment로 넘긴다.
     */
    if (segment_length_m < 1.0e-6) {
      current_segment_index_++;
      distance_along_segment_m_ = 0.0;
      continue;
    }

    const double remaining_in_segment_m =
      segment_length_m - distance_along_segment_m_;

    if (remaining_distance_m < remaining_in_segment_m) {
      distance_along_segment_m_ += remaining_distance_m;
      remaining_distance_m = 0.0;
    } else {
      remaining_distance_m -= remaining_in_segment_m;
      current_segment_index_++;
      distance_along_segment_m_ = 0.0;
    }
  }

  if (current_segment_index_ >= waypoints_.size() - 1) {
    goal_reached_ = true;
    current_segment_index_ = waypoints_.size() - 1;
    distance_along_segment_m_ = 0.0;
  }
}

robot_interfaces::msg::LocalizedRobotPose
MockLocalizationPosePublisherNode::makeCurrentPoseMsg(
  const rclcpp::Time & stamp)
{
  robot_interfaces::msg::LocalizedRobotPose pose_msg;

  pose_msg.header.stamp = stamp;
  pose_msg.header.frame_id = global_frame_;
  pose_msg.robot_id = robot_id_;
  pose_msg.base_frame = base_frame_;

  double x = waypoints_.front().x_m;
  double y = waypoints_.front().y_m;
  double z = waypoints_.front().z_m;
  double yaw = waypoints_.front().yaw_rad;

  if (waypoints_.size() == 1) {
    x = waypoints_[0].x_m;
    y = waypoints_[0].y_m;
    z = waypoints_[0].z_m;
    yaw = waypoints_[0].yaw_rad;
  } else if (goal_reached_ || current_segment_index_ >= waypoints_.size() - 1) {
    const auto & goal = waypoints_.back();

    x = goal.x_m;
    y = goal.y_m;
    z = goal.z_m;
    yaw = goal.yaw_rad;
  } else {
    const auto & start = waypoints_[current_segment_index_];
    const auto & end = waypoints_[current_segment_index_ + 1];

    const double segment_length_m =
      calculateDistance2D(start.x_m, start.y_m, end.x_m, end.y_m);

    const double ratio = segment_length_m > 1.0e-6 ?
      std::clamp(distance_along_segment_m_ / segment_length_m, 0.0, 1.0) : 0.0;

    x = start.x_m + ratio * (end.x_m - start.x_m);
    y = start.y_m + ratio * (end.y_m - start.y_m);
    z = start.z_m + ratio * (end.z_m - start.z_m);

    if (use_segment_yaw_) {
      /*
       * 이동 중인 segment의 방향을 로봇 yaw로 사용한다.
       * 실제 로봇이 path를 따라 전진하는 상황을 가장 직관적으로 흉내 낼 수 있다.
       */
      yaw = std::atan2(end.y_m - start.y_m, end.x_m - start.x_m);
    } else {
      /*
       * waypoint에 들어 있는 yaw_rad를 보간한다.
       * global path의 자세 방향까지 함께 따라가는 mock pose가 필요할 때 사용한다.
       */
      yaw = interpolateAngle(start.yaw_rad, end.yaw_rad, ratio);
    }
  }

  /*
  [Noise 적용]
  내부 path 진행 상태는 깨끗한 waypoint 보간값으로 유지하고,
  최종 publish되는 pose에만 노이즈를 섞는다.
  */
  const double x_noise =
    sampleGaussianNoise(position_noise_std_m_, max_position_noise_m_);

  const double y_noise =
    sampleGaussianNoise(position_noise_std_m_, max_position_noise_m_);

  const double yaw_noise =
    sampleGaussianNoise(yaw_noise_std_rad_, max_yaw_noise_rad_);

  x += x_noise;
  y += y_noise;
  yaw = normalizeAngle(yaw + yaw_noise);

  pose_msg.x_m = static_cast<float>(x);
  pose_msg.y_m = static_cast<float>(y);
  pose_msg.z_m = static_cast<float>(z);
  pose_msg.yaw_rad = static_cast<float>(normalizeAngle(yaw));

  pose_msg.pose.position.x = x;
  pose_msg.pose.position.y = y;
  pose_msg.pose.position.z = z;
  pose_msg.pose.orientation = yawToQuaternion(pose_msg.yaw_rad);

  return pose_msg;
}

double MockLocalizationPosePublisherNode::calculateDistance2D(
  double x1, double y1,
  double x2, double y2) const
{
  return std::hypot(x2 - x1, y2 - y1);
}

double MockLocalizationPosePublisherNode::normalizeAngle(double angle_rad) const
{
  while (angle_rad > kPi) {
    angle_rad -= kTwoPi;
  }

  while (angle_rad < -kPi) {
    angle_rad += kTwoPi;
  }

  return angle_rad;
}

double MockLocalizationPosePublisherNode::interpolateAngle(
  double from_rad,
  double to_rad,
  double ratio) const
{
  const double normalized_ratio = std::clamp(ratio, 0.0, 1.0);
  const double diff_rad = normalizeAngle(to_rad - from_rad);

  return normalizeAngle(from_rad + normalized_ratio * diff_rad);
}

geometry_msgs::msg::Quaternion
MockLocalizationPosePublisherNode::yawToQuaternion(double yaw_rad) const
{
  geometry_msgs::msg::Quaternion q;

  q.x = 0.0;
  q.y = 0.0;
  q.z = std::sin(yaw_rad * 0.5);
  q.w = std::cos(yaw_rad * 0.5);

  return q;
}

bool MockLocalizationPosePublisherNode::hasValidPath() const
{
  return path_received_ && path_valid_ && !waypoints_.empty();
}

}  // namespace spot_navigation

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  auto node = std::make_shared<spot_navigation::MockLocalizationPosePublisherNode>();
  rclcpp::spin(node);

  rclcpp::shutdown();
  return 0;
}