/*
- 경로 : ~/robot_ws/src/perception/lidar_perception/src/memory_fusion_node.cpp
- 역할 : mission_map 기준 obstacle memory grid를 현재 base_link 기준 local occupancy grid에 재투영하여 fused grid 생성
- 입력 :
    - /perception/lidar/local_occupancy_grid
    - /perception/lidar/obstacle_memory_grid
    - /localization/pose
- 출력 :
    - /perception/lidar/local_occupancy_grid_with_memory
- 주요 기능 :
    1. 최신 local occupancy grid를 수신한다.
    2. 최신 obstacle memory grid를 수신한다.
    3. 최신 localization pose를 수신한다.
    4. memory grid의 occupied cell을 mission_map 좌표로 복원한다.
    5. mission_map 좌표를 현재 pose 기준 base_link 좌표로 변환한다.
    6. base_link 좌표를 local occupancy grid cell index로 변환한다.
    7. 해당 cell을 fused grid에서 occupied로 overlay한다.
    8. fused grid를 FreeSpaceModel 입력용 topic으로 발행한다.
- 설계 의도 :
    - obstacle_memory_grid는 mission_map 기준이라 FreeSpaceModel이 직접 사용하기 어렵다.
    - FreeSpaceModel은 base_link 기준 local grid를 angular bin으로 분석한다.
    - 따라서 memory_fusion_node가 memory obstacle을 현재 로봇 기준 local grid로 변환해 FreeSpaceModel 입력 형태로 맞춘다.
*/

#include "lidar_perception/memory_fusion_node.hpp"

#include <cmath>
#include <functional>

namespace lidar_perception
{
    MemoryFusionNode::MemoryFusionNode()
    : Node("memory_fusion_node")
    {
        /*
        입력 local occupancy grid topic.

        이 grid는 base_link 기준 현재 LiDAR frame의 occupancy grid다.
        */
        input_local_grid_topic_ = this->declare_parameter<std::string>(
            "input_local_grid_topic",
            "/perception/lidar/local_occupancy_grid"
        );

        /*
        입력 obstacle memory grid topic.

        이 grid는 obstacle_memory_grid_node가 발행하는 mission_map 기준 memory grid다.
        */
        input_memory_grid_topic_ = this->declare_parameter<std::string>(
            "input_memory_grid_topic",
            "/perception/lidar/obstacle_memory_grid"
        );

        /*
        입력 localization pose topic.

        mission_map 기준 현재 robot pose를 제공한다.
        memory grid의 mission_map 좌표를 base_link 좌표로 변환할 때 필요하다.
        */
        localization_pose_topic_ = this->declare_parameter<std::string>(
            "localization_pose_topic",
            "/localization/pose"
        );

        /*
        출력 fused local occupancy grid topic.

        FreeSpaceModel의 input_topic을 이 topic으로 바꾸면,
        FreeSpaceModel이 현재 LiDAR + obstacle memory를 함께 반영하게 된다.
        */
        output_fused_grid_topic_ = this->declare_parameter<std::string>(
            "output_fused_grid_topic",
            "/perception/lidar/local_occupancy_grid_with_memory"
        );

        /*
        local grid frame.

        현재 local_occupancy_grid_node는 base_link 기준 grid를 발행한다.
        */
        local_grid_frame_ = this->declare_parameter<std::string>(
            "local_grid_frame",
            "base_link"
        );

        /*
        memory grid frame.

        obstacle_memory_grid_node는 mission_map 기준 grid를 발행한다.
        */
        memory_grid_frame_ = this->declare_parameter<std::string>(
            "memory_grid_frame",
            "mission_map"
        );

        /*
        pose timeout.

        pose가 오래되면 mission_map → base_link 변환이 부정확해지므로,
        이 시간보다 오래된 pose는 사용하지 않는다.
        */
        pose_timeout_sec_ = this->declare_parameter<double>(
            "pose_timeout_sec",
            0.5
        );

        /*
        memory grid timeout.

        memory grid가 오래되면 현재 상황과 맞지 않을 수 있으므로,
        이 시간보다 오래된 memory grid는 사용하지 않는다.
        */
        memory_grid_timeout_sec_ = this->declare_parameter<double>(
            "memory_grid_timeout_sec",
            0.5
        );

        /*
        OccupancyGrid cell value 설정.

        일반적으로:
        - unknown = -1
        - free = 0
        - occupied = 100
        */
        unknown_value_ = this->declare_parameter<int>(
            "unknown_value",
            -1
        );

        free_value_ = this->declare_parameter<int>(
            "free_value",
            0
        );

        occupied_value_ = this->declare_parameter<int>(
            "occupied_value",
            100
        );

        /*
        local grid의 occupied 판단 기준.

        현재는 fused grid에서 기존 occupied를 보존하는 데 주로 사용한다.
        */
        local_occupied_threshold_ = this->declare_parameter<int>(
            "local_occupied_threshold",
            50
        );

        /*
        memory grid의 occupied 판단 기준.

        memory grid에서 이 값 이상인 cell만 fused grid에 overlay한다.
        */
        memory_occupied_threshold_ = this->declare_parameter<int>(
            "memory_occupied_threshold",
            50
        );

        /*
        memory grid 또는 pose가 아직 준비되지 않았을 때 local grid를 그대로 publish할지 여부.

        true 권장:
        - FreeSpaceModel 입력을 fused topic으로 바꾼 뒤에도 pipeline이 끊기지 않는다.
        */
        publish_without_memory_ = this->declare_parameter<bool>(
            "publish_without_memory",
            true
        );

        /*
        현재 local grid에서 free로 관측된 cell에도 memory occupied를 overlay할지 여부.

        false 권장:
        - 현재 LiDAR가 free로 본 공간은 memory보다 최신 관측으로 보는 정책이다.
        - memory는 주로 unknown 영역이나 FOV 밖에 남은 장애물을 보완하는 용도로 쓴다.

        true로 바꾸면:
        - memory가 free cell도 occupied로 덮을 수 있어 더 보수적이다.
        - 하지만 stale memory 때문에 통로를 과하게 막을 수 있다.
        */
        overlay_memory_on_free_cells_ = this->declare_parameter<bool>(
            "overlay_memory_on_free_cells",
            false
        );

        /*
        fusion 단계에서 memory obstacle을 추가로 부풀릴 반경.

        obstacle_memory_grid_node와 local_occupancy_grid_node에서 이미 inflation이 되어 있을 수 있으므로,
        초기값은 0.0을 추천한다.
        */
        memory_fusion_inflation_radius_ = this->declare_parameter<double>(
            "memory_fusion_inflation_radius",
            0.0
        );

        /*
        디버그 로그 출력 여부.
        */
        debug_log_ = this->declare_parameter<bool>(
            "debug_log",
            true
        );

        /*
        fusion inflation 반경을 cell 개수로 변환한다.

        주의:
        - local grid resolution은 callback마다 알 수 있다.
        - 여기서는 정확한 cell 수 계산을 위해 local grid callback에서 resolution을 다시 사용해도 되지만,
          구조 단순화를 위해 markMemoryOccupied()에서는 fused grid resolution 기반으로 다시 계산한다.
        - 따라서 이 변수는 현재 보조 용도로만 유지한다.
        */
        memory_fusion_inflation_cells_ = 0;

        /*
        localization pose subscriber 생성.
        */
        pose_sub_ = this->create_subscription<LocalizedPoseMsg>(
            localization_pose_topic_,
            rclcpp::QoS(10),
            std::bind(
                &MemoryFusionNode::poseCallback,
                this,
                std::placeholders::_1
            )
        );

        /*
        obstacle memory grid subscriber 생성.
        */
        memory_grid_sub_ = this->create_subscription<OccupancyGridMsg>(
            input_memory_grid_topic_,
            rclcpp::QoS(10),
            std::bind(
                &MemoryFusionNode::memoryGridCallback,
                this,
                std::placeholders::_1
            )
        );

        /*
        local occupancy grid subscriber 생성.

        local grid callback이 fusion publish의 trigger 역할을 한다.
        */
        local_grid_sub_ = this->create_subscription<OccupancyGridMsg>(
            input_local_grid_topic_,
            rclcpp::QoS(10),
            std::bind(
                &MemoryFusionNode::localGridCallback,
                this,
                std::placeholders::_1
            )
        );

        /*
        fused local occupancy grid publisher 생성.
        */
        fused_grid_pub_ = this->create_publisher<OccupancyGridMsg>(
            output_fused_grid_topic_,
            rclcpp::QoS(10)
        );

        RCLCPP_INFO(this->get_logger(), "memory_fusion_node started!");
        RCLCPP_INFO(this->get_logger(), "input_local_grid_topic : %s", input_local_grid_topic_.c_str());
        RCLCPP_INFO(this->get_logger(), "input_memory_grid_topic: %s", input_memory_grid_topic_.c_str());
        RCLCPP_INFO(this->get_logger(), "localization_pose_topic: %s", localization_pose_topic_.c_str());
        RCLCPP_INFO(this->get_logger(), "output_fused_grid_topic: %s", output_fused_grid_topic_.c_str());
        RCLCPP_INFO(this->get_logger(), "local_grid_frame       : %s", local_grid_frame_.c_str());
        RCLCPP_INFO(this->get_logger(), "memory_grid_frame      : %s", memory_grid_frame_.c_str());
        RCLCPP_INFO(this->get_logger(), "overlay memory on free : %s", overlay_memory_on_free_cells_ ? "true" : "false");
    }

    void MemoryFusionNode::poseCallback(
        const LocalizedPoseMsg::SharedPtr msg)
    {
        /*
        최신 localization pose를 저장한다.

        이 pose는 memory grid의 mission_map 좌표를 base_link 좌표로 변환하는 데 사용된다.
        */
        latest_pose_ = msg;
        latest_pose_time_ = this->now();
    }

    void MemoryFusionNode::memoryGridCallback(
        const OccupancyGridMsg::SharedPtr msg)
    {
        /*
        memory grid frame 확인.

        현재 구현은 memory grid가 mission_map 기준이라고 가정한다.
        frame이 다르면 warning은 출력하지만, 개발 편의를 위해 callback 자체를 중단하지는 않는다.
        */
        if (msg->header.frame_id != memory_grid_frame_) {
            RCLCPP_WARN_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                2000,
                "Input memory grid frame_id(%s) != expected frame(%s). "
                "Fusion assumes memory grid is in %s frame.",
                msg->header.frame_id.c_str(),
                memory_grid_frame_.c_str(),
                memory_grid_frame_.c_str()
            );
        }

        /*
        최신 memory grid를 저장한다.
        fusion은 localGridCallback에서 수행된다.
        */
        latest_memory_grid_ = msg;
        latest_memory_grid_time_ = this->now();
    }

    void MemoryFusionNode::localGridCallback(
        const OccupancyGridMsg::SharedPtr msg)
    {
        const rclcpp::Time now = this->now();

        /*
        local grid frame 확인.

        fused output은 local grid를 복사해서 만들기 때문에,
        local grid가 base_link 기준이어야 FreeSpaceModel 입력으로 의미가 맞다.
        */
        if (msg->header.frame_id != local_grid_frame_) {
            RCLCPP_WARN_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                2000,
                "Input local grid frame_id(%s) != expected frame(%s). "
                "Fused grid will keep input local grid frame.",
                msg->header.frame_id.c_str(),
                local_grid_frame_.c_str()
            );
        }

        /*
        pose가 없거나 너무 오래된 경우.

        이 경우 mission_map memory를 base_link로 변환할 수 없으므로,
        설정에 따라 local grid를 그대로 publish한다.
        */
        if (!hasValidPose(now)) {
            if (publish_without_memory_) {
                publishPassThroughLocalGrid(*msg, "no valid pose");
            } else {
                RCLCPP_WARN_THROTTLE(
                    this->get_logger(),
                    *this->get_clock(),
                    2000,
                    "No valid pose. fused grid publish skipped."
                );
            }
            return;
        }

        /*
        memory grid가 없거나 너무 오래된 경우.

        개발 단계에서는 local grid pass-through를 권장한다.
        */
        if (!hasValidMemoryGrid(now)) {
            if (publish_without_memory_) {
                publishPassThroughLocalGrid(*msg, "no valid memory grid");
            } else {
                RCLCPP_WARN_THROTTLE(
                    this->get_logger(),
                    *this->get_clock(),
                    2000,
                    "No valid memory grid. fused grid publish skipped."
                );
            }
            return;
        }

        /*
        local grid를 복사해서 fused grid의 기본값으로 사용한다.

        즉, 현재 LiDAR 기반 unknown/free/occupied 정보는 그대로 유지하고,
        여기에 memory occupied만 추가 overlay한다.
        */
        OccupancyGridMsg fused_grid = *msg;

        /*
        fused grid header.

        frame_id는 반드시 local grid와 같은 base_link 기준이어야 한다.
        stamp는 현재 local grid stamp를 유지한다.
        */
        fused_grid.header = msg->header;

        /*
        실제 fusion 수행.
        */
        FusionStats stats = fuseMemoryIntoLocalGrid(
            *msg,
            *latest_memory_grid_,
            *latest_pose_,
            fused_grid
        );

        /*
        fused local occupancy grid publish.
        */
        fused_grid_pub_->publish(fused_grid);

        if (debug_log_) {
            RCLCPP_INFO_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                1000,
                "\nmemory fusion"
                "\nlocal frame                  : %s"
                "\nmemory frame                 : %s"
                "\nfused output                 : %s"
                "\nmemory occupied cells        : %d"
                "\nmemory projected inside cells: %d"
                "\nmemory overlay cells         : %d"
                "\nmemory skipped free cells    : %d"
                "\nmemory outside cells         : %d",
                msg->header.frame_id.c_str(),
                latest_memory_grid_->header.frame_id.c_str(),
                output_fused_grid_topic_.c_str(),
                stats.memory_occupied_cells,
                stats.memory_projected_inside_cells,
                stats.memory_overlay_cells,
                stats.memory_skipped_free_cells,
                stats.memory_outside_cells
            );
        }
    }

    bool MemoryFusionNode::hasValidPose(
        const rclcpp::Time &now) const
    {
        if (!latest_pose_) {
            return false;
        }

        const double age_sec =
            (now - latest_pose_time_).seconds();

        return age_sec <= pose_timeout_sec_;
    }

    bool MemoryFusionNode::hasValidMemoryGrid(
        const rclcpp::Time &now) const
    {
        if (!latest_memory_grid_) {
            return false;
        }

        const double age_sec =
            (now - latest_memory_grid_time_).seconds();

        return age_sec <= memory_grid_timeout_sec_;
    }

    double MemoryFusionNode::gridCellCenterX(
        const OccupancyGridMsg &grid,
        const int grid_x) const
    {
        return grid.info.origin.position.x +
            (static_cast<double>(grid_x) + 0.5) *
            static_cast<double>(grid.info.resolution);
    }

    double MemoryFusionNode::gridCellCenterY(
        const OccupancyGridMsg &grid,
        const int grid_y) const
    {
        return grid.info.origin.position.y +
            (static_cast<double>(grid_y) + 0.5) *
            static_cast<double>(grid.info.resolution);
    }

    int MemoryFusionNode::worldToGridX(
        const double x_base,
        const OccupancyGridMsg &local_grid) const
    {
        return static_cast<int>(
            std::floor(
                (x_base - local_grid.info.origin.position.x) /
                static_cast<double>(local_grid.info.resolution)
            )
        );
    }

    int MemoryFusionNode::worldToGridY(
        const double y_base,
        const OccupancyGridMsg &local_grid) const
    {
        return static_cast<int>(
            std::floor(
                (y_base - local_grid.info.origin.position.y) /
                static_cast<double>(local_grid.info.resolution)
            )
        );
    }

    bool MemoryFusionNode::isInsideGrid(
        const int grid_x,
        const int grid_y,
        const int width,
        const int height) const
    {
        return (
            grid_x >= 0 &&
            grid_x < width &&
            grid_y >= 0 &&
            grid_y < height
        );
    }

    int MemoryFusionNode::toIndex(
        const int grid_x,
        const int grid_y,
        const int width) const
    {
        return grid_y * width + grid_x;
    }

    void MemoryFusionNode::transformMapToBase(
        const double x_map,
        const double y_map,
        const LocalizedPoseMsg &pose,
        double &x_base,
        double &y_base) const
    {
        /*
        mission_map 기준 robot pose.
        */
        const double robot_x = static_cast<double>(pose.x_m);
        const double robot_y = static_cast<double>(pose.y_m);
        const double robot_yaw = static_cast<double>(pose.yaw_rad);

        /*
        robot 위치를 기준으로 memory cell의 상대 벡터를 계산한다.
        */
        const double dx = x_map - robot_x;
        const double dy = y_map - robot_y;

        /*
        mission_map → base_link 역회전 변환.

        base_link → mission_map:
        x_map = robot_x + cos(yaw) * x_base - sin(yaw) * y_base
        y_map = robot_y + sin(yaw) * x_base + cos(yaw) * y_base

        따라서 역변환은 아래와 같다.
        */
        const double cos_yaw = std::cos(robot_yaw);
        const double sin_yaw = std::sin(robot_yaw);

        x_base =  cos_yaw * dx + sin_yaw * dy;
        y_base = -sin_yaw * dx + cos_yaw * dy;
    }

    bool MemoryFusionNode::shouldOverlayMemoryOnCell(
        const int current_cell_value) const
    {
        /*
        현재 cell이 occupied면 이미 장애물이므로 memory overlay를 허용한다.
        실제로는 값을 다시 100으로 쓰는 것뿐이다.
        */
        if (current_cell_value >= local_occupied_threshold_) {
            return true;
        }

        /*
        현재 cell이 unknown이면 memory를 overlay하는 것이 핵심 목적에 맞다.

        예:
        - LiDAR FOV 밖
        - 순간 미검출
        - ray tracing으로 관측되지 않은 영역
        */
        if (current_cell_value == unknown_value_) {
            return true;
        }

        /*
        현재 cell이 free인 경우.

        overlay_memory_on_free_cells_가 false이면,
        현재 LiDAR가 free로 본 최신 관측을 우선한다.
        */
        if (current_cell_value == free_value_) {
            return overlay_memory_on_free_cells_;
        }

        /*
        그 외 값은 보수적으로 parameter 정책에 따른다.
        */
        return overlay_memory_on_free_cells_;
    }

    int MemoryFusionNode::markMemoryOccupied(
        OccupancyGridMsg &fused_grid,
        const int center_x,
        const int center_y)
    {
        /*
        local grid resolution을 기준으로 inflation cell 개수를 계산한다.

        memory_fusion_inflation_radius_가 0이면 중심 cell만 표시한다.
        */
        const double resolution =
            static_cast<double>(fused_grid.info.resolution);

        const int inflation_cells =
            static_cast<int>(
                std::ceil(memory_fusion_inflation_radius_ / resolution)
            );

        const int width = static_cast<int>(fused_grid.info.width);
        const int height = static_cast<int>(fused_grid.info.height);

        int marked_count = 0;

        for (int dy = -inflation_cells; dy <= inflation_cells; ++dy) {
            for (int dx = -inflation_cells; dx <= inflation_cells; ++dx) {
                /*
                원형 inflation을 위해 중심 cell에서의 거리를 계산한다.
                */
                const double distance = std::sqrt(
                    std::pow(static_cast<double>(dx) * resolution, 2.0) +
                    std::pow(static_cast<double>(dy) * resolution, 2.0)
                );

                if (distance > memory_fusion_inflation_radius_) {
                    continue;
                }

                const int gx = center_x + dx;
                const int gy = center_y + dy;

                if (!isInsideGrid(gx, gy, width, height)) {
                    continue;
                }

                const int index = toIndex(gx, gy, width);

                if (index < 0 || index >= static_cast<int>(fused_grid.data.size())) {
                    continue;
                }

                /*
                현재 local grid의 cell 상태를 확인한 뒤 overlay 정책을 적용한다.
                */
                const int current_value =
                    static_cast<int>(fused_grid.data[index]);

                if (!shouldOverlayMemoryOnCell(current_value)) {
                    continue;
                }

                fused_grid.data[index] =
                    static_cast<int8_t>(occupied_value_);

                marked_count++;
            }
        }

        return marked_count;
    }

    MemoryFusionNode::FusionStats MemoryFusionNode::fuseMemoryIntoLocalGrid(
        const OccupancyGridMsg &local_grid,
        const OccupancyGridMsg &memory_grid,
        const LocalizedPoseMsg &pose,
        OccupancyGridMsg &fused_grid)
    {
        FusionStats stats;

        const int memory_width =
            static_cast<int>(memory_grid.info.width);

        const int memory_height =
            static_cast<int>(memory_grid.info.height);

        const int local_width =
            static_cast<int>(local_grid.info.width);

        const int local_height =
            static_cast<int>(local_grid.info.height);

        /*
        memory grid의 모든 cell을 순회한다.

        memory grid는 mission_map 기준 rolling window다.
        그중 occupied cell만 현재 base_link local grid로 투영한다.
        */
        for (int my = 0; my < memory_height; ++my) {
            for (int mx = 0; mx < memory_width; ++mx) {
                const int memory_index = toIndex(mx, my, memory_width);

                if (memory_index < 0 ||
                    memory_index >= static_cast<int>(memory_grid.data.size())) {
                    continue;
                }

                const int memory_value =
                    static_cast<int>(memory_grid.data[memory_index]);

                /*
                memory grid에서 occupied_threshold 이상인 cell만 사용한다.
                unknown(-1)은 무시한다.
                */
                if (memory_value < memory_occupied_threshold_) {
                    continue;
                }

                stats.memory_occupied_cells++;

                /*
                memory grid cell 중심을 mission_map 좌표로 복원한다.
                */
                const double x_map = gridCellCenterX(memory_grid, mx);
                const double y_map = gridCellCenterY(memory_grid, my);

                /*
                mission_map 좌표를 현재 pose 기준 base_link 좌표로 변환한다.
                */
                double x_base = 0.0;
                double y_base = 0.0;

                transformMapToBase(
                    x_map,
                    y_map,
                    pose,
                    x_base,
                    y_base
                );

                /*
                base_link 좌표를 현재 local occupancy grid index로 변환한다.
                */
                const int local_x = worldToGridX(x_base, local_grid);
                const int local_y = worldToGridY(y_base, local_grid);

                /*
                현재 local grid 범위 밖이면 이번 fused grid에는 overlay하지 않는다.
                */
                if (!isInsideGrid(local_x, local_y, local_width, local_height)) {
                    stats.memory_outside_cells++;
                    continue;
                }

                stats.memory_projected_inside_cells++;

                const int local_index = toIndex(local_x, local_y, local_width);

                if (local_index < 0 ||
                    local_index >= static_cast<int>(fused_grid.data.size())) {
                    continue;
                }

                /*
                free cell에 memory를 overlay하지 않는 정책이면,
                free로 관측된 cell은 skipped count로 기록한다.
                */
                const int current_value =
                    static_cast<int>(fused_grid.data[local_index]);

                if (!shouldOverlayMemoryOnCell(current_value)) {
                    stats.memory_skipped_free_cells++;
                    continue;
                }

                /*
                fused grid에 memory occupied를 표시한다.
                */
                const int marked_count =
                    markMemoryOccupied(fused_grid, local_x, local_y);

                stats.memory_overlay_cells += marked_count;
            }
        }

        return stats;
    }

    void MemoryFusionNode::publishPassThroughLocalGrid(
        const OccupancyGridMsg &local_grid,
        const std::string &reason)
    {
        /*
        pose 또는 memory가 없을 때 local grid를 그대로 output으로 발행한다.

        이렇게 하면 FreeSpaceModel 입력을 fused topic으로 바꾼 상태에서도,
        memory_fusion_node 초기화 지연 때문에 pipeline이 끊기지 않는다.
        */
        fused_grid_pub_->publish(local_grid);

        if (debug_log_) {
            RCLCPP_WARN_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                2000,
                "Publishing pass-through local grid. reason=%s",
                reason.c_str()
            );
        }
    }
}

int main(int argc, char ** argv)
{
    rclcpp::init(argc, argv);

    auto node = std::make_shared<lidar_perception::MemoryFusionNode>();

    rclcpp::spin(node);

    rclcpp::shutdown();

    return 0;
}