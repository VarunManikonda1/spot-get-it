/*
- 경로 : ~/robot_ws/src/perception/lidar_perception/src/free_space_model_node.cpp
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

#include "lidar_perception/free_space_model_node.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace lidar_perception
{
    namespace {
        constexpr double kPi = 3.14159265358979323846;
    }

    FreeSpaceModelNode::FreeSpaceModelNode()
    : Node("free_space_model_node")
    {   
        // =======================
        // [1] Parameter Declare
        // =======================

        // Topic / frame
        input_topic_ = this->declare_parameter<std::string>(
            "input_topic",
            "/perception/lidar/local_occupancy_grid"
        );

        output_topic_ = this->declare_parameter<std::string>(
            "output_topic",
            "/perception/lidar/free_space_model"
        );

        frame_id_ = this->declare_parameter<std::string>(
            "frame_id",
            "base_link"
        );

        // OccupancyGrid cell value
        unknown_value_ = this->declare_parameter<int>("unknown_value", -1);
        free_value_ = this->declare_parameter<int>("free_value", 0);
        occupied_value_ = this->declare_parameter<int>("occupied_value", 100);

        // Angular bin / FOV
        fov_min_deg_ = this->declare_parameter<double>("fov_min_deg", -60.0);
        fov_max_deg_ = this->declare_parameter<double>("fov_max_deg", 60.0);
        angular_bin_size_deg_ = this->declare_parameter<double>("angular_bin_size_deg", 5.0);

        // Analysis range
        min_check_range_ = this->declare_parameter<double>("min_check_range", 0.20);
        max_check_range_ = this->declare_parameter<double>("max_check_range", 2.50);

        // Block / gap threshold
        occupied_ratio_block_threshold_ =
            this->declare_parameter<double>("occupied_ratio_block_threshold", 0.03);

        min_clearance_block_threshold_ =
            this->declare_parameter<double>("min_clearance_block_threshold", 0.60);

        min_gap_width_deg_ =
            this->declare_parameter<double>("min_gap_width_deg", 20.0);

        free_ratio_candidate_threshold_ =
            this->declare_parameter<double>("free_ratio_candidate_threshold", 0.10);

        unknown_ratio_caution_threshold_ =
            this->declare_parameter<double>("unknown_ratio_caution_threshold", 0.60);

        // Score weights
        free_weight_ = this->declare_parameter<double>("free_weight", 1.0);
        unknown_weight_ = this->declare_parameter<double>("unknown_weight", 0.3);
        occupied_weight_ = this->declare_parameter<double>("occupied_weight", 2.0);
        clearance_weight_ = this->declare_parameter<double>("clearance_weight", 0.8);
        heading_bias_weight_ = this->declare_parameter<double>("heading_bias_weight", 0.2);

        // =========================
        // [2] Parameter Validation
        // =========================
        if (fov_max_deg_ <= fov_min_deg_) {
            throw std::runtime_error("Invalid FOV parameter: fov_max_deg must be greater than fov_min_deg");
        }

        if (angular_bin_size_deg_ <= 0.0) {
            throw std::runtime_error("Invalid angular_bin_size_deg: must be positive");
        }

        if (min_check_range_ < 0.0 || max_check_range_ <= min_check_range_) {
            throw std::runtime_error("Invalid check range: max_check_range must be greater than min_check_range");
        }

        if (min_gap_width_deg_ <= 0.0) {
            throw std::runtime_error("Invalid min_gap_width_deg: must be positive");
        }

        if (occupied_ratio_block_threshold_ < 0.0 || occupied_ratio_block_threshold_ > 1.0) {
            throw std::runtime_error("Invalid occupied_ratio_block_threshold: must be [0, 1]");
        }

        if (free_ratio_candidate_threshold_ < 0.0 || free_ratio_candidate_threshold_ > 1.0) {
            throw std::runtime_error("Invalid free_ratio_candidate_threshold: must be [0, 1]");
        }

        if (unknown_ratio_caution_threshold_ < 0.0 || unknown_ratio_caution_threshold_ > 1.0) {
            throw std::runtime_error("Invalid unknown_ratio_caution_threshold: must be [0, 1]");
        }

        // ===============================
        // [3] Subscriber / Publisher 생성
        // ===============================
        grid_sub_ = this->create_subscription<nav_msgs::msg::OccupancyGrid>(
            input_topic_,
            rclcpp::QoS(10),
            std::bind(&FreeSpaceModelNode::occupancyGridCallback, this, std::placeholders::_1)
        );

        model_pub_ = this->create_publisher<robot_interfaces::msg::FreeSpaceModel>(
            output_topic_,
            rclcpp::QoS(10)
        );

        // 시작 로그
        RCLCPP_INFO(this->get_logger(), "free_space_model_node started!");
        RCLCPP_INFO(this->get_logger(), "input_topic  : %s", input_topic_.c_str());
        RCLCPP_INFO(this->get_logger(), "output_topic : %s", output_topic_.c_str());
        RCLCPP_INFO(this->get_logger(), "frame_id     : %s", frame_id_.c_str());

        RCLCPP_INFO(
            this->get_logger(),
            "angular bins : fov=[%.1f, %.1f] deg, bin_size=%.1f deg",
            fov_min_deg_,
            fov_max_deg_,
            angular_bin_size_deg_
        );

    }

    // =========================================
    // [1] Grid index → base_link 좌표 변환 함수
    // =========================================

    double FreeSpaceModelNode::gridIndexToWorldX(
        const int grid_x,
        const nav_msgs::msg::OccupancyGrid &grid_msg) const
    {
        return grid_msg.info.origin.position.x +
               (static_cast<double>(grid_x) + 0.5) *
               static_cast<double>(grid_msg.info.resolution);
    }

    double FreeSpaceModelNode::gridIndexToWorldY(
        const int grid_y,
        const nav_msgs::msg::OccupancyGrid &grid_msg) const
    {
        return grid_msg.info.origin.position.y +
           (static_cast<double>(grid_y) + 0.5) *
           static_cast<double>(grid_msg.info.resolution);
    }

    // ==================
    // [2] Angle Utility
    // ==================

    double FreeSpaceModelNode::radToDeg(const double rad) const {
        return rad * 180.0 / kPi;
    }

    double FreeSpaceModelNode::degToRad(const double deg) const {
        return deg * kPi / 180.0;
    }

    bool FreeSpaceModelNode::isAngleInsideFov(const double angle_deg) const {
        return angle_deg >= fov_min_deg_ && angle_deg <= fov_max_deg_;
    }

    int FreeSpaceModelNode::angleToBinIndex(const double angle_deg) const {
        
        if (!isAngleInsideFov(angle_deg)) return -1;

        const int bin_count = static_cast<int>(
            std::ceil((fov_max_deg_ - fov_min_deg_) / angular_bin_size_deg_));
        
        if (bin_count <= 0) return -1;

        int index = static_cast<int>(
            std::floor((angle_deg - fov_min_deg_) / angular_bin_size_deg_)
        );

        // angle_deg == fov_max_deg_인 경우, index가 bin_count가 될 수 있기 때문에 clamp 처리
        if (index < 0) index = 0;
        if (index >= bin_count) index = bin_count - 1;

        return index;
    }

    // ========================
    // [3] Angular Bin 처리 함수
    // ========================

    void FreeSpaceModelNode::initializeBins(
        std::vector<AngularBinStats> & bins) const
    {
        const int bin_count = static_cast<int>(
            std::ceil((fov_max_deg_ - fov_min_deg_) / angular_bin_size_deg_)
        );

        bins.clear();
        bins.resize(static_cast<std::size_t>(bin_count));

        for (int i = 0; i < bin_count; ++i) {
            auto &bin = bins[static_cast<std::size_t>(i)];

            bin.bin_index = static_cast<uint32_t>(i);

            bin.start_angle_deg = fov_min_deg_ + static_cast<double>(i) * angular_bin_size_deg_;
            
            bin.end_angle_deg = std::min(
                bin.start_angle_deg + angular_bin_size_deg_,
                fov_max_deg_
            );

            bin.center_angle_deg = 0.5 * (bin.start_angle_deg + bin.end_angle_deg);
        }
    }

    void FreeSpaceModelNode::updateBinStats(
        AngularBinStats &bin,
        const int8_t cell_value,
        const double distance)
    {
        bin.total_count++;

        if (cell_value == static_cast<int8_t>(unknown_value_)) bin.unknown_count++;
        else if (cell_value == static_cast<int8_t>(free_value_)) bin.free_count++;
        else if (cell_value == static_cast<int8_t>(occupied_value_)) {
            bin.occupied_count++;

            if (distance < bin.nearest_occupied_distance) bin.nearest_occupied_distance = distance;
        }
        else {
            // 알 수 없는 cell value는 보수적으로 unknown 처리
            bin.unknown_count++;
        }
    }

    void FreeSpaceModelNode::finalizeBinStats(AngularBinStats &bin) const
    {
        if (bin.total_count <= 0) {
            bin.unknown_ratio  = 1.0;
            bin.free_ratio     = 0.0;
            bin.occupied_ratio = 0.0;
        
            bin.nearest_occupied_distance = max_check_range_;
            bin.score     = -1.0;
            bin.blocked   = false;
            bin.candidate = false;
            return;
        }

        const double total = static_cast<double>(bin.total_count);

        bin.unknown_ratio  = static_cast<double>(bin.unknown_count) / total;
        bin.free_ratio     = static_cast<double>(bin.free_count) / total;
        bin.occupied_ratio = static_cast<double>(bin.occupied_count) / total;

        // 해당 bin 안에 occupied cell이 없으면 검사 범위 내 장애물 없음으로 처리
        if (bin.occupied_count == 0) bin.nearest_occupied_distance = max_check_range_;

        // blocked 판단
        // - occupied 비율이 높거나
        // - 가장 가까운 occupied cell이 너무 가까우면 blocked
        bin.blocked =
            (bin.occupied_ratio >= occupied_ratio_block_threshold_) ||
            (bin.nearest_occupied_distance < min_clearance_block_threshold_);
        
        const double normalized_clearance =
            std::min(bin.nearest_occupied_distance, max_check_range_) / max_check_range_;
        
        const double max_abs_fov_angle =
            std::max(std::abs(fov_min_deg_), std::abs(fov_max_deg_));

        const double normalized_heading_penalty =
            (max_abs_fov_angle > 0.0)
                ? std::abs(bin.center_angle_deg) / max_abs_fov_angle
                : 0.0;

        // unknown은 free보다 낮은 점수를 주되, occupied처럼 즉시 위험으로 보지 않음
        // - return point가 없어도 unknown이 많은 평지도 CAUTION 후보로 생성
        // - 현재 기본값 기준 이론상 최고점은 1.8점
        bin.score =
            free_weight_ * bin.free_ratio +
            unknown_weight_ * bin.unknown_ratio -
            occupied_weight_ * bin.occupied_ratio +
            clearance_weight_ * normalized_clearance -
            heading_bias_weight_ * normalized_heading_penalty;
        
        // candidate 판단
        // - blocked X && 분석 대상 Cell이 존재하면, 통과 후보로 생성
        // - free evidence가 충분하면 좋은 후보,
        // - unknown이 대부분이면 CAUTION 후보로 해석
        bin.candidate = !bin.blocked && (bin.total_count > 0);
    }

    // ==================
    // [4] Gap 추출 함수
    // ==================

    FreeSpaceModelNode::GapCandidate FreeSpaceModelNode::buildGap(
        const std::vector<AngularBinStats> &bins,
        const int start_bin,
        const int end_bin) const
    {
        GapCandidate gap;

        gap.start_bin = start_bin;
        gap.end_bin   = end_bin;

        gap.start_angle_deg =
            bins[static_cast<std::size_t>(start_bin)].start_angle_deg;
        
        gap.end_angle_deg =
            bins[static_cast<std::size_t>(end_bin)].end_angle_deg;

        gap.center_angle_deg =
            0.5 * (gap.start_angle_deg + gap.end_angle_deg);

        gap.width_angle_deg =
            gap.end_angle_deg - gap.start_angle_deg;
        
        double score_sum = 0.0;
        int bin_count    = 0;

        for (int i = start_bin; i <= end_bin; ++i) {
            const auto &bin = bins[static_cast<std::size_t>(i)];

            gap.total_count    += bin.total_count;
            gap.unknown_count  += bin.unknown_count;
            gap.free_count     += bin.free_count;
            gap.occupied_count += bin.occupied_count;
        
            gap.min_clearance = std::min(gap.min_clearance, bin.nearest_occupied_distance);

            score_sum += bin.score;
            bin_count++;
        }

        if (gap.total_count > 0) {
            const double total = static_cast<double>(gap.total_count);

            gap.unknown_ratio  = static_cast<double>(gap.unknown_count) / total;
            gap.free_ratio     = static_cast<double>(gap.free_count) / total;
            gap.occupied_ratio = static_cast<double>(gap.occupied_count) / total;
        } else {
            gap.unknown_ratio  = 1.0;
            gap.free_ratio     = 0.0;
            gap.occupied_ratio = 0.0;
        }

        if (!std::isfinite(gap.min_clearance)) gap.min_clearance = max_check_range_;

        gap.score =
            (bin_count > 0)
            ? score_sum / static_cast<double>(bin_count)
            : -1.0;

        return gap;
    }

    std::vector<FreeSpaceModelNode::GapCandidate>FreeSpaceModelNode::extractGaps(
        const std::vector<AngularBinStats> &bins) const
    {
        std::vector<GapCandidate> gaps;

        int start_bin = -1;

        for (int i = 0; i < static_cast<int>(bins.size()); ++i) {
            if (bins[static_cast<std::size_t>(i)].candidate) {
                if (start_bin < 0) start_bin = i;
            } else {
                if (start_bin >= 0) {
                    const int end_bin = i - 1;
                    auto gap = buildGap(bins, start_bin, end_bin);

                    if (gap.width_angle_deg >= min_gap_width_deg_) gaps.push_back(gap);

                    start_bin = -1;
                }
            }
        }

        // 마지막 bin까지 candidate가 이어진 경우 처리
        if (start_bin >= 0) {
            const int end_bin = static_cast<int>(bins.size()) - 1;
            auto gap = buildGap(bins, start_bin, end_bin);

            if (gap.width_angle_deg >= min_gap_width_deg_) gaps.push_back(gap);
        }

        return gaps;
    }

    bool FreeSpaceModelNode::selectBestGap(
        const std::vector<GapCandidate> &gaps,
        GapCandidate &best_gap) const
    {
        if (gaps.empty()) return false;

        bool found = false;
        double best_score = -std::numeric_limits<double>::infinity();

        for (const auto &gap : gaps) {
            if (!found || gap.score > best_score) {
                best_score = gap.score;
                best_gap   = gap;
                found      = true;
            }
        }

        return found;
    }

    // =============================
    // [5] Risk / Message 변환 함수
    // =============================

    uint8_t FreeSpaceModelNode::determineRiskLevel(
        const bool path_available,
        const GapCandidate &selected_gap) const
    {
        if (!path_available) return robot_interfaces::msg::FreeSpaceModel::RISK_BLOCKED;

        if (selected_gap.min_clearance < min_clearance_block_threshold_)
            return robot_interfaces::msg::FreeSpaceModel::RISK_BLOCKED;

        if (selected_gap.unknown_ratio >= unknown_ratio_caution_threshold_)
            return robot_interfaces::msg::FreeSpaceModel::RISK_CAUTION;

        if (selected_gap.occupied_ratio > 0.0)
            return robot_interfaces::msg::FreeSpaceModel::RISK_CAUTION;

        return robot_interfaces::msg::FreeSpaceModel::RISK_SAFE;
    }

    robot_interfaces::msg::FreeSpaceGap FreeSpaceModelNode::toGapMsg(
        const GapCandidate & gap,
        const bool selected) const
    {
        robot_interfaces::msg::FreeSpaceGap msg;

        msg.start_bin = static_cast<uint32_t>(gap.start_bin);
        msg.end_bin = static_cast<uint32_t>(gap.end_bin);

        msg.start_angle_deg = static_cast<float>(gap.start_angle_deg);
        msg.end_angle_deg = static_cast<float>(gap.end_angle_deg);
        msg.center_angle_deg = static_cast<float>(gap.center_angle_deg);
        msg.width_angle_deg = static_cast<float>(gap.width_angle_deg);

        msg.start_angle_rad = static_cast<float>(degToRad(gap.start_angle_deg));
        msg.end_angle_rad = static_cast<float>(degToRad(gap.end_angle_deg));
        msg.center_angle_rad = static_cast<float>(degToRad(gap.center_angle_deg));
        msg.width_angle_rad = static_cast<float>(degToRad(gap.width_angle_deg));

        msg.min_clearance = static_cast<float>(gap.min_clearance);

        msg.free_ratio = static_cast<float>(gap.free_ratio);
        msg.unknown_ratio = static_cast<float>(gap.unknown_ratio);
        msg.occupied_ratio = static_cast<float>(gap.occupied_ratio);

        msg.score = static_cast<float>(gap.score);
        msg.selected = selected;

        return msg;
    }


    // ============================================================
    // [6] OccupancyGrid Callback
    // ============================================================

    void FreeSpaceModelNode::occupancyGridCallback(
        const nav_msgs::msg::OccupancyGrid::SharedPtr msg)
    {

        // =========================
        // [1] 입력 Grid 유효성 확인
        // =========================
        if (msg->header.frame_id != frame_id_) {
            RCLCPP_WARN_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                2000,
                "Input occupancy grid frame_id(%s) != expected frame_id(%s). "
                "FreeSpaceModel assumes grid cells are in %s frame.",
                msg->header.frame_id.c_str(),
                frame_id_.c_str(),
                frame_id_.c_str()
            );
        }

        const int width  = static_cast<int>(msg->info.width);
        const int height = static_cast<int>(msg->info.height);

        if (width <= 0 || height <= 0 || msg->data.empty()) {
            RCLCPP_WARN_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                2000,
                "Input occupancy grid is empty or invalid. width=%d height=%d data_size=%zu",
                width,
                height,
                msg->data.size()
            );
            return;            
        }

        if (static_cast<std::size_t>(width * height) != msg->data.size()) {
            RCLCPP_WARN_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                2000,
                "OccupancyGrid size mismatch. width*height=%d data_size=%zu",
                width * height,
                msg->data.size()
            );
            return;
        }

        // ==========================
        // [2] Angular bins 초기화
        // ==========================
        std::vector<AngularBinStats> bins;
        initializeBins(bins);

        if (bins.empty()) {
            RCLCPP_WARN_THROTTLE(
                this->get_logger(),
                *this->get_clock(),
                2000,
                "No angular bins created. Check FOV/bin parameters."
            );
            return;
        }

        // ===============================
        // [3] 전체 cell 순회 및 bin 누적
        // ===============================
        uint32_t analyzed_cell_count  = 0;
        uint32_t total_unknown_count  = 0;
        uint32_t total_free_count     = 0;
        uint32_t total_occupied_count = 0;

        for (int grid_y = 0; grid_y < height; ++grid_y) {
            for (int grid_x = 0; grid_x < width; ++grid_x) {
                const int index = grid_y * width + grid_x;

                const double x = gridIndexToWorldX(grid_x, *msg);
                const double y = gridIndexToWorldY(grid_y, *msg);

                const double distance = std::sqrt(x * x + y * y);

                if (distance < min_check_range_ || distance > max_check_range_) continue;

                const double angle_deg = radToDeg(std::atan2(y, x));
                const int bin_index    = angleToBinIndex(angle_deg);

                if (bin_index < 0 || bin_index >= static_cast<int>(bins.size())) continue;

                const int8_t cell_value = msg->data[static_cast<std::size_t>(index)];

                updateBinStats(
                    bins[static_cast<std::size_t>(bin_index)],
                    cell_value,
                    distance
                );

                analyzed_cell_count++;

                if (cell_value == static_cast<int8_t>(unknown_value_)) total_unknown_count++;
                else if (cell_value == static_cast<int8_t>(free_value_)) total_free_count++;
                else if (cell_value == static_cast<int8_t>(occupied_value_)) total_occupied_count++;
                else total_unknown_count++;     // 알 수 없는 값은 unknown으로 통계
            }
        }

        // =========================================
        // [4] bin finalize : ratio/score/candidate
        // =========================================
        uint32_t candidate_bin_count = 0;
        uint32_t blocked_bin_count   = 0;

        for (auto &bin : bins) {
            finalizeBinStats(bin);

            if (bin.candidate) candidate_bin_count++;
            if (bin.blocked)   blocked_bin_count++;
        }

        // ==================================
        // [5] condidate gap 추출 및 선택
        // ===================================
        const auto gaps = extractGaps(bins);

        GapCandidate selected_gap;
        const bool path_available = selectBestGap(gaps, selected_gap);

        // =====================
        // [6] 출력 메시지 생성
        // =====================
        robot_interfaces::msg::FreeSpaceModel out;
        out.header = msg->header;

        out.path_available = path_available;
        out.risk_level = determineRiskLevel(path_available, selected_gap);

        out.bin_count = static_cast<uint32_t>(bins.size());
        out.candidate_bin_count = candidate_bin_count;
        out.blocked_bin_count = blocked_bin_count;
        out.analyzed_cell_count = analyzed_cell_count;

        out.total_unknown_count = total_unknown_count;
        out.total_free_count = total_free_count;
        out.total_occupied_count = total_occupied_count;

        if (analyzed_cell_count > 0) {
            const double total = static_cast<double>(analyzed_cell_count);

            out.total_unknown_ratio =
                static_cast<float>(static_cast<double>(total_unknown_count) / total);

            out.total_free_ratio =
                static_cast<float>(static_cast<double>(total_free_count) / total);

            out.total_occupied_ratio =
                static_cast<float>(static_cast<double>(total_occupied_count) / total);
        } else {
            out.total_unknown_ratio = 1.0f;
            out.total_free_ratio = 0.0f;
            out.total_occupied_ratio = 0.0f;
        }

        out.candidate_gap_count = static_cast<uint32_t>(gaps.size());

        for (const auto & gap : gaps) {
            const bool selected =
                path_available &&
                gap.start_bin == selected_gap.start_bin &&
                gap.end_bin == selected_gap.end_bin;

            out.candidate_gaps.push_back(toGapMsg(gap, selected));
        }

        if (path_available) {
            out.selected_gap = toGapMsg(selected_gap, true);

            out.best_heading_angle_deg =
                static_cast<float>(selected_gap.center_angle_deg);

            out.best_heading_angle_rad =
                static_cast<float>(degToRad(selected_gap.center_angle_deg));

            out.best_clearance =
                static_cast<float>(selected_gap.min_clearance);

            out.best_score =
                static_cast<float>(selected_gap.score);

            out.best_free_ratio =
                static_cast<float>(selected_gap.free_ratio);

            out.best_unknown_ratio =
                static_cast<float>(selected_gap.unknown_ratio);

            out.best_occupied_ratio =
                static_cast<float>(selected_gap.occupied_ratio);
        } else {
            out.best_heading_angle_deg = 0.0f;
            out.best_heading_angle_rad = 0.0f;

            out.best_clearance = 0.0f;
            out.best_score = 0.0f;

            out.best_free_ratio = 0.0f;
            out.best_unknown_ratio = 1.0f;
            out.best_occupied_ratio = 0.0f;
        }

        // ===============
        // [7] publish
        // ===============
        model_pub_->publish(out);

        // ===============
        // [8] Debug 로그
        // ===============
        RCLCPP_INFO_THROTTLE(
            this->get_logger(),
            *this->get_clock(),
            1000,
            "\nfree space model"
            "\npath_available       : %s"
            "\nrisk_level           : %u"
            "\nbest_heading         : %.1f deg"
            "\nbest_clearance       : %.2f m"
            "\nbest_score           : %.2f"
            "\nbest_ratio           : free=%.2f unknown=%.2f occupied=%.2f"
            "\ngaps                 : %u"
            "\nbins                 : total=%u candidate=%u blocked=%u"
            "\ncells                : analyzed=%u free=%u unknown=%u occupied=%u"
            "\ntotal_ratio          : free=%.2f unknown=%.2f occupied=%.2f",
            out.path_available ? "true" : "false",
            out.risk_level,
            out.best_heading_angle_deg,
            out.best_clearance,
            out.best_score,
            out.best_free_ratio,
            out.best_unknown_ratio,
            out.best_occupied_ratio,
            out.candidate_gap_count,
            out.bin_count,
            out.candidate_bin_count,
            out.blocked_bin_count,
            out.analyzed_cell_count,
            out.total_free_count,
            out.total_unknown_count,
            out.total_occupied_count,
            out.total_free_ratio,
            out.total_unknown_ratio,
            out.total_occupied_ratio
        );
    }
}

int main(int argc, char ** argv)
{
    rclcpp::init(argc, argv);

    auto node = std::make_shared<lidar_perception::FreeSpaceModelNode>();
    rclcpp::spin(node);

    rclcpp::shutdown();

    return 0;
}