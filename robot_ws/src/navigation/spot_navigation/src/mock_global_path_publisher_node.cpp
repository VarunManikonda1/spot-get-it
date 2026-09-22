/*
- 경로: spot_navigation/src/mock_global_path_publisher_node.cpp
- 역할:
    팀원이 만든 planning 노드의 /planning/global_path/spot_01 출력을 더미 데이터로 재현한다.
- 입력 토픽: X
- 출력 토픽:
    - /planning/global_path/spot_01 (robot_interfaces/msg/GlobalPathWaypoints)
- 주요 기능:
    - config/mock_global_path_publisher.param.yaml에서 빨간색 global path waypoint를 읽는다.
    - 각 waypoint를 LocalizedRobotPose 메시지로 변환한다.
    - yaw_rad를 quaternion orientation으로 변환해 pose.orientation에 채운다.
    - 일정 주기로 mock global path를 publish한다.
- 좌표계:
    - 첨부된 실제 planning 데이터 기준 frame_id는 "map"이다.
    - path_progress_tracker_node와 localization pose도 같은 frame을 사용해야 한다.
 */

#include "spot_navigation/mock_global_path_publisher_node.hpp"

#include <chrono>
#include <cmath>
#include <stdexcept>

#include "robot_interfaces/msg/localized_robot_pose.hpp"

using namespace std::chrono_literals;

namespace spot_navigation
{

MockGlobalPathPublisherNode::MockGlobalPathPublisherNode()
: Node("mock_global_path_publisher_node")
{
  // [1] YAML 파라미터 선언 및 읽기
  loadParameters();

  // [2] waypoint 배열 길이 검증
  //     x, y, yaw 배열 길이가 다르면 잘못된 path가 생성되므로 노드 시작 시점에 바로 잡는다.
  validateWaypoints();

  // [3] GlobalPathWaypoints publisher 생성
  //     global path는 자주 바뀌지 않는 기준 데이터에 가깝기 때문에 transient_local QoS를 사용한다.
  //     이 설정을 사용하면 subscriber가 나중에 실행되어도 마지막으로 publish된 path를 받을 수 있다.
  auto qos = rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local();

  global_path_pub_ =
    this->create_publisher<robot_interfaces::msg::GlobalPathWaypoints>(
      global_path_topic_,
      qos
    );

  // [4] publish 주기 설정
  //     publish_rate_hz_가 1.0이면 1초마다 global path를 반복 publish한다.
  const auto publish_period =
    std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::duration<double>(1.0 / publish_rate_hz_)
    );

  publish_timer_ = this->create_wall_timer(
    publish_period,
    std::bind(&MockGlobalPathPublisherNode::publishGlobalPath, this)
  );

  // [5] 노드 시작 직후 1회 publish
  //     timer 첫 실행까지 기다리지 않고, launch 직후 topic echo나 subscriber 검증이 가능하게 한다.
  publishGlobalPath();

  RCLCPP_INFO(
    this->get_logger(),
    "mock_global_path_publisher_node started. topic=%s, robot_id=%s, frame=%s, waypoints=%zu",
    global_path_topic_.c_str(),
    robot_id_.c_str(),
    map_frame_.c_str(),
    waypoint_x_.size()
  );
}

void MockGlobalPathPublisherNode::loadParameters()
{
  // -------------------------
  // [1] 파라미터 선언
  // -------------------------
  // declare_parameter는 YAML에 값이 없을 때 사용할 기본값도 함께 정의한다.

  this->declare_parameter<std::string>("robot_id", "spot_01");
  this->declare_parameter<std::string>("global_path_topic", "/planning/global_path/spot_01");
  this->declare_parameter<std::string>("map_frame", "map");
  this->declare_parameter<std::string>("base_frame", "base_link");

  this->declare_parameter<double>("default_z_m", 0.05);
  this->declare_parameter<double>("publish_rate_hz", 1.0);

  this->declare_parameter<std::vector<double>>("waypoint_x", std::vector<double>{});
  this->declare_parameter<std::vector<double>>("waypoint_y", std::vector<double>{});
  this->declare_parameter<std::vector<double>>("waypoint_yaw_rad", std::vector<double>{});

  // -------------------------
  // [2] 파라미터 읽기
  // -------------------------

  robot_id_ = this->get_parameter("robot_id").as_string();
  global_path_topic_ = this->get_parameter("global_path_topic").as_string();
  map_frame_ = this->get_parameter("map_frame").as_string();
  base_frame_ = this->get_parameter("base_frame").as_string();

  default_z_m_ = this->get_parameter("default_z_m").as_double();
  publish_rate_hz_ = this->get_parameter("publish_rate_hz").as_double();

  waypoint_x_ = this->get_parameter("waypoint_x").as_double_array();
  waypoint_y_ = this->get_parameter("waypoint_y").as_double_array();
  waypoint_yaw_rad_ = this->get_parameter("waypoint_yaw_rad").as_double_array();

  // -------------------------
  // [3] 파라미터 1차 보정
  // -------------------------
  // publish_rate_hz_가 0 이하이면 timer period 계산에서 문제가 생기므로 기본값으로 보정한다.
  if (publish_rate_hz_ <= 0.0) {
    RCLCPP_WARN(
      this->get_logger(),
      "publish_rate_hz must be positive. Reset to 1.0 Hz."
    );
    publish_rate_hz_ = 1.0;
  }
}

void MockGlobalPathPublisherNode::validateWaypoints() const
{
  // waypoint가 하나도 없으면 global path로 사용할 수 없으므로 예외 처리한다.
  if (waypoint_x_.empty()) {
    throw std::runtime_error("waypoint_x is empty. Mock global path requires at least one waypoint.");
  }

  // x, y 배열은 반드시 같은 길이를 가져야 한다.
  if (waypoint_x_.size() != waypoint_y_.size()) {
    throw std::runtime_error(
      "waypoint_x and waypoint_y size mismatch. "
      "Each waypoint must have both x and y."
    );
  }

  // yaw 배열도 waypoint 개수와 같아야 한다.
  // yaw가 없으면 path 방향을 자동 계산하는 방식도 가능하지만,
  // 현재 팀원 데이터에는 yaw_rad가 포함되어 있으므로 명시적으로 맞추는 방식을 사용한다.
  if (waypoint_x_.size() != waypoint_yaw_rad_.size()) {
    throw std::runtime_error(
      "waypoint_yaw_rad size mismatch. "
      "Each waypoint must have yaw_rad to match GlobalPathWaypoints echo format."
    );
  }
}

geometry_msgs::msg::Quaternion MockGlobalPathPublisherNode::yawToQuaternion(
  double yaw_rad) const
{
  // roll=0, pitch=0, yaw만 있는 2D 주행 방향 quaternion.
  // qz = sin(yaw/2), qw = cos(yaw/2) 형태를 사용한다.
  geometry_msgs::msg::Quaternion q;

  q.x = 0.0;
  q.y = 0.0;
  q.z = std::sin(yaw_rad * 0.5);
  q.w = std::cos(yaw_rad * 0.5);

  return q;
}

void MockGlobalPathPublisherNode::publishGlobalPath()
{
  // [1] 현재 시각 생성
  //     GlobalPathWaypoints.header와 각 waypoint.header에 같은 stamp를 넣어
  //     하나의 path 메시지가 같은 시점에 생성되었음을 나타낸다.
  const auto now = this->now();

  // [2] 출력 메시지 기본 필드 채우기
  robot_interfaces::msg::GlobalPathWaypoints path_msg;

  path_msg.header.stamp = now;
  path_msg.header.frame_id = map_frame_;
  path_msg.robot_id = robot_id_;

  // [3] waypoint 배열 메모리 미리 확보
  //     push_back 반복 중 불필요한 재할당을 줄이기 위함이다.
  path_msg.waypoints.reserve(waypoint_x_.size());

  // [4] YAML waypoint 배열을 LocalizedRobotPose[]로 변환
  for (std::size_t i = 0; i < waypoint_x_.size(); ++i) {
    robot_interfaces::msg::LocalizedRobotPose waypoint;

    const double x = waypoint_x_[i];
    const double y = waypoint_y_[i];
    const double z = default_z_m_;
    const double yaw = waypoint_yaw_rad_[i];

    // waypoint.header는 전체 path header와 동일한 frame/stamp를 사용한다.
    waypoint.header = path_msg.header;

    // 각 waypoint가 어떤 로봇의 경로에 속하는지 표시한다.
    waypoint.robot_id = robot_id_;

    // 이 waypoint pose가 의미하는 로봇 기준 frame.
    // 실제 로봇 pose의 body frame과 맞추기 위해 base_link를 사용한다.
    waypoint.base_frame = base_frame_;

    // mission/map frame 기준 위치와 heading.
    waypoint.x_m = static_cast<float>(x);
    waypoint.y_m = static_cast<float>(y);
    waypoint.z_m = static_cast<float>(z);
    waypoint.yaw_rad = static_cast<float>(yaw);

    // ROS 표준 Pose도 동일한 위치와 heading으로 채운다.
    // 나중에 RViz, path 변환, 표준 geometry 처리에 재사용하기 좋다.
    waypoint.pose.position.x = x;
    waypoint.pose.position.y = y;
    waypoint.pose.position.z = z;
    waypoint.pose.orientation = yawToQuaternion(yaw);

    path_msg.waypoints.push_back(waypoint);
  }

  // [5] GlobalPathWaypoints publish
  global_path_pub_->publish(path_msg);

  // [6] 디버그 로그
  //     너무 자주 출력되지 않도록 throttle을 적용한다.
  RCLCPP_INFO_THROTTLE(
    this->get_logger(),
    *this->get_clock(),
    2000,
    "\nmock global path"
    "\n  topic      : %s"
    "\n  robot_id   : %s"
    "\n  frame_id   : %s"
    "\n  waypoints  : %zu"
    "\n  start      : (%.2f, %.2f, %.2f)"
    "\n  goal       : (%.2f, %.2f, %.2f)",
    global_path_topic_.c_str(),
    robot_id_.c_str(),
    map_frame_.c_str(),
    path_msg.waypoints.size(),
    waypoint_x_.front(),
    waypoint_y_.front(),
    default_z_m_,
    waypoint_x_.back(),
    waypoint_y_.back(),
    default_z_m_
  );
}

}  // namespace spot_navigation

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  auto node = std::make_shared<spot_navigation::MockGlobalPathPublisherNode>();
  rclcpp::spin(node);

  rclcpp::shutdown();
  return 0;
}