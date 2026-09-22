#pragma once

#include <pthread.h>
#include <stdint.h>

#include "shm_def.h"

void shm_cmd_queue_init(ShmCmdQueue *q, const pthread_mutexattr_t *attr);
void shm_cmd_queue_destroy(ShmCmdQueue *q);
uint8_t shm_cmd_entry_effective_priority(const ShmCmdEntry *entry);
int shm_cmd_queue_pop_highest(ShmCmdQueue *q, ShmCmdEntry *out);
