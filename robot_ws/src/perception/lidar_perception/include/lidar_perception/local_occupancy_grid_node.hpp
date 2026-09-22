/*
- 경로 : ~/robot_ws/src/perception/lidar_perception/include/lidar_perception/local_occupancy_grid_node.hpp
- 역할 : local_occupancy_grid_node 클래스 선언
- 입력 : /perception/lidar/points_filtered
- 출력 : /perception/lidar/local_occupancy_grid
- 기능 :
    - LocalOccupancyGridNode 클래스 선언
    - PointCloud2 subscriber 선언
    - OccupancyGrid publisher 선언
    - PointCloud callback 함수 선언
    - world 좌표를 grid index로 변환하는 helper 함수 선언
    - LiDAR ray tracing 기반 free cell 표시 함수 선언 (확장)
    - occupied cell 및 inflated occupied cell 표시 함수 선언
*/
#ifndef LIDAR_PERCEPTION__LOCAL_OCCUPANCY_GRID_NODE_HPP_
#define LIDAR_PERCEPTION__LOCAL_OCCUPANCY_GRID_NODE_HPP_

#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"
#include "nav_msgs/msg/occupancy_grid.hpp"

namespace lidar_perception
{
    class LocalOccupancyGridNode : public rclcpp::Node
    {
        public:
            LocalOccupancyGridNode();
        
        private:
            // PointCloud2 콜백 함수
            void pointCloudCallback(const sensor_msgs::msg::PointCloud2::SharedPtr msg);

            // 실제 좌표 -> grid 인덱스 변환 헬퍼 함수 
            int worldToGridX(double x) const;
            int worldToGridY(double y) const;

            // 변환된 grid 인덱스가 실제 grid 범위에 있는지 확인 함수
            bool isInsideGrid(int grid_x, int grid_y) const;

            // 2D grid 인덱스 -> 1D 배열 인덱스로 변환
            int toIndex(int grid_x, int grid_y) const;
            
            // Cell 표시 함수 : 해당 grid cell을 free로 표시
            int markFree(
                nav_msgs::msg::OccupancyGrid & grid_msg,
                int grid_x,
                int grid_y);

            // Cell 표시 함수 : 해당 grid cell을 occupied로 표시
            int markOccupied(
                nav_msgs::msg::OccupancyGrid & grid_msg,
                int grid_x,
                int grid_y);

            // Cell 표시 함수 : sensor origin에서 endpoint까지 ray 경로를 free로 표시
            int markRayFree(
                nav_msgs::msg::OccupancyGrid & grid_msg,
                int start_x,
                int start_y,
                int end_x,
                int end_y);
            
            // Cell 표시 함수 : 장애물 cell 주변을 inflation_radius 반경만큼 occupied로 표시
            int markInflatedOccupied(
                nav_msgs::msg::OccupancyGrid & grid_msg,
                int center_x,
                int center_y);
            
            // Topic name
            std::string input_topic_;
            std::string output_topic_;
            std::string frame_id_;

            // 입력 토픽 구독자
            rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr pointcloud_sub_;

            // OccupancyGrid 발행자
            rclcpp::Publisher<nav_msgs::msg::OccupancyGrid>::SharedPtr occupancy_grid_pub_;
            
            // 파라미터
            // Grid geometry
            double origin_x_;       // grid 시작 x 좌표 [m], base_link 기준
            double origin_y_;       // grid 시작 y 좌표 [m], base_link 기준
            double size_x_;         // grid 가로 실제 크기 [m]
            double size_y_;         // grid 세로 실제 크기 [m]
            double resolution_;     // 1셀 = ? cm (ex. 0.05 = 5cm)

            int width_;             // grid 가로(x 방향) cell 수 = size_x / resolution
            int height_;            // grid 세로(y 방향) cell 수 = size_y / resolution

            // Cell value
            int unknown_value_;      // 관측되지 않은 cell value: -1
            int free_value_;         // 비어있는 cell value : 0
            int occupied_value_;     // 장애물 cell value   : 100
            
            // Filtering
            double min_z_;          // 해당 높이 아래 점 무시
            double max_z_;          // 해당 높이 위 점 무시
            double min_range_;      // 가까운 점 무시
            double max_range_;      // 먼 점 무시

            // Ray tracing
            bool use_ray_tracing_;      // ray tracing 사용 여부
            double sensor_origin_x_;    // base_link 기준 LiDAR ray 시작점 x
            double sensor_origin_y_;    // base_link 기준 LiDAR ray 시작점 y

            // Inflation
            bool inflate_obstacles_;    // inflation 사용 여부
            double inflation_radius_;   // inflation 반경 [m]
            int inflation_cells_;       // inflation 반경을 셀 수로 변환한 값
    };
}

#endif