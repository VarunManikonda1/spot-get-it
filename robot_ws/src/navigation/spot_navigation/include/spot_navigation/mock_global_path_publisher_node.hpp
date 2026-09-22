/*
- 경로: spot_navigation/include/spot_navigation/mock_global_path_publisher_node.hpp
- 역할:
    실제 planning 계층의 global_path_manager가 publish하는
    robot별 GlobalPathWaypoints 토픽을 흉내 내는 mock publisher 노드 선언부.
 
- 입력 토픽:
    - 없음
- 출력 토픽:
    - /planning/global_path/spot_01 (robot_interfaces/msg/GlobalPathWaypoints)
- 주요 기능:
    - YAML 파라미터에서 robot_id, frame_id, waypoint 좌표 목록을 읽는다.
    - waypoint_x / waypoint_y / waypoint_yaw_rad 배열을 내부 데이터로 보관한다.
    - timer 주기마다 GlobalPathWaypoints 메시지를 생성해 publish한다.
    - path_progress_tracker_node와 local_path_planner_node의 단독 검증용 입력을 제공한다.
- 주의:
    - 이 노드는 실제 global planner가 아니다.
    - 실제 planning 노드가 완성되면 이 mock node는 끄고,
      동일한 토픽을 실제 planning 노드가 publish하도록 교체한다.*/

#ifndef SPOT_NAVIGATION__MOCK_GLOBAL_PATH_PUBLISHER_NODE_HPP_
#define SPOT_NAVIGATION__MOCK_GLOBAL_PATH_PUBLISHER_NODE_HPP_

#include <string>
#include <vector>

#include "geometry_msgs/msg/quaternion.hpp"
#include "rclcpp/rclcpp.hpp"
#include "robot_interfaces/msg/global_path_waypoints.hpp"

namespace spot_navigation
{

class MockGlobalPathPublisherNode : public rclcpp::Node
{
public:
  /*
   * 생성자 역할:
   *   - ROS 2 노드 이름을 설정한다.
   *   - YAML 파라미터를 선언하고 읽는다.
   *   - waypoint 배열 길이를 검증한다.
   *   - GlobalPathWaypoints publisher와 timer를 생성한다.
   */
  MockGlobalPathPublisherNode();

private:
  // YAML 파라미터를 선언하고 읽는 함수
  void loadParameters();

  // waypoint_x, waypoint_y, waypoint_yaw_rad 배열 길이를 검증하는 함수
  void validateWaypoints() const;

  // timer 주기마다 GlobalPathWaypoints 메시지를 publish하는 함수.
  void publishGlobalPath();

  /*
   * yaw angle을 quaternion으로 변환하는 함수.
   *
   * 변환 이유:
   *   - LocalizedRobotPose.msg는 yaw_rad와 함께 geometry_msgs/Pose도 포함한다.
   *   - pose.orientation에는 yaw_rad에 대응하는 quaternion 값을 넣어야 한다.
   */
  geometry_msgs::msg::Quaternion yawToQuaternion(double yaw_rad) const;

  // -------------------------
  // [1] ROS 통신 객체
  // -------------------------

  // mock global path를 publish하는 publisher.
  // QoS는 transient_local을 사용해, 늦게 실행된 subscriber도 마지막 path를 받을 수 있게 한다.
  rclcpp::Publisher<robot_interfaces::msg::GlobalPathWaypoints>::SharedPtr global_path_pub_;

  // publish_rate_hz_ 주기로 global path를 반복 publish하는 timer.
  rclcpp::TimerBase::SharedPtr publish_timer_;

  // -------------------------
  // [2] Topic / frame 파라미터
  // -------------------------

  // 이 global path가 할당될 로봇 ID.
  // 예: "spot_01"
  std::string robot_id_;

  // GlobalPathWaypoints를 publish할 토픽 이름.
  // 예: "/planning/global_path/spot_01"
  std::string global_path_topic_;

  // global path의 기준 좌표계.
  // 팀원 planning 데이터 기준으로는 "map"을 사용한다.
  std::string map_frame_;

  // 각 waypoint가 의미하는 로봇 body frame.
  // 일반적으로 "base_link"를 사용한다.
  std::string base_frame_;

  // 모든 waypoint에 공통으로 넣을 z 위치.
  // 현재 2D global path이므로 z는 0.05m 고정값으로 사용한다.
  double default_z_m_;

  // global path publish 주기 [Hz].
  double publish_rate_hz_;

  // -------------------------
  // [3] Mock global path waypoint 데이터
  // -------------------------

  // waypoint별 x 좌표 목록 [m].
  // map_frame_ 기준이다.
  std::vector<double> waypoint_x_;

  // waypoint별 y 좌표 목록 [m].
  // map_frame_ 기준이다.
  std::vector<double> waypoint_y_;

  // waypoint별 yaw 방향 [rad].
  // map_frame_ 기준 heading이다.
  std::vector<double> waypoint_yaw_rad_;
};

}  // namespace spot_navigation

#endif  // SPOT_NAVIGATION__MOCK_GLOBAL_PATH_PUBLISHER_NODE_HPP_