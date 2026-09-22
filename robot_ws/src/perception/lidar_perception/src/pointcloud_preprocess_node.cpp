/*
- 위치 : ~/robot_ws/src/perception/lidar_perception/src/pointcloud_preprocess_node.cpp
- 역할  : PointCloud 전처리 노드
- Input : /scan_3D (sensor_msgs/msg/PointCloud2)
- Output: /perception/lidar/points_filtered
- 기능 :
    - PointCloud2 수신                      (완)
    - ROS PointCloud2 → PCL PointCloud 변환 (완)
    - NaN / Inf / zero point 제거           (완)
    - laser_frame → base_link TF 변환        (완)
    - ROI crop                              (완)
    - voxel downsample                      (완)
    - ground / noise 제거                   (확장) 추후 바닥 클러스터가 계속 잡히면 ground removal 추가 구현
*/
#include "lidar_perception/pointcloud_preprocess_node.hpp"

#include <cmath>        // For NaN/Inf/zero point
#include <memory>
#include <string>
#include <cstdint>
#include <Eigen/Geometry>

#include "geometry_msgs/msg/transform_stamped.hpp"

#include "pcl/filters/passthrough.h"                // PCL(Point Cloud Library)
#include "pcl/filters/voxel_grid.h"                 // PCL's VoxelGrid(점군 다운샘플링 필터)
#include "pcl/point_cloud.h"                        // PCL's 3D 점군 자료구조 -  pcl::PointCloud<pcl::PointXYZ>
#include "pcl/point_types.h"                        // PCL's 점군 Type - pcl::PointXYZ
#include "pcl/common/transforms.h"                  // pcl::transformPointCloud()

#include "pcl_conversions/pcl_conversions.h"        // ROS (PointCloud2) ↔ PCL (PointCloud<PointXYZ>) 변환 헤더

#include "tf2/time.h"
#include "tf2_sensor_msgs/tf2_sensor_msgs.hpp"      // PointCloud2에 TF 변환을 적용하는 기능을 추가해주는 헤더

namespace lidar_perception
{
    PointcloudPreprocessNode::PointcloudPreprocessNode()
    : Node("pointcloud_preprocess_node")
    {
        // [1] 파라미터 선언
        // target frame
        this->declare_parameter<std::string>("target_frame", "base_link");

        // ROI 범위 (Spot 기준 전방/좌우/높이 제한)
        this->declare_parameter<double>("roi_min_x", 0.0);
        this->declare_parameter<double>("roi_max_x", 3.0);

        this->declare_parameter<double>("roi_min_y", -2.0);
        this->declare_parameter<double>("roi_max_y",  2.0);

        this->declare_parameter<double>("roi_min_z", -0.2);
        this->declare_parameter<double>("roi_max_z",  1.5);

        // voxel downsampling Leaf size
        this->declare_parameter<double>("voxel_leaf_size", 0.05);

        // invalid point filtering (NaN, Inf, Zero Point 제거 파라미터)
        this->declare_parameter<bool>("remove_invalid_points", true);
        this->declare_parameter<bool>("remove_zero_points", true);
        this->declare_parameter<double>("zero_point_epsilon", 1e-6);

        // [2] 파라미터 읽기
        target_frame_    = this->get_parameter("target_frame").as_string();

        roi_min_x_       = this->get_parameter("roi_min_x").as_double();
        roi_max_x_       = this->get_parameter("roi_max_x").as_double();

        roi_min_y_       = this->get_parameter("roi_min_y").as_double();
        roi_max_y_       = this->get_parameter("roi_max_y").as_double();

        roi_min_z_       = this->get_parameter("roi_min_z").as_double();
        roi_max_z_       = this->get_parameter("roi_max_z").as_double();

        voxel_leaf_size_ = this->get_parameter("voxel_leaf_size").as_double();

        remove_invalid_points_ = this->get_parameter("remove_invalid_points").as_bool();
        remove_zero_points_    = this->get_parameter("remove_zero_points").as_bool();
        zero_point_epsilon_    = this->get_parameter("zero_point_epsilon").as_double();

        // [3] TF buffer / listener 생성
        tf_buffer_   = std::make_unique<tf2_ros::Buffer>(this->get_clock());        // base_link 기준 lidar 좌표 값 TF Tree
        tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_);

        // [4] Subscriber / Publisher 생성
        input_cloud_sub_ = this->create_subscription<sensor_msgs::msg::PointCloud2>(
            "/scan_3D",
            rclcpp::SensorDataQoS(),
            std::bind(&PointcloudPreprocessNode::pointCloudCallback, this, std::placeholders::_1)
        );

        filtered_cloud_pub_ = this->create_publisher<sensor_msgs::msg::PointCloud2>(
            "/perception/lidar/points_filtered",
            10
        );

        RCLCPP_INFO(this->get_logger(), "pointcloud_preprocess_node started!");
    }

    void PointcloudPreprocessNode::pointCloudCallback(
        const sensor_msgs::msg::PointCloud2::SharedPtr msg)
    {
        if (msg->data.empty()) {
            RCLCPP_WARN_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                2000,
                "input /scan_3D is empty"
            );
            return;
        }

        // [1] Raw ROS PointCloud2 -> PCL PointCloud 변환
        // - 아직 frame 의미는 msg->header.frame_id, 즉 보통 laser_frame 기준
        pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_raw(new pcl::PointCloud<pcl::PointXYZ>());
        pcl::fromROSMsg(*msg, *cloud_raw);

        // 입력 점군이 비어있으면 종료
        if (cloud_raw->empty()) {
            RCLCPP_WARN_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                2000,
                "raw /scan_3D cloud is empty"
            );
            return;            
        }

        // [2] TF 변환 전에 NaN / Inf / Zero point 제거
        pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_raw_valid(new pcl::PointCloud<pcl::PointXYZ>());
        cloud_raw_valid->points.reserve(cloud_raw->points.size());

        std::size_t nan_count  = 0;
        std::size_t inf_count  = 0;
        std::size_t zero_count = 0;

        for (const auto & point : cloud_raw->points) {
            const bool has_nan =
                std::isnan(point.x) || std::isnan(point.y) || std::isnan(point.z);

            const bool has_inf =
                std::isinf(point.x) || std::isinf(point.y) || std::isinf(point.z);

            if (has_nan) {
                nan_count++;                        // 발견 개수는 항상 기록
                if (remove_invalid_points_) {       // 제거 옵션이 켜져 있으면 제거
                    continue;
                }
            }

            if (has_inf) {
                inf_count++;
                if (remove_invalid_points_) {
                    continue;
                }
            }

            const bool is_zero_point =
                std::abs(point.x) <= zero_point_epsilon_ &&
                std::abs(point.y) <= zero_point_epsilon_ &&
                std::abs(point.z) <= zero_point_epsilon_;

            if (is_zero_point) {
                    zero_count++;
                    if (remove_zero_points_) {
                        continue;
                    }
                }

            cloud_raw_valid->points.push_back(point);
        }

        cloud_raw_valid->width    = static_cast<std::uint32_t>(cloud_raw_valid->points.size());
        cloud_raw_valid->height   = 1;
        cloud_raw_valid->is_dense = remove_invalid_points_ ? true : cloud_raw->is_dense;
        
        if (cloud_raw_valid->empty()) {
            RCLCPP_WARN_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                2000,
                "valid raw cloud is empty after invalid/zero point filtering"
            );
            return;            
        }

        // [3] TF 변환 정보 조회
        // - source frame: msg->header.frame_id, 보통 laser_frame
        // - target frame: target_frame_, 현재 base_link
        geometry_msgs::msg::TransformStamped transform_stamped;

        try {
            transform_stamped = tf_buffer_->lookupTransform(
                target_frame_,          // target frame: base_link
                msg->header.frame_id,   // source frame: laser_frame
                tf2::TimePointZero      // 최신 available transform 사용
            );
        }
        catch (const tf2::TransformException &ex) {
            RCLCPP_WARN_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                2000,
                "TF transform failed: %s",
                ex.what()
            );
            return;
        }

        // [4] TransformStamped -> Eigen::Affine3f 변환
        // - lookupTransform(base_link, laser_frame)으로 얻은 변환을
        //   PCL PointCloud에 직접 적용하기 위한 행렬로 변환
        const auto & t = transform_stamped.transform.translation;
        const auto & q = transform_stamped.transform.rotation;

        Eigen::Translation3f translation(
            static_cast<float>(t.x),
            static_cast<float>(t.y),
            static_cast<float>(t.z)
        );

        // 주의:
        // ROS Quaternion 메시지 필드 순서: x, y, z, w
        // Eigen::Quaternionf 생성자 순서: w, x, y, z
        Eigen::Quaternionf rotation(
            static_cast<float>(q.w),
            static_cast<float>(q.x),
            static_cast<float>(q.y),
            static_cast<float>(q.z)
        );

        rotation.normalize();

        Eigen::Affine3f tf_eigen = translation * rotation;

        // [5] PCL PointCloud에 직접 TF 적용
        // - cloud_raw_valid: laser_frame 기준
        // - cloud_input    : base_link 기준
        pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_input(new pcl::PointCloud<pcl::PointXYZ>());

        pcl::transformPointCloud(
            *cloud_raw_valid,
            *cloud_input,
            tf_eigen
        );

        cloud_input->width = static_cast<std::uint32_t>(cloud_input->points.size());
        cloud_input->height = 1;
        cloud_input->is_dense = cloud_raw_valid->is_dense;

        if (cloud_input->empty()) {
            RCLCPP_WARN_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                2000,
                "transformed cloud is empty"
            );
            return;
        }

        // [6] ROI crop : x, y, z 범위 밖의 점 제거
        pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_roi_x(new pcl::PointCloud<pcl::PointXYZ>());
        pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_roi_y(new pcl::PointCloud<pcl::PointXYZ>());
        pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_roi_z(new pcl::PointCloud<pcl::PointXYZ>());

        pcl::PassThrough<pcl::PointXYZ> pass;

        // x 범위 필터
        pass.setInputCloud(cloud_input);
        pass.setFilterFieldName("x");
        pass.setFilterLimits(roi_min_x_, roi_max_x_);
        pass.filter(*cloud_roi_x);

        // y 범위 필터
        pass.setInputCloud(cloud_roi_x);
        pass.setFilterFieldName("y");
        pass.setFilterLimits(roi_min_y_, roi_max_y_);
        pass.filter(*cloud_roi_y);

        // z 범위 필터
        pass.setInputCloud(cloud_roi_y);
        pass.setFilterFieldName("z");
        pass.setFilterLimits(roi_min_z_, roi_max_z_);
        pass.filter(*cloud_roi_z);

        if (cloud_roi_z->empty()) {
            RCLCPP_INFO_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                2000,
                "ROI filtered cloud is empty"
            );
        }

        // [7] Voxel downsampling : 점이 너무 많으면 계산량이 커지므로 다운샘플링
        pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_downsampled(new pcl::PointCloud<pcl::PointXYZ>());

        pcl::VoxelGrid<pcl::PointXYZ> voxel_filter;
        voxel_filter.setInputCloud(cloud_roi_z);
        voxel_filter.setLeafSize(
            static_cast<float>(voxel_leaf_size_),
            static_cast<float>(voxel_leaf_size_),
            static_cast<float>(voxel_leaf_size_)
        );

        voxel_filter.filter(*cloud_downsampled);

        cloud_downsampled->width = static_cast<std::uint32_t>(cloud_downsampled->points.size());
        cloud_downsampled->height = 1;
        cloud_downsampled->is_dense = true;
        
        // [8] PCL PointCloud -> ROS PointCloud2 변환
        sensor_msgs::msg::PointCloud2 output_cloud_msg;
        pcl::toROSMsg(*cloud_downsampled, output_cloud_msg);

        output_cloud_msg.header.stamp    = msg->header.stamp;
        output_cloud_msg.header.frame_id = target_frame_;

        // [9] 디버그 로그
        RCLCPP_INFO_THROTTLE(
            this->get_logger(),
            *this->get_clock(),
            1000,
            "\nraw    | frame=%s | points=%zu"
            "\nvalid  | points=%zu | nan=%zu inf=%zu zero=%zu"
            "\ntf     | frame=%s | points=%zu"
            "\nroi_x  | x=[%.2f, %.2f] | points=%zu"
            "\nroi_y  | y=[%.2f, %.2f] | points=%zu"
            "\nroi_z  | z=[%.2f, %.2f] | points=%zu"
            "\nvoxel  | leaf=%.2f"
            "\noutput | frame=%s | points=%zu",
            msg->header.frame_id.c_str(),
            cloud_raw->size(),
            cloud_raw_valid->size(),
            nan_count,
            inf_count,
            zero_count,
            target_frame_.c_str(),
            cloud_input->size(),
            roi_min_x_, roi_max_x_, cloud_roi_x->size(),
            roi_min_y_, roi_max_y_, cloud_roi_y->size(),
            roi_min_z_, roi_max_z_, cloud_roi_z->size(),
            voxel_leaf_size_,
            output_cloud_msg.header.frame_id.c_str(),
            cloud_downsampled->size()
        );
        
        // [10] publish
        filtered_cloud_pub_->publish(output_cloud_msg);
    }
}

int main(int argc, char ** argv)
{
    rclcpp::init(argc, argv);

    auto node = std::make_shared<lidar_perception::PointcloudPreprocessNode>();
    rclcpp::spin(node);

    rclcpp::shutdown();

    return 0;
}