/*
- 경로 : ~/robot_ws/src/perception/lidar_perception/src/local_occupancy_grid_node.cpp
- 역할 : 전처리된 LiDAR PointCloud를 base_link 기준 로컬 OccupancyGrid로 변환
- frame_id: base_link
- 입력 : /perception/lidar/points_filtered
- 출력 : /perception/lidar/local_occupancy_grid
- 기능 :
    - PointCloud2 수신
    - PointCloud2 → PCL 변환
    - base_link 기준 x, y 좌표를 grid cell index로 변환
    - 전체 grid를 unknown 또는 free로 초기화
    - LiDAR ray 경로를 free cell로 표시
    - 장애물 point가 존재하는 cell을 occupied로 표시
    - 필요 시 obstacle inflation 적용
    - nav_msgs/msg/OccupancyGrid publish
*/

#include "lidar_perception/local_occupancy_grid_node.hpp"

#include <cmath>
#include <cstdlib>
#include <functional>
#include <utility>
#include <vector>

#include "pcl/point_cloud.h"
#include "pcl/point_types.h"
#include "pcl_conversions/pcl_conversions.h"

namespace lidar_perception
{
    LocalOccupancyGridNode::LocalOccupancyGridNode()
    : Node("local_occupancy_grid_node")
    {
        // [1] Subscriber / Publisher 선언 및 읽기
        input_topic_ = this->declare_parameter<std::string>(
            "input_topic",
            "/perception/lidar/points_filtered"
        );

        output_topic_ = this->declare_parameter<std::string>(
            "output_topic",
            "/perception/lidar/local_occupancy_grid"
        );

        // [2] Parameters 선언 및 읽기
        frame_id_ = this->declare_parameter<std::string>("frame_id", "base_link");

        origin_x_ = this->declare_parameter<double>("origin_x", -0.5);
        origin_y_ = this->declare_parameter<double>("origin_y", -2.5);

        size_x_ = this->declare_parameter<double>("size_x", 4.5);
        size_y_ = this->declare_parameter<double>("size_y", 5.0);

        resolution_ = this->declare_parameter<double>("resolution", 0.10);

        unknown_value_  = this->declare_parameter<int>("unknown_value", -1);
        free_value_     = this->declare_parameter<int>("free_value", 0);
        occupied_value_ = this->declare_parameter<int>("occupied_value", 100);

        min_z_ = this->declare_parameter<double>("min_z", -0.10);
        max_z_ = this->declare_parameter<double>("max_z", 1.20);

        min_range_ = this->declare_parameter<double>("min_range", 0.10);
        max_range_ = this->declare_parameter<double>("max_range", 4.50);

        use_ray_tracing_ = this->declare_parameter<bool>("use_ray_tracing", true);
        sensor_origin_x_ = this->declare_parameter<double>("sensor_origin_x", 0.14);
        sensor_origin_y_ = this->declare_parameter<double>("sensor_origin_y", 0.0);

        inflate_obstacles_ = this->declare_parameter<bool>("inflate_obstacles", true);
        inflation_radius_  = this->declare_parameter<double>("inflation_radius", 0.15);

        // ====================================================
        // [3] Grid 크기 계산
        // - width    = size_x / resolution = 4.5 / 0.1 = 45칸
        // - height   = size_y / resolution = 5.0 / 0.1 = 50칸
        // - 최종 구조 = 45 × 50개의 cell
        // =====================================================
        width_  = static_cast<int>(std::ceil(size_x_ / resolution_));
        height_ = static_cast<int>(std::ceil(size_y_ / resolution_));
        inflation_cells_ = static_cast<int>(std::ceil(inflation_radius_ / resolution_));

        // ===============================
        // [4] Publisher / Subscriber 생성
        // ===============================
        pointcloud_sub_ = this->create_subscription<sensor_msgs::msg::PointCloud2>(
            input_topic_,
            rclcpp::SensorDataQoS(),
            std::bind(&LocalOccupancyGridNode::pointCloudCallback, this, std::placeholders::_1)
        );

        occupancy_grid_pub_ = this->create_publisher<nav_msgs::msg::OccupancyGrid>(
            output_topic_,
            rclcpp::QoS(10)
        );

        RCLCPP_INFO(this->get_logger(), "local_occupancy_grid_node started!");
        RCLCPP_INFO(this->get_logger(), "input_topic  : %s", input_topic_.c_str());
        RCLCPP_INFO(this->get_logger(), "output_topic : %s", output_topic_.c_str());
        RCLCPP_INFO(this->get_logger(), "frame_id     : %s", frame_id_.c_str());
        RCLCPP_INFO(
            this->get_logger(),
            "grid        : origin=(%.2f, %.2f), size=(%.2f, %.2f), resolution=%.2f, width=%d, height=%d",
            origin_x_,
            origin_y_,
            size_x_,
            size_y_,
            resolution_,
            width_,
            height_
        );

        RCLCPP_INFO(
            this->get_logger(),
            "ray tracing : %s, sensor_origin=(%.2f, %.2f)",
            use_ray_tracing_ ? "true" : "false",
            sensor_origin_x_,
            sensor_origin_y_
        );
    }

    void LocalOccupancyGridNode::pointCloudCallback(
        const sensor_msgs::msg::PointCloud2::SharedPtr msg)
    {
        // [0] 입력 PointCloud frame 확인
        // local occupancy grid는 frame_id_ 기준 좌표라고 가정하고 생성된다.
        // 현재 파이프라인에서는 /points_filtered가 base_link 기준이어야 정상이다.
        if (msg->header.frame_id != frame_id_) {
            RCLCPP_WARN_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                2000,
                "Input cloud frame_id(%s) != occupancy grid frame_id(%s). "
                "Grid will be generated assuming points are already in %s frame.",
                msg->header.frame_id.c_str(),
                frame_id_.c_str(),
                frame_id_.c_str()
            );
        }

        // [1] ROS PointCloud2 → PCL PointCloud 변환
        pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>());
        pcl::fromROSMsg(*msg, *cloud);

        // [2] OccupancyGrid 메시지 생성
        nav_msgs::msg::OccupancyGrid grid_msg;

        // points_filtered의 timestamp를 유지
        grid_msg.header.stamp    = msg->header.stamp;
        grid_msg.header.frame_id = frame_id_;

        grid_msg.info.resolution = resolution_;
        grid_msg.info.width      = static_cast<uint32_t>(width_);
        grid_msg.info.height     = static_cast<uint32_t>(height_);

        // OccupancyGrid의 origin은 grid의 왼쪽 아래 기준점
        grid_msg.info.origin.position.x = origin_x_;        // grid의 왼쪽 아래 기준 좌표 x
        grid_msg.info.origin.position.y = origin_y_;        // grid의 왼쪽 아래 기준 좌표 y
        grid_msg.info.origin.position.z = 0.0;

        grid_msg.info.origin.orientation.x = 0.0;
        grid_msg.info.origin.orientation.y = 0.0;
        grid_msg.info.origin.orientation.z = 0.0;
        grid_msg.info.origin.orientation.w = 1.0;

        // [3] 전체 grid를 free로 초기화
        // - ray tracing을 쓰면 unknown으로 초기화
        // - ray tracing을 끄면 free로 초기화
        const int initial_value = use_ray_tracing_ ? unknown_value_ : free_value_;

        grid_msg.data.assign(
            width_ * height_,
            static_cast<int8_t>(initial_value)
        );

        int valid_point_count   = 0;
        int free_cell_count     = 0;
        int occupied_cell_count = 0;
        int duplicate_occupied_candidate_count = 0;        // 중복 endpoint 후보 개수 디버깅용

        // occupied_candidates : 장애물 위치 좌표 후보를 잠시 저장해두는 리스트
        std::vector<std::pair<int, int>> occupied_candidates;     
        occupied_candidates.reserve(cloud->size());

        // 같은 grid cell이 occupied 후보로 중복 저장되는 것을 방지
        const std::size_t total_cell_count =
            static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
        
        std::vector<bool> occupied_candidate_seen(total_cell_count, false);

        // LiDAR 위치 좌표 -> local grid 좌표로 변환
        const int sensor_grid_x = worldToGridX(sensor_origin_x_);
        const int sensor_grid_y = worldToGridY(sensor_origin_y_);

        // sensor origin이 grid 밖일 때 warning
        const bool sensor_origin_inside = isInsideGrid(sensor_grid_x, sensor_grid_y);

        if (use_ray_tracing_ && !sensor_origin_inside) {
            RCLCPP_WARN_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                2000,
                "sensor origin is outside local occupancy grid. "
                "ray tracing will be skipped for this frame. "
                "sensor_origin=(%.2f, %.2f), sensor_grid=(%d, %d), grid_size=(%d, %d)",
                sensor_origin_x_,
                sensor_origin_y_,
                sensor_grid_x,
                sensor_grid_y,
                width_,
                height_
            );
        }

        // [4] 1차 pass: 유효 point 수집 + ray 경로 free 표시
        for (const auto& point : cloud->points)
        {
            // [4-1] NaN / Inf 제거
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) continue;
            
            // [4-2] z 범위 필터링 : 바닥점이나 너무 높은 점이 grid에 들어가는 것을 방지
            if (point.z < min_z_ || point.z > max_z_) continue;

            // [4-3] base_link 기준 xy 거리 필터링
            const double range_xy = std::sqrt(point.x * point.x + point.y * point.y);

            if (range_xy < min_range_ || range_xy > max_range_) continue;

            // [4-4] worldToGridX/Y(): base_link 기준 좌표 → local occupancy grid 좌표 변환
            const int grid_x = worldToGridX(point.x);
            const int grid_y = worldToGridY(point.y);

            if (!isInsideGrid(grid_x, grid_y)) continue;

            valid_point_count++;

            const int candidate_index = toIndex(grid_x, grid_y);

            // 같은 endpoint cell은 한 번만 occupied 후보로 등록
            if (!occupied_candidate_seen[candidate_index]) {
                occupied_candidate_seen[candidate_index] = true;
                occupied_candidates.emplace_back(grid_x, grid_y);   // endpoint 후보 등록
 
                // 같은 endpoint cell에 대한 ray free marking도 한 번만 수행
                if (use_ray_tracing_ && sensor_origin_inside) {
                    free_cell_count += markRayFree(
                        grid_msg,
                        sensor_grid_x,
                        sensor_grid_y,
                        grid_x,
                        grid_y
                    );
                }
            } else {
                duplicate_occupied_candidate_count++;
            }
        }

        // [5] 2차 pass : endpoint occupied 표시
        // - free marking 이후 occupied를 찍어야 endpoint가 free로 덮이지 않는다
        for (const auto &cell : occupied_candidates) {
            const int grid_x = cell.first;
            const int grid_y = cell.second;

            if (inflate_obstacles_) occupied_cell_count += markInflatedOccupied(grid_msg, grid_x, grid_y);
            else occupied_cell_count += markOccupied(grid_msg, grid_x, grid_y);
        }

        // [6] 최종 unknown/free/occupied cell 갯수 측정
        int final_unknown_cell_count  = 0;
        int final_free_cell_count     = 0;
        int final_occupied_cell_count = 0;
        int final_other_cell_count    = 0;

        for (const auto &cell_value : grid_msg.data) {
            
            if (cell_value == static_cast<int8_t>(unknown_value_)) final_unknown_cell_count++;
            else if (cell_value == static_cast<int8_t>(free_value_)) final_free_cell_count++;
            else if (cell_value == static_cast<int8_t>(occupied_value_)) final_occupied_cell_count++;
            else final_other_cell_count++;
        }

        // [7] OccupancyGrid publish
        occupancy_grid_pub_->publish(grid_msg);

        // [8] 로그 출력
        RCLCPP_INFO_THROTTLE(
            this->get_logger(),
            *this->get_clock(),
            1000,
            "\nlocal occupancy grid"
            "\ninput points           : %zu"
            "\nvalid points           : %d"
            "\nunique occupied cand.  : %zu"
            "\nduplicate candidates   : %d"
            "\nnew free cells         : %d"
            "\nnew occupied cells     : %d"
            "\nfinal unknown cells    : %d"
            "\nfinal free cells       : %d"
            "\nfinal occupied cells   : %d"
            "\nfinal other cells      : %d"
            "\ngrid frame             : %s"
            "\ngrid origin            : (%.2f, %.2f)"
            "\ngrid size              : width=%d height=%d resolution=%.2f"
            "\nray tracing            : %s",
            cloud->size(),
            valid_point_count,
            occupied_candidates.size(),
            duplicate_occupied_candidate_count,
            free_cell_count,
            occupied_cell_count,
            final_unknown_cell_count,
            final_free_cell_count,
            final_occupied_cell_count,
            final_other_cell_count,
            grid_msg.header.frame_id.c_str(),
            origin_x_,
            origin_y_,
            width_,
            height_,
            resolution_,
            use_ray_tracing_ ? "true" : "false"
        );     
    }

    int LocalOccupancyGridNode::worldToGridX(const double x) const {
        return static_cast<int>((x - origin_x_) / resolution_);
    }

    int LocalOccupancyGridNode::worldToGridY(const double y) const {
        return static_cast<int>((y - origin_y_) / resolution_);
    }

    bool LocalOccupancyGridNode::isInsideGrid(const int grid_x, const int grid_y) const {
        return (
            grid_x >= 0 &&
            grid_x < width_ &&
            grid_y >= 0 &&
            grid_y < height_
        );
    }

    int LocalOccupancyGridNode::toIndex(const int grid_x, const int grid_y) const {
        return grid_y * width_ + grid_x;
    }

    int LocalOccupancyGridNode::markFree(
        nav_msgs::msg::OccupancyGrid & grid_msg,
        const int grid_x,
        const int grid_y)
    {
        if (!isInsideGrid(grid_x, grid_y)) return 0;

        const int index = toIndex(grid_x, grid_y);

        // occupied cell은 free로 덮지 않는다.
        if (grid_msg.data[index] == static_cast<int8_t>(occupied_value_)) return 0;

        if (grid_msg.data[index] == static_cast<int8_t>(free_value_)) return 0;

        grid_msg.data[index] = static_cast<int8_t>(free_value_);

        return 1;
    }
    
    int LocalOccupancyGridNode::markOccupied(
        nav_msgs::msg::OccupancyGrid & grid_msg,
        const int grid_x, const int grid_y)
    {
        if (!isInsideGrid(grid_x, grid_y)) return 0;

        const int index = toIndex(grid_x, grid_y);

        // 이미 occupied인 cell이면 중복 카운팅 X
        if (grid_msg.data[index] == static_cast<int8_t>(occupied_value_)) return 0;

        grid_msg.data[index] = static_cast<int8_t>(occupied_value_);
        
        return 1;
    }

    int LocalOccupancyGridNode::markRayFree(
        nav_msgs::msg::OccupancyGrid & grid_msg,
        const int start_x,
        const int start_y,
        const int end_x,
        const int end_y)
    {
        int newly_marked_count = 0;

        int x = start_x;
        int y = start_y;

        const int dx = std::abs(end_x - start_x);
        const int dy = std::abs(end_y - start_y);

        const int sx = (start_x < end_x) ? 1 : -1;
        const int sy = (start_y < end_y) ? 1 : -1;

        int error = dx - dy;

        while(true) {
            // endpoint는 occupied 후보이므로 free로 표시 X
            if (x == end_x && y == end_y) break;

            newly_marked_count += markFree(grid_msg, x, y);

            const int error2 = 2 * error;

            if (error2 > -dy) {
                error -= dy;
                x += sx;
            }

            if (error2 < dx) {
                error += dx;
                y += sy;
            }

            if (!isInsideGrid(x, y)) break;
        }

        return newly_marked_count;
    }

    int LocalOccupancyGridNode::markInflatedOccupied(
        nav_msgs::msg::OccupancyGrid & grid_msg,
        const int center_x,
        const int center_y)
    {
        int newly_marked_count = 0;

        for (int dy = -inflation_cells_; dy <= inflation_cells_; ++dy) {
            for (int dx = -inflation_cells_; dx <= inflation_cells_; ++dx) {
                
                const int nx = center_x + dx;
                const int ny = center_y + dy;

                if (!isInsideGrid(nx, ny)) continue;

                const double distance = std::sqrt(
                    std::pow(dx * resolution_, 2.0) +
                    std::pow(dy * resolution_, 2.0)
                );

                // 원형 inflation 적용
                if (distance <= inflation_radius_) {
                    newly_marked_count += markOccupied(grid_msg, nx, ny);
                }
            }
        }

        return newly_marked_count;
    }
}

int main(int argc, char ** argv) {
    rclcpp::init(argc, argv);

    auto node = std::make_shared<lidar_perception::LocalOccupancyGridNode>();
    rclcpp::spin(node);

    rclcpp::shutdown();

    return 0;
}