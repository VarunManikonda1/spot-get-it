/*
- 경로 : ~/robot_ws/src/perception/lidar_perception/include/lidar_perception/obstacle_memory_grid_node.hpp
- 역할 : mission_map 기준 obstacle memory grid node 클래스 선언
- 입력 :
    - /perception/lidar/local_occupancy_grid
        - 타입 : nav_msgs/msg/OccupancyGrid
        - 기준 frame : base_link
        - 의미 : 현재 LiDAR frame에서 생성된 로봇 기준 local 2D occupancy grid
        - cell 값 :
            - unknown : -1
            - free    : 0
            - occupied: 100

    - /localization/pose
        - 타입 : robot_interfaces/msg/LocalizedRobotPose
        - 기준 frame : mission_map
        - 의미 : 현재 로봇의 mission_map 기준 위치 x, y, yaw

- 출력 :
    - /perception/lidar/obstacle_memory_grid
        - 타입 : nav_msgs/msg/OccupancyGrid
        - 기준 frame : mission_map
        - 의미 : 최근 관측된 장애물 occupied cell을 mission_map 기준으로 일정 시간 유지한 memory grid

- 주요 기능 :
    1. base_link 기준 local occupancy grid에서 occupied cell만 추출한다.
    2. occupied cell 중심 좌표를 base_link 좌표로 복원한다.
    3. /localization/pose의 x, y, yaw를 이용해 base_link 좌표를 mission_map 좌표로 변환한다.
    4. 변환된 mission_map 좌표를 sparse memory map에 저장한다.
    5. 각 memory cell은 last_seen_time을 가진다.
    6. obstacle_memory_ttl_sec 시간이 지난 cell은 삭제한다.
    7. 현재 로봇 주변 publish_size_x/y 범위만 nav_msgs/msg/OccupancyGrid로 발행한다.

- 설계 의도 :
    - local_occupancy_grid는 base_link 기준이므로 매 frame 새로 생성되고 과거 장애물을 기억하지 못한다.
    - obstacle_memory_grid_node는 과거 occupied cell을 mission_map 기준에 저장하여,
      로봇이 움직여도 장애물 위치가 공간상 같은 위치에 남도록 만든다.
    - 단, free/unknown cell은 기억하지 않고 occupied cell만 기억한다.
      이유는 과거 free 공간이 현재도 free라고 보장할 수 없기 때문이다.
*/

#ifndef LIDAR_PERCEPTION__OBSTACLE_MEMORY_GRID_NODE_HPP_
#define LIDAR_PERCEPTION__OBSTACLE_MEMORY_GRID_NODE_HPP_

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "rclcpp/rclcpp.hpp"
#include "nav_msgs/msg/occupancy_grid.hpp"
#include "robot_interfaces/msg/localized_robot_pose.hpp"

namespace lidar_perception
{
    class ObstacleMemoryGridNode : public rclcpp::Node
    {
        public:
            ObstacleMemoryGridNode();

        private:
            using OccupancyGridMsg = nav_msgs::msg::OccupancyGrid;
            using LocalizedPoseMsg = robot_interfaces::msg::LocalizedRobotPose;
            /*
            GridKey:
            - sparse memory map에서 cell 하나를 식별하기 위한 key
              x, y는 mission_map 좌표를 memory_resolution_ 단위로 나눈
              정수 grid index다.

            예:
            - memory_resolution_ = 0.10 m
            - x_map = 1.23 m
            - key.x = floor(1.23 / 0.10) = 12

            이 key를 unordered_map의 key로 사용해서,
            전체 dense grid를 항상 들고 있지 않고 occupied memory cell만 저장한다.
            */
            struct GridKey
            {
                // mission_map 기준 memory grid의 x, y 방향 정수 index
                // 실제 meter 좌표가 아니라 resolution으로 나눈 grid 좌표
                int x{0};
                int y{0};

                // unordered_map에서 key 비교
                // - 두 GridKey가 같은 cell을 의미하려면
                //   x index와 y index가 모두 같아야 한다.
                bool operator==(const GridKey &other) const
                {
                    return x == other.x && y == other.y;
                }
            };

            /*
            GridKeyHash:
            - unordered_map<GridKey, MemoryCell>을 사용하기 위한 hash 함수

            GridKey는 사용자 정의 구조체이기 때문에,
            C++ 표준 unordered_map에서 사용하려면 hash 계산 방법을 직접 제공해야 한다.

            구현 방식:
            - x index를 상위 32bit 쪽에 배치
            - y index를 하위 32bit 쪽에 섞음
            - 최종 int64 값을 std::hash로 변환
            */
            struct GridKeyHash
            {
                std::size_t operator()(const GridKey &key) const
                {
                    const std::int64_t mixed =
                        (static_cast<std::int64_t>(key.x) << 32) ^
                        static_cast<std::uint32_t>(key.y);

                    return std::hash<std::int64_t>{}(mixed);
                }
            };

            /*
            MemoryCell:
            - obstacle memory grid에 저장되는 cell 정보
            - 현재 MVP에서는 cell이 마지막으로 관측된 시간만 저장

            last_seen_time:
            - 해당 cell이 마지막으로 occupied로 관측된 시각
            - 현재 시간과 비교해서 obstacle_memory_ttl_sec_보다 오래되면 삭제

            추후 확장 가능 항목:
            - confidence
            - hit_count
            - decay_score
            - source_robot_id
            */
            struct MemoryCell
            {
                rclcpp::Time last_seen_time;
            };

            // Localization pose를 수신하는 callback 함수
            void poseCallback(const LocalizedPoseMsg::SharedPtr msg);

            // /perception/lidar/local_occupancy_grid를 수신하는 callback 함수
            void localGridCallback(const OccupancyGridMsg::SharedPtr msg);

            // 최신 localization pose를 사용할 수 있는지 검사 함수
            bool hasValidPose(const rclcpp::Time &now) const;

            // 특정 grid index가 주어진 width, height 범위 안에 있는지 검사 함수
            bool isInsideGrid(
                int grid_x, int grid_y,
                int width, int height) const;
            
            /* 
            2D grid index를 1D data 배열 index로 변환 함수
            
            변환 공식:
            - index = grid_y * width + grid_x*/
            int toIndex(int grid_x, int grid_y, int width) const;

            /*
            입력 local occupancy grid에서 특정 x, y index cell의 중심 x,y 좌표를 계산 함수
            
            기준 frame:
            - 입력 grid의 frame_id
            - 현재 설계에서는 base_link

            공식:
            - center_x = origin_x + (grid_x + 0.5) * resolution
            - center_y = origin_y + (grid_y + 0.5) * resolution */
            double gridCellCenterX(
                const OccupancyGridMsg &grid,
                int grid_x) const;

            double gridCellCenterY(
                const OccupancyGridMsg &grid,
                int grid_y) const;

            /*
            mission_map 기준 meter 좌표를 sparse memory map의 정수 key로 변환 함수.

            입력:
            - x_map, y_map: mission_map 기준 meter 좌표

            출력:
            - GridKey: memory_resolution_ 단위의 정수 grid index

            사용 이유:
            - double 좌표를 unordered_map key로 직접 쓰면 부동소수점 오차 문제가 생긴다.
            - 따라서 resolution 단위의 정수 cell key로 변환해서 memory를 관리한다.
            */
            GridKey mapToMemoryKey(double x_map, double y_map) const;

            /*
            memory grid key의 중심 x, y 좌표를 mission_map meter 좌표로 복원 함수.

            사용 위치:
            - sparse memory cell을 OccupancyGrid 메시지로 publish할 때
            - key 기반으로 저장된 cell을 실제 meter 좌표로 되돌릴 때 사용

            공식:
            - x_center = (key.x + 0.5) * memory_resolution_
            - y_center = (key.y + 0.5) * memory_resolution_ */
            double memoryKeyCenterX(const GridKey &key) const;
            double memoryKeyCenterY(const GridKey &key) const;

            /*
            mission_map 기준 occupied 좌표를 memory map에 저장 함수.

            동작:
            - x_map, y_map을 GridKey로 변환한다.
            - 해당 key의 last_seen_time을 now로 갱신한다.
            - memory_inflation_radius_가 0보다 크면 주변 cell도 함께 occupied memory로 저장한다.

            사용 이유:
            - 같은 장애물이 계속 관측되면 last_seen_time이 계속 갱신되어 memory에 남는다.
            - 장애물이 더 이상 관측되지 않으면 last_seen_time이 갱신되지 않고,
              TTL이 지난 뒤 pruneExpiredCells()에서 삭제된다. */
            void insertMemoryCell(
                double x_map, double y_map,
                const rclcpp::Time &now);

            /*
            TTL이 지난 memory cell을 삭제 함수.

            조건:
            - now - last_seen_time > obstacle_memory_ttl_sec_

            사용 이유:
            - 장애물을 영구적으로 기억하면 실제로 사라진 장애물도 계속 남아 주행을 방해한다.
            - 따라서 최근 일정 시간 동안만 기억하고, 오래된 memory는 제거해야 한다. */
            void pruneExpiredCells(const rclcpp::Time &now);

            /*
            현재 sparse memory map을 nav_msgs/msg/OccupancyGrid 메시지로 변환 함수.

            출력 grid 특징:
            - frame_id는 memory_frame_이다.
            - 현재 설계에서는 mission_map이다.
            - 전체 무한 맵을 발행하지 않고, 현재 로봇 주변 publish_size_x/y 범위만 발행한다.
            - memory가 없는 cell은 unknown_value_로 둔다.
            - memory가 있는 cell은 occupied_value_로 표시한다.

            사용 이유:
            - RViz 시각화
            - 이후 memory_fusion_node에서 입력으로 사용  */
            OccupancyGridMsg buildMemoryGridMessage(
                const rclcpp::Time &stamp) const;

            /*
            mission_map 기준 occupied 좌표를 output OccupancyGrid data에 표시 함수.

            동작:
            1. x_map, y_map을 output grid origin 기준 index로 변환한다.
            2. index가 output grid 범위 안에 있으면 occupied_value_로 설정한다.
            3. 범위 밖이면 무시한다.

            사용 이유:
            - sparse memory map에는 전체 memory가 저장되어 있지만,
              publish grid는 현재 로봇 주변 rolling window만 표현하기 때문이다. */
            void markOccupiedInOutputGrid(
                OccupancyGridMsg &grid,
                double x_map,
                double y_map) const;
            
            // =======================
            // Subscriber & Publisher
            // =======================
            std::string input_local_grid_topic_;
            std::string localization_pose_topic_;
            std::string output_memory_grid_topic_;

            std::string input_grid_frame_;
            std::string memory_frame_;

            double memory_resolution_{0.10};
            double publish_size_x_{8.0};
            double publish_size_y_{8.0};

            int publish_width_{0};
            int publish_height_{0};

            double obstacle_memory_ttl_sec_{1.5};
            double pose_timeout_sec_{0.5};

            int unknown_value_{-1};
            int free_value_{0};
            int occupied_value_{100};
            int occupied_threshold_{50};

            double memory_inflation_radius_{0.0};
            int memory_inflation_cells_{0};

            bool debug_log_{true};

            LocalizedPoseMsg::SharedPtr latest_pose_{nullptr};
            rclcpp::Time latest_pose_time_;

            std::unordered_map<GridKey, MemoryCell, GridKeyHash> memory_cells_;

            rclcpp::Subscription<OccupancyGridMsg>::SharedPtr local_grid_sub_;
            rclcpp::Subscription<LocalizedPoseMsg>::SharedPtr pose_sub_;
            rclcpp::Publisher<OccupancyGridMsg>::SharedPtr memory_grid_pub_;
    };
}


#endif