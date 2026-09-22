/*
 * 경로: spot_navigation/include/spot_navigation/mock_localization_pose_publisher_node.hpp
 * 역할:
 *   실제 localization node가 아직 동작하지 않는 상황에서,
 *   mock global path를 따라 로봇 pose가 연속적으로 움직이는 것처럼
 *   /localization/pose를 publish하는 mock localization 노드 선언부.
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
 *   - GlobalPathWaypoints를 수신해 waypoint 목록을 내부에 저장한다.
 *   - 현재 segment index와 segment 내 이동 거리를 기반으로 pose를 선형 보간한다.
 *   - publish_rate_hz 주기로 자연스럽게 이동하는 localization pose를 publish한다.
 *   - yaw는 이동 segment 방향 또는 waypoint yaw 보간 방식으로 생성할 수 있다.
 *
 * 사용 목적:
 *   - 실제 제어/보행/localization이 완성되기 전,
 *     path_progress_tracker_node를 단독 검증하기 위한 더미 입력 제공.
 */

#ifndef SPOT_NAVIGATION__MOCK_LOCALIZATION_POSE_PUBLISHER_NODE_HPP_
#define SPOT_NAVIGATION__MOCK_LOCALIZATION_POSE_PUBLISHER_NODE_HPP_

#include <cstddef>
#include <string>
#include <vector>
#include <random>

#include "geometry_msgs/msg/quaternion.hpp"
#include "rclcpp/rclcpp.hpp"
#include "robot_interfaces/msg/global_path_waypoints.hpp"
#include "robot_interfaces/msg/localized_robot_pose.hpp"

namespace spot_navigation
{

class MockLocalizationPosePublisherNode : public rclcpp::Node
{
public:
  /*
   * 생성자 역할:
   *   - 파라미터를 선언하고 읽는다.
   *   - mock global path subscriber를 생성한다.
   *   - localization pose publisher를 생성한다.
   *   - timer를 생성해 일정 주기로 pose를 publish한다.
   */
  MockLocalizationPosePublisherNode();

private:
  /*
   * 내부 waypoint 표현 구조체.
   * GlobalPathWaypoints의 waypoint 중 mock pose 생성에 필요한 값만 보관한다.
   */
  struct Waypoint2D
  {
    double x_m;
    double y_m;
    double z_m;
    double yaw_rad;
  };

  void loadParameters();

  void globalPathCallback(
    const robot_interfaces::msg::GlobalPathWaypoints::SharedPtr msg);

  void publishMockPose();

  void advanceAlongPath(double delta_distance_m);

  robot_interfaces::msg::LocalizedRobotPose makeCurrentPoseMsg(
    const rclcpp::Time & stamp);

  double sampleGaussianNoise(
    double stddev,
    double max_abs_value);

  void initializeNoiseGenerator();

  double calculateDistance2D(
    double x1, double y1,
    double x2, double y2) const;

  double normalizeAngle(double angle_rad) const;

  double interpolateAngle(
    double from_rad,
    double to_rad,
    double ratio) const;

  geometry_msgs::msg::Quaternion yawToQuaternion(double yaw_rad) const;

  bool hasValidPath() const;

  rclcpp::Subscription<robot_interfaces::msg::GlobalPathWaypoints>::SharedPtr global_path_sub_;
  rclcpp::Publisher<robot_interfaces::msg::LocalizedRobotPose>::SharedPtr pose_pub_;
  rclcpp::TimerBase::SharedPtr publish_timer_;

  std::string robot_id_;
  std::string global_path_topic_;
  std::string localization_pose_topic_;
  std::string global_frame_;
  std::string base_frame_;

  double default_z_m_;
  double publish_rate_hz_;
  double linear_speed_mps_;

  bool loop_path_;
  bool hold_at_goal_;
  bool use_segment_yaw_;
  bool reset_on_new_path_;

  bool path_received_;
  bool path_valid_;
  bool goal_reached_;

  std::vector<Waypoint2D> waypoints_;

  std::size_t current_segment_index_;
  double distance_along_segment_m_;

  rclcpp::Time last_update_time_;
  bool last_update_time_initialized_;

  // publish되는 mock pose에 노이즈를 섞을지 여부.
  bool noise_enabled_;

  // x, y 위치 Gaussian noise 표준편차 [m].
  double position_noise_std_m_;

  // yaw Gaussian noise 표준편차 [rad].
  double yaw_noise_std_rad_;

  // x, y 위치 noise 최대 절댓값 [m].
  // Gaussian noise가 너무 크게 튀는 것을 막기 위한 clamp 값이다.
  double max_position_noise_m_;

  // yaw noise 최대 절댓값 [rad].
  double max_yaw_noise_rad_;

  // noise 재현성을 위한 seed.
  // -1이면 random_device 기반, 0 이상이면 고정 seed 기반으로 동작한다.
  int noise_seed_;

  // Gaussian noise 생성을 위한 랜덤 엔진.
  std::mt19937 random_engine_;

  // 평균 0, 표준편차 1인 표준정규분포.
  // 실제 표준편차는 sampleGaussianNoise()에서 곱해서 사용한다.
  std::normal_distribution<double> normal_distribution_;
};

}  // namespace spot_navigation

#endif  // SPOT_NAVIGATION__MOCK_LOCALIZATION_POSE_PUBLISHER_NODE_HPP_