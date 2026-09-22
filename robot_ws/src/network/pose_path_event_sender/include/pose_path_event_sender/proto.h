/**
 * proto.h — Jetson ↔ BridgeDaemon UDP 와이어 프로토콜
 *
 * 패킷 구조:
 *   [ PktHeader (24B) ][ payload ]
 *
 * Odom/Cmd/Heartbeat는 단일 패킷으로 전송.
 * Image/LiDAR는 MTU(1400B) 단위로 분할 전송.
 */

#pragma once

#include <stdint.h>

/* ─── 데이터 타입 ────────────────────────────────────────────── */
#define PKT_TYPE_IMAGE         0x01
#define PKT_TYPE_LIDAR         0x02
#define PKT_TYPE_ODOM          0x03
#define PKT_TYPE_CMD           0x04   /* RPi5 → Jetson command */
#define PKT_TYPE_CMD_ACK       0x05
#define PKT_TYPE_STATE         0x06
#define PKT_TYPE_EVENT         0x07
#define PKT_TYPE_MISSION       0x08
#define PKT_TYPE_HEALTH        0x09
#define PKT_TYPE_CAPABILITY    0x0A
#define PKT_TYPE_GLOBAL_PATH   0x0B
#define PKT_TYPE_PATH_PROGRESS 0x0C

#define GLOBAL_PATH_MAX_WAYPOINTS 40
#define GLOBAL_PATH_PAYLOAD_MAX   (4 + (GLOBAL_PATH_MAX_WAYPOINTS * 16))
#define ROBOT_ID_STR_LEN          16

/* ─── 커맨드 타입 ────────────────────────────────────────────── */
#define CMD_TYPE_ESTOP          0x01   /* 비상 정지 (즉시) */
#define CMD_TYPE_STOP           0x02   /* 일반 정지 */
#define CMD_TYPE_MOVE           0x03   /* 속도 지령 */
#define CMD_TYPE_HEARTBEAT      0x04   /* RPi → Jetson 생존 확인 */
#define CMD_TYPE_SET_MODE       0x05
#define CMD_TYPE_RETURN_HOME    0x06
#define CMD_TYPE_SET_WAYPOINT   0x07
#define CMD_TYPE_SET_ROUTE      0x08
#define CMD_TYPE_START_MISSION  0x09
#define CMD_TYPE_PAUSE_MISSION  0x0A
#define CMD_TYPE_CANCEL_MISSION 0x0B
#define CMD_TYPE_SENSOR_CTRL    0x0C

#define CMD_FLAG_REQUIRES_ACK   0x01
#define CMD_FLAG_BROADCAST      0x02

#define CMD_PRIORITY_LOW        1
#define CMD_PRIORITY_NORMAL     3
#define CMD_PRIORITY_HIGH       6
#define CMD_PRIORITY_CRITICAL   9

/* ─── 포트 ───────────────────────────────────────────────────── */
#define BRIDGE_PORT      9000   /* Jetson → RPi5 수신 포트 */
#define JETSON_CMD_PORT  9001   /* RPi5 → Jetson 커맨드 포트 */
#define PC_LINK_PORT     9002   /* PC ↔ RPi5 통신 포트 */

/* ─── 크기 제한 ─────────────────────────────────────────────── */
#define PROTO_MTU        1400
#define PROTO_MAX_PAYLOAD \
    ((GLOBAL_PATH_PAYLOAD_MAX > PROTO_MTU) ? GLOBAL_PATH_PAYLOAD_MAX : PROTO_MTU)
#define PROTO_PKT_MAX    (sizeof(PktHeader) + PROTO_MAX_PAYLOAD)

/* ─── 공통 헤더 (모든 패킷, 24B) ────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  type;            /* PKT_TYPE_* */
    uint8_t  robot_id;        /* 로봇 식별자 (0 ~ MAX_ROBOTS-1) */
    uint16_t frag_idx;
    uint16_t frag_total;
    uint16_t payload_len;
    uint32_t frame_id;
    uint32_t payload_offset;
    uint64_t timestamp_us;
} PktHeader;

/* ─── Odom 페이로드 ─────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    float x;
    float y;
    float theta;
    float vx;
    float vy;
    float omega;
    float bus_voltage;
} OdomPayload;

/* ─── 커맨드 페이로드 (RPi5 → Jetson) ──────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  cmd_type;
    float    vx;
    float    vy;
    float    omega;
    uint32_t seq;
} CmdPayload;

/* ─── 인입 커맨드 패킷 (Qt Unix Socket / PC UDP:9002 공용) ───── */
typedef struct __attribute__((packed)) {
    uint8_t  robot_id;
    uint8_t  cmd_type;
    float    vx;
    float    vy;
    float    omega;
    uint32_t seq;
} CmdPacket;

typedef struct __attribute__((packed)) {
    uint8_t  robot_id;
    uint8_t  command_type;
    uint8_t  priority;
    uint8_t  flags;
    uint32_t command_id;
    uint32_t seq;
    uint64_t timestamp_us;
    uint32_t payload_len;
} CommandEnvelope;

typedef struct __attribute__((packed)) {
    uint32_t command_id;
    uint32_t seq;
    uint8_t  cmd_type;
    uint8_t  status;
    uint16_t reserved;
    uint64_t timestamp_us;
} CmdAckPayload;

/* ─── 이벤트 페이로드 ───────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  severity;
    uint8_t  reserved;
    uint16_t event_type;
    uint32_t code;
    char     message[96];
} EventPayload;

/* ─── RPi5 → PC 상태 패킷 (UDP, 주기 송신) ─────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  robot_id;
    uint8_t  connected;      /* 0 or 1 */
    float    x;
    float    y;
    float    theta;
    uint32_t odom_seq;
    uint32_t img_drop;
    uint32_t lidar_drop;
    uint64_t timestamp_us;
} PcStatusPacket;

typedef struct __attribute__((packed)) {
    uint8_t  robot_id;
    uint8_t  connected;
    uint8_t  mode;
    uint8_t  fault_level;
    float    x;
    float    y;
    float    theta;
    float    vx;
    float    vy;
    float    omega;
    float    battery_percent;
    float    link_rtt_ms;
    float    image_fps;
    float    lidar_fps;
    uint32_t odom_seq;
    uint32_t img_drop;
    uint32_t lidar_drop;
    uint32_t event_seq;
    uint64_t last_rx_us;
    uint64_t timestamp_us;
} PcStatusPacketV2;

/* ─── GlobalPath 페이로드 ───────────────────────────────────── */
typedef struct __attribute__((packed)) {
    float x;
    float y;
    float z;
    float yaw;
} PathWaypoint;

typedef struct __attribute__((packed)) {
    uint8_t      count;      /* 0..GLOBAL_PATH_MAX_WAYPOINTS */
    uint8_t      reserved[3];
    PathWaypoint waypoints[GLOBAL_PATH_MAX_WAYPOINTS];
} GlobalPathPayload;

typedef struct __attribute__((packed)) {
    uint8_t      robot_id;
    uint8_t      count;
    uint16_t     reserved;
    uint32_t     path_seq;
    uint64_t     timestamp_us;
    PathWaypoint waypoints[GLOBAL_PATH_MAX_WAYPOINTS];
} PcGlobalPathPacket;

/* ─── PathProgress 페이로드 ─────────────────────────────────── */
typedef struct __attribute__((packed)) {
    char     robot_id[ROBOT_ID_STR_LEN]; /* "spot_01" */
    uint64_t timestamp_us;               /* header.stamp → us */
    uint8_t  path_ok;                    /* path_received && path_valid */
    uint8_t  pose_ok;                    /* pose_received && pose_valid */
    uint8_t  goal_reached;
    uint8_t  reserved;
    uint32_t waypoint_idx;               /* target_index */
    uint32_t total_waypoints;
    float    mission_progress;           /* 0.0 ~ 1.0 */
    uint32_t nearest_index;
    float    distance_to_nearest_m;
    float    nearest_x_m;
    float    nearest_y_m;
    float    target_x_m;
    float    target_y_m;
    float    target_heading_rad;
    float    heading_error_rad;
    float    distance_to_target_m;
    float    distance_to_goal_m;
} PathProgressPayload;

static inline int proto_packet_type_valid(uint8_t type) {
    return type == PKT_TYPE_IMAGE || type == PKT_TYPE_LIDAR ||
           type == PKT_TYPE_ODOM || type == PKT_TYPE_CMD ||
           type == PKT_TYPE_CMD_ACK || type == PKT_TYPE_STATE ||
           type == PKT_TYPE_EVENT || type == PKT_TYPE_MISSION ||
           type == PKT_TYPE_HEALTH || type == PKT_TYPE_CAPABILITY ||
           type == PKT_TYPE_GLOBAL_PATH || type == PKT_TYPE_PATH_PROGRESS;
}

static inline int proto_validate_header(const PktHeader *hdr,
                                        uint16_t actual_payload_len) {
    if (!proto_packet_type_valid(hdr->type)) return 0;
    if (hdr->payload_len != actual_payload_len) return 0;
    if (hdr->frag_total == 0) return 0;
    if (hdr->frag_idx >= hdr->frag_total) return 0;
    if (hdr->type == PKT_TYPE_GLOBAL_PATH) {
        if (actual_payload_len > GLOBAL_PATH_PAYLOAD_MAX) return 0;
    } else if (actual_payload_len > PROTO_MTU) {
        return 0;
    }
    if ((hdr->type == PKT_TYPE_ODOM || hdr->type == PKT_TYPE_CMD ||
         hdr->type == PKT_TYPE_CMD_ACK || hdr->type == PKT_TYPE_GLOBAL_PATH ||
         hdr->type == PKT_TYPE_PATH_PROGRESS) &&
        (hdr->frag_idx != 0 || hdr->frag_total != 1 ||
         hdr->payload_offset != 0)) return 0;
    return 1;
}