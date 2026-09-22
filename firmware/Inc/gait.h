#ifndef GAIT_H
#define GAIT_H

#include <stdbool.h>

/* 양 끝(0, 1)에서 도함수 0 — easing용 */
float gait_smoothstep(float t);

/*
 * 4다리 default pose를 입력 height로 smooth transition.
 * COM 균형 보존: foot_x = DEFAULT_FOOT_X 고정, foot_z = -height_mm.
 * 한계 BODY_HEIGHT_MIN_MM ~ MAX_MM. 범위 밖이면 false 반환 (자세 변경 없음).
 *
 * 호출 전제: foot_pos[]가 현재 자세를 반영하고 있어야 함.
 */
bool stand_at_height(float height_mm);

/*
 * Trot 메인 루프. n_cycles 동안 GAIT_PERIOD_MS 주기로 phase 진행.
 * 매 LOOP_PERIOD_MS: phase → foot_pos → IK → sync_write → IMU/safety
 * ENABLE_MONITOR=1이면 MONITOR_PERIOD_MS마다 모니터 블록 출력.
 */
void run_trot(int n_cycles);

#endif /* GAIT_H */
