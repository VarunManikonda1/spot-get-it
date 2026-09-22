#include "bridge_api.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "utils.h"

#define COMMAND_ACK_TIMEOUT_US 250000ULL

static int packet_type_updates_robot_health(uint8_t pkt_type) {
    return pkt_type == PKT_TYPE_ODOM || pkt_type == PKT_TYPE_PATH_PROGRESS;
}

static void format_spot_robot_id(char *dst, size_t dst_size, int robot_id) {
    if (!dst || dst_size == 0) return;
    const int robot_num = robot_id + 1;
    if (dst_size >= 8 && robot_num >= 1 && robot_num <= 99) {
        dst[0] = 's';
        dst[1] = 'p';
        dst[2] = 'o';
        dst[3] = 't';
        dst[4] = '_';
        dst[5] = (char)('0' + (robot_num / 10));
        dst[6] = (char)('0' + (robot_num % 10));
        dst[7] = '\0';
        return;
    }
    dst[0] = '\0';
}

static const char *cmd_name(uint8_t type) {
    switch (type) {
    case CMD_TYPE_ESTOP: return "estop";
    case CMD_TYPE_STOP: return "stop";
    case CMD_TYPE_MOVE: return "move";
    case CMD_TYPE_HEARTBEAT: return "heartbeat";
    case CMD_TYPE_SET_MODE: return "set_mode";
    case CMD_TYPE_RETURN_HOME: return "return_home";
    case CMD_TYPE_SET_WAYPOINT: return "set_waypoint";
    case CMD_TYPE_SET_ROUTE: return "set_route";
    case CMD_TYPE_START_MISSION: return "start_mission";
    case CMD_TYPE_PAUSE_MISSION: return "pause_mission";
    case CMD_TYPE_CANCEL_MISSION: return "cancel_mission";
    case CMD_TYPE_SENSOR_CTRL: return "sensor_ctrl";
    case CMD_TYPE_MANUAL_MOVE: return "manual_move";
    case CMD_TYPE_SET_AUTO: return "set_auto";
    case CMD_TYPE_BODY_ACTION: return "body_action";
    default: return "unknown";
    }
}

void bridge_api_init(BridgeApi *api, JetsonAddrTable *addr_table,
                     SharedData **shm_arr, int num_robots) {
    memset(api, 0, sizeof(*api));
    api->addr_table = addr_table;
    api->num_robots = num_robots;
    pthread_mutex_init(&api->event_mu, NULL);
    pthread_mutex_init(&api->pending_mu, NULL);
    atomic_init(&api->next_command_id, 1);
    for (int i = 0; i < num_robots; i++) api->shm_arr[i] = shm_arr[i];
}

void bridge_api_destroy(BridgeApi *api) {
    pthread_mutex_destroy(&api->pending_mu);
    pthread_mutex_destroy(&api->event_mu);
}

void bridge_api_init_shm(SharedData *shm, int robot_id) {
    shm->shm_magic = SHM_MAGIC;
    shm->shm_version = SHM_VERSION;
    shm->shm_header_size = (uint16_t)offsetof(SharedData, img_slots);
    shm->shared_data_size = (uint32_t)sizeof(SharedData);
    shm->state.battery_percent = -1.0f;
    shm->state.robot_id = (uint8_t)robot_id;
    format_spot_robot_id(shm->global_path.robot_id,
                         sizeof(shm->global_path.robot_id), robot_id);
    atomic_store(&shm->event_log.write_seq, 0);
    atomic_store(&shm->metrics.rx_packets, 0);
    atomic_store(&shm->metrics.tx_commands, 0);
    atomic_store(&shm->metrics.ack_packets, 0);
    atomic_store(&shm->metrics.retry_commands, 0);
    atomic_store(&shm->metrics.dropped_packets, 0);
}

void bridge_api_publish_event(BridgeApi *api, uint8_t robot_id,
                              uint8_t severity, uint16_t event_type,
                              uint32_t code, const char *message) {
                                
    if (!api || robot_id >= (uint8_t)api->num_robots) return;
    SharedData *shm = api->shm_arr[robot_id];

    pthread_mutex_lock(&api->event_mu);
    int seq = atomic_load_explicit(&shm->event_log.write_seq,
                                   memory_order_relaxed);
    RobotEvent *ev = &shm->event_log.events[seq % EVENT_LOG_SIZE];
    ev->timestamp_us = now_us();
    ev->robot_id = robot_id;
    ev->severity = severity;
    ev->event_type = event_type;
    ev->code = code;
    snprintf(ev->message, sizeof(ev->message), "%s", message ? message : "");
    atomic_store_explicit(&shm->event_log.write_seq, seq + 1,
                          memory_order_release);
    pthread_mutex_unlock(&api->event_mu);
}

void bridge_api_note_rx(BridgeApi *api, uint8_t robot_id, uint8_t pkt_type,
                        uint64_t timestamp_us) {
    (void)timestamp_us;
    if (!api || robot_id >= (uint8_t)api->num_robots) return;
    SharedData *shm = api->shm_arr[robot_id];
    const int health_packet = packet_type_updates_robot_health(pkt_type);
    uint8_t was_connected = atomic_load(&shm->meta.jetson_connected);
    atomic_fetch_add(&shm->metrics.rx_packets, 1);
    shm->metrics.last_rx_us = now_us();

    if (!health_packet) return;

    atomic_store(&shm->meta.jetson_connected, 1);
    atomic_fetch_add(&shm->meta.pkt_count, 1);

    pthread_rwlock_wrlock(&shm->state_lock);
    shm->state.seq++;
    shm->state.connected = 1;
    shm->state.updated_us = shm->metrics.last_rx_us;
    shm->state.last_rx_us = shm->metrics.last_rx_us;
    pthread_rwlock_unlock(&shm->state_lock);

    if (!was_connected) {
        bridge_api_publish_event(api, robot_id, EVENT_SEVERITY_INFO,
                                 EVENT_TYPE_CONNECTED, 0, "robot connected");
    }
}

void bridge_api_update_odom(BridgeApi *api, uint8_t robot_id,
                            const PktHeader *hdr,
                            const OdomPayload *odom) {
    if (!api || robot_id >= (uint8_t)api->num_robots) return;
    SharedData *shm = api->shm_arr[robot_id];
    pthread_rwlock_wrlock(&shm->odom_lock);
    shm->odom_x = odom->x;
    shm->odom_y = odom->y;
    shm->odom_theta = odom->theta;
    shm->odom_vx = odom->vx;
    shm->odom_vy = odom->vy;
    shm->odom_omega = odom->omega;
    shm->odom_timestamp_us = hdr->timestamp_us;
    shm->odom_seq = hdr->frame_id;
    pthread_rwlock_unlock(&shm->odom_lock);

    pthread_rwlock_wrlock(&shm->state_lock);
    shm->state.seq++;
    shm->state.updated_us = now_us();
    shm->state.x = odom->x;
    shm->state.y = odom->y;
    shm->state.theta = odom->theta;
    shm->state.vx = odom->vx;
    shm->state.vy = odom->vy;
    shm->state.omega = odom->omega;
    shm->state.odom_seq = hdr->frame_id;
    pthread_rwlock_unlock(&shm->state_lock);
}

void bridge_api_update_global_path(BridgeApi *api, uint8_t robot_id,
                                   const PktHeader *hdr,
                                   const GlobalPathPayload *path) {
    if (!api || robot_id >= (uint8_t)api->num_robots || !path) return;
    SharedData *shm = api->shm_arr[robot_id];
    uint8_t count = path->count;
    if (count > GLOBAL_PATH_MAX_WAYPOINTS) count = GLOBAL_PATH_MAX_WAYPOINTS;

    pthread_rwlock_wrlock(&shm->path_lock);
    shm->global_path.seq = hdr ? hdr->frame_id : shm->global_path.seq + 1;
    shm->global_path.updated_us = hdr ? hdr->timestamp_us : now_us();
    format_spot_robot_id(shm->global_path.robot_id,
                         sizeof(shm->global_path.robot_id), robot_id);
    shm->global_path.count = count;
    memset(shm->global_path.waypoints, 0, sizeof(shm->global_path.waypoints));
    memcpy(shm->global_path.waypoints, path->waypoints,
           count * sizeof(shm->global_path.waypoints[0]));
    pthread_rwlock_unlock(&shm->path_lock);

    pthread_rwlock_wrlock(&shm->state_lock);
    shm->state.seq++;
    shm->state.updated_us = now_us();
    if (count > 0) {
        shm->state.goal_x = path->waypoints[count - 1].x;
        shm->state.goal_y = path->waypoints[count - 1].y;
    }
    pthread_rwlock_unlock(&shm->state_lock);
}

void bridge_api_update_path_progress(BridgeApi *api, uint8_t robot_id,
                                     const PktHeader *hdr,
                                     const PathProgressPayload *progress) {
    if (!api || robot_id >= (uint8_t)api->num_robots || !progress) return;
    SharedData *shm = api->shm_arr[robot_id];
    float mission_progress = progress->mission_progress;
    if (mission_progress < 0.0f) mission_progress = 0.0f;
    if (mission_progress > 1.0f) mission_progress = 1.0f;
    if (progress->goal_reached) mission_progress = 1.0f;
    const int progress_valid = progress->path_ok && progress->pose_ok
                            && progress->total_waypoints > 0;

    pthread_rwlock_wrlock(&shm->state_lock);
    const int has_existing_progress = shm->state.total_waypoints > 0
                                    || shm->state.mission_progress > 0.0f
                                    || shm->state.goal_reached != 0;
    shm->state.seq++;
    shm->state.updated_us = hdr ? hdr->timestamp_us : now_us();
    if (progress_valid || progress->goal_reached || !has_existing_progress) {
        shm->state.path_ok = progress->path_ok ? 1 : 0;
        shm->state.pose_ok = progress->pose_ok ? 1 : 0;
        shm->state.goal_reached = progress->goal_reached ? 1 : 0;
    }
    if (progress_valid || progress->goal_reached) {
        shm->state.waypoint_idx = progress->waypoint_idx;
        shm->state.total_waypoints = progress->total_waypoints;
        shm->state.mission_progress = mission_progress;
        shm->state.nearest_path_idx = progress->nearest_index;
        shm->state.distance_to_nearest_m = progress->distance_to_nearest_m;
        shm->state.nearest_x = progress->nearest_x_m;
        shm->state.nearest_y = progress->nearest_y_m;
        shm->state.target_x = progress->target_x_m;
        shm->state.target_y = progress->target_y_m;
        shm->state.target_heading = progress->target_heading_rad;
        shm->state.heading_error = progress->heading_error_rad;
        shm->state.distance_to_target_m = progress->distance_to_target_m;
        shm->state.distance_to_goal_m = progress->distance_to_goal_m;
    }
    pthread_rwlock_unlock(&shm->state_lock);
}

void bridge_api_note_frame_ready(BridgeApi *api, uint8_t robot_id,
                                 uint8_t pkt_type, uint32_t frame_id,
                                 uint64_t timestamp_us) {
    if (!api || robot_id >= (uint8_t)api->num_robots) return;
    SharedData *shm = api->shm_arr[robot_id];
    uint64_t now = now_us();
    float fps = 0.0f;
    uint64_t *last = (pkt_type == PKT_TYPE_IMAGE)
        ? &shm->metrics.last_rx_us : &shm->metrics.last_ack_us;
    if (*last && now > *last) fps = 1000000.0f / (float)(now - *last);
    *last = now;

    pthread_rwlock_wrlock(&shm->state_lock);
    shm->state.seq++;
    shm->state.updated_us = now;
    if (pkt_type == PKT_TYPE_IMAGE) shm->state.image_fps = fps;
    if (pkt_type == PKT_TYPE_LIDAR) shm->state.lidar_fps = fps;
    pthread_rwlock_unlock(&shm->state_lock);

    (void)frame_id;
    (void)timestamp_us;
}

void bridge_api_note_drop(BridgeApi *api, uint8_t robot_id, uint8_t pkt_type,
                          const char *reason) {
    if (!api || robot_id >= (uint8_t)api->num_robots) return;
    SharedData *shm = api->shm_arr[robot_id];
    atomic_fetch_add(&shm->metrics.dropped_packets, 1);
    pthread_rwlock_wrlock(&shm->meta_lock);
    if (pkt_type == PKT_TYPE_IMAGE) shm->meta.img_drop_count++;
    if (pkt_type == PKT_TYPE_LIDAR) shm->meta.lidar_drop_count++;
    pthread_rwlock_unlock(&shm->meta_lock);
    bridge_api_publish_event(api, robot_id, EVENT_SEVERITY_WARN,
                             EVENT_TYPE_PACKET_DROP, pkt_type, reason);
}

static int lookup_addr(BridgeApi *api, uint8_t rid, struct sockaddr_in *dst) {
    pthread_mutex_lock(&api->addr_table->mu);
    int has_addr = api->addr_table->set[rid];
    if (has_addr) *dst = api->addr_table->addr[rid];
    pthread_mutex_unlock(&api->addr_table->mu);
    return has_addr;
}

static int send_legacy_cmd(int udp_fd, const struct sockaddr_in *dst,
                           const CmdPacket *c, uint64_t now) {
    uint8_t buf[sizeof(PktHeader) + sizeof(CmdPayload)];
    PktHeader  *hdr = (PktHeader *)buf;
    CmdPayload *cmd = (CmdPayload *)(buf + sizeof(PktHeader));
    memset(buf, 0, sizeof(buf));
    hdr->type = PKT_TYPE_CMD;
    hdr->robot_id = c->robot_id;
    hdr->frag_total = 1;
    hdr->payload_len = (uint16_t)sizeof(CmdPayload);
    hdr->frame_id = c->seq;
    hdr->timestamp_us = now;
    cmd->cmd_type = c->cmd_type;
    cmd->vx = c->vx;
    cmd->vy = c->vy;
    cmd->omega = c->omega;
    cmd->seq = c->seq;
    return (int)sendto(udp_fd, buf, sizeof(buf), 0,
                       (const struct sockaddr *)dst, sizeof(*dst));
}

static void track_pending(BridgeApi *api, const CmdPacket *cmd,
                          uint32_t command_id, uint64_t now, uint8_t retries) {
    pthread_mutex_lock(&api->pending_mu);
    for (int i = 0; i < PENDING_COMMANDS; i++) {
        if (!api->pending[i].in_use) {
            api->pending[i].in_use = 1;
            api->pending[i].cmd = *cmd;
            api->pending[i].requires_ack = 1;
            api->pending[i].command_id = command_id;
            api->pending[i].sent_us = now;
            api->pending[i].deadline_us = now + COMMAND_ACK_TIMEOUT_US;
            api->pending[i].retries_left = retries;
            break;
        }
    }
    pthread_mutex_unlock(&api->pending_mu);
}

int bridge_api_send_command(BridgeApi *api, int udp_fd, const CmdPacket *cmd,
                            uint8_t priority, uint8_t flags,
                            const char *tag) {
    if (!api || cmd->robot_id >= (uint8_t)api->num_robots) {
        fprintf(stderr, "[%s] invalid robot_id=%u\n", tag, cmd->robot_id);
        return -1;
    }
    struct sockaddr_in dst;
    if (!lookup_addr(api, cmd->robot_id, &dst)) {
        fprintf(stderr, "[%s] robot=%u address not learned yet\n",
                tag, cmd->robot_id);
        return -1;
    }

    uint64_t now = now_us();
    int sent = send_legacy_cmd(udp_fd, &dst, cmd, now);
    if (sent < 0) {
        perror("sendto");
        return -1;
    }

    SharedData *shm = api->shm_arr[cmd->robot_id];
    shm->metrics.last_tx_us = now;
    atomic_fetch_add(&shm->metrics.tx_commands, 1);

    uint8_t requires_ack = (flags & CMD_FLAG_REQUIRES_ACK) != 0;
    if (cmd->cmd_type == CMD_TYPE_ESTOP || cmd->cmd_type == CMD_TYPE_STOP ||
        cmd->cmd_type == CMD_TYPE_START_MISSION ||
        cmd->cmd_type == CMD_TYPE_CANCEL_MISSION) {
        requires_ack = 1;
    }
    if (requires_ack) {
        uint32_t command_id = atomic_fetch_add(&api->next_command_id, 1);
        uint8_t retries = (priority >= CMD_PRIORITY_CRITICAL) ? 3 : 1;
        track_pending(api, cmd, command_id, now, retries);
    }

    if (cmd->cmd_type != CMD_TYPE_HEARTBEAT) {
        char msg[96];
        snprintf(msg, sizeof(msg), "cmd sent: %s seq=%u",
                 cmd_name(cmd->cmd_type), cmd->seq);
        bridge_api_publish_event(api, cmd->robot_id, EVENT_SEVERITY_INFO,
                                 EVENT_TYPE_CMD_SENT, cmd->cmd_type, msg);
    }
    fprintf(stderr, "[%s] robot=%u cmd=%s seq=%u priority=%u ack=%u\n",
            tag, cmd->robot_id, cmd_name(cmd->cmd_type), cmd->seq,
            priority, requires_ack);
    return 0;
}

void bridge_api_handle_ack(BridgeApi *api, uint8_t robot_id,
                           const CmdAckPayload *ack) {
    if (!api || robot_id >= (uint8_t)api->num_robots) return;
    SharedData *shm = api->shm_arr[robot_id];
    atomic_fetch_add(&shm->metrics.ack_packets, 1);
    shm->metrics.last_ack_us = now_us();

    pthread_mutex_lock(&api->pending_mu);
    for (int i = 0; i < PENDING_COMMANDS; i++) {
        PendingCommand *p = &api->pending[i];
        if (p->in_use && p->cmd.robot_id == robot_id &&
            (p->cmd.seq == ack->seq || p->command_id == ack->command_id)) {
            p->in_use = 0;
            break;
        }
    }
    pthread_mutex_unlock(&api->pending_mu);

    pthread_rwlock_wrlock(&shm->state_lock);
    shm->state.seq++;
    shm->state.last_cmd_ack_us = shm->metrics.last_ack_us;
    if (ack->timestamp_us && shm->metrics.last_ack_us > ack->timestamp_us)
        shm->state.link_rtt_ms =
            (float)(shm->metrics.last_ack_us - ack->timestamp_us) / 1000.0f;
    pthread_rwlock_unlock(&shm->state_lock);
    bridge_api_publish_event(api, robot_id, EVENT_SEVERITY_INFO,
                             EVENT_TYPE_CMD_ACK, ack->seq, "cmd ack");
}

void bridge_api_poll_timeouts(BridgeApi *api, int udp_fd, const char *tag) {
    if (!api) return;
    uint64_t now = now_us();
    PendingCommand retry[PENDING_COMMANDS];
    int retry_count = 0;

    pthread_mutex_lock(&api->pending_mu);
    for (int i = 0; i < PENDING_COMMANDS; i++) {
        PendingCommand *p = &api->pending[i];
        if (!p->in_use || now < p->deadline_us) continue;
        if (p->retries_left > 0) {
            p->retries_left--;
            p->deadline_us = now + COMMAND_ACK_TIMEOUT_US;
            retry[retry_count++] = *p;
        } else {
            p->in_use = 0;
            bridge_api_publish_event(api, p->cmd.robot_id, EVENT_SEVERITY_ERROR,
                                     EVENT_TYPE_CMD_TIMEOUT, p->cmd.seq,
                                     "cmd ack timeout");
        }
    }
    pthread_mutex_unlock(&api->pending_mu);

    for (int i = 0; i < retry_count; i++) {
        CmdPacket cmd = retry[i].cmd;
        SharedData *shm = api->shm_arr[cmd.robot_id];
        atomic_fetch_add(&shm->metrics.retry_commands, 1);
        struct sockaddr_in dst;
        if (lookup_addr(api, cmd.robot_id, &dst))
            send_legacy_cmd(udp_fd, &dst, &cmd, now);
        fprintf(stderr, "[%s] retry robot=%u cmd=%s seq=%u\n",
                tag, cmd.robot_id, cmd_name(cmd.cmd_type), cmd.seq);
    }
}

int bridge_api_snapshot_status(BridgeApi *api, uint8_t robot_id,
                               PcStatusPacketV2 *out) {
    if (!api || robot_id >= (uint8_t)api->num_robots || !out) return -1;
    SharedData *shm = api->shm_arr[robot_id];
    memset(out, 0, sizeof(*out));
    out->robot_id = robot_id;
    pthread_rwlock_rdlock(&shm->state_lock);
    out->connected = shm->state.connected;
    out->mode = shm->state.mode;
    out->fault_level = shm->state.fault_level;
    out->x = shm->state.x;
    out->y = shm->state.y;
    out->theta = shm->state.theta;
    out->vx = shm->state.vx;
    out->vy = shm->state.vy;
    out->omega = shm->state.omega;
    out->battery_percent = shm->state.battery_percent;
    out->link_rtt_ms = shm->state.link_rtt_ms;
    out->image_fps = shm->state.image_fps;
    out->lidar_fps = shm->state.lidar_fps;
    out->odom_seq = shm->state.odom_seq;
    out->last_rx_us = shm->state.last_rx_us;
    pthread_rwlock_unlock(&shm->state_lock);
    pthread_rwlock_rdlock(&shm->meta_lock);
    out->img_drop = shm->meta.img_drop_count;
    out->lidar_drop = shm->meta.lidar_drop_count;
    pthread_rwlock_unlock(&shm->meta_lock);
    out->event_seq = (uint32_t)atomic_load(&shm->event_log.write_seq);
    out->timestamp_us = now_us();
    return 0;
}

int bridge_api_snapshot_global_path(BridgeApi *api, uint8_t robot_id,
                                    PcGlobalPathPacket *out) {
    if (!api || robot_id >= (uint8_t)api->num_robots || !out) return -1;
    SharedData *shm = api->shm_arr[robot_id];
    memset(out, 0, sizeof(*out));
    pthread_rwlock_rdlock(&shm->path_lock);
    out->robot_id = robot_id;
    out->count = shm->global_path.count;
    out->path_seq = shm->global_path.seq;
    out->timestamp_us = shm->global_path.updated_us;
    memcpy(out->waypoints, shm->global_path.waypoints, sizeof(out->waypoints));
    pthread_rwlock_unlock(&shm->path_lock);
    return 0;
}
