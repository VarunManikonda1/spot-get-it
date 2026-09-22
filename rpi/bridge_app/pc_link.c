/**
 * pc_link.c — PC 연동 스레드
 *
 * 역할:
 *   1. UDP:9002 수신 — PC에서 오는 커맨드를 Jetson으로 전달
 *   2. 1초 주기 — PC로 전체 로봇 상태(odom, 연결 여부) 송신
 *
 * 흐름:
 *   PC ──UDP:9002──▶ pc_link ──UDP:9001──▶ Jetson
 *   PC ◀──UDP──── pc_link (상태 패킷, recvfrom으로 PC 주소 학습)
 *
 * 우선순위: ★★★
 *   PC 연동은 로봇 제어보다 낮은 우선순위.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "proto.h"
#include "shm_def.h"
#include "bridge_ctx.h"
#include "cmd_dispatch.h"
#include "bridge_api.h"
#include "utils.h"

#define STATUS_INTERVAL_MS  1000   /* 상태 패킷 송신 주기 */

/* ─── 상태 패킷 전체 로봇 송신 ──────────────────────────────── */
static void send_status_all(int udp_fd,
                            BridgeApi              *api,
                            const struct sockaddr_in *pc_addr) {
    for (int i = 0; i < api->num_robots; i++) {
        PcStatusPacketV2 s;
        if (bridge_api_snapshot_status(api, (uint8_t)i, &s) < 0) continue;
        sendto(udp_fd, &s, sizeof(s), 0,
               (const struct sockaddr *)pc_addr, sizeof(*pc_addr));

        PcGlobalPathPacket path;
        if (bridge_api_snapshot_global_path(api, (uint8_t)i, &path) == 0 &&
            path.count > 0) {
            sendto(udp_fd, &path, sizeof(path), 0,
                   (const struct sockaddr *)pc_addr, sizeof(*pc_addr));
        }
    }
}

/* ─── UDP 소켓 생성 (수신 + 송신 겸용) ─────────────────────── */
static int create_pc_socket(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("[pc_link] socket"); return -1; }

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    /* 100ms 수신 타임아웃: stop + 상태 패킷 주기 체크 */
    struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PC_LINK_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[pc_link] bind"); close(fd); return -1;
    }
    return fd;
}

/* ─── 스레드 메인 ────────────────────────────────────────────── */
void *pc_link_thread(void *arg) {
    PcLinkCtx *ctx = (PcLinkCtx *)arg;

    int fd = create_pc_socket();
    if (fd < 0) return NULL;
    if (ctx->pc_peer) {
        pthread_mutex_lock(&ctx->pc_peer->mu);
        ctx->pc_peer->fd = fd;
        pthread_mutex_unlock(&ctx->pc_peer->mu);
    }

    fprintf(stderr, "[pc_link] 포트 %d 대기\n", PC_LINK_PORT);

    /* PC 주소 (recvfrom으로 학습) */
    struct sockaddr_in pc_addr;
    int pc_addr_set = 0;

    uint8_t buf[sizeof(CmdPacket)];
    uint64_t last_status_us = now_us();

    while (!atomic_load_explicit(ctx->stop, memory_order_acquire)) {
        struct sockaddr_in src;
        socklen_t slen = sizeof(src);
        ssize_t n = recvfrom(fd, buf, sizeof(buf), 0,
                             (struct sockaddr *)&src, &slen);

        /* 상태 패킷 주기 체크 (PC 주소 등록된 경우만) */
        uint64_t now = now_us();
        if (pc_addr_set &&
            (now - last_status_us) >= (uint64_t)STATUS_INTERVAL_MS * 1000) {
            send_status_all(fd, ctx->api, &pc_addr);
            last_status_us = now;
        }

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            if (errno == EINTR) continue;
            perror("[pc_link] recvfrom");
            continue;
        }
        /* PC 주소 학습 */
        if (!pc_addr_set) {
            pc_addr     = src;
            pc_addr_set = 1;
            if (ctx->pc_peer) {
                pthread_mutex_lock(&ctx->pc_peer->mu);
                ctx->pc_peer->addr = src;
                ctx->pc_peer->set = 1;
                pthread_mutex_unlock(&ctx->pc_peer->mu);
            }
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &src.sin_addr, ip, sizeof(ip));
            fprintf(stderr, "[pc_link] PC 주소 등록: %s:%u -> tx port %u\n",
                    ip, ntohs(src.sin_port), PC_LINK_PORT);
        }

        if (n != (ssize_t)sizeof(CmdPacket)) continue;

        CmdPacket *cmd = (CmdPacket *)buf;
        send_cmd_to_jetson(cmd, ctx->api, fd, CMD_PRIORITY_NORMAL,
                           CMD_FLAG_REQUIRES_ACK, "pc_link");
        bridge_api_poll_timeouts(ctx->api, fd, "pc_link");
    }

    close(fd);
    if (ctx->pc_peer) {
        pthread_mutex_lock(&ctx->pc_peer->mu);
        if (ctx->pc_peer->fd == fd) {
            ctx->pc_peer->fd = -1;
        }
        pthread_mutex_unlock(&ctx->pc_peer->mu);
    }
    fprintf(stderr, "[pc_link] 종료\n");
    return NULL;
}
