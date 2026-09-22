# 실험 001: spotmicro_v1_0_ik_tracking

- **날짜:** 2026-04-24 15:05
- **Task:** spotmicro_test
- **Run name:** `spotmicro_v1_0_ik_tracking`
- **판정:** ✅ PASS

---

## 실험 목적

V1.0: default IK 동작을 모방하여 보행, 환경에 따른 변수를 RL로 보정

---

## 변경점 (이전 커밋 대비)

```diff
(변경 사항 없음 또는 git 미설정)
```

**변경 요약:**
(변경 사항 없음 또는 git 미설정)

---

## 현재 Reward Scales

| Reward | Scale |
|--------|-------|
| action_rate | -0.05 |
| ang_vel_xy | -0.05 |
| collision | -1.0 |
| dof_acc | -2.5e-07 |
| dof_vel | -0.001 |
| lin_vel_z | -2.0 |
| orientation | -1.0 |
| termination | -10.0 |
| torques | -0.001 |
| tracking_ang_vel | 0.9 |
| tracking_ik | 1.0 |
| tracking_lin_vel | 1.0 |
| trot_contact | 0.5 |

---

## 진단 결과 (Diagnostic)

### 핵심 지표

- ✅ Timeout: 100.0% (≥80%)
- ✅ 속도오차 X: 0.1025 m/s (<0.12)
- ⚠️ 토크포화: 16.4% (10~40%)
- ✅ 자세: roll 1.8°, pitch 1.3° (안정)
- ✅ 조기종료: 0.0% (<5%)

### 상세 수치

| 지표 | 값 |
|------|-----|
| Timeout% | 100.0 |
| 조기종료% | 0.0 |
| 속도오차 X | 0.10251015424728394 m/s |
| 속도오차 Y | 0.040156763046979904 m/s |
| 각속도오차 | 0.08413916081190109 rad/s |
| 토크포화% | 16.43087814962815 |
| 평균 높이 | 0.21846975378203384 m |
| Roll (평균) | 1.785449504852295° |
| Pitch (평균) | 1.3066006898880005° |
| Action Rate | 0.005130550358444452 |
| 평균 전력 | 7.793137073516846 W |
| CoT | 1.7522865127823286 |


## 학습 곡선 분석

| 지표 | 값 | 판정 |
|------|-----|------|
| 수렴 iter (90%) | 215 | 빠름 |
| 후반 안정성 (CV) | 0.009 | 안정 |
| 정체 구간 | 있음 (iter 233, 49 iter) | |


## Per-leg 분석

### 발별 접촉/공중 비율

| 발 | 접촉% | 공중% |
|-----|-------|------|
| foot_0 | 54.1% | 45.9% |
| foot_1 | 62.4% | 37.6% |
| foot_2 | 54.8% | 45.2% |
| foot_3 | 47.6% | 52.4% |

### 관절별 토크 포화

| 관절 | 포화% | 최대토크 | 한계 | 상태 |
|------|------|---------|------|------|
| front_left_shoulder | 0.0% | 2.940 | 2.940 | ✅ |
| front_left_leg | 24.4% | 2.940 | 2.940 | ❌ |
| front_left_foot | 7.1% | 2.940 | 2.940 | ⚠️ |
| front_right_shoulder | 0.0% | 2.337 | 2.940 | ✅ |
| front_right_leg | 52.6% | 2.940 | 2.940 | ❌ |
| front_right_foot | 8.5% | 2.940 | 2.940 | ⚠️ |
| rear_left_shoulder | 0.0% | 2.444 | 2.940 | ✅ |
| rear_left_leg | 21.0% | 2.940 | 2.940 | ❌ |
| rear_left_foot | 5.8% | 2.940 | 2.940 | ⚠️ |
| rear_right_shoulder | 0.0% | 1.760 | 2.940 | ✅ |
| rear_right_leg | 63.3% | 2.940 | 2.940 | ❌ |
| rear_right_foot | 14.6% | 2.940 | 2.940 | ⚠️ |


## Gait 분석

| 지표 | 값 |
|------|-----|
| Gait 주파수 | 0.42 Hz |
| Gait 주기 | 30 steps |
| 대각 동기화율 | 91.6% |
| L/R 비대칭 | 0.0%p |
| 판정 | ✅ Trot 패턴 / ✅ 대칭 |


## 관절별 상세

| 관절 | 토크포화% | 사용범위% | 편향(rad) | 한계근접% | 상태 |
|------|----------|----------|----------|----------|------|
| front_left_shoulder | 0.0% | 24% | +0.049 | 0.0% | ✅ |
| front_left_leg | 24.4% | 12% | -0.696 | 0.0% | ⚠️ |
| front_left_foot | 7.1% | 25% | +1.250 | 0.0% | ⚠️ |
| front_right_shoulder | 0.0% | 16% | -0.002 | 0.0% | ✅ |
| front_right_leg | 52.6% | 13% | -0.765 | 0.0% | ⚠️ |
| front_right_foot | 8.5% | 25% | +1.221 | 0.0% | ⚠️ |
| rear_left_shoulder | 0.0% | 16% | -0.002 | 0.0% | ✅ |
| rear_left_leg | 21.0% | 11% | -0.702 | 0.0% | ⚠️ |
| rear_left_foot | 5.8% | 23% | +1.282 | 0.0% | ⚠️ |
| rear_right_shoulder | 0.0% | 20% | +0.030 | 0.0% | ✅ |
| rear_right_leg | 63.3% | 17% | -0.794 | 0.0% | ⚠️ |
| rear_right_foot | 14.6% | 21% | +1.244 | 0.0% | ⚠️ |


## 에너지 효율 상세

| 지표 | 값 |
|------|-----|
| 평균 전력 | 7.79 W |
| 피크 전력 | 36.85 W |
| 피크/평균 비율 | 4.7x |
| CoT | 1.75 |

### 관절별 전력 분배

| 관절 | 평균전력(W) | 비율% |
|------|-----------|-------|
| front_right_foot | 1.427 | 18.3% |
| front_left_foot | 1.298 | 16.7% |
| rear_right_leg | 1.080 | 13.9% |
| rear_right_foot | 1.015 | 13.0% |
| rear_left_leg | 0.872 | 11.2% |
| front_right_leg | 0.743 | 9.5% |
| rear_left_foot | 0.598 | 7.7% |
| front_left_leg | 0.547 | 7.0% |
| rear_left_shoulder | 0.072 | 0.9% |
| front_right_shoulder | 0.063 | 0.8% |
| rear_right_shoulder | 0.045 | 0.6% |
| front_left_shoulder | 0.033 | 0.4% |



## Tensorboard 학습 곡선 요약

| 스칼라 | 최종값 | 최대 | 최소 | 후반10% 평균 |
|--------|--------|------|------|-------------|
| rew_action_rate | -0.0524 | -0.0143 | -0.6553 | -0.0490 |
| rew_ang_vel_xy | -0.0156 | -0.0018 | -0.0684 | -0.0146 |
| rew_collision | 0.0000 | 0.0000 | -0.0008 | 0.0000 |
| rew_dof_acc | -0.0075 | -0.0008 | -0.0499 | -0.0072 |
| rew_dof_vel | -0.0107 | -0.0006 | -0.0415 | -0.0101 |
| rew_lin_vel_z | -0.0030 | -0.0004 | -0.0259 | -0.0027 |
| rew_orientation | -0.0041 | -0.0004 | -0.7909 | -0.0039 |
| rew_termination | -0.0004 | 0.0000 | -0.0100 | -0.0000 |
| rew_torques | -0.0311 | -0.0006 | -0.0489 | -0.0308 |
| rew_tracking_ang_vel | 0.6851 | 0.7283 | 0.0032 | 0.7116 |
| rew_tracking_ik | 0.6507 | 0.7034 | 0.0012 | 0.6848 |
| rew_tracking_lin_vel | 0.9253 | 0.9661 | 0.0086 | 0.9499 |
| rew_trot_contact | 0.4486 | 0.4623 | 0.0046 | 0.4581 |
| learning_rate | 0.0006 | 0.0076 | 0.0000 | 0.0003 |
| surrogate | -0.0005 | 0.0026 | -0.0131 | -0.0019 |
| value_function | 0.0181 | 0.3101 | 0.0004 | 0.0303 |
| collection time | 0.7259 | 0.9004 | 0.6744 | 0.7220 |
| learning_time | 0.2879 | 0.3427 | 0.2717 | 0.2862 |
| total_fps | 96965.0000 | 102483.0000 | 82207.0000 | 97550.7800 |
| mean_noise_std | 0.2059 | 0.9949 | 0.1677 | 0.1951 |
| mean_episode_length | 988.7500 | 1002.0000 | 12.8276 | 999.8567 |
| time | 988.7500 | 1002.0000 | 12.8276 | 999.8567 |
| mean_reward | 52.4059 | 54.2645 | -0.0923 | 53.7354 |
| time | 52.4059 | 54.2645 | -0.0923 | 53.7354 |

총 학습 iteration: 999


---

## 그래프

### Tensorboard 학습 곡선
![tb_training_curves.png](experiments/exp001_spotmicro_v1_0_ik_tracking/tb_training_curves.png)
![tb_individual_rewards.png](experiments/exp001_spotmicro_v1_0_ik_tracking/tb_individual_rewards.png)

### Diagnostic 결과
![diagnostic_report.png](experiments/exp001_spotmicro_v1_0_ik_tracking/diagnostic_report.png)
![joint_detail.png](experiments/exp001_spotmicro_v1_0_ik_tracking/joint_detail.png)
![action_smoothness.png](experiments/exp001_spotmicro_v1_0_ik_tracking/action_smoothness.png)


---

## 결론 및 다음 실험 방향

**자동 판정:** ✅ PASS

대부분 기준을 통과했으나 일부 항목이 보통 수준입니다.
  - ⚠️ 토크포화: 16.4% (10~40%)

> 💡 위 결론은 자동 생성되었습니다. 필요시 수정하세요.

---

## 메모

_(추가 메모가 있으면 여기에 작성)_

