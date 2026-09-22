#include "rx_packet_pool.h"

#include <string.h>

void rx_packet_pool_init(RxPacketPool *pool) {
    memset(pool, 0, sizeof(*pool));
    pthread_mutex_init(&pool->mu, NULL);
    pool->free_count = RX_PACKET_POOL_SIZE;
    for (int i = 0; i < RX_PACKET_POOL_SIZE; i++) {
        pool->free_stack[i] = RX_PACKET_POOL_SIZE - 1 - i;
    }
}

void rx_packet_pool_destroy(RxPacketPool *pool) {
    pthread_mutex_destroy(&pool->mu);
}

int rx_packet_pool_acquire(RxPacketPool *pool) {
    pthread_mutex_lock(&pool->mu);
    if (pool->free_count == 0) {
        pthread_mutex_unlock(&pool->mu);
        return RX_SLOT_INVALID;
    }

    int slot_id = pool->free_stack[--pool->free_count];
    pool->slots[slot_id].len = 0;
    pool->slots[slot_id].in_use = 1;
    pthread_mutex_unlock(&pool->mu);
    return slot_id;
}

void rx_packet_pool_release(RxPacketPool *pool, int slot_id) {
    if (slot_id < 0 || slot_id >= RX_PACKET_POOL_SIZE) return;

    pthread_mutex_lock(&pool->mu);
    if (pool->slots[slot_id].in_use) {
        pool->slots[slot_id].in_use = 0;
        pool->slots[slot_id].len = 0;
        if (pool->free_count < RX_PACKET_POOL_SIZE) {
            pool->free_stack[pool->free_count++] = slot_id;
        }
    }
    pthread_mutex_unlock(&pool->mu);
}

int rx_packet_pool_free_count(RxPacketPool *pool) {
    pthread_mutex_lock(&pool->mu);
    int free_count = pool->free_count;
    pthread_mutex_unlock(&pool->mu);
    return free_count;
}
