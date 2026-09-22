/**
 * utils.h — 공통 유틸리티
 */

#pragma once

#include <stdint.h>
#include <time.h>

/* ─── 단조 시계 (CLOCK_MONOTONIC) 기반 마이크로초 타임스탬프 ── */
static inline uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000;
}
