/**
 * protocol_timer.c — Heartbeat 송신 스레드
 *
 * 역할:
 *   1. 1초 주기로 모든 등록된 Jetson에 heartbeat 패킷 송신
 *   2. Jetson이 RPi가 살아있음을 인지하도록 유지
 *
 * 우선순위: ★★★★★
 *   heartbeat 지연 시 Jetson이 안전 정지 수행 가능.
 *
 * 향후 확장:
 *   - 커맨드 ACK 수신 감시 + 재전송 (Jetson 측 ACK 패킷 정의 후)
 *   - 가변 heartbeat 주기 (연결 품질에 따라 조정)
 */

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "proto.h"
#include "shm_def.h"
#include "bridge_ctx.h"
#include "bridge_api.h"
#include "utils.h"

/* ─── heartbeat 1개 송신 ─────────────────────────────────────── */
static void send_heartbeat(ProtoTimerCtx *ctx, int udp_fd, uint8_t robot_id,
                           uint32_t seq) {
    CmdPacket cmd = {
        .robot_id = robot_id,
        .cmd_type = CMD_TYPE_HEARTBEAT,
        .vx = 0.0f,
        .vy = 0.0f,
        .omega = 0.0f,
        .seq = seq,
    };
    bridge_api_send_command(ctx->api, udp_fd, &cmd, CMD_PRIORITY_LOW, 0,
                            "proto_timer");
}

/* ─── 스레드 메인 ────────────────────────────────────────────── */
void *protocol_timer_thread(void *arg) {
    ProtoTimerCtx *ctx = (ProtoTimerCtx *)arg;

    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) {
        perror("[proto_timer] socket");
        return NULL;
    }

    fprintf(stderr, "[proto_timer] 시작 (heartbeat 1Hz)\n");

    uint32_t seq = 0;

    /* clock_nanosleep으로 정밀한 1초 주기 유지 */
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);

    while (!atomic_load_explicit(ctx->stop, memory_order_acquire)) {
        /* 1초 후 시각 계산 */
        next.tv_sec += 1;

        /* 다음 주기까지 대기 (stop 체크를 위해 100ms 단위로 쪼갬) */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        while (!atomic_load_explicit(ctx->stop, memory_order_acquire)) {
            struct timespec rem = {
                .tv_sec  = next.tv_sec  - now.tv_sec,
                .tv_nsec = next.tv_nsec - now.tv_nsec,
            };
            if (rem.tv_nsec < 0) {
                rem.tv_sec--;
                rem.tv_nsec += 1000000000L;
            }
            if (rem.tv_sec < 0 ||
                (rem.tv_sec == 0 && rem.tv_nsec <= 0)) break;

            /* 최대 100ms씩 잠들며 stop 확인 */
            struct timespec sleep_t = { .tv_sec = 0, .tv_nsec = 100000000L };
            if (rem.tv_sec == 0 && rem.tv_nsec < 100000000L)
                sleep_t.tv_nsec = rem.tv_nsec;

            nanosleep(&sleep_t, NULL);
            clock_gettime(CLOCK_MONOTONIC, &now);
        }
        if (atomic_load_explicit(ctx->stop, memory_order_acquire)) break;

        /* addr_table 스냅샷 (lock 최소화: sendto는 lock 밖에서) */
        struct sockaddr_in snap_addr[MAX_ROBOTS];
        int                snap_set[MAX_ROBOTS];
        pthread_mutex_lock(&ctx->addr_table->mu);
        for (int i = 0; i < ctx->num_robots; i++) {
            snap_set[i]  = ctx->addr_table->set[i];
            snap_addr[i] = ctx->addr_table->addr[i];
        }
        pthread_mutex_unlock(&ctx->addr_table->mu);

        /* lock 해제 후 heartbeat 송신 */
        for (int i = 0; i < ctx->num_robots; i++) {
            if (snap_set[i])
                send_heartbeat(ctx, udp_fd, (uint8_t)i, seq);
        }
        (void)snap_addr;

        bridge_api_poll_timeouts(ctx->api, udp_fd, "proto_timer");
        fprintf(stderr, "[proto_timer] heartbeat seq=%u\n", seq);
        seq++;
    }

    close(udp_fd);
    fprintf(stderr, "[proto_timer] 종료\n");
    return NULL;
}
