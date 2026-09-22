/*
- 경로 : ~/robot_ws/src/perception/lidar_perception/include/lidar_perception/free_space_marker_node.hpp
- 역할 : FreeSpaceModel RViz 시각화 노드 헤더
- 입력 : /perception/lidar/free_space_model (robot_interfaces/msg/FreeSpaceModel)
- 출력 : /perception/lidar/free_space_markers (visualization_msgs/msg/MarkerArray)
- 기능 :
    - selected gap을 부채꼴 Marker로 시각화
    - candidate gaps를 반투명 부채꼴 Marker로 시각화
    - best heading을 Arrow Marker로 시각화
    - risk level을 Text Marker로 시각화
*/

#ifndef LIDAR_PERCEPTION__FREE_SPACE_MARKER_NODE_HPP_
#define LIDAR_PERCEPTION__FREE_SPACE_MARKER_NODE_HPP_

#include <cstdint>
#include <string>

#include "builtin_interfaces/msg/time.hpp"

#include "rclcpp/rclcpp.hpp"

#include "std_msgs/msg/color_rgba.hpp"
#include "visualization_msgs/msg/marker.hpp"
#include "visualization_msgs/msg/marker_array.hpp"

#include "robot_interfaces/msg/free_space_gap.hpp"
#include "robot_interfaces/msg/free_space_model.hpp"

namespace lidar_perception
{

class FreeSpaceMarkerNode : public rclcpp::Node
{
public:
    FreeSpaceMarkerNode();

private:
    // ============================================================
    // [1] Callback
    // ============================================================

    // FreeSpaceModel 메시지를 받아 RViz MarkerArray로 변환
    void freeSpaceCallback(
        const robot_interfaces::msg::FreeSpaceModel::SharedPtr msg);

    // ============================================================
    // [2] Marker 생성 함수
    // ============================================================

    // 이전 frame의 Marker를 모두 삭제하기 위한 Marker 생성
    visualization_msgs::msg::Marker makeDeleteAllMarker(
        const builtin_interfaces::msg::Time & stamp) const;

    // best_heading_angle_rad 방향으로 arrow marker 생성
    visualization_msgs::msg::Marker makeBestHeadingArrow(
        double heading_rad,
        uint8_t risk_level,
        int marker_id,
        const builtin_interfaces::msg::Time & stamp) const;

    // 하나의 FreeSpaceGap을 부채꼴 영역 Marker로 생성
    visualization_msgs::msg::Marker makeGapSectorMarker(
        const robot_interfaces::msg::FreeSpaceGap & gap,
        bool selected,
        int marker_id,
        const builtin_interfaces::msg::Time & stamp,
        const std_msgs::msg::ColorRGBA & color) const;

    // risk level, best heading, clearance 등을 text marker로 생성
    visualization_msgs::msg::Marker makeRiskTextMarker(
        const robot_interfaces::msg::FreeSpaceModel & msg,
        int marker_id,
        const builtin_interfaces::msg::Time & stamp) const;

    // ============================================================
    // [3] Utility
    // ============================================================

    std_msgs::msg::ColorRGBA makeColor(
        float r,
        float g,
        float b,
        float a) const;

    std_msgs::msg::ColorRGBA riskColor(
        uint8_t risk_level,
        float alpha) const;

    std::string riskToString(
        uint8_t risk_level) const;

    std::string formatDouble(
        double value,
        int precision) const;

    void setLifetime(
        visualization_msgs::msg::Marker & marker) const;

private:
    // ============================================================
    // [4] ROS Interface
    // ============================================================

    rclcpp::Subscription<robot_interfaces::msg::FreeSpaceModel>::SharedPtr model_sub_;
    rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr marker_pub_;

    // ============================================================
    // [5] Topic / Frame Parameter
    // ============================================================

    std::string input_topic_;
    std::string output_topic_;
    std::string frame_id_;

    // ============================================================
    // [6] Marker Shape Parameter
    // ============================================================

    // RViz에서 Marker를 띄울 z 높이
    double marker_z_;

    // gap 부채꼴을 그릴 반경 [m]
    double sector_radius_;

    // best heading arrow 길이 [m]
    double arrow_length_;

    // ============================================================
    // [7] Text Marker Parameter
    // ============================================================

    double text_x_;
    double text_y_;
    double text_z_;
    double text_scale_z_;

    // ============================================================
    // [8] Alpha / Lifetime Parameter
    // ============================================================

    double candidate_alpha_;
    double selected_alpha_;
    double marker_lifetime_sec_;

    bool text_follow_best_heading_;
    double text_radius_;
};

}  // namespace lidar_perception

#endif  // LIDAR_PERCEPTION__FREE_SPACE_MARKER_NODE_HPP_