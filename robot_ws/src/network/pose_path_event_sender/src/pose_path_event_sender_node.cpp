#include <rclcpp/rclcpp.hpp>
#include <nav_msgs/msg/path.hpp>
#include <robot_interfaces/msg/global_path_waypoints.hpp>
#include <robot_interfaces/msg/localized_robot_pose.hpp>
#include <robot_interfaces/msg/path_progress.hpp>
#include <robot_interfaces/msg/robot_status.hpp>
#include <std_msgs/msg/bool.hpp>

#include <arpa/inet.h>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include "pose_path_event_sender/proto.h"

#define EVENT_SEVERITY_CRITICAL    4
#define EVENT_TYPE_VICTIM_DETECTED 8

using LocalizationPose    = robot_interfaces::msg::LocalizedRobotPose;
using GlobalPathWaypoints = robot_interfaces::msg::GlobalPathWaypoints;
using PathProgressMsg     = robot_interfaces::msg::PathProgress;
using RobotStatusMsg      = robot_interfaces::msg::RobotStatus;
using NavPathMsg          = nav_msgs::msg::Path;
using BoolMsg             = std_msgs::msg::Bool;

static uint64_t monotonic_us()
{
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL
           + static_cast<uint64_t>(ts.tv_nsec) / 1000ULL;
}

struct RobotState {
    std::string name;
    uint8_t     robot_id {0};
    uint64_t    last_pose_us {0};
    uint32_t    odom_seq {0};
    uint32_t    path_seq {0};
    uint32_t    event_seq {0};
    uint32_t    progress_seq {0};
    std::vector<uint8_t> latest_path_packet;
    size_t      latest_path_waypoints {0};
    uint64_t    last_path_rx_us {0};
};

class PosePathEventSender : public rclcpp::Node
{
public:
    PosePathEventSender()
        : Node("pose_path_event_sender")
    {
        declare_parameter<std::vector<std::string>>("robots", {"spot_01:0"});
        declare_parameter<std::string>("rpi5_ip", "192.168.0.13");
        declare_parameter<int>("bridge_port", BRIDGE_PORT);
        declare_parameter<double>("pose_send_hz", 10.0);
        declare_parameter<double>("global_path_send_hz", 1.0);
        declare_parameter<std::string>("pose_topic", "/localization/pose");
        declare_parameter<std::string>("global_path_topic_prefix", "/planning/global_path");
        declare_parameter<std::string>("global_path_message_type", "robot_interfaces/msg/GlobalPathWaypoints");
        declare_parameter<std::string>("person_topic_prefix", "/perception/person_detected");
        declare_parameter<std::string>("progress_topic_prefix", "/navigation/path_progress");
        declare_parameter<std::string>("status_topic", "/control/actuator/status");

        rpi5_ip_ = get_parameter("rpi5_ip").as_string();
        bridge_port_ = static_cast<uint16_t>(get_parameter("bridge_port").as_int());
        pose_topic_ = get_parameter("pose_topic").as_string();
        global_path_topic_prefix_ = strip_trailing_slashes(
            get_parameter("global_path_topic_prefix").as_string());
        global_path_message_type_ = get_parameter("global_path_message_type").as_string();
        if (!uses_custom_path_msg() && !uses_nav_path_msg()) {
            RCLCPP_WARN(get_logger(),
                        "unknown global_path_message_type '%s'; using nav_msgs/Path",
                        global_path_message_type_.c_str());
            global_path_message_type_ = "nav_msgs/Path";
        }
        person_topic_prefix_ = strip_trailing_slashes(
            get_parameter("person_topic_prefix").as_string());
        progress_topic_prefix_ = strip_trailing_slashes(
            get_parameter("progress_topic_prefix").as_string());
        status_topic_ = get_parameter("status_topic").as_string();

        const double hz = get_parameter("pose_send_hz").as_double();
        pose_min_us_ = static_cast<uint64_t>(1000000.0 / hz);
        global_path_send_hz_ = get_parameter("global_path_send_hz").as_double();

        const auto robot_strs = get_parameter("robots").as_string_array();
        for (const auto &s : robot_strs) {
            const size_t colon = s.find(':');
            if (colon == std::string::npos) {
                RCLCPP_ERROR(get_logger(),
                             "robots parameter format error: '%s' (name:id required)",
                             s.c_str());
                continue;
            }

            RobotState rs;
            rs.name = s.substr(0, colon);
            rs.robot_id = static_cast<uint8_t>(std::stoi(s.substr(colon + 1)));
            robots_.push_back(rs);
        }

        if (robots_.empty()) {
            RCLCPP_FATAL(get_logger(), "no robots registered");
            throw std::runtime_error("robots empty");
        }

        udp_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_fd_ < 0) {
            RCLCPP_FATAL(get_logger(), "UDP socket failed: %s", strerror(errno));
            throw std::runtime_error("socket()");
        }

        std::memset(&rpi5_addr_, 0, sizeof(rpi5_addr_));
        rpi5_addr_.sin_family = AF_INET;
        rpi5_addr_.sin_port = htons(bridge_port_);
        if (inet_pton(AF_INET, rpi5_ip_.c_str(), &rpi5_addr_.sin_addr) != 1) {
            RCLCPP_FATAL(get_logger(), "invalid RPi5 IP: %s", rpi5_ip_.c_str());
            throw std::runtime_error("inet_pton()");
        }

        RCLCPP_INFO(get_logger(),
                    "UDP target=%s:%d odom=%.0fHz global_path=%.2fHz robots=%zu",
                    rpi5_ip_.c_str(), bridge_port_, hz,
                    global_path_send_hz_, robots_.size());

        rclcpp::QoS pose_qos {rclcpp::KeepLast(1)};
        pose_qos.best_effort().durability_volatile();
        pose_sub_ = create_subscription<LocalizationPose>(
            pose_topic_, pose_qos,
            [this](LocalizationPose::SharedPtr msg) { on_pose(msg); });

        rclcpp::QoS status_qos {rclcpp::KeepLast(1)};
        status_qos.best_effort().durability_volatile();
        status_sub_ = create_subscription<RobotStatusMsg>(
            status_topic_, status_qos,
            [this](RobotStatusMsg::SharedPtr msg) { on_robot_status(msg); });

        for (size_t i = 0; i < robots_.size(); ++i) {
            const auto &rs = robots_[i];

            const std::string path_topic = global_path_topic_prefix_ + "/" + rs.name;
            rclcpp::QoS path_qos {rclcpp::KeepLast(1)};
            path_qos.reliable().transient_local();
            rclcpp::QoS path_live_qos {rclcpp::KeepLast(1)};
            path_live_qos.best_effort().durability_volatile();
            if (uses_custom_path_msg()) {
                path_subs_.push_back(create_subscription<GlobalPathWaypoints>(
                    path_topic, path_qos,
                    [this, i](GlobalPathWaypoints::SharedPtr msg) { on_global_path(msg, i); }));
                path_live_subs_.push_back(create_subscription<GlobalPathWaypoints>(
                    path_topic, path_live_qos,
                    [this, i](GlobalPathWaypoints::SharedPtr msg) { on_global_path(msg, i); }));
            }
            if (uses_nav_path_msg()) {
                nav_path_subs_.push_back(create_subscription<NavPathMsg>(
                    path_topic, path_qos,
                    [this, i](NavPathMsg::SharedPtr msg) { on_nav_path(msg, i); }));
                nav_path_live_subs_.push_back(create_subscription<NavPathMsg>(
                    path_topic, path_live_qos,
                    [this, i](NavPathMsg::SharedPtr msg) { on_nav_path(msg, i); }));
            }

            const std::string person_topic = person_topic_prefix_ + "/" + rs.name;
            rclcpp::QoS person_qos {rclcpp::KeepLast(1)};
            person_qos.reliable().durability_volatile();
            person_subs_.push_back(create_subscription<BoolMsg>(
                person_topic, person_qos,
                [this, i](BoolMsg::SharedPtr msg) { on_person_detected(msg, i); }));

            const std::string progress_topic = progress_topic_prefix_ + "/" + rs.name;
            rclcpp::QoS progress_qos {rclcpp::KeepLast(1)};
            progress_qos.best_effort().durability_volatile();
            progress_subs_.push_back(create_subscription<PathProgressMsg>(
                progress_topic, progress_qos,
                [this, i](PathProgressMsg::SharedPtr msg) { on_path_progress(msg, i); }));

            RCLCPP_INFO(get_logger(), "registered robot: %s (robot_id=%u)",
                        rs.name.c_str(), rs.robot_id);
            RCLCPP_INFO(get_logger(), "  pose=%s path=%s type=%s person=%s progress=%s",
                        pose_topic_.c_str(), path_topic.c_str(),
                        global_path_message_type_.c_str(), person_topic.c_str(),
                        progress_topic.c_str());
        }

        if (global_path_send_hz_ > 0.0) {
            const auto period = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::duration<double>(1.0 / global_path_send_hz_));
            global_path_timer_ = create_wall_timer(
                period,
                [this]() { on_global_path_timer(); });
        } else {
            RCLCPP_WARN(get_logger(),
                        "global_path_send_hz <= 0; cached global path resend disabled");
        }
    }

    ~PosePathEventSender() override
    {
        if (udp_fd_ >= 0) {
            close(udp_fd_);
        }
    }

private:
    static std::string strip_trailing_slashes(std::string value)
    {
        while (value.size() > 1 && value.back() == '/') {
            value.pop_back();
        }
        return value;
    }

    bool uses_custom_path_msg() const
    {
        return global_path_message_type_ == "auto"
               || global_path_message_type_ == "robot_interfaces/GlobalPathWaypoints"
               || global_path_message_type_ == "robot_interfaces/msg/GlobalPathWaypoints";
    }

    bool uses_nav_path_msg() const
    {
        return global_path_message_type_ == "auto"
               || global_path_message_type_ == "nav_msgs/Path"
               || global_path_message_type_ == "nav_msgs/msg/Path";
    }

    void on_robot_status(const RobotStatusMsg::SharedPtr &msg)
    {
        latest_bus_voltage_ = std::isfinite(msg->bus_voltage) ? msg->bus_voltage : 0.0f;
    }

    void on_pose(const LocalizationPose::SharedPtr &msg)
    {
        size_t idx = robots_.size();
        for (size_t i = 0; i < robots_.size(); ++i) {
            if (robots_[i].name == msg->robot_id) {
                idx = i;
                break;
            }
        }
        if (idx == robots_.size() && robots_.size() == 1) {
            idx = 0;
            RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 3000,
                                 "pose robot_id '%s' did not match '%s'; using the only configured robot",
                                 msg->robot_id.c_str(), robots_[0].name.c_str());
        }
        if (idx == robots_.size()) {
            RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 3000,
                                 "pose ignored: unknown robot_id '%s'",
                                 msg->robot_id.c_str());
            return;
        }

        auto &rs = robots_[idx];
        const uint64_t now = monotonic_us();
        if (now - rs.last_pose_us < pose_min_us_) {
            return;
        }
        rs.last_pose_us = now;

        constexpr size_t BUF = sizeof(PktHeader) + sizeof(OdomPayload);
        uint8_t buf[BUF];
        std::memset(buf, 0, BUF);

        auto *hdr = reinterpret_cast<PktHeader *>(buf);
        hdr->type = PKT_TYPE_ODOM;
        hdr->robot_id = rs.robot_id;
        hdr->frag_idx = 0;
        hdr->frag_total = 1;
        hdr->payload_len = static_cast<uint16_t>(sizeof(OdomPayload));
        hdr->frame_id = rs.odom_seq++;
        hdr->payload_offset = 0;
        hdr->timestamp_us = now;

        auto *odom = reinterpret_cast<OdomPayload *>(buf + sizeof(PktHeader));
        odom->x = static_cast<float>(msg->x_m);
        odom->y = static_cast<float>(msg->y_m);
        odom->theta = static_cast<float>(msg->yaw_rad);
        odom->vx = 0.0f;
        odom->vy = 0.0f;
        odom->omega = 0.0f;
        odom->bus_voltage = latest_bus_voltage_;

        send_udp(buf, BUF);
        RCLCPP_INFO_THROTTLE(get_logger(), *get_clock(), 350,
                             "pose sent: %s seq=%u x=%.3f y=%.3f yaw=%.3f",
                             rs.name.c_str(), rs.odom_seq - 1,
                             odom->x, odom->y, odom->theta);
    }

    void on_global_path(const GlobalPathWaypoints::SharedPtr &msg, size_t idx)
    {
        auto &rs = robots_[idx];

        const size_t source_n = msg->waypoints.size();
        size_t n = source_n;
        if (source_n == 0) {
            RCLCPP_WARN(get_logger(), "%s global_path empty, ignored", rs.name.c_str());
            return;
        }
        if (source_n > GLOBAL_PATH_MAX_WAYPOINTS) {
            RCLCPP_WARN(get_logger(), "%s global_path sampled: %zu -> %d",
                        rs.name.c_str(), n, GLOBAL_PATH_MAX_WAYPOINTS);
            n = GLOBAL_PATH_MAX_WAYPOINTS;
        }

        rs.latest_path_packet.assign(global_path_packet_size(), 0);
        rs.latest_path_waypoints = n;
        rs.last_path_rx_us = monotonic_us();

        auto *hdr = reinterpret_cast<PktHeader *>(rs.latest_path_packet.data());
        hdr->type = PKT_TYPE_GLOBAL_PATH;
        hdr->robot_id = rs.robot_id;
        hdr->frag_idx = 0;
        hdr->frag_total = 1;
        hdr->payload_len = static_cast<uint16_t>(sizeof(GlobalPathPayload));
        hdr->payload_offset = 0;

        auto *path = reinterpret_cast<GlobalPathPayload *>(
            rs.latest_path_packet.data() + sizeof(PktHeader));
        path->count = static_cast<uint8_t>(n);
        for (size_t i = 0; i < n; ++i) {
            size_t src_i = i;
            if (source_n > n && n > 1) {
                src_i = (i * (source_n - 1) + (n - 1) / 2) / (n - 1);
            }
            path->waypoints[i].x = msg->waypoints[src_i].x_m;
            path->waypoints[i].y = msg->waypoints[src_i].y_m;
            path->waypoints[i].z = msg->waypoints[src_i].z_m;
            path->waypoints[i].yaw = msg->waypoints[src_i].yaw_rad;
        }

        send_cached_global_path(rs, "rx");
    }

    static float yaw_from_quaternion(double x, double y, double z, double w)
    {
        return static_cast<float>(
            std::atan2(2.0 * (w * z + x * y),
                       1.0 - 2.0 * (y * y + z * z)));
    }

    void on_nav_path(const NavPathMsg::SharedPtr &msg, size_t idx)
    {
        auto &rs = robots_[idx];

        const size_t source_n = msg->poses.size();
        size_t n = source_n;
        if (source_n == 0) {
            RCLCPP_WARN(get_logger(), "%s nav_path empty, ignored", rs.name.c_str());
            return;
        }
        if (source_n > GLOBAL_PATH_MAX_WAYPOINTS) {
            RCLCPP_WARN(get_logger(), "%s nav_path sampled: %zu -> %d",
                        rs.name.c_str(), n, GLOBAL_PATH_MAX_WAYPOINTS);
            n = GLOBAL_PATH_MAX_WAYPOINTS;
        }

        rs.latest_path_packet.assign(global_path_packet_size(), 0);
        rs.latest_path_waypoints = n;
        rs.last_path_rx_us = monotonic_us();

        auto *hdr = reinterpret_cast<PktHeader *>(rs.latest_path_packet.data());
        hdr->type = PKT_TYPE_GLOBAL_PATH;
        hdr->robot_id = rs.robot_id;
        hdr->frag_idx = 0;
        hdr->frag_total = 1;
        hdr->payload_len = static_cast<uint16_t>(sizeof(GlobalPathPayload));
        hdr->payload_offset = 0;

        auto *path = reinterpret_cast<GlobalPathPayload *>(
            rs.latest_path_packet.data() + sizeof(PktHeader));
        path->count = static_cast<uint8_t>(n);
        for (size_t i = 0; i < n; ++i) {
            size_t src_i = i;
            if (source_n > n && n > 1) {
                src_i = (i * (source_n - 1) + (n - 1) / 2) / (n - 1);
            }

            const auto &pose = msg->poses[src_i].pose;
            path->waypoints[i].x = static_cast<float>(pose.position.x);
            path->waypoints[i].y = static_cast<float>(pose.position.y);
            path->waypoints[i].z = static_cast<float>(pose.position.z);
            path->waypoints[i].yaw = yaw_from_quaternion(
                pose.orientation.x, pose.orientation.y,
                pose.orientation.z, pose.orientation.w);
        }

        send_cached_global_path(rs, "rx");
    }

    static constexpr size_t global_path_packet_size()
    {
        return sizeof(PktHeader) + sizeof(GlobalPathPayload);
    }

    void on_global_path_timer()
    {
        for (auto &rs : robots_) {
            if (rs.latest_path_packet.empty()) {
                continue;
            }
            send_cached_global_path(rs, "timer");
        }
    }

    void send_cached_global_path(RobotState &rs, const char *source)
    {
        if (rs.latest_path_packet.size() != global_path_packet_size()) {
            return;
        }

        auto *hdr = reinterpret_cast<PktHeader *>(rs.latest_path_packet.data());
        hdr->frame_id = rs.path_seq++;
        hdr->timestamp_us = monotonic_us();

        send_udp(rs.latest_path_packet.data(), rs.latest_path_packet.size());
        RCLCPP_INFO_THROTTLE(
            get_logger(), *get_clock(), 1000,
            "global_path sent: %s source=%s seq=%u waypoints=%zu",
            rs.name.c_str(), source, rs.path_seq - 1, rs.latest_path_waypoints);
    }

    void on_person_detected(const BoolMsg::SharedPtr &msg, size_t idx)
    {
        auto &rs = robots_[idx];

        constexpr size_t BUF = sizeof(PktHeader) + sizeof(EventPayload);
        uint8_t buf[BUF];
        std::memset(buf, 0, BUF);

        auto *hdr = reinterpret_cast<PktHeader *>(buf);
        hdr->type = PKT_TYPE_EVENT;
        hdr->robot_id = rs.robot_id;
        hdr->frag_idx = 0;
        hdr->frag_total = 1;
        hdr->payload_len = static_cast<uint16_t>(sizeof(EventPayload));
        hdr->frame_id = rs.event_seq++;
        hdr->payload_offset = 0;
        hdr->timestamp_us = monotonic_us();

        auto *ev = reinterpret_cast<EventPayload *>(buf + sizeof(PktHeader));
        ev->severity = EVENT_SEVERITY_CRITICAL;
        ev->event_type = EVENT_TYPE_VICTIM_DETECTED;
        ev->code = msg->data ? 1u : 0u;
        std::snprintf(ev->message, sizeof(ev->message),
                      "person %s", msg->data ? "detected" : "cleared");

        send_udp(buf, BUF);
        RCLCPP_INFO(get_logger(), "person_detected sent: %s seq=%u detected=%d",
                    rs.name.c_str(), rs.event_seq - 1, msg->data ? 1 : 0);
    }

    void on_path_progress(const PathProgressMsg::SharedPtr &msg, size_t idx)
    {
        auto &rs = robots_[idx];

        constexpr size_t BUF = sizeof(PktHeader) + sizeof(PathProgressPayload);
        uint8_t buf[BUF];
        std::memset(buf, 0, BUF);

        auto *hdr = reinterpret_cast<PktHeader *>(buf);
        hdr->type = PKT_TYPE_PATH_PROGRESS;
        hdr->robot_id = rs.robot_id;
        hdr->frag_idx = 0;
        hdr->frag_total = 1;
        hdr->payload_len = static_cast<uint16_t>(sizeof(PathProgressPayload));
        hdr->frame_id = rs.progress_seq++;
        hdr->payload_offset = 0;
        hdr->timestamp_us = monotonic_us();

        const bool path_ok = msg->path_received && msg->path_valid;
        const bool pose_ok = msg->pose_received && msg->pose_valid;
        const bool progress_ok = path_ok && pose_ok && msg->total_waypoints > 0;
        if (!progress_ok && !msg->goal_reached) {
            RCLCPP_WARN_THROTTLE(
                get_logger(), *get_clock(), 3000,
                "path_progress ignored: %s path_ok=%d pose_ok=%d total=%u target=%u progress=%.2f",
                rs.name.c_str(), path_ok ? 1 : 0, pose_ok ? 1 : 0,
                msg->total_waypoints, msg->target_index, msg->progress_ratio);
            return;
        }

        auto *pp = reinterpret_cast<PathProgressPayload *>(buf + sizeof(PktHeader));
        strncpy(pp->robot_id, rs.name.c_str(), ROBOT_ID_STR_LEN - 1);
        pp->robot_id[ROBOT_ID_STR_LEN - 1] = '\0';
        pp->timestamp_us = static_cast<uint64_t>(msg->header.stamp.sec) * 1000000ULL
                           + static_cast<uint64_t>(msg->header.stamp.nanosec) / 1000ULL;
        pp->path_ok = path_ok ? 1u : 0u;
        pp->pose_ok = pose_ok ? 1u : 0u;
        pp->goal_reached = msg->goal_reached ? 1u : 0u;
        pp->waypoint_idx = progress_ok ? msg->target_index : 0u;
        pp->total_waypoints = msg->total_waypoints;
        pp->mission_progress = msg->goal_reached ? 1.0f
                             : progress_ok ? msg->progress_ratio : 0.0f;
        pp->nearest_index = msg->nearest_index;
        pp->distance_to_nearest_m = static_cast<float>(msg->distance_to_nearest_m);
        pp->nearest_x_m = static_cast<float>(msg->nearest_x_m);
        pp->nearest_y_m = static_cast<float>(msg->nearest_y_m);
        pp->target_x_m = static_cast<float>(msg->target_x_m);
        pp->target_y_m = static_cast<float>(msg->target_y_m);
        pp->target_heading_rad = static_cast<float>(msg->target_heading_rad);
        pp->heading_error_rad = static_cast<float>(msg->heading_error_rad);
        pp->distance_to_target_m = static_cast<float>(msg->distance_to_target_m);
        pp->distance_to_goal_m = static_cast<float>(msg->distance_to_goal_m);

        send_udp(buf, BUF);
        RCLCPP_INFO_THROTTLE(
            get_logger(), *get_clock(), 350,
            "path_progress sent: %s seq=%u progress=%.2f wp=%u/%u goal=%d "
            "target=(%.2f, %.2f) d_target=%.2f d_goal=%.2f d_path=%.2f heading_err=%.2f",
            rs.name.c_str(), rs.progress_seq - 1,
            pp->mission_progress, pp->waypoint_idx, pp->total_waypoints,
            pp->goal_reached,
            pp->target_x_m, pp->target_y_m,
            pp->distance_to_target_m, pp->distance_to_goal_m,
            pp->distance_to_nearest_m,
            pp->heading_error_rad);
    }

    void send_udp(const uint8_t *buf, size_t len)
    {
        const ssize_t sent = sendto(
            udp_fd_, buf, len, 0,
            reinterpret_cast<const sockaddr *>(&rpi5_addr_),
            sizeof(rpi5_addr_));

        if (sent < 0) {
            RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 1000,
                                  "sendto failed: %s", strerror(errno));
        }
    }

    int udp_fd_ {-1};
    struct sockaddr_in rpi5_addr_ {};

    std::string rpi5_ip_ {"192.168.0.13"};
    std::string pose_topic_ {"/localization/mock_pose"};
    std::string global_path_topic_prefix_ {"/planning/global_path"};
    std::string global_path_message_type_ {"robot_interfaces/msg/GlobalPathWaypoints"};
    std::string person_topic_prefix_ {"/perception/person_detected"};
    std::string progress_topic_prefix_ {"/navigation/path_progress"};
    std::string status_topic_ {"/control/actuator/status"};
    uint16_t    bridge_port_ {9000};
    uint64_t    pose_min_us_ {100000};
    double      global_path_send_hz_ {1.0};
    float       latest_bus_voltage_ {0.0f};

    std::vector<RobotState> robots_;

    rclcpp::Subscription<LocalizationPose>::SharedPtr pose_sub_;
    rclcpp::Subscription<RobotStatusMsg>::SharedPtr status_sub_;
    std::vector<rclcpp::Subscription<GlobalPathWaypoints>::SharedPtr> path_subs_;
    std::vector<rclcpp::Subscription<GlobalPathWaypoints>::SharedPtr> path_live_subs_;
    std::vector<rclcpp::Subscription<NavPathMsg>::SharedPtr> nav_path_subs_;
    std::vector<rclcpp::Subscription<NavPathMsg>::SharedPtr> nav_path_live_subs_;
    std::vector<rclcpp::Subscription<BoolMsg>::SharedPtr> person_subs_;
    std::vector<rclcpp::Subscription<PathProgressMsg>::SharedPtr> progress_subs_;
    rclcpp::TimerBase::SharedPtr global_path_timer_;
};

int main(int argc, char *argv[])
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<PosePathEventSender>());
    rclcpp::shutdown();
    return 0;
}