/*
- 경로 : ~/robot_ws/src/localization/spot_localization/include/spot_localization/mission_tf_node.hpp
- 역할 : mission_map 기준 odom 시작 위치 고정 TF publish 노드 선언부
- 입력 : 없음
- 출력 : 없음
- TF   : mission_map -> odom
- 기능 :
    - YAML 파라미터에서 mission_frame, odom_frame 이름을 읽어옴
    - YAML 파라미터에서 Spot 시작 위치 start_x_m, start_y_m, start_z_m을 읽어옴
    - YAML 파라미터에서 Spot 시작 방향 start_yaw_rad를 읽어옴
    - mission_map 기준 odom 좌표계의 초기 pose를 static TF로 publish
    - 최종 TF 체인 mission_map -> odom -> base_link 구성을 가능하게 함
*/
#ifndef SPOT_LOCALIZATION__MISSION_TF_NODE_HPP_
#define SPOT_LOCALIZATION__MISSION_TF_NODE_HPP_

#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "tf2_ros/static_transform_broadcaster.h"

namespace spot_localization
{
    class MissionTfNode : public rclcpp::Node
    {
        public:
            MissionTfNode();
        
        private:
            // [1] mission_map -> odom static TF publish 함수
            // - YAML에서 읽어온 시작 위치와 시작 yaw를 이용해 static transform을 publish
            void publishStaticMissionTf();

            // [2] Angle Utility
            // - yaw 값을 quaternion으로 변환
            geometry_msgs::msg::Quaternion yawToQuaternion(double yaw_rad) const;

            // ==========================
            // ROS Interface
            // ==========================
            
            // mission_map -> odom static TF broadcaster
            std::shared_ptr<tf2_ros::StaticTransformBroadcaster> static_tf_broadcaster_;

            // ==========================
            // Frame Parameter
            // ==========================

            // 공통 mission map frame 이름
            std::string mission_frame_;

            // 로봇 시작점을 원점으로 하는 local odom frame 이름
            std::string odom_frame_;

            // ==========================
            // Start Pose Parameter
            // - mission_map 기준 odom frame의 시작 위치와 방향
            // - 실제 값은 mission_tf_param.yaml에서 관리한다.
            // ==========================
            double start_x_m_;
            double start_y_m_;
            double start_z_m_;
            double start_yaw_rad_;
    };
}

#endif