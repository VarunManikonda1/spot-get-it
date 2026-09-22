/**
 * frag_queue.h — fragment 큐
 *
 * jetson_rx (producer) → reassembly_shm (consumer)
 *
 * mutex + condvar 기반 블로킹 큐.
 * reassembly_shm은 condvar로 대기, jetson_rx가 push 후 signal.
 */

#pragma once

#include <stdint.h>
#include <pthread.h>
#include "proto.h"

#define FRAG_QUEUE_SIZE  512   /* 패킷 최대 512개 버퍼 */

typedef struct {
    uint8_t  buf[PROTO_PKT_MAX];
    int      len;
} FragEntry;

typedef struct {
    FragEntry       entries[FRAG_QUEUE_SIZE];
    int             head;
    int             tail;
    int             count;
    pthread_mutex_t mu;
    pthread_cond_t  cv;
    int             stop;
} FragQueue;

static inline void frag_queue_init(FragQueue *q) {
    q->head = q->tail = q->count = q->stop = 0;
    pthread_mutex_init(&q->mu, NULL);
    pthread_cond_init(&q->cv, NULL);
}

static inline void frag_queue_destroy(FragQueue *q) {
    pthread_mutex_destroy(&q->mu);
    pthread_cond_destroy(&q->cv);
}

/* 큐에 패킷 push. 큐 꽉 차면 가장 오래된 항목 덮어씀 (드롭). */
static inline void frag_queue_push(FragQueue *q,
                                   const uint8_t *buf, int len) {
    pthread_mutex_lock(&q->mu);
    if (q->count == FRAG_QUEUE_SIZE) {
        /* 큐 포화: 가장 오래된 항목 버림 */
        q->head = (q->head + 1) % FRAG_QUEUE_SIZE;
        q->count--;
    }
    FragEntry *e = &q->entries[q->tail];
    if (len > (int)PROTO_PKT_MAX) len = (int)PROTO_PKT_MAX;
    __builtin_memcpy(e->buf, buf, (size_t)len);
    e->len = len;
    q->tail = (q->tail + 1) % FRAG_QUEUE_SIZE;
    q->count++;
    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->mu);
}

/* 큐에서 패킷 pop. 빌 때까지 블로킹 대기. */
static inline int frag_queue_pop(FragQueue *q,
                                  uint8_t *buf, int max_len) {
    pthread_mutex_lock(&q->mu);
    while (q->count == 0 && !q->stop) pthread_cond_wait(&q->cv, &q->mu);
    
    if (q->stop && q->count == 0) {
        pthread_mutex_unlock(&q->mu);
        return -1;
    }

    FragEntry *e = &q->entries[q->head];
    int len = e->len;
    
    if (len > max_len) len = max_len;
    __builtin_memcpy(buf, e->buf, (size_t)len);
    q->head = (q->head + 1) % FRAG_QUEUE_SIZE;
    q->count--;
    pthread_mutex_unlock(&q->mu);
    return len;
}

/* 종료 신호 */
static inline void frag_queue_stop(FragQueue *q) {
    pthread_mutex_lock(&q->mu);
    q->stop = 1;
    pthread_cond_broadcast(&q->cv);
    pthread_mutex_unlock(&q->mu);
}
