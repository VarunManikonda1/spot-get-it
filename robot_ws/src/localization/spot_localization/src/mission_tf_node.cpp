/*
- 경로 : ~/robot_ws/src/localization/spot_localization/src/mission_tf_node.cpp
- 역할 : mission_map 기준 odom 시작 위치 고정 TF publish 노드 구현부 (static anchor 노드)
- 입력 : 없음
- 출력 : 없음
- TF   : mission_map -> odom
- 기능 :
    - mission_tf_param.yaml에서 mission_frame, odom_frame을 읽어옴
    - mission_tf_param.yaml에서 start_x_m, start_y_m, start_z_m, start_yaw_rad를 읽어옴
    - mission_map 기준 odom frame의 초기 위치와 방향을 static TF로 publish
    - gait_odom_estimator_node가 publish하는 odom -> base_link와 연결되어
      최종 TF 체인 mission_map -> odom -> base_link를 구성
*/
#include "spot_localization/mission_tf_node.hpp"

#include <cmath>

namespace spot_localization
{
    MissionTfNode::MissionTfNode()
    : Node("mission_tf_node")
    {
        // ==========================
        // Frame Parameter
        // - mission_frame: 전체 관제/공통좌표계 frame
        // - odom_frame: 로봇 시작점을 원점으로 하는 local odom frame
        // ==========================

        mission_frame_ = this->declare_parameter<std::string>(
            "mission_frame",
            "mission_map"
        );

        odom_frame_ = this->declare_parameter<std::string>(
            "odom_frame",
            "odom"
        );

        // ==========================
        // Start Pose Parameter
        // ==========================
        // - start_x_m, start_y_m: mission_map 기준에서 odom 원점이 놓일 위치
        // - start_z_m: 실내 2D 주행 MVP에서는 보통 0.0
        // - start_yaw_rad: mission_map 기준에서 odom x축이 바라보는 방향
        //
        // 예:
        //   start_x_m = 2.0
        //   start_y_m = 1.0
        //   start_yaw_rad = 1.5708
        //
        // 의미:
        //   odom 원점은 mission_map 기준 (2.0, 1.0)에 있고,
        //   odom x축은 mission_map 기준 90도 방향을 향한다.
        start_x_m_ = this->declare_parameter<double>(
            "start_x_m",
            0.0
        );

        start_y_m_ = this->declare_parameter<double>(
            "start_y_m",
            0.0
        );

        start_z_m_ = this->declare_parameter<double>(
            "start_z_m",
            0.0
        );

        start_yaw_rad_ = this->declare_parameter<double>(
            "start_yaw_rad",
            0.0
        );

        // Static TF broadcaster 생성
        static_tf_broadcaster_ =
            std::make_shared<tf2_ros::StaticTransformBroadcaster>(this);

        // mission_map -> odom static TF publish
        publishStaticMissionTf();

        RCLCPP_INFO(this->get_logger(), "mission_tf_node started.");
        RCLCPP_INFO(
            this->get_logger(),
            "Static TF: %s -> %s",
            mission_frame_.c_str(),
            odom_frame_.c_str()
        );

        RCLCPP_INFO(
            this->get_logger(),
            "Start pose in %s: x=%.3f, y=%.3f, z=%.3f, yaw=%.3f rad",
            mission_frame_.c_str(),
            start_x_m_,
            start_y_m_,
            start_z_m_,
            start_yaw_rad_
        );        
    }

    void MissionTfNode::publishStaticMissionTf()
    {
        geometry_msgs::msg::TransformStamped tf_msg;

        // ==========================
        // Header
        // - parent frame: mission_map
        // - child frame: odom
        // - 즉, mission_map 기준에서 odom frame이 어디에 있는지 나타냄
        // ==========================
        tf_msg.header.stamp = this->now();
        tf_msg.header.frame_id = mission_frame_;
        tf_msg.child_frame_id = odom_frame_;

        // ==========================
        // Translation
        // - mission_map 기준 odom 원점 위치
        // ==========================
        tf_msg.transform.translation.x = start_x_m_;
        tf_msg.transform.translation.y = start_y_m_;
        tf_msg.transform.translation.z = start_z_m_;

        // ==========================
        // Rotation
        // - mission_map 기준 odom frame의 yaw 방향
        // ==========================
        tf_msg.transform.rotation = yawToQuaternion(start_yaw_rad_);

        static_tf_broadcaster_->sendTransform(tf_msg);
    }

    geometry_msgs::msg::Quaternion MissionTfNode::yawToQuaternion(double yaw_rad) const
    {
        geometry_msgs::msg::Quaternion q;

        // 2D localization MVP에서는 roll = 0, pitch = 0으로 두고 yaw만 사용한다.
        q.x = 0.0;
        q.y = 0.0;
        q.z = std::sin(yaw_rad * 0.5);
        q.w = std::cos(yaw_rad * 0.5);

        return q;
    }
}

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<spot_localization::MissionTfNode>());
    rclcpp::shutdown();

    return 0;
}