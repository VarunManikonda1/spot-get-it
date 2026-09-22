/*
- 경로 : ~/robot_ws/src/perception/lidar_perception/src/obstacle_cluster_node.cpp
- 역할 : 3D 클러스터 생성
- 입력 : /perception/lidar/points_filtered
- 출력 : /perception/lidar/obstacle_clusters
        /preception/lidar/clustered_points_colored
- 기능 :
    - PointCloud2 → PCL 변환
    - KD-Tree 구조화
    - Euclidean clustering
    - 각 클러스터 descriptor 계산
    - ObstacleClusters publish
    - 각 클러스터에 대한 정렬 기반 색상 분류 (Rviz 시각화 디버깅용)
    - 각 클러스터 traking 로직 (확장)
*/

#include "lidar_perception/obstacle_cluster_node.hpp"

#include <cmath>
#include <limits>
#include <sstream>
#include <memory>
#include <vector>
#include <algorithm>

#include "pcl/point_cloud.h"
#include "pcl/point_types.h"                    // PointXYZ, PointXYZRGB
#include "pcl/search/kdtree.h"
#include "pcl/segmentation/extract_clusters.h"

#include "pcl_conversions/pcl_conversions.h"

namespace lidar_perception
{
    namespace
    {
        // rad -> deg 변환
        double radToDeg(double rad) {
            return rad * 180.0 / M_PI;
        }
    }

    ObstacleClusterNode::ObstacleClusterNode()
    : Node("obstacle_cluster_node")
    {
        // =======================
        // [1] Farmeter Declear
        // =======================
        this->declare_parameter<double>("cluster_tolerance", 0.12);
        this->declare_parameter<int>("min_cluster_size", 20);
        this->declare_parameter<int>("max_cluster_size", 5000);

        this->declare_parameter<double>("front_min_deg", -30.0);
        this->declare_parameter<double>("front_max_deg", 30.0);

        this->declare_parameter<double>("left_min_deg", 30.0);
        this->declare_parameter<double>("left_max_deg", 60.0);

        this->declare_parameter<double>("right_min_deg", -60.0);
        this->declare_parameter<double>("right_max_deg", -30.0);

        this->declare_parameter<bool>("tracking_enabled", true);
        this->declare_parameter<bool>("color_by_track_id", true);
        this->declare_parameter<double>("tracking_match_distance", 0.30);
        this->declare_parameter<int>("tracking_max_missed_frames", 3);
        this->declare_parameter<double>("tracking_smoothing_alpha", 0.6);

        // ===================
        // [2] Farmeter Read
        // ===================
        cluster_tolerance_ = this->get_parameter("cluster_tolerance").as_double();  // 클러스터화 점간 임계거리
        min_cluster_size_  = this->get_parameter("min_cluster_size").as_int();      // 클러스터화 최소 포인터 수
        max_cluster_size_  = this->get_parameter("max_cluster_size").as_int();      // 클러스터화 최대 포인터 수

        front_min_deg_     = this->get_parameter("front_min_deg").as_double();
        front_max_deg_     = this->get_parameter("front_max_deg").as_double();

        left_min_deg_      = this->get_parameter("left_min_deg").as_double();
        left_max_deg_      = this->get_parameter("left_max_deg").as_double();

        right_min_deg_     = this->get_parameter("right_min_deg").as_double();
        right_max_deg_     = this->get_parameter("right_max_deg").as_double();

        tracking_enabled_ = this->get_parameter("tracking_enabled").as_bool();
        color_by_track_id_ = this->get_parameter("color_by_track_id").as_bool();
        tracking_match_distance_ = this->get_parameter("tracking_match_distance").as_double();
        tracking_max_missed_frames_ = this->get_parameter("tracking_max_missed_frames").as_int();
        tracking_smoothing_alpha_ = this->get_parameter("tracking_smoothing_alpha").as_double();

        next_track_id_ = 1;

        // ================================
        // [3] Subscriber / Publisher 생성
        // ================================
        input_cloud_sub_ = this->create_subscription<sensor_msgs::msg::PointCloud2>(
            "/perception/lidar/points_filtered",
            rclcpp::SensorDataQoS(),
            std::bind(&ObstacleClusterNode::pointCloudCallback, this, std::placeholders::_1)
        );

        cluster_pub_ = this->create_publisher<robot_interfaces::msg::ObstacleClusters>(
            "/perception/lidar/obstacle_clusters",
            10
        );

        clustered_cloud_pub_ = this->create_publisher<sensor_msgs::msg::PointCloud2>(
            "/perception/lidar/clustered_points_colored",
            10
        );

        RCLCPP_INFO(this->get_logger(), "obstacle_cluster_node started!");
    }

    // centroid 거리 기반 cluster tracking 함수
    void ObstacleClusterNode::assignTrackIds(
        std::vector<robot_interfaces::msg::ObstacleCluster> & clusters)
    {
        if (!tracking_enabled_) {
            for (auto &cluster : clusters) {
                cluster.track_id = 0;
                cluster.tracked  = false;
            }
            return;
        }

        std::vector<bool> track_used(tracks_.size(), false);

        for (auto &cluster : clusters) {
            int best_track_index = -1;
            double best_distance = std::numeric_limits<double>::infinity();

            // 1. 현재 cluster와 가장 가까운 기존 track 탐색
            for (std::size_t i = 0; i < tracks_.size(); ++i) {
                if (track_used[i]) continue;

                const auto &track = tracks_[i];

                const double dx = static_cast<double>(cluster.centroid_x) - track.centroid_x;
                const double dy = static_cast<double>(cluster.centroid_y) - track.centroid_y;
                const double dist = std::sqrt(dx * dx + dy * dy);

                if (dist < best_distance) {
                    best_distance    = dist;
                    best_track_index = static_cast<int>(i);
                }
            }

            // 2. 기존 track과 매칭 성공
            if (best_track_index >= 0 && best_distance <= tracking_match_distance_) {
                auto &track = tracks_[best_track_index];

                cluster.track_id = track.track_id;
                cluster.tracked  = true;

                // tracking 위치 smoothing : alpha가 클수록 이전 track 위치를 더 많이 유지
                track.centroid_x =
                    tracking_smoothing_alpha_ * track.centroid_x +
                    (1.0 - tracking_smoothing_alpha_) * static_cast<double>(cluster.centroid_x);

                track.centroid_y =
                    tracking_smoothing_alpha_ * track.centroid_y +
                    (1.0 - tracking_smoothing_alpha_) * static_cast<double>(cluster.centroid_y);
                
                track.centroid_z =
                    tracking_smoothing_alpha_ * track.centroid_z +
                    (1.0 - tracking_smoothing_alpha_) * static_cast<double>(cluster.centroid_z);

                track.sector_mask  = cluster.sector_mask;
                track.missed_count = 0;
                track.hit_count   += 1;
                track.age         += 1;

                track_used[best_track_index] = true;
            }
            // 3. 매칭 실패 → 새 track 생성
            else {
                TrackState new_track;
                new_track.track_id     = next_track_id_++;
                new_track.centroid_x   = cluster.centroid_x;
                new_track.centroid_y   = cluster.centroid_y;
                new_track.centroid_z   = cluster.centroid_z;
                new_track.sector_mask  = cluster.sector_mask;
                new_track.age          = 1;
                new_track.hit_count    = 1;
                new_track.missed_count = 0;

                cluster.track_id = new_track.track_id;
                cluster.tracked  = false;

                tracks_.push_back(new_track);
                track_used.push_back(true);
            }
        }

        // 4. 이번 프레임에서 매칭되지 않은 기준 track은 missed 증가
        for (std::size_t i = 0; i < tracks_.size(); ++i) {
            if (i < track_used.size() && !track_used[i]) {
                tracks_[i].missed_count += 1;
            }
        }

        // 5. 오래 미검출된 track 제거
        tracks_.erase(
            std::remove_if(
                tracks_.begin(),
                tracks_.end(),
                [this](const TrackState &track) {
                    return track.missed_count > tracking_max_missed_frames_;
                }
            ),
            tracks_.end()
        );
    }

    // nearest point의 각도(rad) 기준 대표 sector(FRONT/LEFT/RIGHT) 분류 함수
    uint8_t ObstacleClusterNode::classifySector(float azimuth_angle_rad) const
    {
        const double angle_deg = radToDeg(azimuth_angle_rad);

        if (angle_deg >= front_min_deg_ && angle_deg <= front_max_deg_) {
            return robot_interfaces::msg::ObstacleCluster::SECTOR_FRONT;
        }
        if (angle_deg >= left_min_deg_ && angle_deg <= left_max_deg_) {
            return robot_interfaces::msg::ObstacleCluster::SECTOR_LEFT;
        }
        if (angle_deg >= right_min_deg_ && angle_deg <= right_max_deg_) {
            return robot_interfaces::msg::ObstacleCluster::SECTOR_RIGHT;
        }

        return robot_interfaces::msg::ObstacleCluster::SECTOR_NONE;
    }

    // cluster 내부 point들의 sector 점유 mask 계산 함수
    uint8_t ObstacleClusterNode::classifySectorMask(float azimuth_angle_rad) const
    {
        const double angle_deg = radToDeg(azimuth_angle_rad);
    
        // sector_mask는 bit flag 조합 (LEFT = 4, FRONT = 2, RIGHT = 1)
        // 예: FRONT | RIGHT = 2 | 1 = 3 (011)
        uint8_t mask = robot_interfaces::msg::ObstacleCluster::SECTOR_NONE;

        // RIGHT 영역에 point가 하나라도 존재하면 RIGHT bit ON (mask |= 1)
        if (angle_deg >= right_min_deg_ && angle_deg <= right_max_deg_) {
            mask |= robot_interfaces::msg::ObstacleCluster::SECTOR_RIGHT;
        }

        // FRONT 영역에 point가 하나라도 존재하면 FRONT bit ON (mask |= 2)
        if (angle_deg >= front_min_deg_ && angle_deg <= front_max_deg_) {
            mask |= robot_interfaces::msg::ObstacleCluster::SECTOR_FRONT;
        }

        // LEFT 영역에 point가 하나라도 존재하면 LEFT bit ON (mask |= 4)
        if (angle_deg >= left_min_deg_ && angle_deg <= left_max_deg_) {
            mask |= robot_interfaces::msg::ObstacleCluster::SECTOR_LEFT;
        }

        return mask;
    }


    // 클러스터별 색상 반영 함수
    std::tuple<uint8_t, uint8_t, uint8_t> ObstacleClusterNode::getClusterColor(int cluster_id) const
    {
        switch (cluster_id %8) {
            case 0: return {255,   0,   0};   // red
            case 1: return {  0, 255,   0};   // green
            case 2: return {  0,   0, 255};   // blue
            case 3: return {255, 255,   0};   // yellow
            case 4: return {255,   0, 255};   // magenta
            case 5: return {  0, 255, 255};   // cyan
            case 6: return {255, 128,   0};   // orange
            case 7: return {128,   0, 255};   // purple
            default: return {255, 255, 255};
        }
    }

    void ObstacleClusterNode::pointCloudCallback(
        const sensor_msgs::msg::PointCloud2::SharedPtr msg)
    {
        // ================================
        // [1] ROS PointCloud2 -> PCL 변환
        // ================================
        pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>());
        pcl::fromROSMsg(*msg, *cloud);

        if (cloud->empty()) {
            /*
            입력 PointCloud가 비어 있는 상황도 정상적인 "장애물 없음" 상태일 수 있다.

            기존에는 여기서 바로 return했기 때문에:
                - /perception/lidar/obstacle_clusters가 publish되지 않음
                - obstacle_model_node callback이 호출되지 않음
                - /perception/lidar/obstacle_model publish가 멈춤
                - local_path_planner_node에서 obstacle_model timeout 발생
                - invalid obstacle model 상태로 전이되어 hold path 생성

            따라서 cloud가 비어 있어도 빈 ObstacleClusters 메시지를 publish한다.
            이 메시지의 의미는 "Perception pipeline은 살아 있고, 이번 LiDAR frame에서
            obstacle cluster가 검출되지 않았다"는 뜻이다.
            */
            robot_interfaces::msg::ObstacleClusters out;
            out.header = msg->header;

            /*
            tracking_enabled_가 true인 경우, 장애물이 없는 frame에서도 기존 track의
            missed_count를 증가시켜야 오래 미검출된 track이 정리된다.
            assignTrackIds()는 빈 cluster vector에 대해서도 기존 track의 missed_count를
            증가시키고 tracking_max_missed_frames_를 넘은 track을 제거한다.
            */
            std::vector<robot_interfaces::msg::ObstacleCluster> empty_cluster_msgs;
            assignTrackIds(empty_cluster_msgs);

            cluster_pub_->publish(out);

            /*
            RViz 디버그용 colored cloud도 빈 PointCloud2로 publish한다.
            이렇게 해야 이전 frame의 colored cluster가 RViz에 남아 보이는 현상을 줄일 수 있다.
            */
            pcl::PointCloud<pcl::PointXYZRGB>::Ptr empty_colored_cloud(
                new pcl::PointCloud<pcl::PointXYZRGB>()
            );

            empty_colored_cloud->width = 0;
            empty_colored_cloud->height = 1;
            empty_colored_cloud->is_dense = true;

            sensor_msgs::msg::PointCloud2 empty_colored_cloud_msg;
            pcl::toROSMsg(*empty_colored_cloud, empty_colored_cloud_msg);
            empty_colored_cloud_msg.header = msg->header;

            clustered_cloud_pub_->publish(empty_colored_cloud_msg);

            RCLCPP_INFO_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                2000,
                "filtered cloud is empty. Publish empty obstacle clusters."
            );

            return;
        }
        // =========================================================
        // [2] KD-Tree 생성
        // - 3D 공간에서 가장 가까운 이웃 점을 빠르게 찾기 위한 자료구조
        // ==========================================================
        pcl::search::KdTree<pcl::PointXYZ>::Ptr tree(new pcl::search::KdTree<pcl::PointXYZ>());
        tree->setInputCloud(cloud);

        // ==========================
        // [3] Euclidean clustering
        // ==========================
        std::vector<pcl::PointIndices> cluster_indices;

        pcl::EuclideanClusterExtraction<pcl::PointXYZ> ec;  // Euclidean clustering 알고리즘 객체
        ec.setClusterTolerance(cluster_tolerance_);         // 같은 클러스터로 묶는 거리 기준
        ec.setMinClusterSize(min_cluster_size_);            // cluster로 인정할 최소 point 수
        ec.setMaxClusterSize(max_cluster_size_);            // cluster로 인정할 최대 point 수
        ec.setSearchMethod(tree);                           // 이웃 탐색에 활용할 tree 지정(KD-Tree)
        ec.setInputCloud(cloud);                            // clustering을 수행할 입력 점군 지정 (filtered_points)
        ec.extract(cluster_indices);                        // clustering 실행
        
        // At time Cluster creation complete!

        // ====================================================
        // [3-1] cluster 정렬을 위한 임시 구조체 생성
        //      가장 가까운 cluster부터 색을 안정적으로 부여하기 위함
        // ====================================================
        struct ClusterOrderInfo
        {
            pcl::PointIndices indices;      // cluster의 point index 묶음

            double nearest_distance_xy;     // base_link 기준 최근접 거리
            double nearest_x;               // base_link 기준 최근접 point x
            double nearest_y;               // base_link 기준 최근접 point y 
            double nearest_z;               // base_link 기준 최근접 point z
        };

        std::vector<ClusterOrderInfo> sorted_clusters;      // 정렬용 cluster 목록
        sorted_clusters.reserve(cluster_indices.size());

        // 각 cluster의 대표 base_link 기준 최근접 거리 계산
        for (const auto & indices : cluster_indices) {
            double nearest_dist_xy = std::numeric_limits<double>::infinity();
            double nearest_x = 0.0;
            double nearest_y = 0.0;
            double nearest_z = 0.0;

            for (const int index : indices.indices) {
                const auto & pt = cloud->points[index];
                const double dist_xy = std::sqrt(pt.x * pt.x + pt.y * pt.y);

                if (dist_xy < nearest_dist_xy) {
                    nearest_dist_xy = dist_xy;
                    nearest_x = pt.x;
                    nearest_y = pt.y;
                    nearest_z = pt.z;
                }
            }

            ClusterOrderInfo info;
            info.indices = indices;
            info.nearest_distance_xy = nearest_dist_xy;
            info.nearest_x = nearest_x;
            info.nearest_y = nearest_y;
            info.nearest_z = nearest_z;

            sorted_clusters.push_back(info);
        }

        // nearest_distance_xy 기준으로 오름차순 정렬
        std::sort(
            sorted_clusters.begin(),
            sorted_clusters.end(),
            [](const ClusterOrderInfo & a, const ClusterOrderInfo & b) {
                return a.nearest_distance_xy < b.nearest_distance_xy;
            }
        );

        // ====================
        // [4] 결과 메시지 생성
        // ====================
        robot_interfaces::msg::ObstacleClusters out;
        out.header = msg->header;

        // ==============================================
        // [5] Cluster descriptor 계산 및 시각화 cloud 생성
        // ==============================================
        
        // pcl::PointCloud<pcl::PointXYZRGB>::Ptr colored_cloud(new pcl::PointCloud<pcl::PointXYZRGB>());   // colored cloud 객체 생성

        // uint32_t cluster_id = 0;  // cluster id
        // std::ostringstream cluster_debug_stream;

        std::vector<robot_interfaces::msg::ObstacleCluster> cluster_msgs;
        std::vector<pcl::PointIndices> cluster_indices_for_color;

        cluster_msgs.reserve(sorted_clusters.size());
        cluster_indices_for_color.reserve(sorted_clusters.size());

        uint32_t cluster_id = 0;

        // 정렬된 sorted_clusters 순회
        // cluster_info는 cluster indices와 정렬 전 계산한 nearest point 정보를 포함
        for (const auto & cluster_info : sorted_clusters) {
            const auto & indices = cluster_info.indices;

            // [5-1] cluster_msg 생성
            robot_interfaces::msg::ObstacleCluster cluster_msg;
            cluster_msg.header = msg->header;
            cluster_msg.valid = true;
            cluster_msg.cluster_id = cluster_id;
            cluster_msg.point_count = static_cast<uint32_t>(indices.indices.size());    // 각 cluster를 이루는 총 point 갯수

            // centroid 계산용 합계
            double sum_x = 0.0;
            double sum_y = 0.0;
            double sum_z = 0.0;

            // bbox 계산용 min/max
            double min_x = std::numeric_limits<double>::infinity();
            double min_y = std::numeric_limits<double>::infinity();
            double min_z = std::numeric_limits<double>::infinity();

            double max_x = -std::numeric_limits<double>::infinity();
            double max_y = -std::numeric_limits<double>::infinity();
            double max_z = -std::numeric_limits<double>::infinity();

            // base_link 기준 nearest point 정보는 정렬 전 단계에서 이미 계산한 값을 재사용
            const double nearest_dist_xy = cluster_info.nearest_distance_xy;
            const double nearest_x = cluster_info.nearest_x;
            const double nearest_y = cluster_info.nearest_y;
            const double nearest_z = cluster_info.nearest_z;

            // cluster가 점유하는 모든 sector mask 담을 변수 선언
            uint8_t sector_mask = robot_interfaces::msg::ObstacleCluster::SECTOR_NONE;

            // [5-2] cluster별 색상 선택
            //const auto [r, g, b] = getClusterColor(static_cast<int>(cluster_id));

            // [5-3] cluster 내부 points 순회
            for (const int index : indices.indices) {
                const auto & pt = cloud->points[index];

                // centroid 계산용 합계 누적
                sum_x += pt.x;
                sum_y += pt.y;
                sum_z += pt.z;

                // bbox min 갱신
                if (pt.x < min_x) min_x = pt.x;
                if (pt.y < min_y) min_y = pt.y;
                if (pt.z < min_z) min_z = pt.z;

                // bbox max 갱신
                if (pt.x > max_x) max_x = pt.x;
                if (pt.y > max_y) max_y = pt.y;
                if (pt.z > max_z) max_z = pt.z;

                // colored cloud에 point 추가
                // pcl::PointXYZRGB rgb_pt;
                // rgb_pt.x = pt.x;
                // rgb_pt.y = pt.y;
                // rgb_pt.z = pt.z;
                // rgb_pt.r = r;
                // rgb_pt.g = g;
                // rgb_pt.b = b;
                // colored_cloud->points.push_back(rgb_pt);

                // sector mask 갱신
                const double point_azimuth_rad = std::atan2(pt.y, pt.x);
                sector_mask |= classifySectorMask(static_cast<float>(point_azimuth_rad));
            }

            // ===============================
            // [6] Cluster descriptor 최종 계산
            // ===============================

            // cluster 중심점 (centroid) 계산
            const double point_count = static_cast<double>(indices.indices.size());

            const double centroid_x = sum_x / point_count;
            const double centroid_y = sum_y / point_count;
            const double centroid_z = sum_z / point_count;
            
            // nearest_distance_xy : 클러스터의 가장 가까운 평면 Point과의 거리 (base_link 기준)
            const double nearest_distance_xy = nearest_dist_xy;

            // centroid_distance_xy : 클러스터 중심 Point과의 거리 (base_link 기준)
            const double centroid_distance_xy =
                std::sqrt(centroid_x * centroid_x + centroid_y * centroid_y);

            // azimuth : 수평 방향각 (base_link 기준)
            const double azimuth_angle_rad =            
                std::atan2(nearest_y, nearest_x);

            // elevation : 수직 방향각 (base_link 기준)
            const double elevation_angle_rad =
                std::atan2(nearest_z, nearest_distance_xy);

            // bbox 계산
            const double bbox_size_x = max_x - min_x;
            const double bbox_size_y = max_y - min_y;
            const double bbox_size_z = max_z - min_z;

            // =================
            // [7] 메시지 채우기
            // =================
            cluster_msg.centroid_x = static_cast<float>(centroid_x);
            cluster_msg.centroid_y = static_cast<float>(centroid_y);
            cluster_msg.centroid_z = static_cast<float>(centroid_z);

            cluster_msg.nearest_x = static_cast<float>(nearest_x);
            cluster_msg.nearest_y = static_cast<float>(nearest_y);
            cluster_msg.nearest_z = static_cast<float>(nearest_z);

            cluster_msg.centroid_distance_xy = static_cast<float>(centroid_distance_xy);
            cluster_msg.nearest_distance_xy = static_cast<float>(nearest_distance_xy);

            cluster_msg.azimuth_angle_rad = static_cast<float>(azimuth_angle_rad);
            cluster_msg.elevation_angle_rad = static_cast<float>(elevation_angle_rad);

            cluster_msg.bbox_size_x = static_cast<float>(bbox_size_x);
            cluster_msg.bbox_size_y = static_cast<float>(bbox_size_y);
            cluster_msg.bbox_size_z = static_cast<float>(bbox_size_z);

            // sector는 nearest point 기준 대표 위험 방향
            cluster_msg.sector = classifySector(static_cast<float>(azimuth_angle_rad));
            
            // sector_mask는 cluster 내부 point들이 실제로 점유하는 모든 sector
            cluster_msg.sector_mask = sector_mask;

            // cluster_debug_stream
            //     << "\ncluster[" << cluster_id << "]"
            //     << " | points=" << cluster_msg.point_count
            //     << " | nearest=" << cluster_msg.nearest_distance_xy
            //     << " | azimuth=" << cluster_msg.azimuth_angle_rad
            //     << " (" << radToDeg(cluster_msg.azimuth_angle_rad) << "deg)"
            //     << " | sector=" << static_cast<int>(cluster_msg.sector)
            //     << " | sector_mask=" << static_cast<int>(cluster_msg.sector_mask);
            
            // out.clusters.push_back(cluster_msg);

            // tracking 전 임시 저장
            cluster_msgs.push_back(cluster_msg);
            cluster_indices_for_color.push_back(indices);

            cluster_id++;
        }

        // [8] tracking 적용
        assignTrackIds(cluster_msgs);

        // [9] colored cloud 생성
        pcl::PointCloud<pcl::PointXYZRGB>::Ptr colored_cloud(
            new pcl::PointCloud<pcl::PointXYZRGB>()
        );

        std::ostringstream cluster_debug_stream;

        for (std::size_t i = 0; i < cluster_msgs.size(); ++i) {
            const auto & cluster_msg = cluster_msgs[i];
            const auto & indices = cluster_indices_for_color[i];

            const int color_id =
                (color_by_track_id_ && cluster_msg.track_id > 0)
                    ? static_cast<int>(cluster_msg.track_id)
                    : static_cast<int>(cluster_msg.cluster_id);

            const auto [r, g, b] = getClusterColor(color_id);

            for (const int index : indices.indices) {
                const auto & pt = cloud->points[index];

                pcl::PointXYZRGB rgb_pt;
                rgb_pt.x = pt.x;
                rgb_pt.y = pt.y;
                rgb_pt.z = pt.z;
                rgb_pt.r = r;
                rgb_pt.g = g;
                rgb_pt.b = b;

                colored_cloud->points.push_back(rgb_pt);
            }

            cluster_debug_stream
                << "\ncluster[" << cluster_msg.cluster_id << "]"
                << " | track_id=" << cluster_msg.track_id
                << " | tracked=" << (cluster_msg.tracked ? "true" : "false")
                << " | points=" << cluster_msg.point_count
                << " | nearest=" << cluster_msg.nearest_distance_xy
                << " | azimuth=" << cluster_msg.azimuth_angle_rad
                << " (" << radToDeg(cluster_msg.azimuth_angle_rad) << "deg)"
                << " | sector=" << static_cast<int>(cluster_msg.sector)
                << " | sector_mask=" << static_cast<int>(cluster_msg.sector_mask);

            out.clusters.push_back(cluster_msg);
        }


        // [10] colored cloud 메타데이터 설정
        colored_cloud->width     = static_cast<uint32_t>(colored_cloud->points.size());
        colored_cloud->height    = 1;
        colored_cloud->is_dense  = true;

        // ==============================================
        // [11] PCL Point Cloud -> ROS PointCloud2로 변환
        // ==============================================
        sensor_msgs::msg::PointCloud2 colored_cloud_msg;
        pcl::toROSMsg(*colored_cloud, colored_cloud_msg);
        colored_cloud_msg.header = msg->header;

        // =============
        // [12] 로그 출력
        // =============
        const std::string cluster_debug_str = cluster_debug_stream.str();

        RCLCPP_INFO_THROTTLE(
            this->get_logger(),
            *this->get_clock(),
            1000,
            "\ninput_cloud | frame=%s | points=%zu"
            "\ncluster_cfg | tolerance=%.2f | min=%d | max=%d"
            "\ntracking    | enabled=%s | tracks=%zu | match_dist=%.2f | max_missed=%d | alpha=%.2f"
            "\noutput      | clusters=%zu"
            "%s",
            msg->header.frame_id.c_str(),
            cloud->size(),
            cluster_tolerance_,
            min_cluster_size_,
            max_cluster_size_,
            tracking_enabled_ ? "true" : "false",
            tracks_.size(),
            tracking_match_distance_,
            tracking_max_missed_frames_,
            tracking_smoothing_alpha_,
            out.clusters.size(),
            cluster_debug_str.c_str()
        );

        // ==============
        // [13] publish
        // ==============
        cluster_pub_->publish(out);
        clustered_cloud_pub_->publish(colored_cloud_msg);
    }
}

int main(int argc, char ** argv)
{
    rclcpp::init(argc, argv);

    auto node = std::make_shared<lidar_perception::ObstacleClusterNode>();
    rclcpp::spin(node);

    rclcpp::shutdown();
    return 0;
}