/*
- 경로: spot_navigation/src/path_progress_tracker_node.cpp
- 역할:
  global path와 localization pose를 입력으로 받아,
  현재 로봇이 global path 상에서 어디쯤 진행 중인지 계산한다.
- 입력 토픽:
    - /planning/mock_global_path/spot_01
    Type: robot_interfaces/msg/GlobalPathWaypoints

    - /localization/mock_pose
    Type: robot_interfaces/msg/LocalizedRobotPose

- 출력 토픽:
    - /navigation/path_progress/spot_01
    Type: robot_interfaces/msg/PathProgress

- 주요 기능:
    - global path 수신 여부와 유효성 검사
    - localization pose 수신 여부와 유효성 검사
    - 현재 pose와 가장 가까운 waypoint를 nearest_index로 계산
    - nearest_index 기준 lookahead 거리 앞의 waypoint를 target_index로 계산
    - target_heading_rad, target_yaw_rad, heading_error_rad, yaw_error_rad 계산
    - progress_ratio, distance_to_goal_m, goal_reached 계산

- 좌표계:
    - global path와 localization pose는 모두 global_frame 기준이어야 한다.
    - 현재 프로젝트에서는 global_frame = "mission_map"으로 사용한다.
*/

#include "spot_navigation/path_progress_tracker_node.hpp"

#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <cstddef>

namespace spot_navigation
{
    namespace 
    {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kTwoPi = 2.0 * kPi;
    }

    PathProgressTrackerNode::PathProgressTrackerNode()
    : Node("path_progress_tracker_node")
    {
        // [1] YAML Parameter Declare&Read
        loadParameters();

        // [2] global path, localization pose Subscriber 생성
        auto path_qos = rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local();

        global_path_sub_ =
            this->create_subscription<GlobalPathMsg>(
                global_path_topic_,
                path_qos,
                std::bind(&PathProgressTrackerNode::globalPathCallback, this, std::placeholders::_1)
            );

        auto pose_qos = rclcpp::QoS(rclcpp::KeepLast(10)).best_effort();

        pose_sub_ =
            this->create_subscription<LocalizedPoseMsg>(
                localization_pose_topic_,
                pose_qos,
                std::bind(&PathProgressTrackerNode::poseCallback, this, std::placeholders::_1)
            );
        
        // [3] PathProgress Publisher 생성
        auto progress_qos = rclcpp::QoS(rclcpp::KeepLast(10)).reliable();

        path_progress_pub_ =
            this->create_publisher<PathProgressMsg>(
                path_progress_topic_,
                progress_qos
            );
        
        // [4] 주기 Timer 생성
        const auto publish_period =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::duration<double>(1.0 / publish_rate_hz_)
            );

        publish_timer_ =
            this->create_wall_timer(
                publish_period,
                std::bind(&PathProgressTrackerNode::publishPathProgress, this)
            );

        RCLCPP_INFO(
            this->get_logger(),
            "path_progress_tracker_node started. path=%s, pose=%s, output=%s, robot_id=%s, frame=%s",
            global_path_topic_.c_str(),
            localization_pose_topic_.c_str(),
            path_progress_topic_.c_str(),
            robot_id_.c_str(),
            global_frame_.c_str()
        );
    }

    void PathProgressTrackerNode::loadParameters()
    {
        robot_id_ = this->declare_parameter<std::string>("robot_id", "spot_01");

        global_path_topic_ = this->declare_parameter<std::string>(
            "global_path_topic",
            "/planning/mock_global_path/spot_01");

        localization_pose_topic_ = this->declare_parameter<std::string>(
            "localization_pose_topic",
            "/localization/pose");

        path_progress_topic_ = this->declare_parameter<std::string>(
            "path_progress_topic",
            "/navigation/path_progress/spot_01");

        global_frame_ = this->declare_parameter<std::string>("global_frame","mission_map");
        lookahead_distance_m_ = this->declare_parameter<double>("lookahead_distance_m", 0.5);
        goal_tolerance_m_ = this->declare_parameter<double>("goal_tolerance_m", 0.3);
        pose_timeout_sec_ = this->declare_parameter<double>("pose_timeout_sec", 0.5);
        publish_rate_hz_ = this->declare_parameter<double>("publish_rate_hz", 10.0);
        allow_backward_index_jump_ = this->declare_parameter<bool>("allow_backward_index_jump", false);                
    }

    void PathProgressTrackerNode::globalPathCallback(const GlobalPathMsg::SharedPtr msg)
    {
        path_received_    = true;

        bool path_changed = true;

        // 이전에 받은 path가 있고, waypoint 개수도 같다면 “같은 path"로 판단
        if (latest_path_ &&
            latest_path_->waypoints.size() == msg->waypoints.size())
        {
            path_changed = false;

            for (std::size_t i = 0; i < msg->waypoints.size(); ++i) {
                const auto &old_wp = latest_path_->waypoints[i];
                const auto &new_wp = msg->waypoints[i];

                const bool waypoint_changed =
                    std::fabs(static_cast<double>(old_wp.x_m) - static_cast<double>(new_wp.x_m)) > 1.0e-6 ||
                    std::fabs(static_cast<double>(old_wp.y_m) - static_cast<double>(new_wp.y_m)) > 1.0e-6 ||
                    std::fabs(static_cast<double>(old_wp.yaw_rad) - static_cast<double>(new_wp.yaw_rad)) > 1.0e-6;
                
                // waypoint_changed = true : 새로운 path로 취급
                if (waypoint_changed) {
                    path_changed = true;
                    break;
                }
            }
        }

        // 새로운 path 저장
        latest_path_ = msg;

        // path_changed=true : 새로운 path 취급으로 상태 초기화
        if (path_changed) {
            has_last_nearest_index_ = false;
            last_nearest_index_     = 0;

            RCLCPP_INFO(
                this->get_logger(),
                "New global path detected. Reset nearest index tracking."
            );
        }

        RCLCPP_INFO_THROTTLE(
            this->get_logger(),
            *this->get_clock(),
            3000,
            "Received global path. robot_id=%s, frame=%s, waypoints=%zu",
            msg->robot_id.c_str(),
            msg->header.frame_id.c_str(),
            msg->waypoints.size()
        );
    }

    void PathProgressTrackerNode::poseCallback(const LocalizedPoseMsg::SharedPtr msg)
    {
        pose_received_ = true;
        latest_pose_   = msg;

        /*
        pose timeout: 메시지 header.stamp가 아니라 실제 수신 시각 기준으로 판단

        이유:
            - mock 데이터, bag replay, time sync 문제 상황에서 header.stamp가 현재 ROS time과 다를 수 있다.
            - tracker 입장에서는 "마지막으로 pose를 실제 받은 시각"이 timeout 판단에 더 안정적이다.
        */
        last_pose_receive_time_ = this->now();
        last_pose_receive_time_initialized_ = true;
    }

    void PathProgressTrackerNode::publishPathProgress()
    {
        const auto now = this->now();

        PathProgressMsg progress_msg;

        progress_msg.header.stamp   = now;
        progress_msg.header.frame_id = global_frame_;
        progress_msg.robot_id        = robot_id_;

        progress_msg.path_received = path_received_;
        progress_msg.pose_received = pose_received_;

        const bool path_valid = isPathValid();
        const bool pose_valid = isPoseValid(now);

        progress_msg.path_valid = path_valid;
        progress_msg.pose_valid = pose_valid;

        progress_msg.total_waypoints = latest_path_ ?
            static_cast<std::uint32_t>(latest_path_->waypoints.size()) : 0U;
        
        /*
        입력 데이터가 아직 계산 가능한 상태가 아니어도 PathProgress는 publish한다.

        이유:
            downstream node가 아래 상태를 보고 원인을 구분할 수 있다.
            - path를 아예 못 받았는지
            - path는 받았지만 유효하지 않은지
            - pose를 아예 못 받았는지
            - pose는 받았지만 timeout/frame mismatch 상태인지
        */
        if (!path_valid || !pose_valid) {
            path_progress_pub_->publish(progress_msg);

            RCLCPP_WARN_THROTTLE(
            this->get_logger(),
            *this->get_clock(),
            2000,
            "Cannot compute path progress. path_received=%s, pose_received=%s, path_valid=%s, pose_valid=%s",
            progress_msg.path_received ? "true" : "false",
            progress_msg.pose_received ? "true" : "false",
            progress_msg.path_valid ? "true" : "false",
            progress_msg.pose_valid ? "true" : "false"
            );

            return;
        }

        const auto &pose      = *latest_pose_;
        const auto &path      = *latest_path_;
        const auto &waypoints = path.waypoints;

        // [1] 현재 pose 기준 nearest_index 계산
        const std::uint32_t nearest_index = findNearestIndex(pose);

        // [2] nearest_index 기준 Lookahead target_index 계산
        const std::uint32_t target_index = findTargetIndex(nearest_index);

        const auto &nearest_wp = waypoints[nearest_index];  // 가장 가까운 global path waypoint
        const auto &target_wp  = waypoints[target_index];   // nearest_index 기준 lookahead 거리 앞쪽의 target waypoint
        const auto &goal_wp    = waypoints.back();          // global path의 마지막 waypoint

        // Spot의 현재 위치(x,y), 방향(yaw)
        const double current_x   = static_cast<double>(pose.x_m);
        const double current_y   = static_cast<double>(pose.y_m);
        const double current_yaw = normalizeAngle(static_cast<double>(pose.yaw_rad));

        // 가장 가까운 waypoint 위치(x,y)
        const double nearest_x = static_cast<double>(nearest_wp.x_m);
        const double nearest_y = static_cast<double>(nearest_wp.y_m);

        // target waypoint 위치(x,y), 방향(yaw)
        const double target_x   = static_cast<double>(target_wp.x_m);
        const double target_y   = static_cast<double>(target_wp.y_m);
        const double target_yaw = normalizeAngle(static_cast<double>(target_wp.yaw_rad));
        
        // 목표 위치(x,y)
        const double goal_x = static_cast<double>(goal_wp.x_m);
        const double goal_y = static_cast<double>(goal_wp.y_m);

        // [3] 주요 거리 계산
        const double distance_to_nearest =
            calculateDistance2D(current_x, current_y, nearest_x, nearest_y);

        const double distance_to_target =
            calculateDistance2D(current_x, current_y, target_x, target_y);

        const double distance_to_goal =
            calculateDistance2D(current_x, current_y, goal_x, goal_y);
        
        // [4] target_heading_rad 계산
        // target_heading_rad: 현재 로봇 위치에서 target waypoint 좌표를 바라보는 각도
        double target_heading = target_yaw;

        if (distance_to_target > 1.0e-6) target_heading = std::atan2(target_y - current_y, target_x - current_x);

        // heading error 계산을 위해 -pi ~ +pi 범위로 정규화
        target_heading = normalizeAngle(target_heading);  

        // [5] heading/yaw error 계산
        // - heading_error_rad: target 좌표를 향하기 위한 필요한 회전 오차
        // - yaw_error_rad: target waypoint가 가진 목표 yaw 방향과 현재 yaw의 차이
        const double heading_error = normalizeAngle(target_heading - current_yaw);
        const double yaw_error     = normalizeAngle(target_yaw - current_yaw);

        // [6] goal 도착 여부 계산
        const bool goal_reached = distance_to_goal <= goal_tolerance_m_;

        // [7] progress_ratio 계산
        // - MVP에서는 index 기반 진행률을 사용한다.
        // - nearest_index가 0이면 0.0, 마지막 index이면 1.0이 된다.
        // - 추후 고도화:
        //  waypoint 간 누적 거리 기반 progress_ratio로 개선 가능하다.
        double progress_ratio = 0.0;

        if (waypoints.size() <= 1) {
            progress_ratio = goal_reached ? 1.0 : 0.0;
        } else {
            progress_ratio =
            static_cast<double>(nearest_index) /
            static_cast<double>(waypoints.size() - 1);
        }

        progress_ratio = clamp01(progress_ratio);
    
        // [8] PathProgress 메시지 채우기
        progress_msg.nearest_index = nearest_index;
        progress_msg.distance_to_nearest_m = static_cast<float>(distance_to_nearest);
        progress_msg.nearest_x_m = static_cast<float>(nearest_x);
        progress_msg.nearest_y_m = static_cast<float>(nearest_y);

        progress_msg.target_index = target_index;
        progress_msg.distance_to_target_m = static_cast<float>(distance_to_target);
        progress_msg.target_x_m = static_cast<float>(target_x);
        progress_msg.target_y_m = static_cast<float>(target_y);
        progress_msg.target_yaw_rad = static_cast<float>(target_yaw);

        progress_msg.target_heading_rad = static_cast<float>(target_heading);
        progress_msg.heading_error_rad = static_cast<float>(heading_error);
        progress_msg.yaw_error_rad = static_cast<float>(yaw_error);

        progress_msg.progress_ratio = static_cast<float>(progress_ratio);
        progress_msg.distance_to_goal_m = static_cast<float>(distance_to_goal);
        progress_msg.goal_reached = goal_reached;

        // [9] publish.
        path_progress_pub_->publish(progress_msg);  
        
        // [10] 다음 cycle에서 index가 뒤로 튀는 것 방지를 위한 nearest_index 저장 (중요)
        last_nearest_index_ = nearest_index;
        has_last_nearest_index_ = true;

        // [11] 로그 출력
        RCLCPP_INFO_THROTTLE(
            this->get_logger(),
            *this->get_clock(),
            1000,
            "\npath progress"
            "\n  nearest_index      : %u"
            "\n  target_index       : %u"
            "\n  progress_ratio     : %.2f"
            "\n  distance_to_goal_m : %.2f"
            "\n  heading_error_rad  : %.2f"
            "\n  yaw_error_rad      : %.2f"
            "\n  goal_reached       : %s",
            nearest_index,
            target_index,
            progress_ratio,
            distance_to_goal,
            heading_error,
            yaw_error,
            goal_reached ? "true" : "false"
        );        
    }

    bool PathProgressTrackerNode::isPathValid() const
    {
        // path를 아직 한 번도 받지 않았거나 저장된 path가 없으면 false
        if (!path_received_ || !latest_path_) return false;

        // 다중 로봇 환경에서 다른 robot_id의 path가 들어온 경우 차단
        if (latest_path_->robot_id != robot_id_) return false;

        // global path는 localization pose와 같은 global_frame 기준이어야 함
        if (latest_path_->header.frame_id != global_frame_) return false;
        
        // waypoint가 없으면 nearest/target/goal 계산 불가
        if (latest_path_->waypoints.empty()) return false;

        // 각 waypoint의 frame과 수치 유효성 검사
        for (const auto &waypoint : latest_path_->waypoints) {
            if (!waypoint.header.frame_id.empty() &&
                waypoint.header.frame_id != global_frame_) return false;
            
            if (!std::isfinite(static_cast<double>(waypoint.x_m)) ||
                !std::isfinite(static_cast<double>(waypoint.y_m)) ||
                !std::isfinite(static_cast<double>(waypoint.yaw_rad))) return false;
        }

        return true;
    }

    bool PathProgressTrackerNode::isPoseValid(const rclcpp::Time &now) const
    {
        // pose를 아직 한 번도 받지 않았거나 저장된 pose가 없으면 계산 불가
        if (!pose_received_ || !latest_pose_) return false;

        // 다른 robot_id의 pose가 들어온 경우 차단
        if (latest_pose_->robot_id != robot_id_) return false;

        // pose와 global path는 같은 global_frame 기준이어야 함
        if (latest_pose_->header.frame_id != global_frame_) return false;

        // 위치/yaw 값이 NaN 또는 inf이면 거리/각도 계산 불가
        if (!std::isfinite(static_cast<double>(latest_pose_->x_m)) ||
            !std::isfinite(static_cast<double>(latest_pose_->y_m)) ||
            !std::isfinite(static_cast<double>(latest_pose_->yaw_rad))) return false;
        
        // pose를 받은 시각이 초기화되지 않았으면 timeout 판단 불가
        if (!last_pose_receive_time_initialized_) return false;

        // 마지막 pose 수신 이후 pose_timeout_sec_ 이상 지나면 stale pose로 판단
        const double pose_age_sec = (now - last_pose_receive_time_).seconds();

        if (pose_age_sec > pose_timeout_sec_) return false;

        return true;
    }

    std::uint32_t PathProgressTrackerNode::findNearestIndex(
        const LocalizedPoseMsg &pose) const
    {
        const auto &waypoints = latest_path_->waypoints;

        if (waypoints.empty()) return 0;

        std::size_t search_start_index = 0;

        /*
        allow_backward_index_jump_=false이면,
        이전 nearest_index보다 뒤쪽 구간은 다시 탐색하지 않는다.

        목적:
            - localization pose 흔들림으로 nearest_index가 뒤로 튀는 현상 방지
            - 전진 주행 기준 progress_ratio 안정화
        */
        if (!allow_backward_index_jump_ && has_last_nearest_index_) {
            if (last_nearest_index_ < waypoints.size()) {
                search_start_index = static_cast<std::size_t>(last_nearest_index_);
            }
        }

        double min_distance = std::numeric_limits<double>::max();
        std::size_t nearest_index = search_start_index;
        
        const double current_x = static_cast<double>(pose.x_m);
        const double current_y = static_cast<double>(pose.y_m);

        // search_start_index부터 마지막 waypoint까지 순회하면서
        // 현재 pose와 가장 가까운 waypoint를 찾는다.
        for (std::size_t i = search_start_index; i < waypoints.size(); ++i) {
            const double wp_x = static_cast<double>(waypoints[i].x_m);
            const double wp_y = static_cast<double>(waypoints[i].y_m);

            const double distance =
                calculateDistance2D(current_x, current_y, wp_x, wp_y);
            
            if (distance < min_distance) {
                min_distance = distance;
                nearest_index = i;
            }
        }

        // 현재 pose에서 가장 가까운 waypoint index
        return static_cast<std::uint32_t>(nearest_index);
    }

    std::uint32_t PathProgressTrackerNode::findTargetIndex(
        std::uint32_t nearest_index) const
    {
        const auto &waypoints = latest_path_->waypoints;

        if (waypoints.empty()) return 0U;

        if (nearest_index >= waypoints.size() - 1)
            return static_cast<std::uint32_t>(waypoints.size() - 1);

        double accumulated_distance_m = 0.0;    // 누적 거리

        /*
        nearest_index부터 path 진행 방향으로 waypoint 간 거리를 누적.
        누적 거리가 lookahead_distance_m_ 이상이 되는 첫 waypoint를 target으로 선택.
        예:
            nearest_index = 0, lookahead_distance_m_ = 0.5
            - P0 -> P1 = 0.47m / accumulated = 0.47m
            - P1 -> P2 = 0.48m / accumulated = 0.95m
            => 0.95m >= 0.5m 이므로 target_index = 2
        */
        for (std::size_t i = static_cast<std::size_t>(nearest_index);
            i+1 < waypoints.size(); ++i)
        {
            const double x1 = static_cast<double>(waypoints[i].x_m);
            const double y1 = static_cast<double>(waypoints[i].y_m);
            const double x2 = static_cast<double>(waypoints[i + 1].x_m);
            const double y2 = static_cast<double>(waypoints[i + 1].y_m);
            
            accumulated_distance_m += calculateDistance2D(x1, y1, x2, y2);

            if (accumulated_distance_m >= lookahead_distance_m_) {
                return static_cast<std::uint32_t>(i+1);
            }
        }

        // target waypoint index
        return static_cast<std::uint32_t>(waypoints.size() - 1);
    }

    double PathProgressTrackerNode::calculateDistance2D(
        double x1, double y1,
        double x2, double y2) const
    {
        return std::hypot(x2-x1, y2-y1);
    }

    double PathProgressTrackerNode::normalizeAngle(double angle_rad) const
    {
        /*
        각도를 -pi ~ +pi 범위로 정규화한다.

        예:
            3.5 rad는 -2.78 rad 근처로 변환된다.
            이렇게 해야 실제 회전해야 할 최소 방향 오차를 얻을 수 있다.*/
        while (angle_rad > kPi) {
            angle_rad -= kTwoPi;
        }

        while (angle_rad < -kPi) {
            angle_rad += kTwoPi;
        }

        return angle_rad;
    }

    double PathProgressTrackerNode::clamp01(double value) const
    {
        if (value < 0.0) return 0.0;
        if (value > 1.0) return 1.0;

        return value;
    }
}

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  auto node = std::make_shared<spot_navigation::PathProgressTrackerNode>();
  rclcpp::spin(node);

  rclcpp::shutdown();
  return 0;    
}