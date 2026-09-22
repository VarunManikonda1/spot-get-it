/*
경로:
  spot_navigation/include/spot_navigation/navigation_debug_visualizer_node.hpp

역할:
  Navigation A 파트에서 사용하는 custom message를 RViz가 표시할 수 있는
  표준 시각화 메시지로 변환하는 디버그 노드 선언부.

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
  - GlobalPathWaypoints를 nav_msgs/Path로 변환한다.
  - LocalizedRobotPose를 geometry_msgs/PoseStamped로 변환한다.
  - PathProgress의 nearest/target/goal 정보를 MarkerArray로 시각화한다.
  - 현재 pose에서 target point까지 이어지는 line marker를 생성한다.
  - nearest_index, target_index, progress_ratio, goal_reached 상태를 text marker로 표시한다.

좌표계:
  - 모든 debug 출력은 global_frame 기준으로 publish한다.
  - 현재 프로젝트에서는 global_frame = "mission_map"으로 사용한다.
*/

#ifndef SPOT_NAVIGATION__NAVIGATION_DEBUG_VISUALIZER_NODE_HPP_
#define SPOT_NAVIGATION__NAVIGATION_DEBUG_VISUALIZER_NODE_HPP_

#include <memory>
#include <string>

#include "builtin_interfaces/msg/duration.hpp"
#include "geometry_msgs/msg/point.hpp"
#include "geometry_msgs/msg/pose_stamped.hpp"
#include "geometry_msgs/msg/quaternion.hpp"
#include "nav_msgs/msg/path.hpp"
#include "rclcpp/rclcpp.hpp"
#include "robot_interfaces/msg/global_path_waypoints.hpp"
#include "robot_interfaces/msg/localized_robot_pose.hpp"
#include "robot_interfaces/msg/path_progress.hpp"
#include "std_msgs/msg/color_rgba.hpp"
#include "visualization_msgs/msg/marker.hpp"
#include "visualization_msgs/msg/marker_array.hpp"

namespace spot_navigation
{

class NavigationDebugVisualizerNode : public rclcpp::Node
{
public:
  // 생성자.
  // 파라미터 로드, subscriber/publisher 생성을 담당한다.
  NavigationDebugVisualizerNode();

private:
  // 코드 가독성을 높이기 위한 메시지 타입 별칭.
  using GlobalPathMsg = robot_interfaces::msg::GlobalPathWaypoints;
  using LocalizedPoseMsg = robot_interfaces::msg::LocalizedRobotPose;
  using PathProgressMsg = robot_interfaces::msg::PathProgress;

  // YAML 또는 launch에서 전달된 ROS 2 파라미터를 선언하고 읽는다.
  void loadParameters();

  // global path custom message 수신 callback.
  // 수신한 GlobalPathWaypoints를 nav_msgs/Path로 변환하여 RViz에 표시한다.
  void globalPathCallback(const GlobalPathMsg::SharedPtr msg);

  // localization pose custom message 수신 callback.
  // 수신한 LocalizedRobotPose를 geometry_msgs/PoseStamped로 변환하여 RViz에 표시한다.
  void poseCallback(const LocalizedPoseMsg::SharedPtr msg);

  // path progress custom message 수신 callback.
  // nearest/target/goal/text marker를 MarkerArray로 변환하여 RViz에 표시한다.
  void pathProgressCallback(const PathProgressMsg::SharedPtr msg);

  // GlobalPathWaypoints를 nav_msgs/Path로 변환하여 publish한다.
  void publishDebugPath(const GlobalPathMsg & msg);

  // LocalizedRobotPose를 geometry_msgs/PoseStamped로 변환하여 publish한다.
  void publishDebugPose(const LocalizedPoseMsg & msg);

  // 최신 pose/path/progress 정보를 이용해 MarkerArray를 생성하고 publish한다.
  void publishProgressMarkers();

  // RViz에서 구체 형태로 표시할 waypoint marker를 생성한다.
  visualization_msgs::msg::Marker makeSphereMarker(
    int marker_id,
    const std::string & marker_name,
    double x,
    double y,
    double z,
    double scale_m,
    const std_msgs::msg::ColorRGBA & color) const;

  // RViz에서 현재 pose와 target point를 잇는 line marker를 생성한다.
  visualization_msgs::msg::Marker makeLineMarker(
    int marker_id,
    const std::string & marker_name,
    const geometry_msgs::msg::Point & start_point,
    const geometry_msgs::msg::Point & end_point,
    double line_width_m,
    const std_msgs::msg::ColorRGBA & color) const;

  // RViz에서 상태 정보를 문자열로 보여줄 text marker를 생성한다.
  visualization_msgs::msg::Marker makeTextMarker(
    int marker_id,
    const std::string & marker_name,
    double x,
    double y,
    double z,
    const std::string & text,
    double text_scale_m,
    const std_msgs::msg::ColorRGBA & color) const;

  // Marker 색상 생성을 위한 helper 함수.
  std_msgs::msg::ColorRGBA makeColor(
    float r,
    float g,
    float b,
    float a) const;

  // yaw(rad)를 quaternion으로 변환한다.
  geometry_msgs::msg::Quaternion yawToQuaternion(double yaw_rad) const;

  // marker_lifetime_sec_ 값을 builtin_interfaces/Duration으로 변환한다.
  builtin_interfaces::msg::Duration makeMarkerLifetime() const;

  // robot_id와 frame_id가 현재 visualizer 대상과 일치하는지 검사한다.
  bool isRobotAndFrameMatched(
    const std::string & msg_robot_id,
    const std::string & msg_frame_id) const;

  // 입력 subscriber.
  rclcpp::Subscription<GlobalPathMsg>::SharedPtr global_path_sub_;
  rclcpp::Subscription<LocalizedPoseMsg>::SharedPtr pose_sub_;
  rclcpp::Subscription<PathProgressMsg>::SharedPtr path_progress_sub_;

  // RViz 표준 메시지 publisher.
  rclcpp::Publisher<nav_msgs::msg::Path>::SharedPtr debug_path_pub_;
  rclcpp::Publisher<geometry_msgs::msg::PoseStamped>::SharedPtr debug_pose_pub_;
  rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr debug_marker_pub_;

  // 파라미터 변수.
  std::string robot_id_;
  std::string global_frame_;

  std::string global_path_topic_;
  std::string localization_pose_topic_;
  std::string path_progress_topic_;

  std::string debug_global_path_topic_;
  std::string debug_pose_topic_;
  std::string debug_marker_topic_;

  std::string marker_namespace_;

  double waypoint_marker_scale_m_;
  double target_line_width_m_;
  double text_marker_scale_m_;
  double marker_lifetime_sec_;
  double marker_z_m_;

  // 최신 입력 상태 저장.
  // progress marker 생성 시 pose/path/progress 정보를 함께 사용하기 위해 보관한다.
  bool path_received_{false};
  bool pose_received_{false};
  bool progress_received_{false};

  GlobalPathMsg::SharedPtr latest_path_{nullptr};
  LocalizedPoseMsg::SharedPtr latest_pose_{nullptr};
  PathProgressMsg::SharedPtr latest_progress_{nullptr};
};

}  // namespace spot_navigation

#endif  // SPOT_NAVIGATION__NAVIGATION_DEBUG_VISUALIZER_NODE_HPP_