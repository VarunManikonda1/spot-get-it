/*
- 경로 : ~/robot_ws/src/perception/lidar_perception/include/lidar_perception/memory_fusion_node.hpp
- 역할 : 현재 base_link 기준 local occupancy grid와 mission_map 기준 obstacle memory grid를 융합하는 노드 선언
- 입력 :
    - /perception/lidar/local_occupancy_grid
        - 타입 : nav_msgs/msg/OccupancyGrid
        - 기준 frame : base_link
        - 의미 : 현재 LiDAR frame에서 생성된 로봇 기준 local occupancy grid

    - /perception/lidar/obstacle_memory_grid
        - 타입 : nav_msgs/msg/OccupancyGrid
        - 기준 frame : mission_map
        - 의미 : 최근 관측된 장애물 occupied cell을 TTL 동안 유지한 mission_map 기준 memory grid

    - /localization/pose
        - 타입 : robot_interfaces/msg/LocalizedRobotPose
        - 기준 frame : mission_map
        - 의미 : 현재 로봇의 mission_map 기준 x, y, yaw pose

- 출력 :
    - /perception/lidar/local_occupancy_grid_with_memory
        - 타입 : nav_msgs/msg/OccupancyGrid
        - 기준 frame : base_link
        - 의미 : 현재 local occupancy grid에 obstacle memory를 overlay한 FreeSpaceModel 입력용 grid

- 주요 기능 :
    1. base_link 기준 local occupancy grid를 수신한다.
    2. mission_map 기준 obstacle memory grid를 수신한다.
    3. mission_map 기준 localization pose를 수신한다.
    4. obstacle memory grid의 occupied cell 중심 좌표를 mission_map 좌표로 복원한다.
    5. mission_map 좌표를 현재 로봇 pose 기준 base_link 좌표로 변환한다.
    6. 변환된 memory occupied cell을 local occupancy grid 위에 overlay한다.
    7. fusion 결과를 base_link 기준 local_occupancy_grid_with_memory로 발행한다.

- 설계 의도 :
    - FreeSpaceModel은 base_link 기준 local occupancy grid를 입력으로 사용한다.
    - obstacle_memory_grid는 mission_map 기준이므로 FreeSpaceModel이 직접 사용하기 어렵다.
    - 따라서 memory_fusion_node가 mission_map memory를 현재 base_link local window로 재투영한다.
    - 이 결과를 FreeSpaceModel 입력으로 바꾸면, FreeSpaceModel이 현재 LiDAR + 최근 memory 장애물을 함께 고려할 수 있다.
*/

#ifndef LIDAR_PERCEPTION__MEMORY_FUSION_NODE_HPP_
#define LIDAR_PERCEPTION__MEMORY_FUSION_NODE_HPP_

#include <cstdint>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "nav_msgs/msg/occupancy_grid.hpp"
#include "robot_interfaces/msg/localized_robot_pose.hpp"

namespace lidar_perception
{
    class MemoryFusionNode : public rclcpp::Node
    {
        public:
            /*
            생성자.

            역할:
            - ROS 2 node 이름 설정
            - parameter 선언 및 로드
            - local occupancy grid subscriber 생성
            - obstacle memory grid subscriber 생성
            - localization pose subscriber 생성
            - fused occupancy grid publisher 생성
            */
            MemoryFusionNode();

        private:
            using OccupancyGridMsg = nav_msgs::msg::OccupancyGrid;
            using LocalizedPoseMsg = robot_interfaces::msg::LocalizedRobotPose;

            /*
            FusionStats

            한 번의 fusion callback에서 어떤 일이 일어났는지 기록하기 위한 디버그 통계 구조체다.

            memory_occupied_cells:
            - memory grid에서 occupied로 판단된 cell 개수

            memory_projected_inside_cells:
            - mission_map memory cell을 base_link로 변환했을 때 현재 local grid 범위 안에 들어온 cell 개수

            memory_overlay_cells:
            - 실제 fused grid에 occupied로 overlay된 cell 개수

            memory_skipped_free_cells:
            - 현재 local grid에서 free로 관측된 cell이라 memory overlay를 건너뛴 cell 개수
            - overlay_memory_on_free_cells_가 false일 때 의미가 있다.

            memory_outside_cells:
            - base_link로 변환된 memory cell이 현재 local grid 범위 밖이라 무시된 cell 개수
            */
            struct FusionStats
            {
                int memory_occupied_cells{0};
                int memory_projected_inside_cells{0};
                int memory_overlay_cells{0};
                int memory_skipped_free_cells{0};
                int memory_outside_cells{0};
            };

            /*
            poseCallback

            /localization/pose를 수신하는 callback이다.

            최신 robot pose를 저장한다.
            mission_map 기준 memory cell을 base_link 기준 local grid로 변환할 때 사용한다.
            */
            void poseCallback(const LocalizedPoseMsg::SharedPtr msg);

            /*
            memoryGridCallback

            /perception/lidar/obstacle_memory_grid를 수신하는 callback이다.

            최신 obstacle memory grid를 저장한다.
            실제 fusion publish는 localGridCallback에서 수행한다.
            */
            void memoryGridCallback(const OccupancyGridMsg::SharedPtr msg);

            /*
            localGridCallback

            /perception/lidar/local_occupancy_grid를 수신하는 callback이다.

            이 노드의 핵심 callback이다.
            local grid가 들어올 때마다 최신 pose와 memory grid를 이용해 fused grid를 생성하고 발행한다.
            */
            void localGridCallback(const OccupancyGridMsg::SharedPtr msg);

            /*
            hasValidPose

            최신 localization pose를 사용할 수 있는지 검사한다.

            조건:
            - latest_pose_가 존재해야 한다.
            - pose 수신 후 pose_timeout_sec_ 이하의 시간만 지나야 한다.
            */
            bool hasValidPose(const rclcpp::Time &now) const;

            /*
            hasValidMemoryGrid

            최신 obstacle memory grid를 사용할 수 있는지 검사한다.

            조건:
            - latest_memory_grid_가 존재해야 한다.
            - memory grid 수신 후 memory_grid_timeout_sec_ 이하의 시간만 지나야 한다.
            */
            bool hasValidMemoryGrid(const rclcpp::Time &now) const;

            /*
            gridCellCenterX

            OccupancyGrid에서 특정 x index cell의 중심 x 좌표를 계산한다.

            기준 frame:
            - 입력 grid의 frame_id

            공식:
            - center_x = origin_x + (grid_x + 0.5) * resolution
            */
            double gridCellCenterX(
                const OccupancyGridMsg &grid,
                int grid_x) const;

            /*
            gridCellCenterY

            OccupancyGrid에서 특정 y index cell의 중심 y 좌표를 계산한다.

            기준 frame:
            - 입력 grid의 frame_id

            공식:
            - center_y = origin_y + (grid_y + 0.5) * resolution
            */
            double gridCellCenterY(
                const OccupancyGridMsg &grid,
                int grid_y) const;

            /*
            worldToGridX

            base_link 기준 x 좌표를 local occupancy grid의 x index로 변환한다.

            입력:
            - x_base : base_link 기준 x 좌표
            - local_grid : base_link 기준 local occupancy grid

            공식:
            - grid_x = floor((x_base - origin_x) / resolution)
            */
            int worldToGridX(
                double x_base,
                const OccupancyGridMsg &local_grid) const;

            /*
            worldToGridY

            base_link 기준 y 좌표를 local occupancy grid의 y index로 변환한다.

            입력:
            - y_base : base_link 기준 y 좌표
            - local_grid : base_link 기준 local occupancy grid

            공식:
            - grid_y = floor((y_base - origin_y) / resolution)
            */
            int worldToGridY(
                double y_base,
                const OccupancyGridMsg &local_grid) const;

            /*
            isInsideGrid

            특정 grid index가 width/height 범위 안에 있는지 검사한다.
            */
            bool isInsideGrid(
                int grid_x,
                int grid_y,
                int width,
                int height) const;

            /*
            toIndex

            2D grid index를 OccupancyGrid.data의 1D index로 변환한다.

            공식:
            - index = grid_y * width + grid_x
            */
            int toIndex(
                int grid_x,
                int grid_y,
                int width) const;

            /*
            transformMapToBase

            mission_map 기준 좌표를 현재 robot pose 기준 base_link 좌표로 변환한다.

            입력:
            - x_map, y_map : mission_map 기준 memory cell 중심 좌표

            출력:
            - x_base, y_base : base_link 기준 memory cell 좌표

            변환식:
            - dx = x_map - robot_x
            - dy = y_map - robot_y
            - x_base =  cos(yaw) * dx + sin(yaw) * dy
            - y_base = -sin(yaw) * dx + cos(yaw) * dy

            이 수식은 base_link → mission_map 변환의 역변환이다.
            */
            void transformMapToBase(
                double x_map,
                double y_map,
                const LocalizedPoseMsg &pose,
                double &x_base,
                double &y_base) const;

            /*
            shouldOverlayMemoryOnCell

            memory occupied cell을 현재 local grid cell 위에 overlay해도 되는지 판단한다.

            정책:
            - 현재 cell이 occupied면 그대로 occupied이므로 overlay해도 의미상 문제 없음
            - 현재 cell이 unknown이면 memory obstacle을 overlay하는 것이 목적에 맞음
            - 현재 cell이 free이면 현재 LiDAR가 비어 있다고 관측한 공간일 수 있으므로,
              overlay_memory_on_free_cells_ 설정에 따라 overlay 여부를 결정한다.

            초기 추천:
            - overlay_memory_on_free_cells_ = false
            - 이유 : 현재 LiDAR가 free로 본 공간보다 과거 memory를 우선하면 stale obstacle이 과하게 남을 수 있기 때문
            */
            bool shouldOverlayMemoryOnCell(int current_cell_value) const;

            /*
            markMemoryOccupied

            fused grid의 특정 cell을 memory occupied로 표시한다.

            memory_fusion_inflation_radius_가 0보다 크면 주변 cell도 함께 occupied로 표시한다.
            */
            int markMemoryOccupied(
                OccupancyGridMsg &fused_grid,
                int center_x,
                int center_y);

            /*
            fuseMemoryIntoLocalGrid

            local grid 복사본 위에 mission_map 기준 memory grid를 base_link 기준으로 변환하여 overlay한다.

            입력:
            - local_grid : 현재 base_link 기준 local occupancy grid
            - memory_grid : mission_map 기준 obstacle memory grid
            - pose : mission_map 기준 현재 robot pose

            출력:
            - fused_grid : local grid에 memory obstacle이 overlay된 결과

            반환:
            - FusionStats : 디버그 통계
            */
            FusionStats fuseMemoryIntoLocalGrid(
                const OccupancyGridMsg &local_grid,
                const OccupancyGridMsg &memory_grid,
                const LocalizedPoseMsg &pose,
                OccupancyGridMsg &fused_grid);

            /*
            publishPassThroughLocalGrid

            pose 또는 memory grid가 없을 때 local grid를 그대로 output topic으로 발행한다.

            사용 이유:
            - FreeSpaceModel 입력을 local_occupancy_grid_with_memory로 바꾼 뒤,
              memory_fusion_node가 memory를 못 받는다고 출력이 끊기면 전체 navigation pipeline이 멈출 수 있다.
            - 개발 단계에서는 fallback으로 local grid를 그대로 발행하는 것이 안전하다.
            */
            void publishPassThroughLocalGrid(
                const OccupancyGridMsg &local_grid,
                const std::string &reason);

            /*
            토픽 파라미터
            */
            std::string input_local_grid_topic_;
            std::string input_memory_grid_topic_;
            std::string localization_pose_topic_;
            std::string output_fused_grid_topic_;

            /*
            frame 파라미터
            */
            std::string local_grid_frame_;
            std::string memory_grid_frame_;

            /*
            timeout 파라미터
            */
            double pose_timeout_sec_{0.5};
            double memory_grid_timeout_sec_{0.5};

            /*
            cell value 및 threshold 파라미터
            */
            int unknown_value_{-1};
            int free_value_{0};
            int occupied_value_{100};

            int local_occupied_threshold_{50};
            int memory_occupied_threshold_{50};

            /*
            fusion 정책 파라미터
            */
            bool publish_without_memory_{true};
            bool overlay_memory_on_free_cells_{false};

            double memory_fusion_inflation_radius_{0.0};
            int memory_fusion_inflation_cells_{0};

            bool debug_log_{true};

            /*
            최신 입력 캐시
            */
            LocalizedPoseMsg::SharedPtr latest_pose_{nullptr};
            rclcpp::Time latest_pose_time_;

            OccupancyGridMsg::SharedPtr latest_memory_grid_{nullptr};
            rclcpp::Time latest_memory_grid_time_;

            /*
            ROS interface
            */
            rclcpp::Subscription<OccupancyGridMsg>::SharedPtr local_grid_sub_;
            rclcpp::Subscription<OccupancyGridMsg>::SharedPtr memory_grid_sub_;
            rclcpp::Subscription<LocalizedPoseMsg>::SharedPtr pose_sub_;

            rclcpp::Publisher<OccupancyGridMsg>::SharedPtr fused_grid_pub_;
    };
}

#endif