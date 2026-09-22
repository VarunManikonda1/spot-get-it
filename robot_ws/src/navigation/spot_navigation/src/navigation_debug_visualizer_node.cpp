/*
경로:
  spot_navigation/src/navigation_debug_visualizer_node.cpp

역할:
  Navigation A 파트에서 사용하는 custom message를 RViz가 표시할 수 있는
  표준 시각화 메시지로 변환한다.

입력 토픽:
  - /planning/mock_global_path/spot_01
    Type: robot_interfaces/msg/GlobalPathWaypoints

  - /localization/mock_pose
    Type: robot_interfaces/msg/LocalizedRobotPose

  - /navigation/path_progress/spot_01
    Type: robot_interfaces/msg/PathProgress

출력 토픽:
  - /debug/navigation/global_path
    Type: nav_msgs/msg/Path

  - /debug/navigation/mock_pose
    Type: geometry_msgs/msg/PoseStamped

  - /debug/navigation/progress_markers
    Type: visualization_msgs/msg/MarkerArray

주요 기능:
  - GlobalPathWaypoints를 nav_msgs/Path로 변환하여 RViz Path display에서 볼 수 있게 한다.
  - LocalizedRobotPose를 geometry_msgs/PoseStamped로 변환하여 RViz Pose display에서 볼 수 있게 한다.
  - PathProgress의 nearest/target/goal 정보를 MarkerArray로 변환하여 RViz MarkerArray display에서 볼 수 있게 한다.
  - 현재 pose에서 target point까지 이어지는 line marker를 publish한다.
  - nearest_index, target_index, progress_ratio, goal_reached 상태를 text marker로 표시한다.

좌표계:
  - 모든 debug 출력은 global_frame 기준이다.
  - 현재 프로젝트에서는 global_frame = "mission_map"으로 사용한다.
*/

#include "spot_navigation/navigation_debug_visualizer_node.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <sstream>

namespace spot_navigation
{

NavigationDebugVisualizerNode::NavigationDebugVisualizerNode()
: Node("navigation_debug_visualizer_node")
{
  // [1] YAML 파라미터 선언 및 읽기.
  loadParameters();

  /*
  [2] subscriber 생성.

  global path:
    mock_global_path_publisher_node가 transient_local로 publish할 수 있으므로
    visualizer도 transient_local subscriber로 맞춰 늦게 실행되어도 path를 받을 수 있게 한다.

  pose:
    실시간 상태값이므로 최신 pose를 계속 받는 것이 중요하다.

  path progress:
    tracker가 RELIABLE로 publish하므로 RELIABLE subscriber로 받는다.
  */
  auto path_qos = rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local();
  auto pose_qos = rclcpp::QoS(rclcpp::KeepLast(10)).best_effort();
  auto progress_qos = rclcpp::QoS(rclcpp::KeepLast(10)).reliable();

  global_path_sub_ =
    this->create_subscription<GlobalPathMsg>(
      global_path_topic_,
      path_qos,
      std::bind(&NavigationDebugVisualizerNode::globalPathCallback, this, std::placeholders::_1)
    );

  pose_sub_ =
    this->create_subscription<LocalizedPoseMsg>(
      localization_pose_topic_,
      pose_qos,
      std::bind(&NavigationDebugVisualizerNode::poseCallback, this, std::placeholders::_1)
    );

  path_progress_sub_ =
    this->create_subscription<PathProgressMsg>(
      path_progress_topic_,
      progress_qos,
      std::bind(&NavigationDebugVisualizerNode::pathProgressCallback, this, std::placeholders::_1)
    );

  /*
  [3] publisher 생성.

  debug global path는 RViz를 늦게 켜도 마지막 path가 보이도록 transient_local로 둔다.
  pose와 marker는 계속 갱신되는 디버그 정보이므로 일반 reliable QoS를 사용한다.
  */
  auto debug_path_qos = rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local();
  auto debug_pose_qos = rclcpp::QoS(rclcpp::KeepLast(10)).reliable();
  auto debug_marker_qos = rclcpp::QoS(rclcpp::KeepLast(10)).reliable();

  debug_path_pub_ =
    this->create_publisher<nav_msgs::msg::Path>(
      debug_global_path_topic_,
      debug_path_qos
    );

  debug_pose_pub_ =
    this->create_publisher<geometry_msgs::msg::PoseStamped>(
      debug_pose_topic_,
      debug_pose_qos
    );

  debug_marker_pub_ =
    this->create_publisher<visualization_msgs::msg::MarkerArray>(
      debug_marker_topic_,
      debug_marker_qos
    );

  RCLCPP_INFO(
    this->get_logger(),
    "navigation_debug_visualizer_node started. path=%s, pose=%s, progress=%s",
    global_path_topic_.c_str(),
    localization_pose_topic_.c_str(),
    path_progress_topic_.c_str()
  );

  RCLCPP_INFO(
    this->get_logger(),
    "debug outputs. rviz_path=%s, rviz_pose=%s, rviz_markers=%s, frame=%s",
    debug_global_path_topic_.c_str(),
    debug_pose_topic_.c_str(),
    debug_marker_topic_.c_str(),
    global_frame_.c_str()
  );
}

void NavigationDebugVisualizerNode::loadParameters()
{
  robot_id_ =
    this->declare_parameter<std::string>(
      "robot_id",
      "spot_01"
    );

  global_frame_ =
    this->declare_parameter<std::string>(
      "global_frame",
      "mission_map"
    );

  global_path_topic_ =
    this->declare_parameter<std::string>(
      "global_path_topic",
      "/planning/global_path/spot_01"
    );

  localization_pose_topic_ =
    this->declare_parameter<std::string>(
      "localization_pose_topic",
      "/localization/pose"
    );

  path_progress_topic_ =
    this->declare_parameter<std::string>(
      "path_progress_topic",
      "/navigation/path_progress/spot_01"
    );

  debug_global_path_topic_ =
    this->declare_parameter<std::string>(
      "debug_global_path_topic",
      "/debug/navigation/global_path"
    );

  debug_pose_topic_ =
    this->declare_parameter<std::string>(
      "debug_pose_topic",
      "/debug/navigation/pose"
    );

  debug_marker_topic_ =
    this->declare_parameter<std::string>(
      "debug_marker_topic",
      "/debug/navigation/progress_markers"
    );

  marker_namespace_ =
    this->declare_parameter<std::string>(
      "marker_namespace",
      "navigation_debug"
    );

  waypoint_marker_scale_m_ =
    this->declare_parameter<double>(
      "waypoint_marker_scale_m",
      0.18
    );

  target_line_width_m_ =
    this->declare_parameter<double>(
      "target_line_width_m",
      0.04
    );

  text_marker_scale_m_ =
    this->declare_parameter<double>(
      "text_marker_scale_m",
      0.25
    );

  marker_lifetime_sec_ =
    this->declare_parameter<double>(
      "marker_lifetime_sec",
      0.0
    );

  marker_z_m_ =
    this->declare_parameter<double>(
      "marker_z_m",
      0.15
    );

  if (waypoint_marker_scale_m_ <= 0.0) {
    RCLCPP_WARN(
      this->get_logger(),
      "waypoint_marker_scale_m must be positive. Reset to 0.18 m."
    );
    waypoint_marker_scale_m_ = 0.18;
  }

  if (target_line_width_m_ <= 0.0) {
    RCLCPP_WARN(
      this->get_logger(),
      "target_line_width_m must be positive. Reset to 0.04 m."
    );
    target_line_width_m_ = 0.04;
  }

  if (text_marker_scale_m_ <= 0.0) {
    RCLCPP_WARN(
      this->get_logger(),
      "text_marker_scale_m must be positive. Reset to 0.25 m."
    );
    text_marker_scale_m_ = 0.25;
  }

  if (marker_lifetime_sec_ < 0.0) {
    RCLCPP_WARN(
      this->get_logger(),
      "marker_lifetime_sec must be non-negative. Reset to 0.0 sec."
    );
    marker_lifetime_sec_ = 0.0;
  }
}

void NavigationDebugVisualizerNode::globalPathCallback(
  const GlobalPathMsg::SharedPtr msg)
{
  if (!isRobotAndFrameMatched(msg->robot_id, msg->header.frame_id)) {
    RCLCPP_WARN_THROTTLE(
      this->get_logger(),
      *this->get_clock(),
      2000,
      "Ignore global path. robot_id=%s frame=%s expected_robot=%s expected_frame=%s",
      msg->robot_id.c_str(),
      msg->header.frame_id.c_str(),
      robot_id_.c_str(),
      global_frame_.c_str()
    );
    return;
  }

  if (msg->waypoints.empty()) {
    RCLCPP_WARN_THROTTLE(
      this->get_logger(),
      *this->get_clock(),
      2000,
      "Ignore empty global path."
    );
    return;
  }

  latest_path_ = msg;
  path_received_ = true;

  publishDebugPath(*msg);
  publishProgressMarkers();

  RCLCPP_INFO_THROTTLE(
    this->get_logger(),
    *this->get_clock(),
    3000,
    "Debug global path published. waypoints=%zu",
    msg->waypoints.size()
  );
}

void NavigationDebugVisualizerNode::poseCallback(
  const LocalizedPoseMsg::SharedPtr msg)
{
  if (!isRobotAndFrameMatched(msg->robot_id, msg->header.frame_id)) {
    RCLCPP_WARN_THROTTLE(
      this->get_logger(),
      *this->get_clock(),
      2000,
      "Ignore localization pose. robot_id=%s frame=%s expected_robot=%s expected_frame=%s",
      msg->robot_id.c_str(),
      msg->header.frame_id.c_str(),
      robot_id_.c_str(),
      global_frame_.c_str()
    );
    return;
  }

  latest_pose_ = msg;
  pose_received_ = true;

  publishDebugPose(*msg);
  publishProgressMarkers();
}

void NavigationDebugVisualizerNode::pathProgressCallback(
  const PathProgressMsg::SharedPtr msg)
{
  if (!isRobotAndFrameMatched(msg->robot_id, msg->header.frame_id)) {
    RCLCPP_WARN_THROTTLE(
      this->get_logger(),
      *this->get_clock(),
      2000,
      "Ignore path progress. robot_id=%s frame=%s expected_robot=%s expected_frame=%s",
      msg->robot_id.c_str(),
      msg->header.frame_id.c_str(),
      robot_id_.c_str(),
      global_frame_.c_str()
    );
    return;
  }

  latest_progress_ = msg;
  progress_received_ = true;

  publishProgressMarkers();
}

void NavigationDebugVisualizerNode::publishDebugPath(
  const GlobalPathMsg & msg)
{
  nav_msgs::msg::Path path_msg;

  path_msg.header.stamp = this->now();
  path_msg.header.frame_id = global_frame_;

  path_msg.poses.reserve(msg.waypoints.size());

  for (const auto & waypoint : msg.waypoints) {
    geometry_msgs::msg::PoseStamped pose_stamped;

    pose_stamped.header.stamp = path_msg.header.stamp;
    pose_stamped.header.frame_id = global_frame_;

    pose_stamped.pose.position.x = static_cast<double>(waypoint.x_m);
    pose_stamped.pose.position.y = static_cast<double>(waypoint.y_m);

    pose_stamped.pose.position.z =
      std::isfinite(static_cast<double>(waypoint.z_m)) ?
      static_cast<double>(waypoint.z_m) :
      marker_z_m_;

    pose_stamped.pose.orientation =
      yawToQuaternion(static_cast<double>(waypoint.yaw_rad));

    path_msg.poses.push_back(pose_stamped);
  }

  debug_path_pub_->publish(path_msg);
}

void NavigationDebugVisualizerNode::publishDebugPose(
  const LocalizedPoseMsg & msg)
{
  geometry_msgs::msg::PoseStamped pose_stamped;

  pose_stamped.header.stamp = this->now();
  pose_stamped.header.frame_id = global_frame_;

  pose_stamped.pose.position.x = static_cast<double>(msg.x_m);
  pose_stamped.pose.position.y = static_cast<double>(msg.y_m);
  pose_stamped.pose.position.z = static_cast<double>(msg.z_m);

  pose_stamped.pose.orientation =
    yawToQuaternion(static_cast<double>(msg.yaw_rad));

  debug_pose_pub_->publish(pose_stamped);
}

void NavigationDebugVisualizerNode::publishProgressMarkers()
{
  if (!progress_received_ || !latest_progress_) {
    return;
  }

  const auto & progress = *latest_progress_;

  visualization_msgs::msg::MarkerArray marker_array;

  const auto now = this->now();

  /*
  marker id 규칙:
    0: nearest waypoint sphere
    1: target waypoint sphere
    2: goal waypoint sphere
    3: current pose -> target point line
    4: status text
  */

  const auto nearest_color = makeColor(0.0f, 1.0f, 0.0f, 1.0f);
  const auto target_color = makeColor(1.0f, 1.0f, 0.0f, 1.0f);
  const auto goal_color = makeColor(1.0f, 0.0f, 0.0f, 1.0f);
  const auto line_color = makeColor(0.0f, 0.4f, 1.0f, 1.0f);
  const auto text_color = makeColor(1.0f, 1.0f, 1.0f, 1.0f);

  marker_array.markers.push_back(
    makeSphereMarker(
      0,
      "nearest_waypoint",
      static_cast<double>(progress.nearest_x_m),
      static_cast<double>(progress.nearest_y_m),
      marker_z_m_,
      waypoint_marker_scale_m_,
      nearest_color
    )
  );

  marker_array.markers.push_back(
    makeSphereMarker(
      1,
      "target_waypoint",
      static_cast<double>(progress.target_x_m),
      static_cast<double>(progress.target_y_m),
      marker_z_m_,
      waypoint_marker_scale_m_,
      target_color
    )
  );

  /*
  goal marker는 PathProgress에 goal_x/goal_y가 없으므로,
  최신 global path의 마지막 waypoint를 사용한다.
  */
  if (path_received_ && latest_path_ && !latest_path_->waypoints.empty()) {
    const auto & goal_wp = latest_path_->waypoints.back();

    marker_array.markers.push_back(
      makeSphereMarker(
        2,
        "goal_waypoint",
        static_cast<double>(goal_wp.x_m),
        static_cast<double>(goal_wp.y_m),
        marker_z_m_,
        waypoint_marker_scale_m_ * 1.2,
        goal_color
      )
    );
  }

  /*
  현재 pose에서 target waypoint까지 이어지는 line marker.
  latest_pose_가 아직 없다면 line은 생략한다.
  */
  if (pose_received_ && latest_pose_) {
    geometry_msgs::msg::Point start_point;
    start_point.x = static_cast<double>(latest_pose_->x_m);
    start_point.y = static_cast<double>(latest_pose_->y_m);
    start_point.z = marker_z_m_;

    geometry_msgs::msg::Point end_point;
    end_point.x = static_cast<double>(progress.target_x_m);
    end_point.y = static_cast<double>(progress.target_y_m);
    end_point.z = marker_z_m_;

    marker_array.markers.push_back(
      makeLineMarker(
        3,
        "pose_to_target_line",
        start_point,
        end_point,
        target_line_width_m_,
        line_color
      )
    );
  }

  /*
  상태 text marker.
  pose가 있으면 현재 pose 위쪽에 표시하고,
  pose가 아직 없으면 target point 위쪽에 표시한다.
  */
  double text_x = static_cast<double>(progress.target_x_m);
  double text_y = static_cast<double>(progress.target_y_m);

  if (pose_received_ && latest_pose_) {
    text_x = static_cast<double>(latest_pose_->x_m);
    text_y = static_cast<double>(latest_pose_->y_m);
  }

  std::ostringstream text_stream;
  text_stream
    << "nearest: " << progress.nearest_index
    << "\ntarget: " << progress.target_index
    << "\nprogress: " << static_cast<int>(progress.progress_ratio * 100.0f) << "%"
    << "\ngoal: " << (progress.goal_reached ? "true" : "false");

  marker_array.markers.push_back(
    makeTextMarker(
      4,
      "progress_text",
      text_x,
      text_y,
      marker_z_m_ + 0.45,
      text_stream.str(),
      text_marker_scale_m_,
      text_color
    )
  );

  /*
  모든 marker의 stamp를 현재 시간으로 통일한다.
  makeSphereMarker/makeLineMarker/makeTextMarker 내부에서도 header를 채우지만,
  이곳에서 한 번 더 동일 stamp로 맞춘다.
  */
  for (auto & marker : marker_array.markers) {
    marker.header.stamp = now;
  }

  debug_marker_pub_->publish(marker_array);
}

visualization_msgs::msg::Marker NavigationDebugVisualizerNode::makeSphereMarker(
  int marker_id,
  const std::string & marker_name,
  double x,
  double y,
  double z,
  double scale_m,
  const std_msgs::msg::ColorRGBA & color) const
{
  visualization_msgs::msg::Marker marker;

  marker.header.frame_id = global_frame_;
  marker.header.stamp = this->now();

  marker.ns = marker_namespace_ + "/" + marker_name;
  marker.id = marker_id;

  marker.type = visualization_msgs::msg::Marker::SPHERE;
  marker.action = visualization_msgs::msg::Marker::ADD;

  marker.pose.position.x = x;
  marker.pose.position.y = y;
  marker.pose.position.z = z;
  marker.pose.orientation.w = 1.0;

  marker.scale.x = scale_m;
  marker.scale.y = scale_m;
  marker.scale.z = scale_m;

  marker.color = color;
  marker.lifetime = makeMarkerLifetime();

  return marker;
}

visualization_msgs::msg::Marker NavigationDebugVisualizerNode::makeLineMarker(
  int marker_id,
  const std::string & marker_name,
  const geometry_msgs::msg::Point & start_point,
  const geometry_msgs::msg::Point & end_point,
  double line_width_m,
  const std_msgs::msg::ColorRGBA & color) const
{
  visualization_msgs::msg::Marker marker;

  marker.header.frame_id = global_frame_;
  marker.header.stamp = this->now();

  marker.ns = marker_namespace_ + "/" + marker_name;
  marker.id = marker_id;

  marker.type = visualization_msgs::msg::Marker::LINE_STRIP;
  marker.action = visualization_msgs::msg::Marker::ADD;

  marker.pose.orientation.w = 1.0;

  marker.points.push_back(start_point);
  marker.points.push_back(end_point);

  marker.scale.x = line_width_m;

  marker.color = color;
  marker.lifetime = makeMarkerLifetime();

  return marker;
}

visualization_msgs::msg::Marker NavigationDebugVisualizerNode::makeTextMarker(
  int marker_id,
  const std::string & marker_name,
  double x,
  double y,
  double z,
  const std::string & text,
  double text_scale_m,
  const std_msgs::msg::ColorRGBA & color) const
{
  visualization_msgs::msg::Marker marker;

  marker.header.frame_id = global_frame_;
  marker.header.stamp = this->now();

  marker.ns = marker_namespace_ + "/" + marker_name;
  marker.id = marker_id;

  marker.type = visualization_msgs::msg::Marker::TEXT_VIEW_FACING;
  marker.action = visualization_msgs::msg::Marker::ADD;

  marker.pose.position.x = x;
  marker.pose.position.y = y;
  marker.pose.position.z = z;
  marker.pose.orientation.w = 1.0;

  /*
  TEXT_VIEW_FACING marker는 scale.z가 글자 크기 역할을 한다.
  scale.x, scale.y는 보통 사용하지 않는다.
  */
  marker.scale.z = text_scale_m;

  marker.color = color;
  marker.text = text;
  marker.lifetime = makeMarkerLifetime();

  return marker;
}

std_msgs::msg::ColorRGBA NavigationDebugVisualizerNode::makeColor(
  float r,
  float g,
  float b,
  float a) const
{
  std_msgs::msg::ColorRGBA color;

  color.r = r;
  color.g = g;
  color.b = b;
  color.a = a;

  return color;
}

geometry_msgs::msg::Quaternion
NavigationDebugVisualizerNode::yawToQuaternion(double yaw_rad) const
{
  geometry_msgs::msg::Quaternion q;

  q.x = 0.0;
  q.y = 0.0;
  q.z = std::sin(yaw_rad * 0.5);
  q.w = std::cos(yaw_rad * 0.5);

  return q;
}

builtin_interfaces::msg::Duration
NavigationDebugVisualizerNode::makeMarkerLifetime() const
{
  builtin_interfaces::msg::Duration duration;

  if (marker_lifetime_sec_ <= 0.0) {
    duration.sec = 0;
    duration.nanosec = 0;
    return duration;
  }

  const double sec_floor = std::floor(marker_lifetime_sec_);

  duration.sec = static_cast<int32_t>(sec_floor);
  duration.nanosec =
    static_cast<uint32_t>((marker_lifetime_sec_ - sec_floor) * 1.0e9);

  return duration;
}

bool NavigationDebugVisualizerNode::isRobotAndFrameMatched(
  const std::string & msg_robot_id,
  const std::string & msg_frame_id) const
{
  return msg_robot_id == robot_id_ && msg_frame_id == global_frame_;
}

}  // namespace spot_navigation

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  auto node = std::make_shared<spot_navigation::NavigationDebugVisualizerNode>();
  rclcpp::spin(node);

  rclcpp::shutdown();
  return 0;
}