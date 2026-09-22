/*
- 경로: spot_navigation/include/spot_navigation/local_path_planner_node.hpp
- 역할: local path를 생성하는 Local Path Planner 노드 선언부.
- Input:
    - /navigation/path_progress/spot_01 (Type: robot_interfaces/msg/PathProgress)
    - /localization/mock_pose (Type: robot_interfaces/msg/LocalizedRobotPose)
    - /perception/lidar/obstacle_model (Type: robot_interfaces/msg/ObstacleModel)
    - /perception/lidar/free_space_model (Type: robot_interfaces/msg/FreeSpaceModel)

- Ouput:
    - /navigation/local_path/spot_01 (Type: nav_msgs/msg/Path)
    - /navigation/local_planner_status/spot_01 (Type: robot_interfaces/msg/LocalPlannerStatus)

- 주요 기능:
    - PathProgress와 localization pose의 수신 여부 및 timeout을 검사한다.
    - LiDAR ObstacleModel과 FreeSpaceModel의 수신 여부 및 timeout을 검사한다.
    - 1차 MVP 상태인 GLOBAL_SUB_GOAL, GLOBAL_GOAL_REACHED, INVALID_INPUT을 처리한다.
    - 2차 MVP 상태인 AVOIDANCE, REJOIN, BLOCKED를 처리한다.
    - GLOBAL_SUB_GOAL 상태에서는 PathProgress target 기반 local path를 생성한다.
    - AVOIDANCE 상태에서는 FreeSpaceModel의 best heading 기반 회피 local path를 생성한다.
    - REJOIN 상태에서는 PathProgress target을 이용해 global path 복귀 local path를 생성한다.
    - BLOCKED 상태에서는 현재 pose 유지용 hold local path를 생성한다.

- 좌표계:
    - PathProgress, localization pose, local path는 global_frame 기준이다.
    - 현재 프로젝트에서는 global_frame = "mission_map"으로 사용한다.
    - ObstacleModel과 FreeSpaceModel의 heading/sector 정보는 base_link 기준이다.
    - 따라서 FreeSpaceModel.best_heading_angle_rad를 local path에 사용할 때는
      current_yaw를 더해 mission_map 기준 heading으로 변환해야 한다.
*/

#ifndef SPOT_NAVIGATION__LOCAL_PATH_PLANNER_NODE_HPP_
#define SPOT_NAVIGATION__LOCAL_PATH_PLANNER_NODE_HPP_

#include <cstdint>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"

#include "geometry_msgs/msg/pose_stamped.hpp"
#include "geometry_msgs/msg/quaternion.hpp"

#include "nav_msgs/msg/path.hpp"

#include "robot_interfaces/msg/local_planner_status.hpp"
#include "robot_interfaces/msg/localized_robot_pose.hpp"
#include "robot_interfaces/msg/path_progress.hpp"
#include "robot_interfaces/msg/free_space_model.hpp"
#include "robot_interfaces/msg/obstacle_model.hpp"

namespace spot_navigation
{
    class LocalPathPlannerNode : public rclcpp::Node
    {
        public:
            LocalPathPlannerNode();

        private:
            using PathProgressMsg       = robot_interfaces::msg::PathProgress;
            using LocalizedPoseMsg      = robot_interfaces::msg::LocalizedRobotPose;
            using LocalPlannerStatusMsg = robot_interfaces::msg::LocalPlannerStatus;
            using ObstacleModelMsg      = robot_interfaces::msg::ObstacleModel;
            using FreeSpaceModelMsg     = robot_interfaces::msg::FreeSpaceModel;

            // [1] Parameter Declare & Read
            void loadParameters();

            // ===================================
            // [2] 수신 Callback 함수
            // - PathProgress/Localization Pose
            // - Obstacle Model/Free Space Model
            // ===================================
            void pathProgressCallback(const PathProgressMsg::SharedPtr msg);
            void poseCallback(const LocalizedPoseMsg::SharedPtr msg);
            void obstacleModelCallback(const ObstacleModelMsg::SharedPtr msg);
            void freeSpaceModelCallback(const FreeSpaceModelMsg::SharedPtr msg);

            // [3] Local path planning 주기 실행 함수
            void publishLocalPlanning();

            // ===================================
            // [4] Input Validity Check 함수
            // - PathProgress/Localization Pose
            // - Obstacle Model/Free Space Model
            // ===================================
            bool isPathProgressValid(const rclcpp::Time &now) const;
            bool isPoseValid(const rclcpp::Time &now) const;
            bool isObstacleModelValid(const rclcpp::Time &now) const;
            bool isFreeSpaceModelValid(const rclcpp::Time &now) const;

            // ==========================================
            // [4] Perception-based Decision Helper 함수
            // - isFrontDanger(): 전방 위험 여부 판단 함수
            // - isFrontClear(): 전방 위험 해소 여부 판단 함수
            // - isFreeSpaceAcceptableForMotion(): selected/best gap을 회피 주행에 사용 가능한지 판단 함수
            // - isRejoinDone(): REJOIN 완료 여부 판단 함수
            //   - 초기 구현: distance_ok || heading_ok
            //   - 실제 주행 안정화 후 보수적 옵션: distance_ok && heading_ok 
            // ==========================================
            bool isFrontDanger(const ObstacleModelMsg &obstacle_model) const;
            bool isFrontClear(const ObstacleModelMsg &obstacle_model) const;

            // [추가] 측면 장애물 위험/해소 판단 함수
            // - Left/Right sector의 대표 장애물 기반으로
            //   AVOIDANCE 유지 및 REJOIN 허용 여부 판단
            bool isLeftDanger(const ObstacleModelMsg &obstacle_model) const;
            bool isRightDanger(const ObstacleModelMsg &obstacle_model) const;
            bool isSideDanger(const ObstacleModelMsg &obstacle_model) const;

            bool isLeftClear(const ObstacleModelMsg &obstacle_model) const;
            bool isRightClear(const ObstacleModelMsg &obstacle_model) const;
            bool isSideClear(const ObstacleModelMsg &obstacle_model) const;

            bool isFreeSpaceAcceptableForMotion(const FreeSpaceModelMsg &free_space_model) const;
            bool isRejoinDone(const PathProgressMsg &progress) const;

            // ==========================================
            // [추가] AVOIDANCE Latch Helper 함수
            // - AVOIDANCE 진입 시점/위치를 저장한다.
            // - obstacle_clear가 true가 되더라도 최소 시간/거리 조건을 만족하기 전까지
            //   REJOIN으로 전환하지 않도록 한다.
            // - local path 자체를 고정하는 것이 아니라, AVOIDANCE 상태만 유지한다.
            // ==========================================
            void startAvoidanceLatch(
                const LocalizedPoseMsg &pose,
                const rclcpp::Time &now);
            
            void resetAvoidanceLatch();

            bool isAvoidanceLatchDone(
                const LocalizedPoseMsg &pose,
                const rclcpp::Time &now) const;

            // ==========================================
            // [5] 기본 상태 메시지 생성 함수
            // - header, robot_id 채우기
            // - 입력 수신 여부 채우기
            // - 기본 상태값을 안전한 stop 상태로 초기화
            // ==========================================
            LocalPlannerStatusMsg makeBaseStatus(
                const rclcpp::Time &stamp,
                bool path_progress_valid,
                bool pose_valid) const;

            // ==========================================
            // [6] Local Path Generation Helper 함수
            // ==========================================

            // [6-1] 빈 Local path 생성 함수
            // - pose도 유효하지 않아 hold path조차 만들 수 없는 경우
            // - 초기 입력 대기 상태
            nav_msgs::msg::Path makeEmptyPath(const rclcpp::Time &stamp) const;

            // [6-2] 현재 pose를 유지하는 hold path 생성 함수
            // - State: GLOBAL_GOAL_REACHED/BLOCKED
            // - pose는 유효하지만 planning 입력이 불완전한 INVALID_INPUT
            nav_msgs::msg::Path makeHoldLocalPath(
                const LocalizedPoseMsg &pose,
                const rclcpp::Time &stamp) const;

            // [6-3] 현재 pose에서 local target까지 이어지는 local path 생성 함수
            // - pose: 현재 로봇 pose
            // - target_x_m, target_y_m, target_yaw_rad: local target
            // - selected_heading_rad: local path 진행 방향
            // - 1차 MVP: PathProgress target까지 직선 보간 path 생성
            // - 2차 MVP:
            //  - AVOIDANCE 상태에서는 FreeSpaceModel best heading으로 만든 임시 target까지 path 생성
            //  - REJOIN 상태에서는 PathProgress target까지 복귀 path 생성
            //  - 추후 곡선 path로 확장
            nav_msgs::msg::Path makeLocalPathToTarget(
                const LocalizedPoseMsg &pose,
                double target_x_m, double target_y_m,
                double target_yaw_rad, double selected_heading_rad,
                const rclcpp::Time &stamp, double &path_length_m) const;
            
            // [6-4] nav_msgs/Path에 들어갈 PoseStamped 하나를 생성하는 함수
            // - local path의 각 pose를 만들 때 활용
            // - x/y/z/yaw 값을 받아 global_frame_ 기준 PoseStamped로 변환
            geometry_msgs::msg::PoseStamped makePoseStamped(
                double x_m, double y_m, double z_m,
                double yaw_rad, const rclcpp::Time &stamp) const;
  
            // =====================================
            // [7] State-specific Publish Functions
            // =====================================
  
            // [7-1] INVALID_INPUT 상태 처리 함수
            // - pose 유효 O: hold path 생성
            // - pose 유효 X: empty path 생성
            void publishInvalidInput(
                const rclcpp::Time &stamp,
                bool path_progress_valid, bool pose_valid,
                const std::string &reason);
            
            // [7-2] GLOBAL_GOAL_REACHED 상태 처리 함수
            // - 현재 pose 기반 hold path publish
            // - stop_required=true 반영
            void publishGlobalGoalReached(
                const rclcpp::Time &stamp,
                const LocalizedPoseMsg &pose,
                const PathProgressMsg &progress);
            
            // [7-3] GLOBAL_SUB_GOAL 상태 처리 함수
            // - PathProgress target를 Local target으로 활용
            // - 직선 Local path 생성
            void publishGlobalSubGoalPath(
                const rclcpp::Time &stamp,
                const LocalizedPoseMsg &pose,
                const PathProgressMsg &progress);

            // [7-4] AVOIDANCE 상태 처리 함수
            // - 회피 Local path(current pose → avoidance target) 생성
            // - used_free_space_heading = true
            // - stop_required = false
            void publishAvoidancePath(
                const rclcpp::Time &stamp,
                const LocalizedPoseMsg &pose,
                const PathProgressMsg &progress,
                const ObstacleModelMsg &obstacle_model,
                const FreeSpaceModelMsg &free_space_model);

            // [7-5] REJOIN 상태 처리 함수
            // - 회피 후 global path 복귀 Local path(current pose → PathProgress target) 생성
            // - used_rejoin_target = true
            // - stop_required = false
            void publishRejoinPath(
                const rclcpp::Time & stamp,
                const LocalizedPoseMsg & pose,
                const PathProgressMsg & progress,
                const ObstacleModelMsg & obstacle_model,
                const FreeSpaceModelMsg & free_space_model);

            // [7-6] BLOCKED 상태 처리 함수
            // - 주행이 불가능한 상황에서 hold path(current pose → current pose hold path) 생성
            // - blocked = true
            // - stop_required = true
            // - 추후 새로운 local path를 찾기 위한 로직으로 고도화 예정
            void publishBlocked(
                const rclcpp::Time &stamp,
                const LocalizedPoseMsg &pose,
                const PathProgressMsg &progress,
                const ObstacleModelMsg &obstacle_model,
                const FreeSpaceModelMsg &free_space_model,
                const std::string &reason);

            // [7-7] local path와 LocalPlannerStatus publish 함수
            void publishPathAndStatus(
                const nav_msgs::msg::Path &path_msg,
                const LocalPlannerStatusMsg &status_msg);

            // ==================
            // [8] Math Utility
            // ==================

            // [8-1] 두 점 사이 2D 유클리드 거리 계산 함수
            double calculateDistance2D(
                double x1, double y1,
                double x2, double y2
            ) const;

            // [8-2] 각도 -pi ~ +pi 범위로 정규화 함수
            double normalizeAngle(double angle_rad) const;

            // [8-3] yaw(rad) -> quaternion 변환 함수
            geometry_msgs::msg::Quaternion yawToQuaternion(double yaw_rad) const;

            // [8-4] NaN/inf 입력값 검사 함수
            bool isFinite(double value) const;

            // =======================
            // Subscriber & Publisher
            // =======================
            rclcpp::Subscription<PathProgressMsg>::SharedPtr path_progress_sub_;
            rclcpp::Subscription<LocalizedPoseMsg>::SharedPtr pose_sub_;
            rclcpp::Subscription<ObstacleModelMsg>::SharedPtr obstacle_model_sub_;
            rclcpp::Subscription<FreeSpaceModelMsg>::SharedPtr free_space_model_sub_;

            rclcpp::Publisher<nav_msgs::msg::Path>::SharedPtr local_path_pub_;
            rclcpp::Publisher<LocalPlannerStatusMsg>::SharedPtr local_planner_status_pub_;

            // =======
            // Timer
            // =======
            rclcpp::TimerBase::SharedPtr publish_timer_;

            // ===========
            // Parameter
            // ===========
            std::string robot_id_;
            std::string global_frame_;

            std::string path_progress_topic_;
            std::string localization_pose_topic_;
            std::string obstacle_model_topic_;
            std::string free_space_model_topic_;

            std::string local_path_topic_;
            std::string local_planner_status_topic_;

            double publish_rate_hz_;

            double path_progress_timeout_sec_;
            double pose_timeout_sec_;
            double obstacle_model_timeout_sec_;
            double free_space_model_timeout_sec_;

            double front_block_distance_m_;         // 전방 위험 진입 거리
            double front_clear_distance_m_;         // 전방 위험 해제 거리

            // [추가] 측면 장애물 위험/해제 거리
            // - AVOIDANCE 중 장애물이 front에서 Left/Right로 빠졌을 때,
            //   너무 빨리 REJOIN하지 않도록 측면 clearance를 함께 판단
            double side_block_distance_m_;          // 측면 위험 진입 거리
            double side_clear_distance_m_;          // 측면 위험 해제 거리

            double avoidance_min_clearance_m_;      // 실제 회피 주행에 사용할 최소 clearance
            double avoidance_horizon_m_;            // FreeSpaceModel이 선택한 회피 heading 방향으로 70cm 앞에 target 생성 거리

            // [추가] AVOIDANCE latch 파라미터
            // - AVOIDANCE에 진입한 뒤 최소 시간 또는 최소 이동거리 조건을 만족하기 전까지
            //   REJOIN 전환을 막기 위한 값이다.
            double avoidance_min_hold_sec_;         // AVOIDANCE 최소 유지 시간 [sec]
            double avoidance_min_travel_m_;         // AVOIDANCE 최소 이동 거리 [m]

            double rejoin_tolerance_m_;             // 회피 후 global path로 복귀(REJOIN) 완료 기준 거리
            double rejoin_heading_tolerance_rad_;   // 회피 후 global path로 복귀(REJOIN) 완료 기준 각도

            double local_path_spacing_m_;
            double max_local_path_length_m_;
            double local_target_tolerance_m_;

            bool use_path_progress_target_yaw_;
            //bool use_perception_;
            //bool require_perception_input_;
            //bool rejoin_require_both_distance_and_heading_;

            // =================================
            // Latest input state: 최신 입력 캐시
            // =================================
            bool path_progress_received_{false};
            bool pose_received_{false};
            bool obstacle_model_received_{false};
            bool free_space_model_received_{false};

            PathProgressMsg::SharedPtr latest_path_progress_{nullptr};
            LocalizedPoseMsg::SharedPtr latest_pose_{nullptr};
            ObstacleModelMsg::SharedPtr latest_obstacle_model_{nullptr};
            FreeSpaceModelMsg::SharedPtr latest_free_space_model_{nullptr};

            rclcpp::Time last_path_progress_receive_time_;
            rclcpp::Time last_pose_receive_time_;
            rclcpp::Time last_obstacle_model_receive_time_;
            rclcpp::Time last_free_space_model_receive_time_;

            bool last_path_progress_receive_time_initialized_{false};
            bool last_pose_receive_time_initialized_{false};
            bool last_obstacle_model_receive_time_initialized_{false};
            bool last_free_space_model_receive_time_initialized_{false};

            // ==============
            // Planner state
            // ==============
            std::uint8_t planner_state_{LocalPlannerStatusMsg::IDLE};

            // [추가] AVOIDANCE latch state
            // - AVOIDANCE episode가 언제, 어디서 시작되었는지 저장한다.
            // - path를 고정하는 용도가 아니라 REJOIN 허용 조건을 판단하기 위한 상태값이다.
            rclcpp::Time avoidance_start_time_;         // latch 시작 시간
            bool avoidance_latch_active_{false};        // latch 활성화 flag
            double avoidance_start_x_m_{0.0};           // latch 시작 위치 x
            double avoidance_start_y_m_{0.0};           // latch 시작 위치 y
    };
}

#endif