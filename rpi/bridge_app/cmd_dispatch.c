/**
 * cmd_dispatch.c — Jetson 커맨드 패킷 송신 공통 구현
 */

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "cmd_dispatch.h"
#include "bridge_api.h"
#include "utils.h"

void send_cmd_to_jetson(const CmdPacket *cmd,
                        BridgeApi       *api,
                        int              udp_fd,
                        uint8_t          priority,
                        uint8_t          flags,
                        const char      *tag) {
    bridge_api_send_command(api, udp_fd, cmd, priority, flags, tag);
}
