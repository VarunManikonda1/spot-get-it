/**
 * bridge_api.h — middleware-facing API shared by planes.
 *
 * Sensor, state, command, event, and ops paths call this layer instead of
 * hand-editing SHM or hand-building UDP command packets in each module.
 */

#pragma once

#include <stdint.h>
#include <netinet/in.h>

#include "bridge_ctx.h"
#include "proto.h"

#define EVENT_SEVERITY_INFO     1
#define EVENT_SEVERITY_WARN     2
#define EVENT_SEVERITY_ERROR    3
#define EVENT_SEVERITY_CRITICAL 4

#define EVENT_TYPE_CONNECTED       1
#define EVENT_TYPE_DISCONNECTED    2
#define EVENT_TYPE_CMD_SENT        3
#define EVENT_TYPE_CMD_ACK         4
#define EVENT_TYPE_CMD_TIMEOUT     5
#define EVENT_TYPE_PACKET_DROP     6
#define EVENT_TYPE_FRAME_READY     7
#define EVENT_TYPE_VICTIM_DETECTED 8

void bridge_api_init(BridgeApi *api, JetsonAddrTable *addr_table,
                     SharedData **shm_arr, int num_robots);

void bridge_api_destroy(BridgeApi *api);

void bridge_api_init_shm(SharedData *shm, int robot_id);

void bridge_api_publish_event(BridgeApi *api, uint8_t robot_id,
                              uint8_t severity, uint16_t event_type,
                              uint32_t code, const char *message);

void bridge_api_note_rx(BridgeApi *api, uint8_t robot_id, uint8_t pkt_type,
                        uint64_t timestamp_us);

void bridge_api_update_odom(BridgeApi *api, uint8_t robot_id,
                            const PktHeader *hdr,
                            const OdomPayload *odom);

void bridge_api_update_global_path(BridgeApi *api, uint8_t robot_id,
                                   const PktHeader *hdr,
                                   const GlobalPathPayload *path);

void bridge_api_update_path_progress(BridgeApi *api, uint8_t robot_id,
                                     const PktHeader *hdr,
                                     const PathProgressPayload *progress);

void bridge_api_note_frame_ready(BridgeApi *api, uint8_t robot_id,
                                 uint8_t pkt_type, uint32_t frame_id,
                                 uint64_t timestamp_us);

void bridge_api_note_drop(BridgeApi *api, uint8_t robot_id, uint8_t pkt_type,
                          const char *reason);

int bridge_api_send_command(BridgeApi *api, int udp_fd, const CmdPacket *cmd,
                            uint8_t priority, uint8_t flags,
                            const char *tag);

void bridge_api_handle_ack(BridgeApi *api, uint8_t robot_id,
                           const CmdAckPayload *ack);

void bridge_api_poll_timeouts(BridgeApi *api, int udp_fd, const char *tag);

int bridge_api_snapshot_status(BridgeApi *api, uint8_t robot_id,
                               PcStatusPacketV2 *out);

int bridge_api_snapshot_global_path(BridgeApi *api, uint8_t robot_id,
                                    PcGlobalPathPacket *out);
