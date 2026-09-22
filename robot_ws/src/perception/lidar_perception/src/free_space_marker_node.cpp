/*
- 경로 : ~/robot_ws/src/perception/lidar_perception/src/free_space_marker_node.cpp
- 역할 : FreeSpaceModel RViz 시각화 노드
- 입력 : /perception/lidar/free_space_model (robot_interfaces/msg/FreeSpaceModel)
- 출력 : /perception/lidar/free_space_markers (visualization_msgs/msg/MarkerArray)
- 기능 :
    - selected gap을 부채꼴 영역으로 시각화
    - candidate gaps를 반투명 부채꼴 영역으로 시각화
    - best heading을 base_link 기준 화살표로 시각화
    - risk level을 TEXT marker로 시각화
*/

#include "lidar_perception/free_space_marker_node.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include "geometry_msgs/msg/point.hpp"

namespace lidar_perception
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
}  // namespace

FreeSpaceMarkerNode::FreeSpaceMarkerNode()
: Node("free_space_marker_node")
{
    // =======================
    // [1] Parameter Declare
    // =======================

    input_topic_ = this->declare_parameter<std::string>(
        "input_topic",
        "/perception/lidar/free_space_model"
    );

    output_topic_ = this->declare_parameter<std::string>(
        "output_topic",
        "/perception/lidar/free_space_markers"
    );

    frame_id_ = this->declare_parameter<std::string>(
        "frame_id",
        "base_link"
    );

    // RViz에서 local occupancy grid와 겹쳐 보이도록 낮은 z 높이에 표시
    marker_z_ = this->declare_parameter<double>(
        "marker_z",
        0.05
    );

    // selected_gap / candidate_gap 부채꼴 표시 반경
    sector_radius_ = this->declare_parameter<double>(
        "sector_radius",
        1.20
    );

    // best heading arrow 길이
    arrow_length_ = this->declare_parameter<double>(
        "arrow_length",
        1.00
    );

    // risk text 위치
    text_x_ = this->declare_parameter<double>("text_x", 0.30);
    text_y_ = this->declare_parameter<double>("text_y", 0.00);
    text_z_ = this->declare_parameter<double>("text_z", 0.45);
    
    // risk text 글자 크기
    text_scale_z_ = this->declare_parameter<double>("text_scale_z", 0.08);
    
    text_follow_best_heading_ =
        this->declare_parameter<bool>("text_follow_best_heading", true);

    text_radius_ =
        this->declare_parameter<double>("text_radius", 1.45);

    // candidate gap은 연하게, selected gap은 더 진하게 표시
    candidate_alpha_ = this->declare_parameter<double>(
        "candidate_alpha",
        0.18
    );

    selected_alpha_ = this->declare_parameter<double>(
        "selected_alpha",
        0.45
    );

    // Marker가 오래 남지 않도록 lifetime 설정
    marker_lifetime_sec_ = this->declare_parameter<double>(
        "marker_lifetime_sec",
        0.30
    );

    

    // ================================
    // [2] Subscriber / Publisher 생성
    // ================================

    model_sub_ = this->create_subscription<robot_interfaces::msg::FreeSpaceModel>(
        input_topic_,
        rclcpp::QoS(10),
        std::bind(&FreeSpaceMarkerNode::freeSpaceCallback, this, std::placeholders::_1)
    );

    marker_pub_ = this->create_publisher<visualization_msgs::msg::MarkerArray>(
        output_topic_,
        rclcpp::QoS(10)
    );

    RCLCPP_INFO(this->get_logger(), "free_space_marker_node started!");
    RCLCPP_INFO(this->get_logger(), "input_topic  : %s", input_topic_.c_str());
    RCLCPP_INFO(this->get_logger(), "output_topic : %s", output_topic_.c_str());
    RCLCPP_INFO(this->get_logger(), "frame_id     : %s", frame_id_.c_str());
}

// ============================================================
// [1] Callback
// ============================================================

void FreeSpaceMarkerNode::freeSpaceCallback(
    const robot_interfaces::msg::FreeSpaceModel::SharedPtr msg)
{
    visualization_msgs::msg::MarkerArray marker_array;

    // 매 callback마다 이전 marker를 삭제하고 새 marker를 그림
    // candidate gap 개수가 frame마다 달라질 수 있기 때문에 DELETEALL 방식이 안전함
    marker_array.markers.push_back(makeDeleteAllMarker(msg->header.stamp));

    int marker_id = 0;

    // ============================
    // [1] candidate gaps 시각화
    // ============================
    // selected gap은 아래에서 더 진하게 다시 그릴 것이므로 여기서는 제외
    for (const auto & gap : msg->candidate_gaps) {
        if (gap.selected) {
            continue;
        }

        marker_array.markers.push_back(
            makeGapSectorMarker(
                gap,
                false,
                marker_id++,
                msg->header.stamp,
                makeColor(
                    0.2f,
                    0.6f,
                    1.0f,
                    static_cast<float>(candidate_alpha_)
                )
            )
        );
    }

    // ===========================
    // [2] selected gap 시각화
    // ===========================
    if (msg->path_available) {
        marker_array.markers.push_back(
            makeGapSectorMarker(
                msg->selected_gap,
                true,
                marker_id++,
                msg->header.stamp,
                riskColor(
                    msg->risk_level,
                    static_cast<float>(selected_alpha_)
                )
            )
        );

        // ===========================
        // [3] best heading arrow
        // ===========================
        marker_array.markers.push_back(
            makeBestHeadingArrow(
                msg->best_heading_angle_rad,
                msg->risk_level,
                marker_id++,
                msg->header.stamp
            )
        );
    }

    // ===========================
    // [4] risk text
    // ===========================
    marker_array.markers.push_back(
        makeRiskTextMarker(
            *msg,
            marker_id++,
            msg->header.stamp
        )
    );

    marker_pub_->publish(marker_array);
}

// ============================================================
// [2] Marker 생성 Helper
// ============================================================

visualization_msgs::msg::Marker FreeSpaceMarkerNode::makeDeleteAllMarker(
    const builtin_interfaces::msg::Time & stamp) const
{
    visualization_msgs::msg::Marker marker;

    marker.header.frame_id = frame_id_;
    marker.header.stamp = stamp;

    marker.ns = "free_space_model";
    marker.id = 0;
    marker.action = visualization_msgs::msg::Marker::DELETEALL;

    return marker;
}

visualization_msgs::msg::Marker FreeSpaceMarkerNode::makeBestHeadingArrow(
    const double heading_rad,
    const uint8_t risk_level,
    const int marker_id,
    const builtin_interfaces::msg::Time & stamp) const
{
    visualization_msgs::msg::Marker marker;

    marker.header.frame_id = frame_id_;
    marker.header.stamp = stamp;

    marker.ns = "free_space_best_heading";
    marker.id = marker_id;

    marker.type = visualization_msgs::msg::Marker::ARROW;
    marker.action = visualization_msgs::msg::Marker::ADD;

    // ARROW marker는 points[0] → points[1] 방향으로 그려짐
    geometry_msgs::msg::Point start;
    start.x = 0.0;
    start.y = 0.0;
    start.z = marker_z_ + 0.05;

    geometry_msgs::msg::Point end;
    end.x = arrow_length_ * std::cos(heading_rad);
    end.y = arrow_length_ * std::sin(heading_rad);
    end.z = marker_z_ + 0.05;

    marker.points.push_back(start);
    marker.points.push_back(end);

    // scale.x : shaft diameter
    // scale.y : head diameter
    // scale.z : head length
    marker.scale.x = 0.04;
    marker.scale.y = 0.12;
    marker.scale.z = 0.18;

    marker.color = riskColor(risk_level, 1.0f);

    setLifetime(marker);

    return marker;
}

visualization_msgs::msg::Marker FreeSpaceMarkerNode::makeGapSectorMarker(
    const robot_interfaces::msg::FreeSpaceGap & gap,
    const bool selected,
    const int marker_id,
    const builtin_interfaces::msg::Time & stamp,
    const std_msgs::msg::ColorRGBA & color) const
{
    visualization_msgs::msg::Marker marker;

    marker.header.frame_id = frame_id_;
    marker.header.stamp = stamp;

    marker.ns = selected ? "free_space_selected_gap" : "free_space_candidate_gap";
    marker.id = marker_id;

    marker.type = visualization_msgs::msg::Marker::TRIANGLE_LIST;
    marker.action = visualization_msgs::msg::Marker::ADD;

    marker.pose.orientation.w = 1.0;

    // 중요: TRIANGLE_LIST는 scale이 0이면 RViz에서 안 보일 수 있음
    marker.scale.x = 1.0;
    marker.scale.y = 1.0;
    marker.scale.z = 1.0;

    marker.color = color;

    setLifetime(marker);

    const double start_angle = static_cast<double>(gap.start_angle_rad);
    const double end_angle   = static_cast<double>(gap.end_angle_rad);

    const double angle_width = std::abs(end_angle - start_angle);

    const double step_rad = 2.0 * kPi / 180.0;
    int step_count = static_cast<int>(std::ceil(angle_width / step_rad));
    if (step_count < 1) {
        step_count = 1;
    }

    geometry_msgs::msg::Point center;
    center.x = 0.0;
    center.y = 0.0;
    center.z = selected ? marker_z_ + 0.03 : marker_z_;

    for (int i = 0; i < step_count; ++i) {
        const double a0 =
            start_angle +
            (end_angle - start_angle) *
            static_cast<double>(i) / static_cast<double>(step_count);

        const double a1 =
            start_angle +
            (end_angle - start_angle) *
            static_cast<double>(i + 1) / static_cast<double>(step_count);

        geometry_msgs::msg::Point p0;
        p0.x = sector_radius_ * std::cos(a0);
        p0.y = sector_radius_ * std::sin(a0);
        p0.z = center.z;

        geometry_msgs::msg::Point p1;
        p1.x = sector_radius_ * std::cos(a1);
        p1.y = sector_radius_ * std::sin(a1);
        p1.z = center.z;

        marker.points.push_back(center);
        marker.points.push_back(p0);
        marker.points.push_back(p1);
    }

    return marker;
}

visualization_msgs::msg::Marker FreeSpaceMarkerNode::makeRiskTextMarker(
    const robot_interfaces::msg::FreeSpaceModel & msg,
    const int marker_id,
    const builtin_interfaces::msg::Time & stamp) const
{
    visualization_msgs::msg::Marker marker;

    marker.header.frame_id = frame_id_;
    marker.header.stamp = stamp;

    marker.ns = "free_space_risk_text";
    marker.id = marker_id;

    marker.type = visualization_msgs::msg::Marker::TEXT_VIEW_FACING;
    marker.action = visualization_msgs::msg::Marker::ADD;

    if (text_follow_best_heading_ && msg.path_available) {
        marker.pose.position.x =
            text_radius_ * std::cos(static_cast<double>(msg.best_heading_angle_rad));
        marker.pose.position.y =
            text_radius_ * std::sin(static_cast<double>(msg.best_heading_angle_rad));
        marker.pose.position.z = text_z_;
    } else {
        marker.pose.position.x = text_x_;
        marker.pose.position.y = text_y_;
        marker.pose.position.z = text_z_;
    }

    marker.pose.orientation.w = 1.0;

    // TEXT_VIEW_FACING은 scale.z가 글자 크기
    marker.scale.z = text_scale_z_;

    marker.color = riskColor(msg.risk_level, 1.0f);

    marker.text =
        "FreeSpace: " + riskToString(msg.risk_level) +
        "\npath=" + std::string(msg.path_available ? "true" : "false") +
        "\nheading=" + formatDouble(msg.best_heading_angle_deg, 1) + " deg" +
        "\nclearance=" + formatDouble(msg.best_clearance, 2) + " m" +
        "\ngaps=" + std::to_string(msg.candidate_gap_count);

    setLifetime(marker);

    return marker;
}

// ============================================================
// [3] Utility
// ============================================================

std_msgs::msg::ColorRGBA FreeSpaceMarkerNode::makeColor(
    const float r,
    const float g,
    const float b,
    const float a) const
{
    std_msgs::msg::ColorRGBA color;

    color.r = r;
    color.g = g;
    color.b = b;
    color.a = a;

    return color;
}

std_msgs::msg::ColorRGBA FreeSpaceMarkerNode::riskColor(
    const uint8_t risk_level,
    const float alpha) const
{
    if (risk_level == robot_interfaces::msg::FreeSpaceModel::RISK_SAFE) {
        return makeColor(0.0f, 1.0f, 0.2f, alpha);      // green
    }

    if (risk_level == robot_interfaces::msg::FreeSpaceModel::RISK_CAUTION) {
        return makeColor(1.0f, 0.75f, 0.0f, alpha);     // yellow/orange
    }

    if (risk_level == robot_interfaces::msg::FreeSpaceModel::RISK_BLOCKED) {
        return makeColor(1.0f, 0.0f, 0.0f, alpha);      // red
    }

    return makeColor(0.7f, 0.7f, 0.7f, alpha);          // gray
}

std::string FreeSpaceMarkerNode::riskToString(
    const uint8_t risk_level) const
{
    if (risk_level == robot_interfaces::msg::FreeSpaceModel::RISK_SAFE) {
        return "SAFE";
    }

    if (risk_level == robot_interfaces::msg::FreeSpaceModel::RISK_CAUTION) {
        return "CAUTION";
    }

    if (risk_level == robot_interfaces::msg::FreeSpaceModel::RISK_BLOCKED) {
        return "BLOCKED";
    }

    return "UNKNOWN";
}

std::string FreeSpaceMarkerNode::formatDouble(
    const double value,
    const int precision) const
{
    char buffer[64];

    std::snprintf(
        buffer,
        sizeof(buffer),
        "%.*f",
        precision,
        value
    );

    return std::string(buffer);
}

void FreeSpaceMarkerNode::setLifetime(
    visualization_msgs::msg::Marker & marker) const
{
    // local model은 계속 갱신되는 실시간 데이터이므로
    // marker lifetime을 짧게 둬서 노드가 멈췄을 때 오래된 marker가 남지 않게 한다.
    const int sec = static_cast<int>(marker_lifetime_sec_);
    const double fractional = marker_lifetime_sec_ - static_cast<double>(sec);

    marker.lifetime.sec = sec;
    marker.lifetime.nanosec = static_cast<uint32_t>(fractional * 1e9);
}

}  // namespace lidar_perception

int main(int argc, char ** argv)
{
    rclcpp::init(argc, argv);

    auto node = std::make_shared<lidar_perception::FreeSpaceMarkerNode>();
    rclcpp::spin(node);

    rclcpp::shutdown();

    return 0;
}