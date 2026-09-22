/**
 * cmd_dispatch.h — Jetson 커맨드 패킷 송신 공통 로직
 *
 * jetson_tx (Qt Unix Socket 경로)와 pc_link (PC UDP 경로) 양쪽에서
 * 동일한 패킷 구성 및 sendto 로직을 사용하므로 공통 함수로 분리.
 */

#pragma once

#include "proto.h"
#include "bridge_ctx.h"

/**
 * send_cmd_to_jetson — CmdPacket을 PktHeader + CmdPayload로 래핑해 UDP 송신
 *
 * @param cmd        송신할 커맨드 패킷
 * @param tbl        Jetson 주소 테이블
 * @param udp_fd     송신용 UDP 소켓
 * @param num_robots 등록된 로봇 수 (robot_id 범위 검증용)
 * @param tag        로그 접두어 (예: "jetson_tx", "pc_link")
 */
void send_cmd_to_jetson(const CmdPacket *cmd,
                        BridgeApi       *api,
                        int              udp_fd,
                        uint8_t          priority,
                        uint8_t          flags,
                        const char      *tag);
