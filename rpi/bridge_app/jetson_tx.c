/**
 * jetson_tx.c — Jetson 커맨드 송신 스레드
 *
 * 역할:
 *   1. Qt가 SharedData.cmd_queue에 넣은 CmdPacket을 읽음
 *   2. send_cmd_to_jetson()으로 Jetson에 UDP 전달
 *
 * 흐름:
 *   Qt ──SHM cmd_queue──▶ jetson_tx ──UDP:9001──▶ Jetson
 *
 * 우선순위: ★★★★★
 *   정지/비상 명령 지연은 허용 불가.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "proto.h"
#include "shm_def.h"
#include "bridge_ctx.h"
#include "cmd_dispatch.h"
#include "bridge_api.h"
#include "shm_cmd_queue.h"

/* ─── UDP 소켓 생성 (송신 전용) ─────────────────────────────── */
static int create_udp_sock(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("[jetson_tx] UDP socket"); return -1; }
    return fd;
}

static void send_shm_cmd_entry(JetsonTxCtx *ctx, int udp_fd,
                               const ShmCmdEntry *entry) {
    if (entry->flags & CMD_FLAG_TARGET_PC) {
        struct sockaddr_in pc_addr;
        int pc_addr_set = 0;
        int pc_fd = -1;
        if (ctx->pc_peer) {
            pthread_mutex_lock(&ctx->pc_peer->mu);
            pc_addr_set = ctx->pc_peer->set;
            if (pc_addr_set) {
                pc_addr = ctx->pc_peer->addr;
            }
            pc_fd = ctx->pc_peer->fd;
            pthread_mutex_unlock(&ctx->pc_peer->mu);
        }
        if (!pc_addr_set) {
            fprintf(stderr, "[jetson_tx] PC address not learned yet for robot=%u cmd=%u\n",
                    entry->cmd.robot_id, entry->cmd.cmd_type);
            return;
        }
        pc_addr.sin_port = htons(JETSON_CMD_PORT);
        const int send_fd = pc_fd >= 0 ? pc_fd : udp_fd;
        if (sendto(send_fd, &entry->cmd, sizeof(entry->cmd), 0,
                   (const struct sockaddr *)&pc_addr, sizeof(pc_addr)) < 0) {
            perror("[jetson_tx] sendto pc");
            return;
        }
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &pc_addr.sin_addr, ip, sizeof(ip));
        fprintf(stderr, "[jetson_tx] pc %s:%u robot=%u cmd=%u seq=%u src=%s\n",
                ip, ntohs(pc_addr.sin_port), entry->cmd.robot_id,
                entry->cmd.cmd_type, entry->cmd.seq,
                send_fd == pc_fd ? "pc_link" : "jetson_tx");
        return;
    }

    uint8_t priority = shm_cmd_entry_effective_priority(entry);
    uint8_t flags = entry->flags;
    send_cmd_to_jetson(&entry->cmd, ctx->api, udp_fd,
                       priority, flags, "jetson_tx");
}

/* ─── 송신 스레드 메인 ──────────────────────────────────────── */
void *jetson_tx_thread(void *arg) {
    JetsonTxCtx *ctx = (JetsonTxCtx *)arg;

    int udp_fd = create_udp_sock();
    if (udp_fd < 0) return NULL;

    fprintf(stderr, "[jetson_tx] SHM command queue 대기\n");

    int next_robot = 0;

    while (!atomic_load_explicit(ctx->stop, memory_order_acquire)) {
        int handled = 0;
        bridge_api_poll_timeouts(ctx->api, udp_fd, "jetson_tx");

        for (int offset = 0; offset < ctx->num_robots; offset++) {
            int rid = (next_robot + offset) % ctx->num_robots;
            ShmCmdQueue *q = &ctx->shm_arr[rid]->cmd_queue;
            ShmCmdEntry entry;
            if (shm_cmd_queue_pop_highest(q, &entry)) {
                send_shm_cmd_entry(ctx, udp_fd, &entry);
                next_robot = (rid + 1) % ctx->num_robots;
                handled = 1;
                break;
            }
        }

        if (!handled)
            usleep(5000);
    }

    close(udp_fd);
    fprintf(stderr, "[jetson_tx] 종료\n");
    return NULL;
}
