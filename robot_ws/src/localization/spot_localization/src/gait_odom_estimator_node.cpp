/*
- 경로 : ~/robot_ws/src/localization/spot_localization/src/gait_odom_estimator_node.cpp
- 역할 : STM gait/IMU 기반 Odometry 추정 노드 구현부
- 입력 : /localization/stm_motion (robot_interfaces/msg/StmMotion)
- 출력 : /localization/odometry (nav_msgs/msg/Odometry)
- TF   : odom -> base_link
- 기능 :
    - STM motion source 데이터를 구독
    - gait_cycle_count + gait_phase 기반 전체 gait 진행량 계산
    - 이전 callback 대비 delta_phase 계산
    - motion_state에 따라 body frame 기준 delta 이동량 산출
    - IMU yaw 변화량을 odom yaw로 변환
    - body frame 이동량을 odom frame 이동량으로 변환하여 x, y 누적
    - nav_msgs/Odometry 메시지 publish
    - odom -> base_link TF publish
*/

#include "spot_localization/gait_odom_estimator_node.hpp"

#include <cmath>
#include <functional>

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTwoPi = 2.0 * kPi;

    // yaw 값만 사용하는 2D odometry을 quaternion 생성 함수
    // roll = 0, pitch = 0, yaw = 입력 yaw
    geometry_msgs::msg::Quaternion yawToQuaternion(double yaw_rad)
    {
        geometry_msgs::msg::Quaternion q;

        q.x = 0.0;
        q.y = 0.0;
        q.z = std::sin(yaw_rad * 0.5);
        q.w = std::cos(yaw_rad * 0.5);

        return q;
    }
}

namespace spot_localization
{
    GaitOdomEstimatorNode::GaitOdomEstimatorNode()
    : Node("gait_odom_estimator_node")
    {
        // Topic & Frame Parameter
        stm_motion_topic_ = this->declare_parameter<std::string>(
            "stm_motion_topic",
            "/localization/stm_motion"
        );

        odom_topic_ = this->declare_parameter<std::string>(
            "odom_topic",
            "/localization/odometry"
        );

        odom_frame_ = this->declare_parameter<std::string>(
            "odom_frame",
            "odom"
        );

        base_frame_ = this->declare_parameter<std::string>(
            "base_frame",
            "base_link"
        );

        // ===========================
        // Gait Calibration Parameter
        // - gait cycle 1회당 이동 거리
        // ===========================
        forward_step_length_m_ = this->declare_parameter<double>(
            "forward_step_length_m",
            0.030
        );

        backward_step_length_m_ = this->declare_parameter<double>(
            "backward_step_length_m",
            0.025
        );

        strafe_left_step_length_m_ = this->declare_parameter<double>(
            "strafe_left_step_length_m",
            0.018
        );

        strafe_right_step_length_m_ = this->declare_parameter<double>(
            "strafe_right_step_length_m",
            0.018
        );

        // =========================
        // Yaw Calibration Parameter
        // =========================
        // IMU yaw 변화량을 odom yaw로 변환할 때 적용하는 잔여 보정값
        // 현재 MVP에서는 0.0으로 두고, 실측 결과 yaw가 일정하게 어긋날 때만 튜닝한다.
        yaw_offset_rad_ = this->declare_parameter<double>(
            "yaw_offset_rad",
            0.0
        );

        // STM IMU yaw 부호가 ROS 기준과 같으면 1.0
        // ROS 기준과 반대이면 -1.0
        // ROS 기준: +yaw = 좌회전, 반시계 방향
        imu_yaw_sign_ = this->declare_parameter<double>(
            "imu_yaw_sign",
            1.0
        );

        // =============================
        // Safety / Filtering Parameter
        // ==============================

        // 한 callback에서 허용할 최대 gait phase 변화량
        // 예: gait_cycle_hz=1.0, publish_rate=50Hz이면 정상 delta_phase는 약 0.02
        // 초기 추천값은 0.10 ~ 0.20 범위이다.
        max_delta_phase_ = this->declare_parameter<double>(
            "max_delta_phase",
            0.15
        );

        // 현재 MVP에서는 이 노드가 odom -> base_link TF를 직접 publish한다.
        // 나중에 EKF 등이 동일 TF를 publish하면 false로 바꿔 TF 중복 publish를 막는다.
        publish_tf_ = this->declare_parameter<bool>(
            "publish_tf",
            true
        );

        // STOP 상태에서 gyro_z가 거의 0이면 IMU yaw drift를 odom yaw에 반영하지 않는다.
        freeze_yaw_when_stopped_ = this->declare_parameter<bool>(
            "freeze_yaw_when_stopped",
            true
        );

        // STOP 상태에서 실제 회전이 아니라고 판단할 gyro_z threshold
        gyro_stop_threshold_rad_s_ = this->declare_parameter<double>(
            "gyro_stop_threshold_rad_s",
            0.03
        );

        // ==========================
        // ROS Interface
        // ==========================
        auto stm_motion_qos = rclcpp::QoS(rclcpp::KeepLast(10)).best_effort();

        stm_motion_sub_ = this->create_subscription<StmMotion>(
            stm_motion_topic_,
            stm_motion_qos,
            std::bind(
                &GaitOdomEstimatorNode::stmMotionCallback,
                this,
                std::placeholders::_1
            )
        );

        odom_pub_ = this->create_publisher<nav_msgs::msg::Odometry>(
            odom_topic_,
            rclcpp::QoS(10)
        );

        tf_broadcaster_ = std::make_unique<tf2_ros::TransformBroadcaster>(*this);

        RCLCPP_INFO(this->get_logger(), "gait_odom_estimator_node started.");
        RCLCPP_INFO(this->get_logger(), "Subscribe : %s", stm_motion_topic_.c_str());
        RCLCPP_INFO(this->get_logger(), "Publish   : %s", odom_topic_.c_str());
        RCLCPP_INFO(this->get_logger(), "TF        : %s -> %s", odom_frame_.c_str(), base_frame_.c_str());
    }

    void GaitOdomEstimatorNode::stmMotionCallback(const StmMotion::SharedPtr msg)
    {
        // [1] 입력 메시지 검증
        if (!isValidMessage(*msg)) return;

        // [2] timestamp 결정
        // - bridge node가 header.stamp를 정상적으로 넣어주면 해당 시간 사용
        // - header.stamp가 비어 있으면 현재 ROS 시간 사용
        const bool has_valid_stamp =
            !(msg->header.stamp.sec == 0 && msg->header.stamp.nanosec == 0);
        
        const rclcpp::Time stamp =
            has_valid_stamp ? rclcpp::Time(msg->header.stamp) : this->now();
        
        // ========================
        // [3] 전체 gait phase 계산
        // - gait_phase는 0.0 ~ 1.0 사이에서 반복되므로 단독으로 쓰면 wrap 문제 발생
        // - 따라서 완료된 cycle 수와 현재 phase를 합쳐 전체 진행량으로 계산
        // 예:
        //   gait_cycle_count = 10
        //   gait_phase       = 0.25
        //   total_phase      = 10.25
        // ===========================
        const double total_phase =
            static_cast<double>(msg->gait_cycle_count) +
            static_cast<double>(msg->gait_phase);
        
        // [4] 첫 메시지 수신 시 초기화
        if (!initialized_) {
            initialized_ = true;
            
            imu_yaw_start_rad_ = static_cast<double>(msg->imu_yaw_rad);

            prev_total_phase_  = total_phase;
            prev_seq_          = msg->seq;
            prev_timestamp_ms_ = msg->timestamp_ms;
            prev_stamp_        = stamp;

            odom_x_m_     = 0.0;
            odom_y_m_     = 0.0;
            odom_yaw_rad_ = 0.0;

            publishOdometry(stamp, *msg, 0.0, 0.0, 0.0);

            if (publish_tf_) publishTf(stamp);

            RCLCPP_INFO(
                this->get_logger(),
                "Gait odom initialized. imu_yaw_start_rad = %.4f rad",
                imu_yaw_start_rad_
            );

            return;
        }
        
        // ==============================
        // [5] seq 연속성 확인
        // - seq가 1씩 증가하지 않으면 SPI/bridge 단계에서 패킷 누락 발생
        // - 초기 MVP에서는 경고만 출력하고 계산은 계속 진행 추후 개선
        // ==============================
        if (msg->seq != prev_seq_ + 1) {
            RCLCPP_WARN_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                2000,
                "STM seq jump detected. prev = %u, current = %u",
                prev_seq_,
                msg->seq
            );
        }

        // ============================
        // [6] STM timestamp 역행 확인
        // timestamp_ms가 이전 값보다 작아지면 STM reset 또는 패킷 순서 꼬임 가능성
        // ============================
        if (msg->timestamp_ms < prev_timestamp_ms_) {
            RCLCPP_WARN(
                this->get_logger(),
                "STM timestamp went backward. prev = %u ms, current = %u ms",
                prev_timestamp_ms_,
                msg->timestamp_ms
            );
        }
        
        // ==================================
        // [7] dt 계산
        // - twist 계산 : ROS timestamp 기준 dt 계산
        // - odom 위치 누적 : delta_phase 기반이지만, 속도 계산은 dt 활용
        // ==================================
        double dt_s = 0.0;

        if (msg->timestamp_ms >= prev_timestamp_ms_) {
            dt_s = static_cast<double>(msg->timestamp_ms - prev_timestamp_ms_) / 1000.0;
        } else {
            RCLCPP_WARN(
                this->get_logger(),
                "STM timestamp went backward. prev = %u ms, current = %u ms",
                prev_timestamp_ms_,
                msg->timestamp_ms
            );

            dt_s = 0.0;
        }

        if (dt_s <= 0.0 || dt_s > 1.0) {
            RCLCPP_WARN_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                2000,
                "Abnormal STM dt detected: %.4f s. Twist will be set to zero.",
                dt_s
            );

            dt_s = 0.0;
        }
        
        // =================================
        // [8] delta_phase 계산
        // - 이전 callback 이후 gait cycle이 얼마나 진행되었는지 계산
        // =================================
        const double delta_phase = total_phase - prev_total_phase_;

        if (delta_phase < -1e-6) {
            RCLCPP_WARN(
                this->get_logger(),
                "Negative delta_phase detected. prev_total = %.4f, current_total = %.4f. Skip integration.",
                prev_total_phase_,
                total_phase
            );

            prev_total_phase_ = total_phase;
            prev_seq_ = msg->seq;
            prev_timestamp_ms_ = msg->timestamp_ms;
            prev_stamp_ = stamp;

            return;
        }

        if (delta_phase > max_delta_phase_) {
            RCLCPP_WARN(
                this->get_logger(),
                "Too large delta_phase detected: %.4f > %.4f. Skip integration.",
                delta_phase,
                max_delta_phase_
            );

            prev_total_phase_ = total_phase;
            prev_seq_ = msg->seq;
            prev_timestamp_ms_ = msg->timestamp_ms;
            prev_stamp_ = stamp;

            return;
        }
        
        // ===========================================
        // [9] motion state 기반 body frame 이동량 계산
        // - delta_phase를 전진/후진/좌우 이동 중 어떤 방향으로 해석할지 결정
        // ===========================================
        double delta_body_x_m = 0.0;
        double delta_body_y_m = 0.0;

        computeBodyDelta(
            msg->motion_state,
            delta_phase,
            delta_body_x_m,
            delta_body_y_m
        );

        // ===============================
        // [10] IMU yaw 기반 odom yaw 계산
        // - 시작 시점의 imu_yaw_start_rad_를 기준으로 현재 yaw 변화량을 계산
        // ===============================

        const double odom_yaw_rad = computeOdomYaw(*msg);

        // ==============================
        // [11] odom frame 기준 pose 누적
        // - body frame 기준 이동량을 현재 yaw 기준으로 odom frame에 회전 변환하여 누적
        // ==============================
        integrateOdom(
            delta_body_x_m,
            delta_body_y_m,
            odom_yaw_rad
        );

        // ==========================
        // [12] Odometry / TF publish
        // ==========================
        publishOdometry(
            stamp,
            *msg,
            dt_s,
            delta_body_x_m,
            delta_body_y_m
        );

        if (publish_tf_) publishTf(stamp);

        // ==================
        // [13] 이전 상태 갱신
        // ==================
        prev_total_phase_ = total_phase;
        prev_seq_ = msg->seq;
        prev_timestamp_ms_ = msg->timestamp_ms;
        prev_stamp_ = stamp;        
    }

    bool GaitOdomEstimatorNode::isValidMessage(const StmMotion &msg) const
    {
        // gait phase는 0.0 ~ 1.0 범위 이내여야 한다.
        if (msg.gait_phase < 0.0F || msg.gait_phase > 1.0F) {
            RCLCPP_WARN(
                this->get_logger(),
                "Invalid gait_phase: %.4f. Expected range is 0.0 ~ 1.0",
                msg.gait_phase
            );

            return false;
        }

        // IMU yaw 값이 NaN or Inf이면 odom yaw 계산이 망가짐
        if (!isFinite(static_cast<double>(msg.imu_yaw_rad))) {
            RCLCPP_WARN(this->get_logger(), "Invalid imu_yaw_rad: NaN or Inf");
            return false;
        }

        // gyro_z 값이 NaN 또는 Inf이면 twist 및 yaw drift 판단이 망가짐
        if (!isFinite(static_cast<double>(msg.gyro_z_rad_s))) {
            RCLCPP_WARN(this->get_logger(), "Invalid gyro_z_rad_s: NaN or Inf");
            return false;
        }

        // motion_state는 StmMotion.msg에 정의된 enum 범위 안에 있어야 한다.
        if (msg.motion_state > StmMotion::UNKNOWN) {
            RCLCPP_WARN(
                this->get_logger(),
                "Invalid motion_state: %u",
                msg.motion_state
            );

            return false;
        }
        return true;        
    }

    void GaitOdomEstimatorNode::computeBodyDelta(
        uint8_t motion_state,
        double delta_phase,
        double &delta_body_x_m,
        double &delta_body_y_m) const
    {
        delta_body_x_m = 0.0;
        delta_body_y_m = 0.0;

        switch (motion_state) {
            case StmMotion::WALK_FORWARD:   // 전진상태 : base_link 기준 +x 방향 이동
                delta_body_x_m = forward_step_length_m_ * delta_phase;
                delta_body_y_m = 0.0;
                break;
            
            case StmMotion::WALK_BACKWARD:  // 후진 상태 : base_link 기준 -x 방향 이동
                delta_body_x_m = -backward_step_length_m_ * delta_phase;
                delta_body_y_m = 0.0;
                break;
        
            case StmMotion::WALK_FORWARD_TURN_LEFT:
            case StmMotion::WALK_FORWARD_TURN_RIGHT:
                // 전진하면서 좌/우회전:
                // 병진 이동은 전진과 동일하게 base_link 기준 +x로 누적한다.
                // 좌/우로 휘어지는 효과는 computeOdomYaw()의 yaw 변화와
                // integrateOdom()의 mid_yaw 좌표 변환에서 반영된다.
                delta_body_x_m = forward_step_length_m_ * delta_phase;
                delta_body_y_m = 0.0;
                break;
    
            case StmMotion::TURN_LEFT:
            case StmMotion::TURN_RIGHT:
                // 초기 MVP에서는 회전 중 translational slip은 무시한다.
                // 즉, 제자리 회전 상태에서는 x/y 이동량은 0으로 두고 yaw만 IMU로 반영한다.
                //
                // 나중에 실제 로봇 테스트에서 회전 중 전후/좌우 밀림이 크면
                // turn_left_dx_per_cycle, turn_right_dx_per_cycle 같은 파라미터를 추가해 보정할 수 있다.
                delta_body_x_m = 0.0;
                delta_body_y_m = 0.0;
                break;

            case StmMotion::STRAFE_LEFT:
                // 좌측 횡이동 : ROS base_link 기준 +y 방향은 로봇의 왼쪽
                delta_body_x_m = 0.0;
                delta_body_y_m = strafe_left_step_length_m_ * delta_phase;
                break;

            case StmMotion::STRAFE_RIGHT:
                // 우측 횡이동 : ROS base_link 기준 -y 방향은 로봇의 오른쪽
                delta_body_y_m = -strafe_right_step_length_m_ * delta_phase;
                delta_body_x_m = 0.0;
                break;

            case StmMotion::STOP:
                // 정지 상태에서는 위치 이동량을 누적 X
                delta_body_x_m = 0.0;
                delta_body_y_m = 0.0;
                break;

            case StmMotion::TRANSITION:
            case StmMotion::UNKNOWN:
            default:
                // 동작 전환 중이거나 상태를 알 수 없으면 보수적으로 이동량을 누적하지 않는다.
                delta_body_x_m = 0.0;
                delta_body_y_m = 0.0;
                break;
        }
    }

    double GaitOdomEstimatorNode::computeOdomYaw(const StmMotion &msg) const
    {
        // STOP 상태에서 gyro_z가 거의 0이면 실제 회전이 아니라 IMU drift일 가능성이 높음
        // 이 경우 기존 odom_yaw_rad_를 유지
        if (
            freeze_yaw_when_stopped_ &&
            msg.motion_state == StmMotion::STOP &&
            std::abs(static_cast<double>(msg.gyro_z_rad_s)) < gyro_stop_threshold_rad_s_
        ) {
            return odom_yaw_rad_;
        }

        // 시작 시점의 IMU yaw를 기준으로 현재 yaw 변화량만 사용
        // 즉, odom frame에서는 로봇 시작 heading를 yaw = 0으로 본다.
        const double yaw_delta_rad =
            imu_yaw_sign_ *
            (static_cast<double>(msg.imu_yaw_rad) - imu_yaw_start_rad_);

        return normalizeAngle(yaw_delta_rad + yaw_offset_rad_);        
    }

    void GaitOdomEstimatorNode::integrateOdom(
        double delta_body_x_m,
        double delta_body_y_m,
        double new_odom_yaw_rad)
    {
        // 이전 callback까지 누적된 odom yaw.
        // 이 값은 이번 이동량을 적분하기 전의 base_link 방향이다.
        const double prev_yaw_rad = odom_yaw_rad_;

        // 이번 callback에서 IMU 기반으로 계산된 새로운 odom yaw.
        const double new_yaw_rad = normalizeAngle(new_odom_yaw_rad);

        // 한 callback 구간 동안 발생한 yaw 변화량.
        // normalizeAngle()을 적용해 -pi ~ +pi 범위에서 최단 회전량으로 계산한다.
        const double delta_yaw_rad = normalizeAngle(new_yaw_rad - prev_yaw_rad);

        // 이번 구간에서 로봇이 회전하면서 이동했다고 보고,
        // 이전 yaw와 현재 yaw의 중간 방향을 병진 이동량 변환에 사용한다.
        const double mid_yaw_rad = normalizeAngle(prev_yaw_rad + 0.5 * delta_yaw_rad);
        
        // body frame 기준 이동량을 odom frame 기준 이동량으로 변환
        // - body frame : +x = 로봇 전방, +y = 로봇 왼쪽
        // - odom frame : 로봇 시작 위치를 원점으로 하는 고정 좌표계
        const double cos_yaw = std::cos(mid_yaw_rad);
        const double sin_yaw = std::sin(mid_yaw_rad);

        const double delta_odom_x_m =
            delta_body_x_m * cos_yaw -
            delta_body_y_m * sin_yaw;
        
        const double delta_odom_y_m =
            delta_body_x_m * sin_yaw +
            delta_body_y_m * cos_yaw;

        odom_x_m_ += delta_odom_x_m;
        odom_y_m_ += delta_odom_y_m;

        // mid_yaw_rad는 x/y 변환에만 사용하는 적분용 방향각이다.
        // 최종 odom yaw는 현재 IMU 기반 yaw로 갱신한다.
        odom_yaw_rad_ = new_yaw_rad;       
    }

    void GaitOdomEstimatorNode::publishOdometry(
        const rclcpp::Time &stamp,
        const StmMotion &msg,
        double dt_s,
        double delta_body_x_m,
        double delta_body_y_m)
    {
        nav_msgs::msg::Odometry odom_msg;

        // ==========================
        // Header
        // - header.frame_id : pose가 표현되는 기준 좌표계
        // - child_frame_id  : 움직이는 로봇 frame
        // ==========================
        odom_msg.header.stamp = stamp;
        odom_msg.header.frame_id = odom_frame_;
        odom_msg.child_frame_id = base_frame_;

        // ==========================
        // Pose
        // - odom 좌표계 기준 base_link의 현재 위치와 방향
        // ==========================
        odom_msg.pose.pose.position.x = odom_x_m_;
        odom_msg.pose.pose.position.y = odom_y_m_;
        odom_msg.pose.pose.position.z = 0.0;

        odom_msg.pose.pose.orientation = yawToQuaternion(odom_yaw_rad_);

        // ==========================
        // Twist
        // - nav_msgs/Odometry에서 twist는 보통 child_frame_id 기준 속도로 해석
        // - 여기서는 body frame delta를 dt로 나눠 base_link 기준 속도로 넣는다.
        // ==========================
        if (dt_s > 1e-6) {
            odom_msg.twist.twist.linear.x = delta_body_x_m / dt_s;
            odom_msg.twist.twist.linear.y = delta_body_y_m / dt_s;
            odom_msg.twist.twist.linear.z = 0.0;

            odom_msg.twist.twist.angular.x = 0.0;
            odom_msg.twist.twist.angular.y = 0.0;
            odom_msg.twist.twist.angular.z = static_cast<double>(msg.gyro_z_rad_s);
        } else {
            odom_msg.twist.twist.linear.x = 0.0;
            odom_msg.twist.twist.linear.y = 0.0;
            odom_msg.twist.twist.linear.z = 0.0;

            odom_msg.twist.twist.angular.x = 0.0;
            odom_msg.twist.twist.angular.y = 0.0;
            odom_msg.twist.twist.angular.z = 0.0;
        }

        // ==========================
        // Covariance
        // - 초기 MVP에서는 정교한 covariance 모델링 X
        // - 단, downstream node가 covariance 필드를 참고할 수 있으므로 대략적인 값을 넣어둔다.
        // - pose.covariance index:
        //   [0]  = x
        //   [7]  = y
        //   [14] = z
        //   [21] = roll
        //   [28] = pitch
        //   [35] = yaw
        // ==========================

        odom_msg.pose.covariance[0] = 0.05 * 0.05;    // x
        odom_msg.pose.covariance[7] = 0.05 * 0.05;    // y
        odom_msg.pose.covariance[14] = 999.0;         // z 사용 안 함
        odom_msg.pose.covariance[21] = 999.0;         // roll 사용 안 함
        odom_msg.pose.covariance[28] = 999.0;         // pitch 사용 안 함
        odom_msg.pose.covariance[35] = 0.10 * 0.10;   // yaw

        odom_msg.twist.covariance[0] = 0.10 * 0.10;   // vx
        odom_msg.twist.covariance[7] = 0.10 * 0.10;   // vy
        odom_msg.twist.covariance[14] = 999.0;        // vz 사용 안 함
        odom_msg.twist.covariance[21] = 999.0;        // wx 사용 안 함
        odom_msg.twist.covariance[28] = 999.0;        // wy 사용 안 함
        odom_msg.twist.covariance[35] = 0.20 * 0.20;  // wz

        odom_pub_->publish(odom_msg); 
    }

    void GaitOdomEstimatorNode::publishTf(const rclcpp::Time &stamp)
    {
        geometry_msgs::msg::TransformStamped tf_msg;

        // odom 좌표계 기준 base_link의 현재 transform
        tf_msg.header.stamp = stamp;
        tf_msg.header.frame_id = odom_frame_;
        tf_msg.child_frame_id = base_frame_;

        tf_msg.transform.translation.x = odom_x_m_;
        tf_msg.transform.translation.y = odom_y_m_;
        tf_msg.transform.translation.z = 0.0;

        tf_msg.transform.rotation = yawToQuaternion(odom_yaw_rad_);

        tf_broadcaster_->sendTransform(tf_msg);
    }

    double GaitOdomEstimatorNode::normalizeAngle(double angle_rad)
    {
        while (angle_rad > kPi) {
            angle_rad -= kTwoPi;
        }

        while (angle_rad < -kPi) {
            angle_rad += kTwoPi;
        }

        return angle_rad;
    }

    bool GaitOdomEstimatorNode::isFinite(double value)
    {
        return std::isfinite(value);
    }
}

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<spot_localization::GaitOdomEstimatorNode>());
    rclcpp::shutdown();

    return 0;
}