#ifndef LIDAR_PERCEPTION__OBSTACLE_CLUSTER_NODE_HPP_
#define LIDAR_PERCEPTION__OBSTACLE_CLUSTER_NODE_HPP_

#include <memory>
#include <string>
#include <tuple>
#include <vector>
#include <cstdint>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"

#include "robot_interfaces/msg/obstacle_cluster.hpp"
#include "robot_interfaces/msg/obstacle_clusters.hpp"

namespace lidar_perception
{
    class ObstacleClusterNode : public rclcpp::Node
    {
        public:
            ObstacleClusterNode();

        private:
            struct TrackState
            {
                uint32_t track_id;  // 시간적으로 유지되는 물체 ID

                // centroid 기준 tracking 매칭 기준 위치
                double centroid_x;
                double centroid_y;
                double centroid_z;

                uint8_t sector_mask; // 최근에 해당 track에 점유한 sector 정보

                int age;             // 생성 후 누적 프레임 수
                int hit_count;       // 매칭 성공 횟수
                int missed_count;    // 연속 미검출 횟수

            };

            // 전처리된 PointCloud2를 받아 클러스터링 수행
            void pointCloudCallback(const sensor_msgs::msg::PointCloud2::SharedPtr msg);

            // 현재 프레임 cluster들에 track_id 할당
            void assignTrackIds(std::vector<robot_interfaces::msg::ObstacleCluster> & clusters);
            
            // nearest point 기준 대표 sector 분류
            uint8_t classifySector(float azimuth_angle_rad) const;

            // cluster 내부 point들의 sector 점유 mask 계산에 사용
            uint8_t classifySectorMask(float azimuth_angle_rad) const;

            // cluster id에 따라 색상 반환
            std::tuple<uint8_t, uint8_t, uint8_t> getClusterColor(int cluster_id) const;

            // 입력 점군 구독
            rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr input_cloud_sub_;

            // 클러스터 결과 발행
            rclcpp::Publisher<robot_interfaces::msg::ObstacleClusters>::SharedPtr cluster_pub_;

            // 클러스터 시각화 데이터 발행
            rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr clustered_cloud_pub_;

            // 클러스터링 파라미터
            double cluster_tolerance_;          // 클러스터링을 위한 점간 임계거리
            int min_cluster_size_;              // 클러스터로 인정하는 최소 포인트 수
            int max_cluster_size_;              // 클러스터로 인정하는 최대 포인트 수

            // tracking parameters
            bool tracking_enabled_;             // tracking 모드 ON/OFF 여부
            bool color_by_track_id_;            
            double tracking_match_distance_;    // 같은 물체인지에 대한 threshold [m]
            int tracking_max_missed_frames_;    // 기존 track 유지할 프레임 수
            double tracking_smoothing_alpha_;

            // tracking states
            uint32_t next_track_id_;
            std::vector<TrackState> tracks_;

            // sector 분류 기준 각도(deg)
            double front_min_deg_;
            double front_max_deg_;
            double left_min_deg_;
            double left_max_deg_;
            double right_min_deg_;
            double right_max_deg_;
    };
}

#endif