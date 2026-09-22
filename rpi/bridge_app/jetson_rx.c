/**
 * jetson_rx.c — Jetson UDP 수신 스레드
 *
 * 역할:
 *   1. UDP 소켓 단 1개에서 모든 로봇의 패킷을 수신
 *   2. 헤더의 robot_id로 대상 로봇 판별 (O(1) 배열 인덱스)
 *   3. 최초 수신 시 Jetson IP를 addr_table에 등록 (jetson_tx가 참조)
 *   4. 타입별 dispatch:
 *      - ODOM  → shm_arr[robot_id] 에 직접 기록
 *      - IMAGE / LIDAR → fq_arr[robot_id] 에 push
 *
 * 우선순위: ★★★★ (커널 수신 버퍼 overflow 방지)
 */

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>

#include "proto.h"
#include "shm_def.h"
#include "frag_index_queue.h"
#include "rx_packet_pool.h"
#include "bridge_ctx.h"
#include "bridge_api.h"

typedef struct __attribute__((packed)) {
    char     robot_id[ROBOT_ID_STR_LEN];
    uint64_t timestamp_us;
    uint8_t  path_ok;
    uint8_t  pose_ok;
    uint8_t  goal_reached;
    uint8_t  reserved;
    uint32_t waypoint_idx;
    float    mission_progress;
} PathProgressPayloadV0;

typedef struct __attribute__((packed)) {
    char     robot_id[ROBOT_ID_STR_LEN];
    uint64_t timestamp_us;
    uint8_t  path_ok;
    uint8_t  pose_ok;
    uint8_t  goal_reached;
    uint8_t  reserved;
    uint32_t waypoint_idx;
    float    mission_progress;
    uint32_t nearest_index;
    float    distance_to_nearest_m;
    float    nearest_x_m;
    float    nearest_y_m;
    float    target_x_m;
    float    target_y_m;
    float    target_heading_rad;
    float    heading_error_rad;
    float    distance_to_goal_m;
} PathProgressPayloadV1;

/* ─── 소켓 생성 ─────────────────────────────────────────────── */
static int create_jetson_socket(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("[jetson_rx] socket"); return -1; }

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    /*
    SOL_SOCKET 소켓 자체 옵션 (tcp,udp 보다 하위 레벨)
    SO_REUSEADDR: bind() 시 "Address already in use" 방지 reuse=1로 설정하면 이미 사용 중인 포트라도 bind() 허용 
    */

    /* 수신 버퍼 8MB: 로봇 10대 버스트 대비 */
    int rcvbuf = 8 * 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    /* recv 타임아웃 100ms: stop 체크 가능 */
    struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    /*
    SO_RCVTIMEO: recvfrom() 호출 시 타임아웃 설정(안전장치)
    tv_sec: 초 단위, tv_usec: 마이크로초 단위 (100ms = 100,000us)
    
    */

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(BRIDGE_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[jetson_rx] bind"); close(fd); return -1;
    }
    return fd;
}

static int packet_type_supported(uint8_t type) {
    return type == PKT_TYPE_ODOM || type == PKT_TYPE_GLOBAL_PATH ||
           type == PKT_TYPE_PATH_PROGRESS ||
           type == PKT_TYPE_EVENT || type == PKT_TYPE_CMD_ACK ||
           type == PKT_TYPE_IMAGE || type == PKT_TYPE_LIDAR;
}

static int packet_type_updates_command_addr(uint8_t type) {
    return type == PKT_TYPE_ODOM ||
           type == PKT_TYPE_IMAGE || type == PKT_TYPE_LIDAR ||
           type == PKT_TYPE_CMD_ACK;
}

static int packet_type_learns_pc_peer(uint8_t type) {
    return type == PKT_TYPE_ODOM || type == PKT_TYPE_PATH_PROGRESS ||
           type == PKT_TYPE_EVENT || type == PKT_TYPE_IMAGE ||
           type == PKT_TYPE_LIDAR;
}

static int packet_robot_is_pc_peer_candidate(const JetsonRxCtx *ctx, uint8_t rid) {
    if (ctx->num_robots >= 5) {
        return rid != 0;  /* robot_id=0 is the real S05 Jetson in the 5-robot setup. */
    }
    return rid < 4;
}

static void learn_pc_peer_from_robot_packet(JetsonRxCtx *ctx, uint8_t rid,
                                            const struct sockaddr_in *src,
                                            uint8_t packet_type) {
    if (!ctx->pc_peer ||
        !packet_type_learns_pc_peer(packet_type) ||
        !packet_robot_is_pc_peer_candidate(ctx, rid)) {
        return;
    }

    int learned = 0;
    int updated = 0;
    struct in_addr old_addr;
    memset(&old_addr, 0, sizeof(old_addr));

    pthread_mutex_lock(&ctx->pc_peer->mu);
    if (!ctx->pc_peer->set) {
        ctx->pc_peer->addr = *src;
        ctx->pc_peer->addr.sin_port = htons(JETSON_CMD_PORT);
        ctx->pc_peer->set = 1;
        learned = 1;
    } else if (ctx->pc_peer->addr.sin_addr.s_addr != src->sin_addr.s_addr) {
        old_addr = ctx->pc_peer->addr.sin_addr;
        ctx->pc_peer->addr = *src;
        ctx->pc_peer->addr.sin_port = htons(JETSON_CMD_PORT);
        updated = 1;
    }
    pthread_mutex_unlock(&ctx->pc_peer->mu);

    if (learned || updated) {
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &src->sin_addr, ip, sizeof(ip));
        if (learned) {
            fprintf(stderr, "[jetson_rx] PC peer learned from robot=%u data: %s -> tx port %u\n",
                    rid, ip, JETSON_CMD_PORT);
        } else {
            char old_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &old_addr, old_ip, sizeof(old_ip));
            fprintf(stderr, "[jetson_rx] PC peer changed from robot=%u data: %s -> %s tx port %u\n",
                    rid, old_ip, ip, JETSON_CMD_PORT);
        }
    }
}

/* ─── Jetson IP 학습/검증 ───────────────────────────────────── */
static int learn_or_validate_addr(JetsonAddrTable *tbl, uint8_t rid,
                                  const struct sockaddr_in *src,
                                  uint8_t packet_type) {
    int learned = 0;
    int updated = 0;
    struct in_addr old_addr;
    memset(&old_addr, 0, sizeof(old_addr));

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &src->sin_addr, ip, sizeof(ip));

    if (!packet_type_updates_command_addr(packet_type)) {
        return 1;
    }

    pthread_mutex_lock(&tbl->mu);
    if (!tbl->set[rid]) {
        tbl->addr[rid]          = *src;
        tbl->addr[rid].sin_port = htons(JETSON_CMD_PORT);
        tbl->set[rid]           = 1;
        learned = 1;
    } else if (tbl->addr[rid].sin_addr.s_addr != src->sin_addr.s_addr) {
        if (packet_type == PKT_TYPE_CMD_ACK) {
            old_addr = tbl->addr[rid].sin_addr;
            tbl->addr[rid]          = *src;
            tbl->addr[rid].sin_port = htons(JETSON_CMD_PORT);
            updated = 1;
        }
    }
    pthread_mutex_unlock(&tbl->mu);

    if (learned) {
        fprintf(stderr, "[jetson_rx] robot=%u 주소 등록: %s\n", rid, ip);
    } else if (updated) {
        char old_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &old_addr, old_ip, sizeof(old_ip));
        fprintf(stderr, "[jetson_rx] robot=%u 주소 변경(CMD_ACK): %s -> %s\n",
                rid, old_ip, ip);
    }
    return 1;
}

/* ─── Odom 직접 SHM 기록 ────────────────────────────────────── */
static void handle_odom(JetsonRxCtx *ctx, uint8_t rid, const PktHeader *hdr,
                        const uint8_t *payload, int plen) {
    if (plen < (int)sizeof(OdomPayload)) return;
    const OdomPayload *o = (const OdomPayload *)payload;
    bridge_api_update_odom(ctx->api, rid, hdr, o);
}

static int parse_robot_id_string(const char *robot_id, uint8_t fallback) {
    char buf[ROBOT_ID_STR_LEN + 1];
    memcpy(buf, robot_id, ROBOT_ID_STR_LEN);
    buf[ROBOT_ID_STR_LEN] = '\0';

    const char *digits = NULL;
    for (const char *p = buf; *p; ++p) {
        if (*p >= '0' && *p <= '9') digits = p;
    }
    if (!digits) return fallback;
    while (digits > buf && digits[-1] >= '0' && digits[-1] <= '9') --digits;

    char *end = NULL;
    long n = strtol(digits, &end, 10);
    if (end == digits || n <= 0 || n > 255) return fallback;
    return (int)n - 1;
}

/* ─── Global path 직접 SHM 기록 ─────────────────────────────── */
static void handle_global_path(JetsonRxCtx *ctx, uint8_t rid,
                               const PktHeader *hdr,
                               const uint8_t *payload, int plen) {
    if (plen < (int)offsetof(GlobalPathPayload, waypoints)) return;
    const GlobalPathPayload *in = (const GlobalPathPayload *)payload;
    uint8_t count = in->count;
    if (count > GLOBAL_PATH_MAX_WAYPOINTS) count = GLOBAL_PATH_MAX_WAYPOINTS;

    size_t needed = offsetof(GlobalPathPayload, waypoints) +
                    (size_t)count * sizeof(PathWaypoint);
    if ((size_t)plen < needed) {
        fprintf(stderr, "[jetson_rx] global path short payload robot=%u len=%d needed=%zu count=%u\n",
                rid, plen, needed, count);
        return;
    }

    GlobalPathPayload path;
    memset(&path, 0, sizeof(path));
    path.count = count;
    memcpy(path.waypoints, in->waypoints, count * sizeof(PathWaypoint));
    bridge_api_update_global_path(ctx->api, rid, hdr, &path);
    if (count > 0) {
        const PathWaypoint *first = &path.waypoints[0];
        const PathWaypoint *last = &path.waypoints[count - 1];
        fprintf(stderr,
                "[jetson_rx] global_path robot=%u seq=%u count=%u first=(%.2f, %.2f) last=(%.2f, %.2f)\n",
                rid, hdr ? hdr->frame_id : 0, count,
                first->x, first->y, last->x, last->y);
    }
}

/* ─── Path progress 직접 SHM 기록 ───────────────────────────── */
static void handle_path_progress(JetsonRxCtx *ctx, uint8_t rid,
                                 const PktHeader *hdr,
                                 const uint8_t *payload, int plen) {
    PathProgressPayload progress;
    memset(&progress, 0, sizeof(progress));

    if (plen >= (int)sizeof(PathProgressPayload)) {
        memcpy(&progress, payload, sizeof(progress));
    } else if (plen >= (int)sizeof(PathProgressPayloadV1)) {
        const PathProgressPayloadV1 *legacy = (const PathProgressPayloadV1 *)payload;
        memcpy(progress.robot_id, legacy->robot_id, sizeof(progress.robot_id));
        progress.timestamp_us = legacy->timestamp_us;
        progress.path_ok = legacy->path_ok;
        progress.pose_ok = legacy->pose_ok;
        progress.goal_reached = legacy->goal_reached;
        progress.waypoint_idx = legacy->waypoint_idx;
        progress.mission_progress = legacy->mission_progress;
        progress.nearest_index = legacy->nearest_index;
        progress.distance_to_nearest_m = legacy->distance_to_nearest_m;
        progress.nearest_x_m = legacy->nearest_x_m;
        progress.nearest_y_m = legacy->nearest_y_m;
        progress.target_x_m = legacy->target_x_m;
        progress.target_y_m = legacy->target_y_m;
        progress.target_heading_rad = legacy->target_heading_rad;
        progress.heading_error_rad = legacy->heading_error_rad;
        progress.distance_to_goal_m = legacy->distance_to_goal_m;
    } else if (plen >= (int)sizeof(PathProgressPayloadV0)) {
        const PathProgressPayloadV0 *legacy = (const PathProgressPayloadV0 *)payload;
        memcpy(progress.robot_id, legacy->robot_id, sizeof(progress.robot_id));
        progress.timestamp_us = legacy->timestamp_us;
        progress.path_ok = legacy->path_ok;
        progress.pose_ok = legacy->pose_ok;
        progress.goal_reached = legacy->goal_reached;
        progress.waypoint_idx = legacy->waypoint_idx;
        progress.mission_progress = legacy->mission_progress;
    } else {
        fprintf(stderr, "[jetson_rx] path progress short payload robot=%u len=%d needed>=%zu\n",
                rid, plen, sizeof(PathProgressPayloadV0));
        return;
    }

    int target_id = parse_robot_id_string(progress.robot_id, rid);
    if (target_id < 0 || target_id >= ctx->num_robots) {
        fprintf(stderr, "[jetson_rx] path progress invalid robot_id=\"%.*s\" fallback=%u\n",
                ROBOT_ID_STR_LEN, progress.robot_id, rid);
        return;
    }

    bridge_api_update_path_progress(ctx->api, (uint8_t)target_id, hdr, &progress);
}

/* ─── Event 직접 SHM 기록 ───────────────────────────────────── */
static void handle_event(JetsonRxCtx *ctx, uint8_t rid,
                         const uint8_t *payload, int plen) {
    if (plen < (int)sizeof(EventPayload)) return;
    const EventPayload *event = (const EventPayload *)payload;
    char message[sizeof(event->message) + 1];
    memcpy(message, event->message, sizeof(event->message));
    message[sizeof(event->message)] = '\0';
    bridge_api_publish_event(ctx->api, rid,
                             event->severity,
                             event->event_type,
                             event->code,
                             message);
}

/* ─── 수신 스레드 메인 ──────────────────────────────────────── */
void *jetson_rx_thread(void *arg) {
    JetsonRxCtx *ctx = (JetsonRxCtx *)arg;

    int fd = create_jetson_socket();
    if (fd < 0) {
        fprintf(stderr, "[jetson_rx] 소켓 생성 실패\n");
        return NULL;
    }
    fprintf(stderr, "[jetson_rx] 포트 %d 수신 대기 (최대 %d대)\n",
            BRIDGE_PORT, ctx->num_robots);

    uint8_t drain_buf[PROTO_PKT_MAX];

    while (!atomic_load_explicit(ctx->stop, memory_order_acquire)) {
        struct sockaddr_in src_addr;
        socklen_t addrlen = sizeof(src_addr);
        int slot_id = rx_packet_pool_acquire(ctx->rx_pool);
        uint8_t *pkt = NULL;
        int using_pool = 1;

        if (slot_id == RX_SLOT_INVALID) {
            pkt = drain_buf;
            using_pool = 0;
        } else {
            pkt = ctx->rx_pool->slots[slot_id].buf;
        }

        /* recvfrom: 커널 UDP 버퍼에서 pool slot으로 직접 수신 */
        ssize_t n = recvfrom(fd, pkt, PROTO_PKT_MAX, 0,
                             (struct sockaddr *)&src_addr, &addrlen);

        if (n < 0) {
            if (using_pool) rx_packet_pool_release(ctx->rx_pool, slot_id);
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            if (errno == EINTR) continue;
            perror("[jetson_rx] recvfrom");
            continue;
        }
        if (n == 0) {
            if (using_pool) rx_packet_pool_release(ctx->rx_pool, slot_id);
            continue;
        }
        if (!using_pool) {
            bridge_api_note_drop(ctx->api, 0, 0, "rx packet pool exhausted");
            continue;
        }
        ctx->rx_pool->slots[slot_id].len = (int)n;

        if (n < (ssize_t)sizeof(PktHeader)) {
            rx_packet_pool_release(ctx->rx_pool, slot_id);
            continue;
        }

        const PktHeader *hdr     = (const PktHeader *)pkt;
        const uint8_t   *payload = pkt + sizeof(PktHeader);
        int              plen    = (int)n - (int)sizeof(PktHeader);

        if (!proto_validate_header(hdr, (uint16_t)plen)) {
            fprintf(stderr, "[jetson_rx] 잘못된 packet header type=0x%02x plen=%d declared=%u\n",
                    hdr->type, plen, hdr->payload_len);
            rx_packet_pool_release(ctx->rx_pool, slot_id);
            continue;
        }

        /* ── robot_id 검증 ──────────────────────────────────── */
        uint8_t rid = hdr->robot_id;
        if (rid >= (uint8_t)ctx->num_robots) {
            fprintf(stderr, "[jetson_rx] 범위 밖 robot_id=%u (max=%d)\n",
                    rid, ctx->num_robots);
            rx_packet_pool_release(ctx->rx_pool, slot_id);
            continue;
        }

        if (!packet_type_supported(hdr->type)) {
            fprintf(stderr, "[jetson_rx] 지원하지 않는 타입: 0x%02x (robot=%u)\n",
                    hdr->type, rid);
            rx_packet_pool_release(ctx->rx_pool, slot_id);
            continue;
        }

        learn_pc_peer_from_robot_packet(ctx, rid, &src_addr, hdr->type);

        /* ── Jetson IP 학습 (최초 1회) + 이후 source 검증 ───── */
        if (!learn_or_validate_addr(ctx->addr_table, rid, &src_addr, hdr->type)) {
            rx_packet_pool_release(ctx->rx_pool, slot_id);
            continue;
        }

        FragIndexQueue  *fq  = ctx->fq_arr[rid];

        bridge_api_note_rx(ctx->api, rid, hdr->type, hdr->timestamp_us);

        /* ── 타입별 dispatch ────────────────────────────────── */
        switch (hdr->type) {
        case PKT_TYPE_ODOM:
            handle_odom(ctx, rid, hdr, payload, plen);
            rx_packet_pool_release(ctx->rx_pool, slot_id);
            break;
        case PKT_TYPE_GLOBAL_PATH:
            handle_global_path(ctx, rid, hdr, payload, plen);
            rx_packet_pool_release(ctx->rx_pool, slot_id);
            break;
        case PKT_TYPE_PATH_PROGRESS:
            handle_path_progress(ctx, rid, hdr, payload, plen);
            rx_packet_pool_release(ctx->rx_pool, slot_id);
            break;
        case PKT_TYPE_EVENT:
            handle_event(ctx, rid, payload, plen);
            rx_packet_pool_release(ctx->rx_pool, slot_id);
            break;
        case PKT_TYPE_CMD_ACK:
            if (plen >= (int)sizeof(CmdAckPayload))
                bridge_api_handle_ack(ctx->api, rid, (const CmdAckPayload *)payload);
            rx_packet_pool_release(ctx->rx_pool, slot_id);
            break;
        case PKT_TYPE_IMAGE:
        case PKT_TYPE_LIDAR:
        {
            int dropped_slot_id = RX_SLOT_INVALID;
            frag_index_queue_push(fq, slot_id, &dropped_slot_id);
            if (dropped_slot_id != RX_SLOT_INVALID) {
                rx_packet_pool_release(ctx->rx_pool, dropped_slot_id);
                bridge_api_note_drop(ctx->api, rid, hdr->type, "fragment index queue full");
            }
            slot_id = RX_SLOT_INVALID; /* ownership transferred to reassembly */
            break;
        }
        default:
            fprintf(stderr, "[jetson_rx] 알 수 없는 타입: 0x%02x (robot=%u)\n",
                    hdr->type, rid);
            rx_packet_pool_release(ctx->rx_pool, slot_id);
            break;
        }
    }

    /* 종료 시 모든 FragIndexQueue에 stop 전파 */
    for (int i = 0; i < ctx->num_robots; i++)
        frag_index_queue_stop(ctx->fq_arr[i]);

    close(fd);
    fprintf(stderr, "[jetson_rx] 종료\n");
    return NULL;
}
