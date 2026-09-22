#ifndef LIDAR_PERCEPTION__OBSTACLE_MODEL_NODE_HPP_
#define LIDAR_PERCEPTION__OBSTACLE_MODEL_NODE_HPP_

#include <memory>

#include "rclcpp/rclcpp.hpp"                            // ROS2 C++ 라이브러리

#include "robot_interfaces/msg/obstacle_cluster.hpp"
#include "robot_interfaces/msg/obstacle_clusters.hpp"
#include "robot_interfaces/msg/obstacle_model.hpp"      // ObstacleModel.msg의 헤더 파일

namespace lidar_perception
{
    class ObstacleModelNode : public rclcpp::Node
    {
        public:
            ObstacleModelNode();

        private:
            // 클러스터 목록을 받아 대표 장애물 모델 생성
            void obstacleClustersCallback(
                const robot_interfaces::msg::ObstacleClusters::SharedPtr msg
            );

            // 특정 sector mask에 해당하는 대표 클러스터 선택
            // 기준 : nearest_distance_xy가 가장 작은 클러스터
            bool selectRepresentativeCluster(
                const robot_interfaces::msg::ObstacleClusters::SharedPtr msg,
                uint8_t target_sector_mask,
                robot_interfaces::msg::ObstacleCluster & representative_cluster
            ) const;

            // 입력 : 전체 클러스터 목록
            rclcpp::Subscription<robot_interfaces::msg::ObstacleClusters>::SharedPtr clusters_sub_;

            // 출력 : 대표 장애물 요약 모델
            rclcpp::Publisher<robot_interfaces::msg::ObstacleModel>::SharedPtr model_pub_;
    };
}

#endif