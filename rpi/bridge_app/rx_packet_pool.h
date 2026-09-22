#pragma once

#include <pthread.h>
#include <stdint.h>

#include "proto.h"

#define RX_PACKET_POOL_SIZE 4096
#define RX_SLOT_INVALID     (-1)

typedef struct {
    uint8_t buf[PROTO_PKT_MAX];
    int     len;
    uint8_t in_use;
} RxPacketSlot;

typedef struct {
    RxPacketSlot  slots[RX_PACKET_POOL_SIZE];
    int           free_stack[RX_PACKET_POOL_SIZE];
    int           free_count;
    pthread_mutex_t mu;
} RxPacketPool;

void rx_packet_pool_init(RxPacketPool *pool);
void rx_packet_pool_destroy(RxPacketPool *pool);
int  rx_packet_pool_acquire(RxPacketPool *pool);
void rx_packet_pool_release(RxPacketPool *pool, int slot_id);
int  rx_packet_pool_free_count(RxPacketPool *pool);
