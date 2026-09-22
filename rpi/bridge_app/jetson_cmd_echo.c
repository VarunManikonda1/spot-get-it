/*
 * jetson_cmd_echo.c
 *
 * Jetson side command echo tester.
 *
 * Usage:
 *   ./jetson_cmd_echo <robot_id> [bridge_ip]
 *
 * It listens on UDP 9001 for BridgeDaemon command packets:
 *   [ PktHeader(type=PKT_TYPE_CMD) ][ CmdPayload ]
 *
 * When a command arrives, it prints the command. For cmd_type=1 it sends a
 * CMD_ACK packet back to BridgeDaemon UDP 9000 so the Qt/Bridge path can be
 * checked end-to-end.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "proto.h"

static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static int create_cmd_socket(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(JETSON_CMD_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind UDP 9001");
        close(fd);
        return -1;
    }

    return fd;
}

static int send_cmd_ack(int fd, const struct sockaddr_in *bridge_addr,
                        uint8_t robot_id, uint32_t seq, uint8_t cmd_type,
                        uint64_t command_timestamp_us) {
    uint8_t buf[sizeof(PktHeader) + sizeof(CmdAckPayload)];
    PktHeader *hdr = (PktHeader *)buf;
    CmdAckPayload *ack = (CmdAckPayload *)(buf + sizeof(PktHeader));
    uint64_t t = now_us();

    memset(buf, 0, sizeof(buf));
    hdr->type = PKT_TYPE_CMD_ACK;
    hdr->robot_id = robot_id;
    hdr->frag_total = 1;
    hdr->payload_len = (uint16_t)sizeof(CmdAckPayload);
    hdr->frame_id = seq;
    hdr->timestamp_us = t;

    ack->command_id = 0;
    ack->seq = seq;
    ack->cmd_type = cmd_type;
    ack->status = 0;
    ack->timestamp_us = command_timestamp_us ? command_timestamp_us : t;

    return (int)sendto(fd, buf, sizeof(buf), 0,
                       (const struct sockaddr *)bridge_addr,
                       sizeof(*bridge_addr));
}

static int parse_robot_id(const char *s, uint8_t *out) {
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (!s[0] || (end && *end) || v < 0 || v > 255) {
        return -1;
    }
    *out = (uint8_t)v;
    return 0;
}

static int fill_bridge_addr(struct sockaddr_in *addr, const char *ip) {
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(BRIDGE_PORT);
    if (inet_pton(AF_INET, ip, &addr->sin_addr) != 1) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <robot_id> [bridge_ip]\n", argv[0]);
        return 2;
    }

    uint8_t robot_id = 0;
    if (parse_robot_id(argv[1], &robot_id) < 0) {
        fprintf(stderr, "invalid robot_id: %s\n", argv[1]);
        return 2;
    }

    int fd = create_cmd_socket();
    if (fd < 0) {
        return 1;
    }

    struct sockaddr_in configured_bridge;
    int has_configured_bridge = 0;
    if (argc == 3) {
        if (fill_bridge_addr(&configured_bridge, argv[2]) < 0) {
            fprintf(stderr, "invalid bridge_ip: %s\n", argv[2]);
            close(fd);
            return 2;
        }
        has_configured_bridge = 1;
        if (send_cmd_ack(fd, &configured_bridge, robot_id, 0, 1, 0) < 0) {
            perror("initial announce ACK");
        } else {
            fprintf(stderr, "[echo] announce robot=%u -> %s:%d\n",
                    robot_id, argv[2], BRIDGE_PORT);
        }
    }

    fprintf(stderr, "[echo] listening UDP %d for robot=%u\n",
            JETSON_CMD_PORT, robot_id);

    for (;;) {
        uint8_t buf[PROTO_PKT_MAX];
        struct sockaddr_in src;
        socklen_t src_len = sizeof(src);
        ssize_t n = recvfrom(fd, buf, sizeof(buf), 0,
                             (struct sockaddr *)&src, &src_len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recvfrom");
            break;
        }
        if (n < (ssize_t)sizeof(PktHeader)) {
            fprintf(stderr, "[echo] short packet: %zd bytes\n", n);
            continue;
        }

        const PktHeader *hdr = (const PktHeader *)buf;
        const uint8_t *payload = buf + sizeof(PktHeader);
        int plen = (int)n - (int)sizeof(PktHeader);

        if (!proto_validate_header(hdr, (uint16_t)plen)) {
            fprintf(stderr, "[echo] invalid header type=0x%02x plen=%d declared=%u\n",
                    hdr->type, plen, hdr->payload_len);
            continue;
        }
        if (hdr->type != PKT_TYPE_CMD || plen < (int)sizeof(CmdPayload)) {
            fprintf(stderr, "[echo] ignored packet type=0x%02x plen=%d\n",
                    hdr->type, plen);
            continue;
        }
        if (hdr->robot_id != robot_id) {
            fprintf(stderr, "[echo] ignored robot_id=%u, this robot=%u\n",
                    hdr->robot_id, robot_id);
            continue;
        }

        const CmdPayload *cmd = (const CmdPayload *)payload;
        char src_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &src.sin_addr, src_ip, sizeof(src_ip));

        fprintf(stderr,
                "[echo] from=%s cmd_type=%u seq=%u vx=%.3f vy=%.3f omega=%.3f\n",
                src_ip, cmd->cmd_type, cmd->seq, cmd->vx, cmd->vy, cmd->omega);

        struct sockaddr_in bridge_addr = has_configured_bridge ? configured_bridge : src;
        bridge_addr.sin_port = htons(BRIDGE_PORT);

        if (cmd->cmd_type == 1) {
            if (send_cmd_ack(fd, &bridge_addr, hdr->robot_id, cmd->seq,
                             cmd->cmd_type, hdr->timestamp_us) < 0) {
                perror("send CMD_ACK");
                continue;
            }
            fprintf(stderr, "[echo] echoed cmd_type=1 seq=%u -> %s:%d\n",
                    cmd->seq, src_ip, BRIDGE_PORT);
        }
    }

    close(fd);
    return 1;
}

