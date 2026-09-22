/*
- 위치 : ~/robot_ws/src/perception/lidar_perception/src/obstacle_model_node.cpp
- 역할 : Decision/Planning에서 활용할 LiDAR 데이터 후처리
- 입력 : /perception/lidar/obstacle_clusters
- 출력 : /perception/lidar/obstacle_model
- 기능 :
    - 여러 클러스터 중에서 Spot(base_link) 기준 front/left/right 섹터별 대표 장애물 선택
    - 가장 가까운 대표 장애물만 ObstacleModel.msg로 요약
    - 필요 시, emergency_stop_required도 계산
*/

#include "lidar_perception/obstacle_model_node.hpp"

#include <limits>
#include <memory>
#include <cmath>

namespace lidar_perception
{
    // rad -> deg 변환 함수
    double radToDeg(double rad)
    {
        return rad * 180.0 / M_PI;
    }

    ObstacleModelNode::ObstacleModelNode()
    : Node("obstacle_model_node")
    {
        // ===============================
        // [1] Subscriber / Publisher 생성
        // ===============================
        clusters_sub_ = this->create_subscription<robot_interfaces::msg::ObstacleClusters>(
            "/perception/lidar/obstacle_clusters",
            10,
            std::bind(&ObstacleModelNode::obstacleClustersCallback, this, std::placeholders::_1)
        );

        model_pub_ = this->create_publisher<robot_interfaces::msg::ObstacleModel>(
            "/perception/lidar/obstacle_model",
            10
        );

        RCLCPP_INFO(this->get_logger(), "obstacle_model_node started!");
    }

    // 특정 섹터에 속하는 대표 클러스터 선별 함수
    bool ObstacleModelNode::selectRepresentativeCluster(
        const robot_interfaces::msg::ObstacleClusters::SharedPtr msg,
        uint8_t target_sector_mask,
        robot_interfaces::msg::ObstacleCluster & representative_cluster
    ) const
    {
        bool found = false;
        float best_distance = std::numeric_limits<float>::infinity();

        for (const auto& cluster : msg->clusters) {

            // 유효하지 않은 cluster Pass
            if (!cluster.valid) continue;

            // 해당 sector를 점유하지 않으면 Pass
            if ((cluster.sector_mask & target_sector_mask) == 0) continue;

            // 더 가까운 cluster를 대표로 선택
            if (!found || cluster.nearest_distance_xy < best_distance) {
                representative_cluster = cluster;
                best_distance = cluster.nearest_distance_xy;
                found = true;
            }
        }

        return found;
    }

    void ObstacleModelNode::obstacleClustersCallback(
        const robot_interfaces::msg::ObstacleClusters::SharedPtr msg)
    {   
        // ===================
        // [1] 출력 메시지 생성
        // ===================
        robot_interfaces::msg::ObstacleModel out;
        out.header = msg->header;

        // Default : 아무 장애물이 없는 경우
        out.obstacle_detected = false;

        // front / left / right 기본 invalid 상태
        out.front.valid = false;
        out.left.valid  = false;
        out.right.valid = false;

        // ==============================
        // [2] Sector별 대표 클러스터 선별
        // ==============================
        const bool front_found = selectRepresentativeCluster(
            msg,
            robot_interfaces::msg::ObstacleCluster::SECTOR_FRONT,
            out.front
        );

        const bool left_found = selectRepresentativeCluster(
            msg,
            robot_interfaces::msg::ObstacleCluster::SECTOR_LEFT,
            out.left
        );

        const bool right_found = selectRepresentativeCluster(
            msg,
            robot_interfaces::msg::ObstacleCluster::SECTOR_RIGHT,
            out.right
        );

        // =======================
        // [3] 전체 장애물 감지 여부
        // =======================
        out.obstacle_detected = front_found || left_found || right_found;

        // ==============
        // [4] 로그 출력
        // ==============
        RCLCPP_INFO_THROTTLE(
            this->get_logger(),
            *this->get_clock(),
            1000,
            "\nclusters | total=%zu"
            "\nfront    | valid=%s | nearest=%.2f | azimuth=%.2f (%.1fdeg) | cluster_id=%u | track_id=%u | tracked=%s | sector=%u | mask=%u"
            "\nleft     | valid=%s | nearest=%.2f | azimuth=%.2f (%.1fdeg) | cluster_id=%u | track_id=%u | tracked=%s | sector=%u | mask=%u"
            "\nright    | valid=%s | nearest=%.2f | azimuth=%.2f (%.1fdeg) | cluster_id=%u | track_id=%u | tracked=%s | sector=%u | mask=%u"
            "\nmodel    | obstacle_detected=%s",
            msg->clusters.size(),

            front_found ? "true" : "false",
            front_found ? out.front.nearest_distance_xy : 0.0f,
            front_found ? out.front.azimuth_angle_rad : 0.0f,
            front_found ? radToDeg(out.front.azimuth_angle_rad) : 0.0,
            front_found ? out.front.cluster_id : 0u,
            front_found ? out.front.track_id : 0u,
            front_found ? (out.front.tracked ? "true" : "false") : "false",
            front_found ? out.front.sector : 0u,
            front_found ? out.front.sector_mask : 0u,

            left_found ? "true" : "false",
            left_found ? out.left.nearest_distance_xy : 0.0f,
            left_found ? out.left.azimuth_angle_rad : 0.0f,
            left_found ? radToDeg(out.left.azimuth_angle_rad) : 0.0,
            left_found ? out.left.cluster_id : 0u,
            left_found ? out.left.track_id : 0u,
            left_found ? (out.left.tracked ? "true" : "false") : "false",
            left_found ? out.left.sector : 0u,
            left_found ? out.left.sector_mask : 0u,

            right_found ? "true" : "false",
            right_found ? out.right.nearest_distance_xy : 0.0f,
            right_found ? out.right.azimuth_angle_rad : 0.0f,
            right_found ? radToDeg(out.right.azimuth_angle_rad) : 0.0,
            right_found ? out.right.cluster_id : 0u,
            right_found ? out.right.track_id : 0u,
            right_found ? (out.right.tracked ? "true" : "false") : "false",
            right_found ? out.right.sector : 0u,
            right_found ? out.right.sector_mask : 0u,

            out.obstacle_detected ? "true" : "false"
        );

        // =============
        // [5] publish
        // =============
        model_pub_->publish(out);
    }
}

int main(int argc, char ** argv)
{
    rclcpp::init(argc, argv);

    auto node = std::make_shared<lidar_perception::ObstacleModelNode>();
    rclcpp::spin(node);

    rclcpp::shutdown();

    return 0;
}