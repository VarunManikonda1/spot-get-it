# 실험 004: spotmicro_v1_1_2_orientation_improve

- **날짜:** 2026-04-24 16:48
- **Task:** spotmicro_test
- **Run name:** `spotmicro_v1_1_2_orientation_improve`
- **판정:** ✅ PASS

---

## 실험 목적

V1.1.2: 추종을 유지하며 줄 수 있는 페널티 수치 확인

---

## 변경점 (이전 커밋 대비)

```diff
diff --git a/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py b/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
index 196707a..ee8e725 100644
--- a/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
+++ b/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
@@ -53,8 +53,8 @@ class SpotmicroTestCfg(LeggedRobotCfg):
             tracking_ang_vel = 0.9 
             termination = -10.0
             lin_vel_z = -2.0
-            ang_vel_xy = -0.1
-            orientation = -2.0
+            ang_vel_xy = -0.2
+            orientation = -4.0
             torques = -0.001
             dof_vel = -0.001
             dof_acc = -2.5e-7
@@ -118,7 +118,7 @@ class SpotmicroTestCfgPPO(LeggedRobotCfgPPO):
         entropy_coef = 0.01
 
     class runner(LeggedRobotCfgPPO.runner):
-        run_name = 'spotmicro_v1_1_1_lin_vel_improve'
+        run_name = 'spotmicro_v1_1_2_orientation_improve'
         experiment_name = 'spotmicro_test'
         max_iterations = 1000
         save_interval = 100
```

**변경 요약:**
  - ang_vel_xy = -0.1
  - orientation = -2.0
  + ang_vel_xy = -0.2
  + orientation = -4.0
  - run_name = 'spotmicro_v1_1_1_lin_vel_improve'
  + run_name = 'spotmicro_v1_1_2_orientation_improve'

---

## 현재 Reward Scales

| Reward | Scale |
|--------|-------|
| action_rate | -0.05 |
| ang_vel_xy | -0.2 |
| collision | -1.0 |
| dof_acc | -2.5e-07 |
| dof_vel | -0.001 |
| lin_vel_z | -2.0 |
| orientation | -4.0 |
| termination | -10.0 |
| torques | -0.001 |
| tracking_ang_vel | 0.9 |
| tracking_ik | 1.0 |
| tracking_lin_vel | 1.5 |
| trot_contact | 0.5 |

---

## 진단 결과 (Diagnostic)

### 핵심 지표

- ✅ Timeout: 100.0% (≥80%)
- ✅ 속도오차 X: 0.0871 m/s (<0.12)
- ⚠️ 토크포화: 12.7% (10~40%)
- ✅ 자세: roll 0.9°, pitch 0.9° (안정)
- ✅ 조기종료: 0.0% (<5%)

### 상세 수치

| 지표 | 값 |
|------|-----|
| Timeout% | 100.0 |
| 조기종료% | 0.0 |
| 속도오차 X | 0.08709336817264557 m/s |
| 속도오차 Y | 0.020574402064085007 m/s |
| 각속도오차 | 0.08779294788837433 rad/s |
| 토크포화% | 12.664982586857587 |
| 평균 높이 | 0.21925541703458076 m |
| Roll (평균) | 0.9321458339691162° |
| Pitch (평균) | 0.9332594871520996° |
| Action Rate | 0.0039540682919323444 |
| 평균 전력 | 7.56022834777832 W |
| CoT | 1.5662537512200045 |


## 학습 곡선 분석

| 지표 | 값 | 판정 |
|------|-----|------|
| 수렴 iter (90%) | 213 | 빠름 |
| 후반 안정성 (CV) | 0.006 | 안정 |
| 정체 구간 | 있음 (iter 449, 49 iter) | |


## Per-leg 분석

### 발별 접촉/공중 비율

| 발 | 접촉% | 공중% |
|-----|-------|------|
| foot_0 | 56.1% | 43.9% |
| foot_1 | 55.4% | 44.6% |
| foot_2 | 53.5% | 46.5% |
| foot_3 | 51.0% | 49.0% |

### 관절별 토크 포화

| 관절 | 포화% | 최대토크 | 한계 | 상태 |
|------|------|---------|------|------|
| front_left_shoulder | 0.0% | 1.472 | 2.940 | ✅ |
| front_left_leg | 21.2% | 2.940 | 2.940 | ❌ |
| front_left_foot | 11.3% | 2.940 | 2.940 | ⚠️ |
| front_right_shoulder | 0.0% | 2.898 | 2.940 | ✅ |
| front_right_leg | 21.7% | 2.940 | 2.940 | ❌ |
| front_right_foot | 11.7% | 2.940 | 2.940 | ⚠️ |
| rear_left_shoulder | 0.1% | 2.940 | 2.940 | ✅ |
| rear_left_leg | 24.7% | 2.940 | 2.940 | ❌ |
| rear_left_foot | 3.1% | 2.940 | 2.940 | ✅ |
| rear_right_shoulder | 0.0% | 1.601 | 2.940 | ✅ |
| rear_right_leg | 54.1% | 2.940 | 2.940 | ❌ |
| rear_right_foot | 4.2% | 2.940 | 2.940 | ✅ |


## Gait 분석

| 지표 | 값 |
|------|-----|
| Gait 주파수 | 0.42 Hz |
| Gait 주기 | 30 steps |
| 대각 동기화율 | 94.1% |
| L/R 비대칭 | 0.0%p |
| 판정 | ✅ Trot 패턴 / ✅ 대칭 |


## 관절별 상세

| 관절 | 토크포화% | 사용범위% | 편향(rad) | 한계근접% | 상태 |
|------|----------|----------|----------|----------|------|
| front_left_shoulder | 0.0% | 18% | -0.016 | 0.0% | ✅ |
| front_left_leg | 21.2% | 10% | -0.704 | 0.0% | ⚠️ |
| front_left_foot | 11.3% | 25% | +1.243 | 0.0% | ⚠️ |
| front_right_shoulder | 0.0% | 22% | +0.004 | 0.0% | ✅ |
| front_right_leg | 21.7% | 9% | -0.698 | 0.0% | ⚠️ |
| front_right_foot | 11.7% | 24% | +1.286 | 0.0% | ⚠️ |
| rear_left_shoulder | 0.1% | 17% | +0.043 | 0.0% | ✅ |
| rear_left_leg | 24.7% | 11% | -0.648 | 0.0% | ⚠️ |
| rear_left_foot | 3.1% | 22% | +1.248 | 0.0% | ⚠️ |
| rear_right_shoulder | 0.0% | 17% | +0.032 | 0.0% | ✅ |
| rear_right_leg | 54.1% | 14% | -0.765 | 0.0% | ⚠️ |
| rear_right_foot | 4.2% | 23% | +1.238 | 0.0% | ⚠️ |


## 에너지 효율 상세

| 지표 | 값 |
|------|-----|
| 평균 전력 | 7.56 W |
| 피크 전력 | 32.24 W |
| 피크/평균 비율 | 4.3x |
| CoT | 1.57 |

### 관절별 전력 분배

| 관절 | 평균전력(W) | 비율% |
|------|-----------|-------|
| front_right_foot | 1.526 | 20.2% |
| front_left_foot | 1.499 | 19.8% |
| rear_right_leg | 1.075 | 14.2% |
| rear_left_leg | 0.860 | 11.4% |
| rear_right_foot | 0.680 | 9.0% |
| front_left_leg | 0.577 | 7.6% |
| front_right_leg | 0.548 | 7.3% |
| rear_left_foot | 0.507 | 6.7% |
| rear_left_shoulder | 0.095 | 1.3% |
| front_right_shoulder | 0.083 | 1.1% |
| rear_right_shoulder | 0.073 | 1.0% |
| front_left_shoulder | 0.037 | 0.5% |


## 이전 실험 대비 비교

| 지표 | 이전 (exp003) | 현재 (exp004) | 변화 |
|------|-------|-------|------|
| Timeout% | 100.0% | 100.0% | → 유지 |
| 속도오차 X | 0.0825 | 0.0871 | ⚠️ ↑ 0.0046m/s |
| 토크포화 | 14.2% | 12.7% | ✅ ↓ 1.4947% |
| Roll | 2.1° | 0.9° | ✅ ↓ 1.1269° |
| Pitch | 0.9° | 0.9° | ⚠️ ↑ 0.0379° |
| 평균 전력 | 7.9069W | 7.5602W | ✅ ↓ 0.3466W |


## Tensorboard 학습 곡선 요약

| 스칼라 | 최종값 | 최대 | 최소 | 후반10% 평균 |
|--------|--------|------|------|-------------|
| rew_action_rate | -0.0463 | -0.0144 | -0.7068 | -0.0452 |
| rew_ang_vel_xy | -0.0460 | -0.0112 | -0.3083 | -0.0438 |
| rew_collision | 0.0000 | 0.0000 | -0.0011 | 0.0000 |
| rew_dof_acc | -0.0093 | -0.0011 | -0.0627 | -0.0091 |
| rew_dof_vel | -0.0121 | -0.0009 | -0.0487 | -0.0112 |
| rew_lin_vel_z | -0.0042 | -0.0006 | -0.0239 | -0.0037 |
| rew_orientation | -0.0057 | -0.0024 | -2.0969 | -0.0067 |
| rew_termination | 0.0000 | 0.0000 | -0.0100 | -0.0000 |
| rew_torques | -0.0321 | -0.0007 | -0.0597 | -0.0312 |
| rew_tracking_ang_vel | 0.6341 | 0.6468 | 0.0030 | 0.6285 |
| rew_tracking_ik | 0.7040 | 0.7310 | 0.0011 | 0.7048 |
| rew_tracking_lin_vel | 1.4348 | 1.4637 | 0.0131 | 1.4396 |
| rew_trot_contact | 0.4702 | 0.4723 | 0.0044 | 0.4681 |
| learning_rate | 0.0000 | 0.0051 | 0.0000 | 0.0003 |
| surrogate | -0.0005 | 0.0050 | -0.0134 | -0.0024 |
| value_function | 0.1475 | 0.9139 | 0.0004 | 0.0871 |
| collection time | 0.7523 | 1.0758 | 0.7053 | 0.7673 |
| learning_time | 0.2908 | 0.3451 | 0.2685 | 0.2885 |
| total_fps | 94242.0000 | 99163.0000 | 71518.0000 | 93177.7800 |
| mean_noise_std | 0.1911 | 0.9964 | 0.1521 | 0.1839 |
| mean_episode_length | 1002.0000 | 1002.0000 | 13.0674 | 998.4939 |
| time | 1002.0000 | 1002.0000 | 13.0674 | 998.4939 |
| mean_reward | 61.8936 | 62.5182 | -0.1082 | 61.6679 |
| time | 61.8936 | 62.5182 | -0.1082 | 61.6679 |

총 학습 iteration: 999


---

## 그래프

### Tensorboard 학습 곡선
![tb_training_curves.png](experiments/exp004_spotmicro_v1_1_2_orientation_improve/tb_training_curves.png)
![tb_individual_rewards.png](experiments/exp004_spotmicro_v1_1_2_orientation_improve/tb_individual_rewards.png)

### Diagnostic 결과
![diagnostic_report.png](experiments/exp004_spotmicro_v1_1_2_orientation_improve/diagnostic_report.png)
![joint_detail.png](experiments/exp004_spotmicro_v1_1_2_orientation_improve/joint_detail.png)
![action_smoothness.png](experiments/exp004_spotmicro_v1_1_2_orientation_improve/action_smoothness.png)


---

## 결론 및 다음 실험 방향

**자동 판정:** ✅ PASS

대부분 기준을 통과했으나 일부 항목이 보통 수준입니다.
  - ⚠️ 토크포화: 12.7% (10~40%)

> 💡 위 결론은 자동 생성되었습니다. 필요시 수정하세요.

---

## 메모

roll 감소가 눈에 띄게 좋아졌다. 그러면서도 추종도는 크게 나빠지지 않았다. 안정성 관련 가중치는 이렇게 유지하면서 추종 오차를 더 줄일 
수 있을지 보자. 이 때 trot 형태(IK)를 유지해야 함
