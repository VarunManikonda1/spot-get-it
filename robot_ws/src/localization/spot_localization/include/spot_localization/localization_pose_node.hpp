/*
- 경로 : ~/robot_ws/src/localization/spot_localization/include/spot_localization/localization_pose_node.hpp
- 역할 : TF 기반 최종 Localization Pose publish 노드 선언부
- 입력 : TF tree
    - mission_map -> odom
    - odom -> base_link
- 출력 : /localization/pose (robot_interfaces/msg/LocalizedRobotPose)
- 기능 :
    - TF buffer를 통해 mission_map 기준 base_link transform 조회
    - mission_map 기준 Spot의 현재 위치 x, y, z 추출
    - mission_map 기준 Spot의 현재 방향 yaw 추출
    - robot_id, base_frame, x, y, z, yaw, pose를 포함한 LocalizedRobotPose 메시지 publish
    - 관제 시스템, mission manager, global/local planner가 사용할 최종 pose 제공
*/

#ifndef SPOT_LOCALIZATION__LOCALIZATION_POSE_NODE_HPP_
#define SPOT_LOCALIZATION__LOCALIZATION_POSE_NODE_HPP_

#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"

#include "geometry_msgs/msg/quaternion.hpp"
#include "geometry_msgs/msg/transform_stamped.hpp"

#include "robot_interfaces/msg/localized_robot_pose.hpp"

#include "tf2_ros/buffer.h"
#include "tf2_ros/transform_listener.h"

namespace spot_localization
{
    class LocalizationPoseNode : public rclcpp::Node
    {
        public:
            LocalizationPoseNode();
        
        private:
            using LocalizedRobotPose = robot_interfaces::msg::LocalizedRobotPose;

            // Timer Callback 함수
            // - 일정 주기마다 TF에서 mission_frame -> base_frame transform 조회
            // - 조회 성공 시 /Localization/pose publish
            void timerCallback();

            // [1] TF 조회 함수
            // - mission_map 기준 base_link의 현재 transform을 조회
            // - 성공하면 true, 실패하면 false
            bool lookupRobotTransform(geometry_msgs::msg::TransformStamped &transform_msg);

            // [2] Localization Pose 메시지 생성 함수
            // - TransformStamped를 robot_interfaces/msg/LocalizedRobotPose로 반환
            LocalizedRobotPose toLocalizationPoseMsg(
                const geometry_msgs::msg::TransformStamped &transform_msg) const;
            
            // [3] Quaternion Utility
            // - quaternion에서 yaw(rad) 추출
            double quaternionToYaw(const geometry_msgs::msg::Quaternion &q) const;

            // ===============
            // ROS Interface
            // ===============
            // 최종 localization pose publisher
            rclcpp::Publisher<LocalizedRobotPose>::SharedPtr pose_pub_;

            // 주기적으로 TF를 조회하기 위한 timer
            rclcpp::TimerBase::SharedPtr timer_;

            // TF buffer / listener
            // - 내부적으로 /tf, /tf_static을 구독하여 transform 정보를 저장
            std::shared_ptr<tf2_ros::Buffer> tf_buffer_;
            std::shared_ptr<tf2_ros::TransformListener> tf_listener_;
            
            // ==========================
            // Topic & Frame Parameter
            // ==========================
            
            // 최종 pose 출력 topic 이름
            std::string pose_topic_;

            // 관제 시스템에서 사용하는 로봇 식별자
            std::string robot_id_;

            // 공통 mission map frame 이름
            std::string mission_frame_;

            // 로봇 몸체 중심 frame 이름
            std::string base_frame_;

            // ==========================
            // Publish / TF Parameter
            // ==========================

            // /localization/pose publish 주기 [Hz]
            double publish_rate_hz_;

            // TF 조회 timeout [s]
            double tf_lookup_timeout_s_;

            // TF 조회 실패 warning throttle 주기 [ms]
            int warning_throttle_ms_;

            // pose publisher queue depth
            int pose_qos_depth_;            
    };
}

#endif