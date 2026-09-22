#include "shm_cmd_queue.h"

#include <stdatomic.h>
#include <string.h>

void shm_cmd_queue_init(ShmCmdQueue *q, const pthread_mutexattr_t *attr) {
    pthread_mutex_init(&q->mu, attr);
    atomic_store(&q->head, 0);
    atomic_store(&q->tail, 0);
    atomic_store(&q->count, 0);
    atomic_store(&q->write_seq, 0);
    atomic_store(&q->drop_count, 0);
    memset(q->entries, 0, sizeof(q->entries));
}

void shm_cmd_queue_destroy(ShmCmdQueue *q) {
    pthread_mutex_destroy(&q->mu);
}

uint8_t shm_cmd_entry_effective_priority(const ShmCmdEntry *entry) {
    if (entry->priority != 0)
        return entry->priority;
    return (entry->cmd.cmd_type == CMD_TYPE_ESTOP)
        ? CMD_PRIORITY_CRITICAL : CMD_PRIORITY_NORMAL;
}

int shm_cmd_queue_pop_highest(ShmCmdQueue *q, ShmCmdEntry *out) {
    int popped = 0;
    pthread_mutex_lock(&q->mu);
    int count = atomic_load_explicit(&q->count, memory_order_acquire);
    if (count > 0) {
        int head = atomic_load_explicit(&q->head, memory_order_relaxed);
        int best_offset = 0;
        uint8_t best_priority = shm_cmd_entry_effective_priority(&q->entries[head]);

        for (int offset = 1; offset < count; offset++) {
            int idx = (head + offset) % SHM_CMD_QUEUE_SIZE;
            uint8_t priority = shm_cmd_entry_effective_priority(&q->entries[idx]);
            if (priority > best_priority) {
                best_priority = priority;
                best_offset = offset;
            }
        }

        int best_idx = (head + best_offset) % SHM_CMD_QUEUE_SIZE;
        *out = q->entries[best_idx];

        for (int offset = best_offset; offset > 0; offset--) {
            int dst = (head + offset) % SHM_CMD_QUEUE_SIZE;
            int src = (head + offset - 1) % SHM_CMD_QUEUE_SIZE;
            q->entries[dst] = q->entries[src];
        }

        head = (head + 1) % SHM_CMD_QUEUE_SIZE;
        atomic_store_explicit(&q->head, head, memory_order_release);
        atomic_store_explicit(&q->count, count - 1, memory_order_release);
        popped = 1;
    }
    pthread_mutex_unlock(&q->mu);
    return popped;
}
