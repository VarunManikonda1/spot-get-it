/**
 * shm_def.h — RPi5 공유 메모리 레이아웃
 *
 * BridgeDaemon(writer)과 QtApp(reader)이 이 헤더 하나를 공유한다.
 *
 * 다중 로봇: 로봇마다 독립된 SHM 파일을 생성한다.
 *   /robot_bridge_0, /robot_bridge_1, ... /robot_bridge_(MAX_ROBOTS-1)
 *
 * 동기화 전략:
 *   Image / LiDAR : atomic index swap (Triple Buffer)
 *                   → Writer와 Reader가 서로 다른 슬롯 → lock 불필요
 *   Odom          : pthread_rwlock (작고 단순한 데이터)
 *   Meta          : pthread_rwlock
 *
 * 알림:
 *   sem_t img_sem / lidar_sem → BridgeDaemon이 post, Qt가 wait
 *   (eventfd 대신 POSIX 세마포어: SHM 안에 올려서 fd 전달 불필요)
 */

#pragma once

#include <stdint.h>
#ifdef __cplusplus
#include <atomic>
#define ATOMIC_UINT8 std::atomic<uint8_t>
#define ATOMIC_INT std::atomic<int>
#else
#include <stdatomic.h>
#define ATOMIC_UINT8 _Atomic uint8_t
#define ATOMIC_INT _Atomic int
#endif
#include <pthread.h>
#include <semaphore.h>

#include "proto.h"

/* ─── 다중 로봇 ─────────────────────────────────────────────── */
#define MAX_ROBOTS        10
#define SHM_NAME_FMT      "/robot_bridge_%d"   /* sprintf용 포맷 */
#define SHM_MAGIC         0x42524745u          /* "BRGE" */
#define SHM_VERSION       7

/* ─── 크기 상수 ─────────────────────────────────────────────── */
#define IMG_SLOT_SIZE     (200 * 1024)   /* 200KB: JPEG 여유치 */
#define IMG_SLOTS         3              /* Triple Buffer */

#define LIDAR_MAX_PTS     8192           /* 3D: VLP-16 1/4 decimation(~7200pts) 여유 */
#define LIDAR_SLOTS       3              /* Triple Buffer (3D 프레임 여유) */
#define EVENT_LOG_SIZE    1024
#define SHM_CMD_QUEUE_SIZE 64

typedef struct {
    CmdPacket cmd;
    uint8_t   priority;
    uint8_t   flags;
    uint16_t  reserved;
    uint64_t  enqueue_us;
} ShmCmdEntry;

typedef struct {
    pthread_mutex_t mu;          /* process-shared: Qt producer, bridge consumer */
    ATOMIC_INT      head;
    ATOMIC_INT      tail;
    ATOMIC_INT      count;
    ATOMIC_INT      write_seq;   /* diagnostic/wakeup generation counter */
    ATOMIC_INT      drop_count;
    ShmCmdEntry     entries[SHM_CMD_QUEUE_SIZE];
} ShmCmdQueue;

/* ─── 이미지 슬롯 ────────────────────────────────────────────── */
typedef struct {
    uint32_t size;            /* 실제 JPEG 크기 */
    uint64_t timestamp_us;    /* 송신 측 타임스탬프 */
    uint32_t frame_id;
    uint8_t  data[IMG_SLOT_SIZE];
} ImgSlot;

/* ─── LiDAR 슬롯 ─────────────────────────────────────────────── */
/*
 * 페이로드 와이어 포맷 (Jetson 송신 측 약속):
 *   [ uint32_t count ]
 *   [ float x0 ][ float y0 ][ float z0 ][ float intensity0 ]
 *   [ float x1 ][ float y1 ][ float z1 ][ float intensity1 ]
 *   ...
 * 포인트당 16바이트, 인터리브 xyzI 포맷.
 * 2D LiDAR는 z=0, intensity=0으로 채워서 동일 포맷 사용.
 */
typedef struct {
    uint32_t count;           /* 유효 포인트 수 */
    uint64_t timestamp_us;
    uint32_t frame_id;
    float    x[LIDAR_MAX_PTS];
    float    y[LIDAR_MAX_PTS];
    float    z[LIDAR_MAX_PTS];         /* 3D: 높이값, 2D: 0.0f */
    float    intensity[LIDAR_MAX_PTS]; /* 반사 강도 (0.0~1.0) */
} LidarSlot;

/* ─── 메타 (연결 상태 / 통계) ────────────────────────────────── */
typedef struct {
    ATOMIC_UINT8 jetson_connected;   /* 0 or 1 — atomic: lock 없이 읽기/쓰기 */
    ATOMIC_INT   pkt_count;          /* jetson_rx 수신마다 증가 — watchdog 연결 감지용 */
    uint32_t img_drop_count;
    uint32_t lidar_drop_count;
    uint32_t odom_seq;
    float    avg_img_latency_us;
} BridgeMeta;

typedef struct {
    uint32_t seq;
    uint64_t updated_us;
    uint8_t  robot_id;
    uint8_t  connected;
    uint8_t  mode;
    uint8_t  fault_level;
    uint8_t  control_owner;
    float    x;
    float    y;
    float    theta;
    float    vx;
    float    vy;
    float    omega;
    float    battery_percent;
    float    voltage;
    float    current;
    float    temperature;
    float    link_rtt_ms;
    float    image_fps;
    float    lidar_fps;
    float    drop_rate;
    uint32_t odom_seq;
    uint32_t mission_id;
    uint32_t waypoint_idx;
    uint32_t total_waypoints;
    float    mission_progress;
    uint8_t  path_ok;
    uint8_t  pose_ok;
    uint8_t  goal_reached;
    uint8_t  path_progress_reserved;
    float    goal_x;
    float    goal_y;
    uint32_t nearest_path_idx;
    float    distance_to_nearest_m;
    float    nearest_x;
    float    nearest_y;
    float    target_x;
    float    target_y;
    float    target_heading;
    float    heading_error;
    float    distance_to_target_m;
    float    distance_to_goal_m;
    uint32_t fault_code;
    char     fault_text[96];
    uint64_t last_rx_us;
    uint64_t last_cmd_ack_us;
} RobotState;

typedef struct {
    uint32_t seq;
    uint64_t updated_us;
    char     robot_id[ROBOT_ID_STR_LEN];
    uint8_t  count;
    uint8_t  reserved[3];
    PathWaypoint waypoints[GLOBAL_PATH_MAX_WAYPOINTS];
} GlobalPathState;

typedef struct {
    uint64_t timestamp_us;
    uint8_t  robot_id;
    uint8_t  severity;
    uint16_t event_type;
    uint32_t code;
    char     message[96];
} RobotEvent;

typedef struct {
    ATOMIC_INT write_seq;
    RobotEvent events[EVENT_LOG_SIZE];
} EventLog;

typedef struct {
    ATOMIC_INT rx_packets;
    ATOMIC_INT tx_commands;
    ATOMIC_INT ack_packets;
    ATOMIC_INT retry_commands;
    ATOMIC_INT dropped_packets;
    uint64_t   last_rx_us;
    uint64_t   last_tx_us;
    uint64_t   last_ack_us;
} BridgeMetrics;

/* ─── 공유 메모리 전체 구조 (로봇 1대분) ────────────────────── */
typedef struct {
    uint32_t shm_magic;
    uint16_t shm_version;
    uint16_t shm_header_size;
    uint32_t shared_data_size;
    uint32_t reserved0;

    /* ── 이미지: Triple Buffer ──────────────────────────────── */
    ImgSlot        img_slots[IMG_SLOTS];
    ATOMIC_INT     img_ready_idx;    /* 최신 슬롯 인덱스 (-1: 아직 없음) */
    sem_t          img_sem;          /* 새 프레임 알림 */

    /* ── LiDAR: Triple Buffer ──────────────────────────────── */
    LidarSlot      lidar_slots[LIDAR_SLOTS];
    ATOMIC_INT     lidar_ready_idx;  /* 최신 슬롯 인덱스 (-1: 아직 없음) */
    sem_t          lidar_sem;        /* 새 스캔 알림 */

    /* ── Odom: rwlock ───────────────────────────────────────── */
    pthread_rwlock_t odom_lock;
    float            odom_x;
    float            odom_y;
    float            odom_theta;
    float            odom_vx;
    float            odom_vy;
    float            odom_omega;
    uint64_t         odom_timestamp_us;
    uint32_t         odom_seq;

    /* ── 메타: rwlock ───────────────────────────────────────── */
    pthread_rwlock_t meta_lock;
    BridgeMeta       meta;

    pthread_rwlock_t state_lock;
    RobotState       state;

    pthread_rwlock_t path_lock;
    GlobalPathState  global_path;

    EventLog         event_log;
    BridgeMetrics    metrics;
    ShmCmdQueue      cmd_queue;

} SharedData;
