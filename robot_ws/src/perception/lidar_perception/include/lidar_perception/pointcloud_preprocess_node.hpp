// 경로 : ~/robot_ws/src/perception/lidar_perception/include/lidar_perception/pointcloud_preprocess_node.hpp
#ifndef LIDAR_PERCEPTION__POINTCLOUD_PREPROCESS_NODE_HPP_
#define LIDAR_PERCEPTION__POINTCLOUD_PREPROCESS_NODE_HPP_

#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"

#include "tf2_ros/buffer.h"
#include "tf2_ros/transform_listener.h"

namespace lidar_perception
{
    // /scan_3D 원본 PointCloud2를 받아서
    // 1) base_Link 기준으로 변환
    // 2) ROI crop
    // 3) voxel downsampling
    class PointcloudPreprocessNode : public rclcpp::Node
    {
        public:
            PointcloudPreprocessNode();

        private:
            // PointCloud2 콜백 함수
            void pointCloudCallback(const sensor_msgs::msg::PointCloud2::SharedPtr msg);

            // 입력 토픽 구독자
            rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr input_cloud_sub_;

            // 전처리된 점군 발행자
            rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr filtered_cloud_pub_;

            // TF buffer / listener
            std::unique_ptr<tf2_ros::Buffer> tf_buffer_;
            std::shared_ptr<tf2_ros::TransformListener> tf_listener_;

            // target frame
            std::string target_frame_;

            // ROI 파라미터
            double roi_min_x_;
            double roi_max_x_;
            double roi_min_y_;
            double roi_max_y_;
            double roi_min_z_;
            double roi_max_z_;

            // voxel Leaf size
            double voxel_leaf_size_;

            // invalid point filtering
            bool remove_invalid_points_;
            bool remove_zero_points_;
            double zero_point_epsilon_;
    };
}

#endif