/**
 * bridge_ctx.h — 스레드 컨텍스트 구조체 공유 헤더
 */

#pragma once

#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>
#include "shm_def.h"
#include "frag_index_queue.h"
#include "rx_packet_pool.h"

/* ─── Jetson 주소 테이블 ─────────────────────────────────────── */
typedef struct {
    pthread_mutex_t    mu;
    struct sockaddr_in addr[MAX_ROBOTS];
    int                set[MAX_ROBOTS];
} JetsonAddrTable;

typedef struct {
    pthread_mutex_t    mu;
    struct sockaddr_in addr;
    int                set;
    int                fd;
} PcCommandPeer;

typedef struct {
    uint8_t  in_use;
    uint8_t  requires_ack;
    CmdPacket cmd;
    uint32_t command_id;
    uint64_t sent_us;
    uint64_t deadline_us;
    uint8_t  retries_left;
} PendingCommand;

#define PENDING_COMMANDS 128

typedef struct {
    JetsonAddrTable *addr_table;
    SharedData      *shm_arr[MAX_ROBOTS];
    int              num_robots;
    pthread_mutex_t  event_mu;
    pthread_mutex_t  pending_mu;
    PendingCommand   pending[PENDING_COMMANDS];
    atomic_uint      next_command_id;
} BridgeApi;

/* ─── jetson_rx ─────────────────────────────────────────────── */
typedef struct {
    SharedData      *shm_arr[MAX_ROBOTS];
    FragIndexQueue  *fq_arr[MAX_ROBOTS];
    RxPacketPool    *rx_pool;
    JetsonAddrTable *addr_table;
    PcCommandPeer   *pc_peer;
    int              num_robots;
    atomic_bool     *stop;
    BridgeApi       *api;
} JetsonRxCtx;

/* ─── jetson_tx ─────────────────────────────────────────────── */
typedef struct {
    JetsonAddrTable *addr_table;
    PcCommandPeer   *pc_peer;
    SharedData      *shm_arr[MAX_ROBOTS];
    int              num_robots;
    atomic_bool     *stop;
    BridgeApi       *api;
} JetsonTxCtx;

/* ─── protocol_timer ────────────────────────────────────────── */
typedef struct {
    JetsonAddrTable *addr_table;
    int              num_robots;
    atomic_bool     *stop;
    BridgeApi       *api;
} ProtoTimerCtx;

/* ─── pc_link ───────────────────────────────────────────────── */
typedef struct {
    JetsonAddrTable *addr_table;
    PcCommandPeer   *pc_peer;
    SharedData      *shm_arr[MAX_ROBOTS];
    int              num_robots;
    atomic_bool     *stop;
    BridgeApi       *api;
} PcLinkCtx;

/* ─── reassembly_shm ────────────────────────────────────────── */
typedef struct {
    SharedData *shm;
    FragIndexQueue *fq;
    RxPacketPool   *rx_pool;
    BridgeApi  *api;
    uint8_t     robot_id;
} ReasmCtx;
