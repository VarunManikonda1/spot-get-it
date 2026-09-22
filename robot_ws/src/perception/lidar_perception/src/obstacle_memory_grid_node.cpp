/*
- 경로 : ~/robot_ws/src/perception/lidar_perception/src/obstacle_memory_grid_node.cpp
- 역할 : base_link 기준 local occupancy grid의 occupied cell을 mission_map 기준 obstacle memory grid로 변환하는 노드
- 입력 :
    - /perception/lidar/local_occupancy_grid
        - 타입 : nav_msgs/msg/OccupancyGrid
        - 기준 frame : base_link
        - 의미 : 현재 LiDAR frame에서 생성된 로봇 기준 local 2D occupancy grid

    - /localization/pose
        - 타입 : robot_interfaces/msg/LocalizedRobotPose
        - 기준 frame : mission_map
        - 의미 : 현재 로봇의 mission_map 기준 x, y, yaw pose

- 출력 :
    - /perception/lidar/obstacle_memory_grid
        - 타입 : nav_msgs/msg/OccupancyGrid
        - 기준 frame : mission_map
        - 의미 : 최근 관측된 occupied cell을 TTL 동안 기억하는 mission_map 기준 obstacle memory grid

- 주요 기능 :
    1. local occupancy grid에서 occupied cell만 추출한다.
    2. occupied cell 중심 좌표를 base_link 좌표로 복원한다.
    3. /localization/pose의 x, y, yaw를 이용해 base_link 좌표를 mission_map 좌표로 변환한다.
    4. 변환된 mission_map 좌표를 sparse memory map에 저장한다.
    5. 각 memory cell은 마지막 관측 시간(last_seen_time)을 가진다.
    6. obstacle_memory_ttl_sec 시간이 지난 cell은 삭제한다.
    7. 현재 로봇 주변 publish_size_x/y 범위만 nav_msgs/msg/OccupancyGrid로 발행한다.

- 설계 의도 :
    - local_occupancy_grid는 base_link 기준이므로 로봇 기준 현재 순간의 장애물만 표현한다.
    - LiDAR FOV 밖으로 장애물이 사라지거나 순간적으로 미검출되면 local grid에서도 바로 사라진다.
    - 이 노드는 최근 관측된 occupied cell을 mission_map 기준에 저장하여,
      로봇이 이동하더라도 장애물 기억이 공간상 같은 위치에 남도록 만든다.
    - free/unknown은 기억하지 않고 occupied만 기억한다.
*/

#include "lidar_perception/obstacle_memory_grid_node.hpp"

#include <cmath>
#include <functional>
#include <vector>

namespace lidar_perception
{
    /*
    생성자.

    역할:
    - ROS 2 node 이름을 obstacle_memory_grid_node로 설정한다.
    - parameter를 선언하고 기본값을 설정한다.
    - local occupancy grid subscriber를 생성한다.
    - localization pose subscriber를 생성한다.
    - obstacle memory grid publisher를 생성한다.
    - publish grid 크기와 inflation cell 개수를 계산한다.

    이 노드는 local occupancy grid callback이 들어올 때마다 memory를 갱신하고 publish한다.
    */
    ObstacleMemoryGridNode::ObstacleMemoryGridNode()
    : Node("obstacle_memory_grid_node")
    {
        /*
        Input1: local occupancy grid topic.

        - local_occupancy_grid_node가 발행하는 현재 frame 기준 grid.
        - frame_id: base_link
        */
        input_local_grid_topic_ = this->declare_parameter<std::string>(
            "input_local_grid_topic",
            "/perception/lidar/local_occupancy_grid"
        );

        /*
        Input2: localization pose topic.

        - 이 pose는 mission_map 기준 현재 로봇 위치와 yaw를 제공 함.
        - base_link 기준 local grid cell을 mission_map 기준 memory cell로 변환할 때 사용.
        */
        localization_pose_topic_ = this->declare_parameter<std::string>(
            "localization_pose_topic",
            "/localization/pose"
        );

        /*
        Output: obstacle memory grid topic.

        - RViz 시각화와 이후 memory_fusion_node 입력으로 사용할 수 있다.
        */
        output_memory_grid_topic_ = this->declare_parameter<std::string>(
            "output_memory_grid_topic",
            "/perception/lidar/obstacle_memory_grid"
        );

        /*
        입력 grid의 기대 frame.

        실제 msg->header.frame_id와 비교해서 다르면 warning을 출력한다.
        현재 변환 수식은 input grid가 base_link 기준이라고 가정한다.
        */
        input_grid_frame_ = this->declare_parameter<std::string>(
            "input_grid_frame",
            "base_link"
        );

        /*
        obstacle memory grid를 저장하고 publish할 기준 frame.

        현재 프로젝트에서는 global path, localization pose와 맞추기 위해 mission_map을 사용한다.
        */
        memory_frame_ = this->declare_parameter<std::string>(
            "memory_frame",
            "mission_map"
        );

        // memory grid 해상도(단위: meter/cell)
        memory_resolution_ = this->declare_parameter<double>(
            "memory_resolution",
            0.10
        );

        // obstacle memory grid의 x, y 방향 크기
        publish_size_x_ = this->declare_parameter<double>("publish_size_x", 8.0);

        publish_size_y_ = this->declare_parameter<double>("publish_size_y", 8.0);

        // obstacle memory TTL.
        obstacle_memory_ttl_sec_ = this->declare_parameter<double>("obstacle_memory_ttl_sec", 5);

        // localization pose timeout.
        pose_timeout_sec_ = this->declare_parameter<double>("pose_timeout_sec", 0.5);

        // unknown cell value.
        // - nav_msgs/msg/OccupancyGrid에서 일반적으로 unknown은 -1이다.
        // - obstacle memory grid에서는 memory가 없는 cell을 unknown으로 둔다.
        unknown_value_ = this->declare_parameter<int>("unknown_value", -1);

        // free cell value.
        free_value_ = this->declare_parameter<int>("free_value", 0);

        // occupied cell value.
        // - memory에 저장된 장애물 cell은 output grid에서 이 값으로 표시된다.
        // - 일반적으로 occupied는 100이다.
        occupied_value_ = this->declare_parameter<int>("occupied_value", 100);

        // 입력 local occupancy grid에서 occupied로 판단할 threshold.
        // - cell_value >= occupied_threshold_이면 obstacle memory 저장 대상
        // - 현재 local grid가 -1, 0, 100만 사용한다면 100인 cell만 저장된다.
        occupied_threshold_ = this->declare_parameter<int>("occupied_threshold", 50);

        // memory 저장 시 추가 inflation 반경.
        // - local_occupancy_grid_node에서 이미 obstacle inflation을 수행하고 있다면,
        // - 여기서는 0.0 또는 아주 작은 값을 추천한다.
        memory_inflation_radius_ = this->declare_parameter<double>("memory_inflation_radius", 0.0);

        // 디버그 로그 출력 여부.
        debug_log_ = this->declare_parameter<bool>("debug_log", true);

        /*
        publish_width_ / publish_height_ 계산.

        - publish_size_x/y는 meter 단위이고,
        - OccupancyGrid width/height는 cell 개수 단위이므로 resolution으로 나눠 cell 개수로 변환한다.
        */
        publish_width_ = static_cast<int>(
            std::ceil(publish_size_x_ / memory_resolution_));

        publish_height_ = static_cast<int>(
            std::ceil(publish_size_y_ / memory_resolution_));

        /*
        memory inflation 반경을 cell 단위로 변환한다.

        예:
        - memory_inflation_radius_ = 0.20
        - memory_resolution_ = 0.10
        - memory_inflation_cells_ = 2
        */
        memory_inflation_cells_ = static_cast<int>(
            std::ceil(memory_inflation_radius_ / memory_resolution_));

        /*
        localization pose subscriber 생성.

        이 subscriber는 최신 robot pose를 저장한다.
        localGridCallback에서 base_link → mission_map 변환에 사용한다.
        */
        pose_sub_ = this->create_subscription<LocalizedPoseMsg>(
            localization_pose_topic_,
            rclcpp::QoS(10),
            std::bind(
                &ObstacleMemoryGridNode::poseCallback,
                this,
                std::placeholders::_1
            )
        );

        /*
        local occupancy grid subscriber 생성.

        local grid가 들어올 때마다 occupied cell을 추출하고,
        obstacle memory grid를 갱신한 뒤 publish한다.
        */
        local_grid_sub_ = this->create_subscription<OccupancyGridMsg>(
            input_local_grid_topic_,
            rclcpp::QoS(10),
            std::bind(
                &ObstacleMemoryGridNode::localGridCallback,
                this,
                std::placeholders::_1
            )
        );

        /*
        obstacle memory grid publisher 생성.

        출력 메시지는 nav_msgs/msg/OccupancyGrid이며,
        frame_id는 memory_frame_으로 설정된다.
        */
        memory_grid_pub_ = this->create_publisher<OccupancyGridMsg>(
            output_memory_grid_topic_,
            rclcpp::QoS(10)
        );

        /*
        노드 시작 로그.

        launch 시 parameter가 의도대로 들어갔는지 확인하기 좋다.
        */
        RCLCPP_INFO(this->get_logger(), "obstacle_memory_grid_node started!");
        RCLCPP_INFO(this->get_logger(), "input_local_grid_topic  : %s", input_local_grid_topic_.c_str());
        RCLCPP_INFO(this->get_logger(), "localization_pose_topic : %s", localization_pose_topic_.c_str());
        RCLCPP_INFO(this->get_logger(), "output_memory_grid_topic: %s", output_memory_grid_topic_.c_str());
        RCLCPP_INFO(this->get_logger(), "input_grid_frame        : %s", input_grid_frame_.c_str());
        RCLCPP_INFO(this->get_logger(), "memory_frame            : %s", memory_frame_.c_str());
        RCLCPP_INFO(this->get_logger(), "memory_resolution       : %.3f", memory_resolution_);
        RCLCPP_INFO(this->get_logger(), "publish_width/height    : %d / %d", publish_width_, publish_height_);
        RCLCPP_INFO(this->get_logger(), "memory ttl              : %.3f sec", obstacle_memory_ttl_sec_);
    }

    /*
    poseCallback

    /localization/pose를 수신할 때 호출된다.

    이 callback에서는 좌표 변환을 직접 수행하지 않고,
    가장 최근 pose만 저장한다.

    저장하는 정보:
    - latest_pose_      : 최신 pose 메시지
    - latest_pose_time_ : pose가 수신된 node 시간

    latest_pose_time_을 따로 저장하는 이유:
    - message header stamp가 없거나 신뢰하기 어려운 경우에도,
      노드 수신 시간을 기준으로 timeout을 판단할 수 있기 때문이다.
    */
    void ObstacleMemoryGridNode::poseCallback(
        const LocalizedPoseMsg::SharedPtr msg)
    {
        latest_pose_ = msg;
        latest_pose_time_ = this->now();
    }

    /*
    localGridCallback

    /perception/lidar/local_occupancy_grid를 수신할 때 호출된다.

    이 함수가 obstacle memory update의 핵심이다.

    처리 순서:
    1. 최신 pose가 유효한지 확인한다.
    2. 입력 grid frame이 기대 frame과 맞는지 확인한다.
    3. local grid의 모든 cell을 순회한다.
    4. occupied_threshold_ 이상인 cell만 추출한다.
    5. 해당 cell 중심을 base_link 좌표로 복원한다.
    6. base_link 좌표를 mission_map 좌표로 변환한다.
    7. mission_map 좌표를 memory cell로 저장한다.
    8. TTL 지난 cell을 삭제한다.
    9. 현재 memory 상태를 OccupancyGrid로 publish한다.
    */
    void ObstacleMemoryGridNode::localGridCallback(
        const OccupancyGridMsg::SharedPtr msg)
    {
        /*
        현재 node 시간을 기준으로 pose timeout과 memory TTL을 계산한다.
        */
        const rclcpp::Time now = this->now();

        /*
        localization pose가 아직 들어오지 않았거나 너무 오래되었다면,
        base_link → mission_map 변환을 신뢰할 수 없으므로 memory update를 하지 않는다.
        */
        if (!hasValidPose(now)) {
            RCLCPP_WARN_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                2000,
                "No valid localization pose. obstacle memory update skipped."
            );
            return;
        }

        /*
        입력 local grid가 base_link 기준인지 확인한다.

        현재 구현은 TF를 직접 조회하지 않고,
        /localization/pose의 x, y, yaw를 사용해서 base_link → mission_map 변환을 수행한다.
        따라서 입력 grid가 base_link 기준이라는 가정이 중요하다.
        */
        if (msg->header.frame_id != input_grid_frame_) {
            RCLCPP_WARN_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                2000,
                "Input local grid frame_id(%s) != expected frame(%s). "
                "Memory update assumes input grid is in %s frame.",
                msg->header.frame_id.c_str(),
                input_grid_frame_.c_str(),
                input_grid_frame_.c_str()
            );
        }

        /*
        최신 localization pose에서 mission_map 기준 robot pose를 꺼낸다.

        robot_x, robot_y:
        - mission_map 기준 로봇 위치

        robot_yaw:
        - mission_map 기준 로봇 heading
        - base_link 좌표를 mission_map으로 회전 변환할 때 사용
        */
        const double robot_x = static_cast<double>(latest_pose_->x_m);
        const double robot_y = static_cast<double>(latest_pose_->y_m);
        const double robot_yaw = static_cast<double>(latest_pose_->yaw_rad);

        const double cos_yaw = std::cos(robot_yaw);
        const double sin_yaw = std::sin(robot_yaw);

        // [Debug] 이번 local grid frame에서 새로 관측된 occupied cell 개수.
        int observed_occupied_count = 0;

        /*
        OccupancyGrid의 width/height는 uint32_t이지만,
        반복문과 index 계산 편의를 위해 int로 변환.
        */
        const int width = static_cast<int>(msg->info.width);
        const int height = static_cast<int>(msg->info.height);

        /*
        local occupancy grid의 모든 cell을 순회한다.

        msg->data는 row-major 1D 배열이므로,
        gx, gy를 toIndex()를 통해 1D index로 변환해서 접근한다.
        */
        for (int gy = 0; gy < height; ++gy) {
            for (int gx = 0; gx < width; ++gx) {
                const int index = toIndex(gx, gy, width);

                /*
                정상적인 OccupancyGrid라면 index는 data size 내부에 있어야 한다.
                하지만 잘못된 메시지나 width/height/data size 불일치가 있을 수 있으므로 확인한다.
                */
                if (index < 0 || index >= static_cast<int>(msg->data.size())) {
                    continue;
                }

                //OccupancyGrid data 타입 int8_t 배열 → int로 변환
                const int cell_value = static_cast<int>(msg->data[index]);

                /*
                occupied_threshold_ 미만이면 memory 저장 대상이 아니다.

                예:
                - -1 unknown → 무시
                - 0 free      → 무시
                - 100 occupied → 저장
                */
                if (cell_value < occupied_threshold_) {
                    continue;
                }

                /*
                입력 local occupancy grid의 cell 중심 좌표를 base_link 좌표로 복원한다.

                OccupancyGrid origin은 grid의 왼쪽 아래 기준 좌표이고,
                cell 중심은 origin + (cell_index + 0.5) * resolution 이다.

                현재 local occupancy grid는 base_link 기준이므로,
                x_base, y_base는 로봇 몸체 기준 장애물 위치다.
                */
                const double x_base = gridCellCenterX(*msg, gx);
                const double y_base = gridCellCenterY(*msg, gy);

                /*
                base_link → mission_map 좌표 변환.

                변환식:
                x_map = robot_x + cos(yaw) * x_base - sin(yaw) * y_base
                y_map = robot_y + sin(yaw) * x_base + cos(yaw) * y_base

                의미:
                - base_link 기준 장애물 좌표를 robot yaw만큼 회전
                - mission_map 기준 robot 위치를 더해 전역 좌표로 변환
                */
                const double x_map =
                    robot_x + cos_yaw * x_base - sin_yaw * y_base;

                const double y_map =
                    robot_y + sin_yaw * x_base + cos_yaw * y_base;

                /*
                mission_map 기준 occupied 좌표를 memory map에 저장한다.

                같은 위치가 반복 관측되면 last_seen_time이 갱신된다.
                관측이 끊기면 TTL 이후 삭제된다.
                */
                insertMemoryCell(x_map, y_map, now);

                observed_occupied_count++;
            }
        }

        /*
        TTL이 지난 memory cell을 삭제한다.

        이 처리를 하지 않으면 한 번 본 장애물이 영구히 남게 되어
        실제로 사라진 장애물도 계속 회피하게 된다.
        */
        pruneExpiredCells(now);

        /*
        sparse memory map을 nav_msgs/msg/OccupancyGrid 메시지로 변환한다.

        내부 memory_cells_는 sparse 구조이고,
        publish 메시지는 현재 로봇 주변 rolling window만 dense grid로 표현한다.
        */
        OccupancyGridMsg memory_grid_msg = buildMemoryGridMessage(now);

        /*
        obstacle memory grid 발행.

        RViz에서 확인하거나,
        다음 단계의 memory_fusion_node에서 입력으로 사용할 수 있다.
        */
        memory_grid_pub_->publish(memory_grid_msg);

        /*
        [Debug] 관측 occupied cell 수, 현재 memory cell 수 출력
        memory가 TTL에 따라 유지/삭제되는지 확인.
        */
        if (debug_log_) {
            RCLCPP_INFO_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                1000,
                "\nobstacle memory grid"
                "\nobserved occupied cells : %d"
                "\nmemory cells            : %zu"
                "\noutput frame            : %s"
                "\noutput size             : width=%d height=%d resolution=%.2f"
                "\nttl                     : %.2f sec",
                observed_occupied_count,
                memory_cells_.size(),
                memory_frame_.c_str(),
                publish_width_,
                publish_height_,
                memory_resolution_,
                obstacle_memory_ttl_sec_
            );
        }
    }

    /*
    hasValidPose

    최신 localization pose를 사용할 수 있는지 확인한다.

    반환 true 조건:
    - latest_pose_가 존재해야 한다.
    - latest_pose_time_이 pose_timeout_sec_보다 오래되지 않아야 한다.

    필요한 이유:
    - local occupancy grid를 mission_map 기준으로 변환하려면 현재 pose가 필요하다.
    - pose가 너무 오래되면 장애물을 잘못된 mission_map 위치에 저장할 수 있다.
    */
    bool ObstacleMemoryGridNode::hasValidPose(
        const rclcpp::Time &now) const
    {
        if (!latest_pose_) {
            return false;
        }

        const double age_sec = (now - latest_pose_time_).seconds();

        return age_sec <= pose_timeout_sec_;
    }

    /*
    isInsideGrid

    특정 grid index가 주어진 width/height 범위 안에 있는지 확인한다.

    사용 위치:
    - output memory grid에 occupied cell을 찍을 때
    - 범위 밖 index 접근을 막기 위해 사용한다.
    */
    bool ObstacleMemoryGridNode::isInsideGrid(
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

    /*
    toIndex

    2D grid index를 OccupancyGrid.data의 1D 배열 index로 변환한다.

    OccupancyGrid.data는 row-major 구조다.

    공식:
    - index = grid_y * width + grid_x
    */
    int ObstacleMemoryGridNode::toIndex(
        const int grid_x,
        const int grid_y,
        const int width) const
    {
        return grid_y * width + grid_x;
    }

    /*
    gridCellCenterX

    입력 OccupancyGrid에서 특정 x index cell의 중심 x 좌표를 계산한다.

    grid.info.origin.position.x:
    - grid의 시작점 좌표

    grid.info.resolution:
    - cell 하나의 크기

    +0.5를 하는 이유:
    - cell의 왼쪽 모서리가 아니라 중심 좌표를 사용하기 위해서다.
    */
    double ObstacleMemoryGridNode::gridCellCenterX(
        const OccupancyGridMsg &grid,
        const int grid_x) const
    {
        return grid.info.origin.position.x +
            (static_cast<double>(grid_x) + 0.5) *
            static_cast<double>(grid.info.resolution);
    }

    /*
    gridCellCenterY

    입력 OccupancyGrid에서 특정 y index cell의 중심 y 좌표를 계산한다.

    gridCellCenterX()와 동일한 방식으로 y축 중심 좌표를 계산한다.
    */
    double ObstacleMemoryGridNode::gridCellCenterY(
        const OccupancyGridMsg &grid,
        const int grid_y) const
    {
        return grid.info.origin.position.y +
            (static_cast<double>(grid_y) + 0.5) *
            static_cast<double>(grid.info.resolution);
    }

    /*
    mapToMemoryKey

    mission_map 기준 meter 좌표를 sparse memory map의 정수 key로 변환한다.

    double 좌표를 unordered_map key로 직접 사용하지 않는 이유:
    - 부동소수점 오차 때문에 같은 위치라도 key가 미세하게 달라질 수 있다.
    - resolution 단위로 정수화하면 안정적으로 같은 cell을 식별할 수 있다.

    예:
    - memory_resolution_ = 0.10
    - x_map = 1.23
    - key.x = floor(1.23 / 0.10) = 12
    */
    ObstacleMemoryGridNode::GridKey ObstacleMemoryGridNode::mapToMemoryKey(
        const double x_map,
        const double y_map) const
    {
        GridKey key;

        key.x = static_cast<int>(
            std::floor(x_map / memory_resolution_)
        );

        key.y = static_cast<int>(
            std::floor(y_map / memory_resolution_)
        );

        return key;
    }

    /*
    memoryKeyCenterX

    memory key를 mission_map 기준 meter 좌표로 복원한다.

    key는 cell index이므로, cell 중심 좌표를 얻기 위해 +0.5를 한다.
    */
    double ObstacleMemoryGridNode::memoryKeyCenterX(
        const GridKey &key) const
    {
        return (static_cast<double>(key.x) + 0.5) * memory_resolution_;
    }

    /*
    memoryKeyCenterY

    memory key를 mission_map 기준 meter 좌표로 복원한다.

    memoryKeyCenterX()와 동일한 방식으로 y축 중심 좌표를 계산한다.
    */
    double ObstacleMemoryGridNode::memoryKeyCenterY(
        const GridKey &key) const
    {
        return (static_cast<double>(key.y) + 0.5) * memory_resolution_;
    }

    /*
    insertMemoryCell

    mission_map 기준 occupied 좌표를 sparse memory map에 저장한다.

    동작:
    1. x_map, y_map을 memory grid key로 변환한다.
    2. memory_inflation_radius_에 따라 주변 cell도 함께 저장한다.
    3. 저장되는 cell의 last_seen_time을 now로 갱신한다.

    같은 cell이 반복 관측되면:
    - 기존 entry의 last_seen_time만 최신 시간으로 갱신된다.

    관측이 끊기면:
    - last_seen_time이 더 이상 갱신되지 않는다.
    - pruneExpiredCells()에서 TTL 초과 시 삭제된다.
    */
    void ObstacleMemoryGridNode::insertMemoryCell(
        const double x_map,
        const double y_map,
        const rclcpp::Time &now)
    {
        // 중심 occupied point를 memory grid key로 변환한다.
        const GridKey center_key = mapToMemoryKey(x_map, y_map);

        /*
        memory inflation cell 범위만큼 주변 cell을 순회한다.

        memory_inflation_cells_가 0이면 dx=0, dy=0만 처리하므로
        중심 cell 하나만 저장된다.
        */
        for (int dy = -memory_inflation_cells_; dy <= memory_inflation_cells_; ++dy) {
            for (int dx = -memory_inflation_cells_; dx <= memory_inflation_cells_; ++dx) {
                /*
                현재 주변 cell이 inflation radius 안에 있는지 거리로 검사한다.

                사각형 전체를 채우지 않고 원형 반경만 채우기 위해 distance를 계산한다.
                */
                const double distance = std::sqrt(
                    std::pow(static_cast<double>(dx) * memory_resolution_, 2.0) +
                    std::pow(static_cast<double>(dy) * memory_resolution_, 2.0)
                );

                if (distance > memory_inflation_radius_) {
                    continue;
                }

                // 중심 key 기준으로 주변 cell key를 만든다.
                GridKey key;
                key.x = center_key.x + dx;
                key.y = center_key.y + dy;

                /*
                해당 key를 memory에 저장하거나 이미 있으면 last_seen_time을 갱신한다.

                unordered_map의 operator[]는 key가 없으면 새 MemoryCell을 생성한다.
                */
                memory_cells_[key].last_seen_time = now;
            }
        }
    }

    /*
    pruneExpiredCells

    TTL이 지난 memory cell을 삭제한다.

    삭제 조건:
    - now - last_seen_time > obstacle_memory_ttl_sec_

    구현상 주의:
    - unordered_map을 순회하면서 erase할 때는 iterator 반환값을 받아야 안전하다.
    */
    void ObstacleMemoryGridNode::pruneExpiredCells(
        const rclcpp::Time &now)
    {
        for (auto it = memory_cells_.begin(); it != memory_cells_.end(); ) {
            const double age_sec =
                (now - it->second.last_seen_time).seconds();

            if (age_sec > obstacle_memory_ttl_sec_) {
                /*
                erase()는 삭제된 다음 위치의 iterator를 반환한다.
                */
                it = memory_cells_.erase(it);
            } else {
                /*
                아직 TTL이 지나지 않은 cell은 유지한다.
                */
                ++it;
            }
        }
    }

    /*
    buildMemoryGridMessage

    sparse memory map을 nav_msgs/msg/OccupancyGrid 메시지로 변환한다.

    내부 memory_cells_는 mission_map 기준 sparse 저장소다.
    하지만 OccupancyGrid는 dense 2D 배열이므로, publish할 때 현재 로봇 주변 window만 잘라서 발행한다.

    출력 grid 특징:
    - frame_id = memory_frame_
    - 현재 프로젝트에서는 mission_map
    - origin은 현재 로봇 pose를 중심으로 publish_size_x/y의 절반만큼 뒤로 이동한 위치
    - data는 unknown으로 초기화
    - memory cell이 output window 안에 들어오면 occupied로 표시
    */
    ObstacleMemoryGridNode::OccupancyGridMsg
    ObstacleMemoryGridNode::buildMemoryGridMessage(
        const rclcpp::Time &stamp) const
    {
        OccupancyGridMsg grid_msg;

        /*
        header 설정.

        stamp:
        - 현재 node 시간

        frame_id:
        - memory_frame_
        - 기본값 mission_map
        */
        grid_msg.header.stamp = stamp;
        grid_msg.header.frame_id = memory_frame_;

        /*
        OccupancyGrid metadata 설정.
        */
        grid_msg.info.resolution = memory_resolution_;
        grid_msg.info.width = static_cast<std::uint32_t>(publish_width_);
        grid_msg.info.height = static_cast<std::uint32_t>(publish_height_);

        /*
        output grid의 origin을 현재 로봇 주변 rolling window로 설정한다.

        예:
        - robot_x = 2.0
        - publish_size_x = 8.0
        - origin_x = 2.0 - 4.0 = -2.0

        그러면 x 방향으로 [-2.0, 6.0] 범위가 publish된다.
        */
        const double robot_x =
            latest_pose_ ? static_cast<double>(latest_pose_->x_m) : 0.0;

        const double robot_y =
            latest_pose_ ? static_cast<double>(latest_pose_->y_m) : 0.0;

        const double origin_x = robot_x - 0.5 * publish_size_x_;
        const double origin_y = robot_y - 0.5 * publish_size_y_;

        /*
        origin은 mission_map 기준 output grid의 왼쪽 아래 좌표다.
        */
        grid_msg.info.origin.position.x = origin_x;
        grid_msg.info.origin.position.y = origin_y;
        grid_msg.info.origin.position.z = 0.0;

        /*
        output memory grid는 mission_map 축에 정렬된 grid로 둔다.
        따라서 orientation은 identity quaternion이다.
        */
        grid_msg.info.origin.orientation.x = 0.0;
        grid_msg.info.origin.orientation.y = 0.0;
        grid_msg.info.origin.orientation.z = 0.0;
        grid_msg.info.origin.orientation.w = 1.0;

        /*
        data 배열 초기화.

        memory가 없는 cell은 free가 아니라 unknown으로 둔다.
        이유:
        - obstacle memory grid는 “최근 봤던 장애물”만 표현하는 grid다.
        - memory가 없다고 해서 그 공간이 free라고 단정할 수 없다.
        */
        grid_msg.data.assign(
            publish_width_ * publish_height_,
            static_cast<int8_t>(unknown_value_)
        );

        // sparse memory cell들을 순회하면서 output grid window 내부에 들어오는 cell만 occupied로 표시한다.
        for (const auto &memory_pair : memory_cells_) {
            const GridKey &key = memory_pair.first;

            // memory key → mission_map 기준 meter 좌표로 복원
            const double x_map = memoryKeyCenterX(key);
            const double y_map = memoryKeyCenterY(key);

            /*
            output grid의 범위 안에 있으면 occupied로 표시한다.
            범위 밖이면 markOccupiedInOutputGrid() 내부에서 무시된다.
            */
            markOccupiedInOutputGrid(grid_msg, x_map, y_map);
        }

        return grid_msg;
    }

    /*
    markOccupiedInOutputGrid

    mission_map 기준 memory 좌표를 output OccupancyGrid의 cell로 표시한다.

    처리 순서:
    1. output grid origin 기준 상대 좌표를 계산한다.
    2. resolution으로 나눠 grid index를 계산한다.
    3. grid 범위 안에 있으면 data[index]를 occupied_value_로 설정한다.
    */
    void ObstacleMemoryGridNode::markOccupiedInOutputGrid(
        OccupancyGridMsg &grid,
        const double x_map,
        const double y_map) const
    {
        /*
        output grid origin.

        이 origin은 buildMemoryGridMessage()에서 현재 로봇 주변 rolling window 기준으로 설정된다.
        */
        const double origin_x = grid.info.origin.position.x;
        const double origin_y = grid.info.origin.position.y;

        // mission_map 좌표 → output grid index로 변환
        const int gx = static_cast<int>(
            std::floor((x_map - origin_x) / memory_resolution_));

        const int gy = static_cast<int>(
            std::floor((y_map - origin_y) / memory_resolution_));

        /*
        publish window 밖에 있는 memory cell은 이번 OccupancyGrid 메시지에 표시하지 않는다.
        내부 sparse memory에서는 계속 유지될 수 있다.
        */
        if (!isInsideGrid(gx, gy, publish_width_, publish_height_)) {
            return;
        }

        const int index = toIndex(gx, gy, publish_width_);

        // grid 내부 데이터 확인
        if (index < 0 || index >= static_cast<int>(grid.data.size())) {
            return;
        }

        // 해당 output grid cell을 occupied로 설정
        grid.data[index] = static_cast<int8_t>(occupied_value_);
    }
}

int main(int argc, char ** argv)
{
    rclcpp::init(argc, argv);

    auto node = std::make_shared<lidar_perception::ObstacleMemoryGridNode>();

    rclcpp::spin(node);

    rclcpp::shutdown();

    return 0;
}