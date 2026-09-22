/*
- 경로 : spot_localization/include/spot_localization/gait_odom_estimator_node.hpp
- 역할 : Declare the gait-based odometry estimator node.
- input     : /localization/stm_motion (robot_interfaces/msg/StmMotion)
- output    : /localization/odometry (nav_msgs/msg/Odometry)
- output TF : odom -> base_link
- 기능 :
    - STM gait/IMU motion source data 구독
    - gait phase 진행량 바탕으로 body-frame 기반 delta 이동량 변환
    - IMU yaw 값을 현재 heading 방향 기준으로 사용
    - odom 좌표계 기준 로봇 pose 누적 계산
    - Publish odometry topic and odom -> base_link TF
*/
#ifndef SPOT_LOCALIZATION__GAIT_ODOM_ESTIMATOR_NODE_HPP_
#define SPOT_LOCALIZATION__GAIT_ODOM_ESTIMATOR_NODE_HPP_

#include <cstdint>
#include <memory>
#include <string>

#include "geometry_msgs/msg/transform_stamped.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "rclcpp/rclcpp.hpp"
#include "robot_interfaces/msg/stm_motion.hpp"
#include "tf2_ros/transform_broadcaster.h"

namespace spot_localization 
{
    class GaitOdomEstimatorNode : public rclcpp::Node
    {
        public:
            GaitOdomEstimatorNode();
        
        private:
            using StmMotion = robot_interfaces::msg::StmMotion;

            // Callback 함수
            void stmMotionCallback(const StmMotion::SharedPtr msg);

            // [1] 입력 데이터 검증 함수
            // - gait_phase 범위 확인
            // - imu_yaw_rad, gyro_z_rad_s NaN/Inf 여부 확인
            // - motion_state enum 범위 확인
            bool isValidMessage(const StmMotion &msg) const;

            // [2] Body Frame 이동량 계산 함수
            // - motion_state와 delta_phase를 기반으로 base_link 기준 이동량 계산
            // - +x : 로봇 전방, +y : 로봇 좌측
            void computeBodyDelta(
                uint8_t motion_state,
                double delta_phase,
                double &delta_body_x_m,
                double &delta_body_y_m) const;
            
            // [3] IMU yaw 기반 odom yaw 계산 함수
            double computeOdomYaw(const StmMotion &msg) const;
            //double computeOdomYaw(const StmMotion &msg, uint8_t mapped_motion_state) const; // 추가0518

            // [4] Odometry 누적 함수
            // - base_link 기준 delta 이동량을 odom 좌표계 기준 delta 이동량으로 변환
            // - odom_x_m_, odom_y_m_, odom_yaw_rad_ 갱신
            void integrateOdom(
                double delta_body_x_m,
                double delta_body_y_m,
                double new_odom_yaw_rad);
            
            // [5] Odometry 메시지 publish 함수
            void publishOdometry(
                const rclcpp::Time &stamp,
                const StmMotion &msg,
                double dt_s,
                double delta_body_x_m,
                double delta_body_y_m);
            
            // [6] TF publish 함수
            // - publish TF: odom -> base_link
            // - odom 좌표계 기준 로봇 위치를 조회 가능
            void publishTf(const rclcpp::Time &stamp);

            // [7] Angle Utility : angle 값  -pi ~ +pi 범위로 정규화 함수
            static double normalizeAngle(double angle);

            // [8] Numeric Utility : 입력값이 정상적인 유한수인지 확인
            static bool isFinite(double value);

            /*
            * STM에서 들어온 raw motion_state 값을 odometry 계산용 motion_state로 변환한다.
            *
            * 현재 STM 담당자 구현:
            *   - 0: 정지
            *   - 8: 주행
            *
            * 기존 StmMotion.msg 정의:
            *   - 0: STOP
            *   - 1: WALK_FORWARD
            *   - 8: UNKNOWN
            *
            * 따라서 현재 MVP에서는 raw 8을 WALK_FORWARD로 매핑한다.
            * 추후 STM이 WALK_FORWARD, TURN_LEFT, STRAFE_LEFT 등을 구분해서 보내면
            * 이 mapping 함수를 제거하거나 direct mapping으로 변경하면 된다.
            */
            //uint8_t mapRawMotionState(uint8_t raw_motion_state) const;
            
            // ==============
            // ROS Interface
            // ==============
            rclcpp::Subscription<StmMotion>::SharedPtr stm_motion_sub_;         // STM motion source subscriber
            rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr odom_pub_;      // Odometry publisher
            std::unique_ptr<tf2_ros::TransformBroadcaster> tf_broadcaster_;     // odom -> base_link TF broadcaster
            
            // ========================
            // Topic & Frame Parameter
            // ========================
            std::string stm_motion_topic_;
            std::string odom_topic_;
            std::string odom_frame_;
            std::string base_frame_;

            // ===================================
            // Gait Calibration Parameter
            // - gait cycle 1회당 이동거리
            // - 실제 로봇 실측 기반으로 YAML에서 튜닝
            // ===================================
            double forward_step_length_m_;
            double backward_step_length_m_;
            double strafe_left_step_length_m_;
            double strafe_right_step_length_m_;

            // ==========================
            // Yaw Calibration Parameter
            // ==========================
            // IMU yaw와 실제 base_link yaw 사이의 고정 offset 보정값
            double yaw_offset_rad_;

            // STM IMU yaw 부호가 ROS 기준과 반대일 경우 -1.0으로 설정
            double imu_yaw_sign_;

            // Safety / Filtering Parameter
            double max_delta_phase_;            // 한 callback에서 허용할 최대 gait phase 변화량
            bool publish_tf_;                   // odom -> base_link TF publish 여부
            bool freeze_yaw_when_stopped_;      // STOP 상태에서 gyro_z가 거의 0일 때, IMU yaw drift를 반영하지 않을지에 대한 여부
            double gyro_stop_threshold_rad_s_;  // STOP 상태에서 yaw drift 판단을 위한 gyro_z threshold [rad/s]
            
            // ========================
            // Internal Odometry State
            // ========================
            bool initialized_ = false;  // 첫 메시지 수신 후 초기화 완료 여부

            // odom 좌표계 기준 base_link 현재 위치 [m]
            double odom_x_m_ = 0.0;
            double odom_y_m_ = 0.0;

            // odom 좌표계 기준 base_link 현재 yaw [rad]
            double odom_yaw_rad_ = 0.0;
            
            // ===================
            // Previous STM State
            // ===================

            // 이전 callback에서의 전체 gait phase
            // total_phase = gait_cycle_count + gait_phase
            double prev_total_phase_ = 0.0;

            // 이전 STM seq 값
            uint32_t prev_seq_ = 0;

            // 이전 STM timestamp_ms 값
            uint32_t prev_timestamp_ms_ = 0;

            // 이전 ROS timestamp : twist 계산을 위한 dt 산출에 사용
            rclcpp::Time prev_stamp_;
            
            // ==========================
            // Initial Heading Reference
            // ==========================
            // 첫 메시지에서 받은 IMU yaw 값 : 시작 시점 heading을 odom yaw = 0으로 맞추기 위한 기준값
            double imu_yaw_start_rad_ = 0.0;
    };
}

#endif