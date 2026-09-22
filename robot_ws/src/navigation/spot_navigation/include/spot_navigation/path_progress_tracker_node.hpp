/*
- 경로: spot_navigation/include/spot_navigation/path_progress_tracker_node.hpp
- 역할:
    할당받은 global path와 현재 localization pose를 비교하여,
    로봇이 path 상에서 어디쯤 진행 중인지 계산하는 Path Progress Tracker 노드 선언부.
- 입력 토픽:
    - /planning/mock_global_path/spot_01
        Type: robot_interfaces/msg/GlobalPathWaypoints

    - /localization/mock_pose
        Type: robot_interfaces/msg/LocalizedRobotPose

- 출력 토픽:
    - /navigation/path_progress/spot_01
        Type: robot_interfaces/msg/PathProgress

- 주요 기능:
    - global path 수신 여부와 유효성을 판단한다.
    - localization pose 수신 여부와 유효성을 판단한다.
    - 현재 pose와 가장 가까운 global path waypoint를 nearest_index로 계산한다.
    - nearest_index에서 path 진행 방향으로 lookahead_distance_m 이상 앞에 있는 waypoint를 target_index로 계산한다.
    - target_heading_rad, target_yaw_rad, heading_error_rad, yaw_error_rad를 계산한다.
    - progress_ratio, distance_to_goal_m, goal_reached를 계산한다.

- 좌표계:
    - global path와 localization pose는 모두 global_frame 기준이어야 한다.
    - 현재 프로젝트에서는 global_frame = "mission_map"으로 사용한다.
*/
#ifndef SPOT_NAVIGATION__PATH_PROGRESS_TRACKER_NODE_HPP_
#define SPOT_NAVIGATION__PATH_PROGRESS_TRACKER_NODE_HPP_

#include <cstdint>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "robot_interfaces/msg/global_path_waypoints.hpp"
#include "robot_interfaces/msg/localized_robot_pose.hpp"
#include "robot_interfaces/msg/path_progress.hpp"

namespace spot_navigation
{
    class PathProgressTrackerNode : public rclcpp::Node
    {
        public:
            PathProgressTrackerNode();

        private:
            using GlobalPathMsg    = robot_interfaces::msg::GlobalPathWaypoints;
            using LocalizedPoseMsg = robot_interfaces::msg::LocalizedRobotPose;
            using PathProgressMsg  = robot_interfaces::msg::PathProgress;

            // Parameter Load 함수
            void loadParameters();

            // Callback 함수
            void globalPathCallback(const GlobalPathMsg::SharedPtr msg);
            void poseCallback(const LocalizedPoseMsg::SharedPtr msg);

            // publish 함수
            void publishPathProgress();

            // global path와 localization path의 유효 값 검증 함수
            bool isPathValid() const;
            bool isPoseValid(const rclcpp::Time &now) const;

            // 현재 pose에서 가장 가까운 global path waypoint index 검색 함수
            std::uint32_t findNearestIndex(const LocalizedPoseMsg &pose) const;

            // nearest index 기준으로 path 진행 방향에 있는 다음 target index 검색 함수
            std::uint32_t findTargetIndex(std::uint32_t nearest_index) const;

            // 두 점 사이 유클리드 거리 계산 함수
            double calculateDistance2D(
                double x1, double y1,
                double x2, double y2) const;
            
            // 각도 정규화 함수 : -pi ~ +pi 범위
            double normalizeAngle(double angle_rad) const;
            
            // Clamp 함수 : 입력 값(value)를 0.0 ~ 1.0으로 클램프
            double clamp01(double value) const;

            // Subscriber
            rclcpp::Subscription<GlobalPathMsg>::SharedPtr global_path_sub_;
            rclcpp::Subscription<LocalizedPoseMsg>::SharedPtr pose_sub_;

            // Publisher
            rclcpp::Publisher<PathProgressMsg>::SharedPtr path_progress_pub_;

            // Timer: publish_rate_hz 주기로 publishPathProgress() 호출하는 타이머
            rclcpp::TimerBase::SharedPtr publish_timer_;

            // Parameter
            std::string robot_id_;
            std::string global_path_topic_;         // Subscrption Topic 1: global path
            std::string localization_pose_topic_;   // Subscrption Topic 2: Localization pose
            std::string path_progress_topic_;       // Publisher Topic: /navigation/path_progress/spot_{id} 
            std::string global_frame_;              // mission_map
            
            double lookahead_distance_m_;            // 몇 m 앞을 target으로 볼지 결정하는 거리
            double goal_tolerance_m_;               // goal 도착 판정 거리
            double pose_timeout_sec_;               // pose timeout 기준 시간
            double publish_rate_hz_;                // publish 주기
            
            // nearest_index가 이전 값보다 뒤로 감소하는 것을 허용할지 여부
            // - false : 주행 중 nearest_index가 뒤로 튀는 것을 막는다 (일반적인 path 추종 상태)
            // - true  : Spot 후진 or path를 되돌아가는 상황도 허용
            bool allow_backward_index_jump_;
            
            // [Debug] global path, pose 메시지 수신 여부 
            bool path_received_{false};
            bool pose_received_{false};

            // 가장 최근에 수신한 global path, localization pose 값 
            GlobalPathMsg::SharedPtr latest_path_{nullptr};          // 고도화: 임무 재할당에 활용됨
            LocalizedPoseMsg::SharedPtr latest_pose_{nullptr};
            
            // 마지막 pose를 수신한 ROS 시간: pose timeout 판단에 사용
            rclcpp::Time last_pose_receive_time_;

            // Last_pose_receiver_time_이 유효하게 초기화되었는지 여부
            // - pose를 받지 않은 상태에서 timeout 계산을 하지 않기 위함
            bool last_pose_receive_time_initialized_{false};

            // 이전 cycle에서의 계산된 nearest_index
            // - allow_backward_index_jump=false --> 다음 index 탐색 제한
            std::uint32_t last_nearest_index_{0};
            
            // last_nearest_index_가 유효한 값을 가지고 있는지 여부
            bool has_last_nearest_index_{false};
    };
}

#endif