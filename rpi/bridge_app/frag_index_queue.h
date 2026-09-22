#pragma once

#include <pthread.h>

#include "rx_packet_pool.h"

#define FRAG_QUEUE_SIZE 512

typedef struct {
    int             slot_ids[FRAG_QUEUE_SIZE];
    int             head;
    int             tail;
    int             count;
    pthread_mutex_t mu;
    pthread_cond_t  cv;
    int             stop;
} FragIndexQueue;

static inline void frag_index_queue_init(FragIndexQueue *q) {
    q->head = q->tail = q->count = q->stop = 0;
    pthread_mutex_init(&q->mu, NULL);
    pthread_cond_init(&q->cv, NULL);
}

static inline void frag_index_queue_destroy(FragIndexQueue *q) {
    pthread_mutex_destroy(&q->mu);
    pthread_cond_destroy(&q->cv);
}

static inline void frag_index_queue_push(FragIndexQueue *q, int slot_id,
                                         int *dropped_slot_id) {
    if (dropped_slot_id) *dropped_slot_id = RX_SLOT_INVALID;

    pthread_mutex_lock(&q->mu);
    if (q->count == FRAG_QUEUE_SIZE) {
        int old_slot_id = q->slot_ids[q->head];
        q->head = (q->head + 1) % FRAG_QUEUE_SIZE;
        q->count--;
        if (dropped_slot_id) *dropped_slot_id = old_slot_id;
    }

    q->slot_ids[q->tail] = slot_id;
    q->tail = (q->tail + 1) % FRAG_QUEUE_SIZE;
    q->count++;
    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->mu);
}

static inline int frag_index_queue_pop(FragIndexQueue *q) {
    pthread_mutex_lock(&q->mu);
    while (q->count == 0 && !q->stop) pthread_cond_wait(&q->cv, &q->mu);

    if (q->stop && q->count == 0) {
        pthread_mutex_unlock(&q->mu);
        return RX_SLOT_INVALID;
    }

    int slot_id = q->slot_ids[q->head];
    q->head = (q->head + 1) % FRAG_QUEUE_SIZE;
    q->count--;
    pthread_mutex_unlock(&q->mu);
    return slot_id;
}

static inline void frag_index_queue_stop(FragIndexQueue *q) {
    pthread_mutex_lock(&q->mu);
    q->stop = 1;
    pthread_cond_broadcast(&q->cv);
    pthread_mutex_unlock(&q->mu);
}
