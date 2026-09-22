/*
- 경로 : ~/robot_ws/src/localization/spot_localization/src/localization_pose_node.cpp
- 역할 : TF 기반 최종 Localization Pose publish 노드 구현부
- 입력 : TF tree
    - mission_map -> odom
    - odom -> base_link
- 출력 : /localization/pose (robot_interfaces/msg/LocalizedRobotPose)
- 기능 :
    - TF buffer에서 mission_map -> base_link transform 조회
    - 조회한 transform의 translation을 x_m, y_m, z_m 및 pose.position으로 변환
    - 조회한 transform의 rotation을 yaw_rad 및 pose.orientation으로 변환
    - robot_id를 포함한 공통좌표계 기준 현재 Spot pose publish
*/

#include "spot_localization/localization_pose_node.hpp"

#include <chrono>
#include <cmath>
#include <functional>

#include "tf2/exceptions.h"
#include "tf2/time.h"

namespace spot_localization
{
    LocalizationPoseNode::LocalizationPoseNode()
    : Node("localization_pose_node")
    {
        // ==========================
        // Topic & Frame Parameter
        // ==========================

        // 최종 localization pose 출력 topic
        pose_topic_ = this->declare_parameter<std::string>(
            "pose_topic",
            "/localization/pose"
        );

        // 관제 시스템에서 사용할 로봇 식별자
        // 예: spot_01, spot_02
        robot_id_ = this->declare_parameter<std::string>(
            "robot_id",
            "spot_01"
        );

        // 공통좌표계 frame
        mission_frame_ = this->declare_parameter<std::string>(
            "mission_frame",
            "mission_map"
        );

        // 로봇 몸체 중심 frame
        base_frame_ = this->declare_parameter<std::string>(
            "base_frame",
            "base_link"
        );
        
        // ==========================
        // Publish / TF Parameter
        // ==========================

        // pose publish 주기
        // odometry가 50Hz라면 상위 계층용 pose는 30~50Hz 정도면 충분하다.
        publish_rate_hz_ = this->declare_parameter<double>(
            "publish_rate_hz",
            30.0
        );

        if (publish_rate_hz_ <= 0.0) {
            RCLCPP_WARN(
                this->get_logger(),
                "Invalid publish_rate_hz: %.3f. Use fallback 30.0 Hz.",
                publish_rate_hz_
            );
            publish_rate_hz_ = 30.0;
        }

        // TF 조회 timeout
        // 너무 길게 잡으면 timer callback이 오래 막힐 수 있으므로 짧게 둔다.
        tf_lookup_timeout_s_ = this->declare_parameter<double>(
            "tf_lookup_timeout_s",
            0.05
        );

        // TF 조회 실패 warning 출력 throttle 주기
        warning_throttle_ms_ = this->declare_parameter<int>(
            "warning_throttle_ms",
            2000
        );

        // pose publisher queue depth
        pose_qos_depth_ = this->declare_parameter<int>(
            "pose_qos_depth",
            10
        );

        if (pose_qos_depth_ <= 0) {
            RCLCPP_WARN(
                this->get_logger(),
                "Invalid pose_qos_depth: %d. Use fallback 10.",
                pose_qos_depth_
            );
            pose_qos_depth_ = 10;
        }
        
        // ==========================
        // ROS Interface
        // ==========================

        auto pose_qos = rclcpp::QoS(
            rclcpp::KeepLast(static_cast<size_t>(pose_qos_depth_))
        ).reliable();

        pose_pub_ = this->create_publisher<LocalizedRobotPose>(
            pose_topic_,
            pose_qos
        );

        // TF listener는 내부적으로 /tf, /tf_static을 구독해서 buffer에 transform을 저장한다.
        tf_buffer_ = std::make_shared<tf2_ros::Buffer>(this->get_clock());
        tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_);

        const double publish_period_s = 1.0 / publish_rate_hz_;

        timer_ = this->create_wall_timer(
            std::chrono::duration<double>(publish_period_s),
            std::bind(&LocalizationPoseNode::timerCallback, this)
        );

        RCLCPP_INFO(this->get_logger(), "localization_pose_node started.");
        RCLCPP_INFO(this->get_logger(), "Publish pose : %s", pose_topic_.c_str());
        RCLCPP_INFO(this->get_logger(), "Robot ID     : %s", robot_id_.c_str());
        RCLCPP_INFO(
            this->get_logger(),
            "Lookup TF    : %s -> %s",
            mission_frame_.c_str(),
            base_frame_.c_str()
        );
        RCLCPP_INFO(this->get_logger(), "Publish rate : %.2f Hz", publish_rate_hz_);        
    }

    void LocalizationPoseNode::timerCallback()
    {
        geometry_msgs::msg::TransformStamped transform_msg;

        // mission_map -> base_link transform 조회
        if (!lookupRobotTransform(transform_msg)) return;

        // TransformStamped를 LocalizedRobotPose로 변환
        const LocalizedRobotPose pose_msg =
            toLocalizationPoseMsg(transform_msg);

        pose_pub_->publish(pose_msg);

        // 디버그용 로그
        // 필요 시 아래 로그를 보려면 실행 시 log level을 debug로 변경하면 된다.
        RCLCPP_DEBUG(
            this->get_logger(),
            "Robot[%s] pose in %s: x=%.3f, y=%.3f, yaw=%.3f rad",
            pose_msg.robot_id.c_str(),
            pose_msg.header.frame_id.c_str(),
            pose_msg.x_m,
            pose_msg.y_m,
            pose_msg.yaw_rad
        );        
    }

    bool LocalizationPoseNode::lookupRobotTransform(
        geometry_msgs::msg::TransformStamped &transform_msg)
    {
        try {
            // lookupTransform(target_frame, source_frame, time, timeout)
            // - target_frame = mission_map
            // - source_frame = base_link
            // - 의미: mission_map 기준에서 base_link가 어디에 있는지 조회한다.
            // - tf2::TimePointZero: 가장 최신 transform을 조회
            transform_msg = tf_buffer_->lookupTransform(
                mission_frame_,
                base_frame_,
                tf2::TimePointZero,
                tf2::durationFromSec(tf_lookup_timeout_s_)
            );

            return true;
        } catch (const tf2::TransformException &ex) {
            RCLCPP_WARN_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                warning_throttle_ms_,
                "Failed to lookup TF %s -> %s: %s",
                mission_frame_.c_str(),
                base_frame_.c_str(),
                ex.what()
            );

            return false;
        }
    }

    LocalizationPoseNode::LocalizedRobotPose
    LocalizationPoseNode::toLocalizationPoseMsg(
        const geometry_msgs::msg::TransformStamped &transform_msg) const
    {
        LocalizedRobotPose pose_msg;

        // ==========================
        // Header
        // ==========================
        // header.frame_id는 이 pose가 표현되는 기준 좌표계이다.
        // 여기서는 공통좌표계인 mission_map 기준 pose를 publish한다.
        pose_msg.header.stamp = transform_msg.header.stamp;
        pose_msg.header.frame_id = mission_frame_;

        // ==========================
        // Robot Metadata
        // ==========================
        // robot_id:
        //   관제 시스템에서 여러 로봇을 구분하기 위한 논리적 식별자
        //
        // base_frame:
        //   이 pose가 의미하는 실제 로봇 frame
        pose_msg.robot_id = robot_id_;
        pose_msg.base_frame = base_frame_;

        // ==========================
        // Position
        // ==========================
        // mission_map 기준 base_link의 위치
        pose_msg.x_m = static_cast<float>(transform_msg.transform.translation.x);
        pose_msg.y_m = static_cast<float>(transform_msg.transform.translation.y);
        pose_msg.z_m = static_cast<float>(transform_msg.transform.translation.z);

        pose_msg.pose.position.x = transform_msg.transform.translation.x;
        pose_msg.pose.position.y = transform_msg.transform.translation.y;
        pose_msg.pose.position.z = transform_msg.transform.translation.z;

        // ==========================
        // Orientation
        // ==========================
        // mission_map 기준 base_link의 방향
        // 관제/UI에서 바로 쓰기 쉽도록 yaw_rad를 별도 필드로 제공한다.
        pose_msg.pose.orientation = transform_msg.transform.rotation;
        pose_msg.yaw_rad = static_cast<float>(
            quaternionToYaw(transform_msg.transform.rotation)
        );

        return pose_msg;
    }

    double LocalizationPoseNode::quaternionToYaw(
        const geometry_msgs::msg::Quaternion &q) const
    {
        // roll, pitch가 0에 가까운 2D yaw quaternion 기준 변환식
        //
        // yaw = atan2(2(wz + xy), 1 - 2(y^2 + z^2))
        const double siny_cosp =
            2.0 * (q.w * q.z + q.x * q.y);

        const double cosy_cosp =
            1.0 - 2.0 * (q.y * q.y + q.z * q.z);

        return std::atan2(siny_cosp, cosy_cosp);
    }
}

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<spot_localization::LocalizationPoseNode>());
    rclcpp::shutdown();

    return 0;
}