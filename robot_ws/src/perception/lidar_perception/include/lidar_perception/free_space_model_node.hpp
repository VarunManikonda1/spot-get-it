/*
- 경로 : ~/robot_ws/src/perception/lidar_perception/include/lidar_perception/free_space_model_node.hpp
- 역할 : Local OccupancyGrid 기반 Free-Space 후보 영역 추출 노드
- 입력 : /perception/lidar/local_occupancy_grid (nav_msgs/msg/OccupancyGrid)
- 출력 : /perception/lidar/free_space_model (robot_interfaces/msg/FreeSpaceModel)
- 기능 :
    - OccupancyGrid cell을 base_link 기준 x, y 좌표로 복원
    - 각 cell의 거리와 방향각을 계산
    - LiDAR FOV 범위를 angular bin 단위로 분할
    - 각 bin별 unknown/free/occupied 통계 계산
    - occupied ratio 및 nearest occupied distance 기반 blocked bin 판단
    - free/unknown/occupied ratio와 clearance 기반 bin score 계산
    - 연속된 candidate bin들을 FreeSpaceGap으로 묶음
    - 후보 gap 중 가장 안전한 selected gap 및 best heading 산출
    - FreeSpaceModel 메시지 publish
*/

#ifndef LIDAR_PERCEPTION__FREE_SPACE_MODEL_NODE_HPP_
#define LIDAR_PERCEPTION__FREE_SPACE_MODEL_NODE_HPP_

#include <limits>       // numeric_Limits<float>::infinity() 사용
#include <memory>       // SharedPtr 사용
#include <string>
#include <cstdint>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "nav_msgs/msg/occupancy_grid.hpp"

#include "robot_interfaces/msg/free_space_model.hpp"
#include "robot_interfaces/msg/free_space_gap.hpp"

namespace lidar_perception
{
    class FreeSpaceModelNode : public rclcpp::Node
    {
        public:
            FreeSpaceModelNode();
        
        private:
            // Angular Bin Stats : 하나의 angular bin에 대한 cell 통계 구조체
            struct AngularBinStats
            {
                // bin 식별자
                uint32_t bin_index = 0;

                // bin 각도 범위 [deg]
                // x+ : 전방(0 deg), y+ : 좌측(y>0), y- : 우측(y<0)
                double start_angle_deg  = 0.0;
                double end_angle_deg    = 0.0;
                double center_angle_deg = 0.0;
                
                // cell count
                int total_count    = 0;
                int unknown_count  = 0;
                int free_count     = 0;
                int occupied_count = 0;

                // ratio
                double unknown_ratio  = 0.0;    // angular bin 대비 unknown 비율
                double free_ratio     = 0.0;    // angular bin 대비 주행 가능 영역 비율
                double occupied_ratio = 0.0;    // angular bin 대비 장애물 비율

                // 해당 bin 내부에서 가장 가까운 occupied cell까지의 거리 [m]
                // - occupied cell이 없으면 finalize에서 max_check_range_로 clamp
                double nearest_occupied_distance =
                    std::numeric_limits<double>::infinity();
                
                // free/unknown/occupied/clearance/heading bias 기반 종합 점수
                double score = 0.0;

                // blocked 상태
                // - occupied_ratio가 높거나, nearest occupied cell이 너무 가까우면 true
                bool blocked = false;

                // candidate 상태
                // - blocked가 아니고, free 또는 unknown evidence를 바탕으로 통과 후보로 볼 수 있으면 true
                bool candidate = false;
            };

            // Gap candidate : 연속된 candidate bin을 하나의 통과 가능 Gap 후보로 묶는 구조체
            struct GapCandidate
            {
                // gap을 구성하는 bin index 범위
                int start_bin = 0;
                int end_bin   = 0;

                // gap 각도 범위 [deg]
                double start_angle_deg  = 0.0;
                double end_angle_deg    = 0.0;
                double center_angle_deg = 0.0;
                double width_angle_deg  = 0.0;

                // gap 내부에서 가장 가까운 occupied cell까지의 거리 [m]
                // - 해당 통로의 최소 여유 거리
                double min_clearance =
                    std::numeric_limits<double>::infinity();

                // gap 내부 cell count 합산
                int total_count    = 0;
                int unknown_count  = 0;
                int free_count     = 0;
                int occupied_count = 0;

                // gap 내부 ratio
                double unknown_ratio  = 0.0;
                double free_ratio     = 0.0;
                double occupied_ratio = 0.0;

                // gap 종합 점수
                double score = 0.0;
            };

            // Callback 함수
            // - Occupancy Grid 전체 cell 순회 + angular bin 통계 계산
            // - cadidate gap과 selected gap 산출
            void occupancyGridCallback(const nav_msgs::msg::OccupancyGrid::SharedPtr msg);

            // [1] Grid index → base_link 좌표 변환 함수
            double gridIndexToWorldX(int grid_x, const nav_msgs::msg::OccupancyGrid &grid_msg) const;
            double gridIndexToWorldY(int grid_y, const nav_msgs::msg::OccupancyGrid &grid_msg) const;

            // [2] Angle Utility
            double radToDeg(double rad) const;
            double degToRad(double deg) const;

            // [3] angle_deg가 분석 FOV 범위 안에 있는지 확인
            bool isAngleInsideFov(double angle_deg) const;

            // [4] angle_deg를 angular bin index로 변환 - Lidar FOV 처리를 위한 변환 과정
            int angleToBinIndex(double angle_deg) const;

            // ==========================
            // [5] Angular Bin 처리 함수
            // ===========================
            // LiDAR FOV 범위를 angular_bin_size_deg_ 단위로 나눈 AngularBinStats 배열 초기화
            void initializeBins(std::vector<AngularBinStats> &bins) const;

            // Angular Bin 업데이트 함수
            void updateBinStats(AngularBinStats &bin, int8_t cell_value, double distance);

            // bin 통계 누적 이후 ratio, blocked 여부, candidate 여부, score 계산 함수
            void finalizeBinStats(AngularBinStats &bin) const;

            // =================
            // [6] Gap 추출 함수
            // ==================
            // 연속된 condidate bin들을 FreeSpace gap 후보 추출 함수
            std::vector<GapCandidate> extractGaps(const std::vector<AngularBinStats> &bins) const;

            // start bin ~ end bin 범위의 bin들을 하나의 gap으로 합산하는 함수
            GapCandidate buildGap(const std::vector<AngularBinStats> &bins, int start_bin, int end_bin) const;

            // 여러 gap 후보 중 selected gap 선택
            bool selectBestGap(const std::vector<GapCandidate> &gaps, GapCandidate &best_gap) const;

            // ============================
            // [7] Risk / Message 변환 함수
            // ============================

            // selected gap의 unknown_ratio, occupied_ ratio, path_available 바탕으로 FreeSpaceModel의 risk level 결정 함수 
            uint8_t determineRiskLevel(bool path_available, const GapCandidate &selected_gap) const;

            // 내부 GapCandidate 구조체 -> robot_interfaces/msg/FreeSpaceGap 메시지로 변환
            robot_interfaces::msg::FreeSpaceGap toGapMsg(const GapCandidate &gap, bool selected) const;

            // ROS Interface
            rclcpp::Subscription<nav_msgs::msg::OccupancyGrid>::SharedPtr grid_sub_;
            rclcpp::Publisher<robot_interfaces::msg::FreeSpaceModel>::SharedPtr model_pub_;

            // Topic & Frame Parameter
            std::string input_topic_;
            std::string output_topic_;
            std::string frame_id_;

            // Occupancy Grid cell value Parameter
            int unknown_value_;
            int free_value_;
            int occupied_value_;

            // Angular bin / FOV Parameter
            double fov_min_deg_;            // -60 (Max Right)
            double fov_max_deg_;            // +60 (Max Left)
            double angular_bin_size_deg_;   // +5 (튜닝 값)

            // Analysis Range Threshold
            double min_check_range_;
            double max_check_range_;

            // Block / Gap 판단 Threshold
            double occupied_ratio_block_threshold_;     // occupied 비율이 해당 값 이상이면 bin은 blocked 처리
            double min_clearance_block_threshold_;      // nearest occupied distance가 이 값보다 가까우면 blocked 처리
            double min_gap_width_deg_;                  // gap 후보로 인정할 최소 각도 폭 [deg]
            double free_ratio_candidate_threshold_;     // candidate bin으로 인정하기 위한 최소 free ratio
            double unknown_ratio_caution_threshold_;    // selected gap의 unknown ratio가 이 값 이상이면 RISK_CAUTION으로 판단

            // Score Weight Parameter
            double free_weight_;
            double unknown_weight_;
            double occupied_weight_;
            double clearance_weight_;
            double heading_bias_weight_;
    };
}

#endif