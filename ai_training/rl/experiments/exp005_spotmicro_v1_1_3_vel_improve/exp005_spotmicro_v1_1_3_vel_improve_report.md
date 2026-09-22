# 실험 005: spotmicro_v1_1_3_vel_improve

- **날짜:** 2026-04-24 17:22
- **Task:** spotmicro_test
- **Run name:** `spotmicro_v1_1_3_vel_improve`
- **판정:** ❌ FAIL (일부 기준 미달)

---

## 실험 목적

V1.1.3: 직선속도, 각속도 추종 오차 최소화

---

## 변경점 (이전 커밋 대비)

```diff
diff --git a/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py b/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
index ee8e725..5938093 100644
--- a/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
+++ b/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
@@ -49,8 +49,8 @@ class SpotmicroTestCfg(LeggedRobotCfg):
 
     class rewards(LeggedRobotCfg.rewards):
         class scales:
-            tracking_lin_vel = 1.5
-            tracking_ang_vel = 0.9 
+            tracking_lin_vel = 2.5
+            tracking_ang_vel = 2.0 
             termination = -10.0
             lin_vel_z = -2.0
             ang_vel_xy = -0.2
@@ -118,7 +118,7 @@ class SpotmicroTestCfgPPO(LeggedRobotCfgPPO):
         entropy_coef = 0.01
 
     class runner(LeggedRobotCfgPPO.runner):
-        run_name = 'spotmicro_v1_1_2_orientation_improve'
+        run_name = 'spotmicro_v1_1_3_vel_improve'
         experiment_name = 'spotmicro_test'
         max_iterations = 1000
         save_interval = 100
```

**변경 요약:**
  - tracking_lin_vel = 1.5
  - tracking_ang_vel = 0.9
  + tracking_lin_vel = 2.5
  + tracking_ang_vel = 2.0
  - run_name = 'spotmicro_v1_1_2_orientation_improve'
  + run_name = 'spotmicro_v1_1_3_vel_improve'

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
| tracking_ang_vel | 2.0 |
| tracking_ik | 1.0 |
| tracking_lin_vel | 2.5 |
| trot_contact | 0.5 |

---

## 진단 결과 (Diagnostic)

### 핵심 지표

- ✅ Timeout: 81.5% (≥80%)
- ✅ 속도오차 X: 0.0889 m/s (<0.12)
- ❌ 토크포화: 47.5% (>40%)
- ✅ 자세: roll 3.8°, pitch 6.1° (안정)
- ✅ 조기종료: 2.5% (<5%)

### 상세 수치

| 지표 | 값 |
|------|-----|
| Timeout% | 81.52866242038218 |
| 조기종료% | 2.547770700636943 |
| 속도오차 X | 0.088941790163517 m/s |
| 속도오차 Y | 0.05416371673345566 m/s |
| 각속도오차 | 0.2676193416118622 rad/s |
| 토크포화% | 47.458184176934175 |
| 평균 높이 | 0.20035401746863887 m |
| Roll (평균) | 3.7584283351898193° |
| Pitch (평균) | 6.133426189422607° |
| Action Rate | 0.07799544930458069 |
| 평균 전력 | 18.425939559936523 W |
| CoT | 3.302237546928112 |


## 학습 곡선 분석

| 지표 | 값 | 판정 |
|------|-----|------|
| 수렴 iter (90%) | 130 | 빠름 |
| 후반 안정성 (CV) | 0.017 | 안정 |
| 정체 구간 | 없음 | |


## Per-leg 분석

### 발별 접촉/공중 비율

| 발 | 접촉% | 공중% |
|-----|-------|------|
| foot_0 | 48.9% | 51.1% |
| foot_1 | 42.9% | 57.1% |
| foot_2 | 79.6% | 20.4% |
| foot_3 | 74.5% | 25.5% |

### 관절별 토크 포화

| 관절 | 포화% | 최대토크 | 한계 | 상태 |
|------|------|---------|------|------|
| front_left_shoulder | 1.5% | 2.940 | 2.940 | ✅ |
| front_left_leg | 43.5% | 2.940 | 2.940 | ❌ |
| front_left_foot | 53.3% | 2.940 | 2.940 | ❌ |
| front_right_shoulder | 0.1% | 2.940 | 2.940 | ✅ |
| front_right_leg | 40.2% | 2.940 | 2.940 | ❌ |
| front_right_foot | 36.4% | 2.940 | 2.940 | ❌ |
| rear_left_shoulder | 22.8% | 2.940 | 2.940 | ❌ |
| rear_left_leg | 80.7% | 2.940 | 2.940 | ❌ |
| rear_left_foot | 48.0% | 2.940 | 2.940 | ❌ |
| rear_right_shoulder | 82.6% | 2.940 | 2.940 | ❌ |
| rear_right_leg | 99.2% | 2.940 | 2.940 | ❌ |
| rear_right_foot | 61.0% | 2.940 | 2.940 | ❌ |


## Gait 분석

| 지표 | 값 |
|------|-----|
| Gait 주파수 | 0.42 Hz |
| Gait 주기 | 30 steps |
| 대각 동기화율 | 63.9% |
| L/R 비대칭 | 0.0%p |
| 판정 | ⚠️ 부분적 Trot / ✅ 대칭 |


## 관절별 상세

| 관절 | 토크포화% | 사용범위% | 편향(rad) | 한계근접% | 상태 |
|------|----------|----------|----------|----------|------|
| front_left_shoulder | 1.5% | 83% | -0.114 | 1.8% | ⚠️ |
| front_left_leg | 43.5% | 31% | -1.010 | 0.0% | ⚠️ |
| front_left_foot | 53.3% | 86% | +1.204 | 0.0% | ⚠️ |
| front_right_shoulder | 0.1% | 79% | -0.113 | 0.0% | ⚠️ |
| front_right_leg | 40.2% | 36% | -0.861 | 0.0% | ⚠️ |
| front_right_foot | 36.4% | 59% | +1.708 | 0.1% | ⚠️ |
| rear_left_shoulder | 22.8% | 99% | -0.016 | 0.9% | ⚠️ |
| rear_left_leg | 80.7% | 48% | -0.854 | 0.0% | ⚠️ |
| rear_left_foot | 48.0% | 62% | +1.066 | 0.0% | ⚠️ |
| rear_right_shoulder | 82.6% | 101% | +0.000 | 4.6% | ⚠️ |
| rear_right_leg | 99.2% | 37% | -0.740 | 0.0% | ⚠️ |
| rear_right_foot | 61.0% | 67% | +1.100 | 0.0% | ⚠️ |


## 에너지 효율 상세

| 지표 | 값 |
|------|-----|
| 평균 전력 | 18.43 W |
| 피크 전력 | 33.36 W |
| 피크/평균 비율 | 1.8x |
| CoT | 3.30 |

### 관절별 전력 분배

| 관절 | 평균전력(W) | 비율% |
|------|-----------|-------|
| rear_left_leg | 3.036 | 16.5% |
| rear_right_leg | 2.962 | 16.1% |
| front_left_foot | 2.017 | 10.9% |
| front_right_foot | 1.936 | 10.5% |
| rear_left_foot | 1.719 | 9.3% |
| front_right_leg | 1.687 | 9.2% |
| front_left_leg | 1.540 | 8.4% |
| rear_right_foot | 1.486 | 8.1% |
| rear_right_shoulder | 0.987 | 5.4% |
| rear_left_shoulder | 0.653 | 3.5% |
| front_right_shoulder | 0.204 | 1.1% |
| front_left_shoulder | 0.197 | 1.1% |


## 이전 실험 대비 비교

| 지표 | 이전 (exp004) | 현재 (exp005) | 변화 |
|------|-------|-------|------|
| Timeout% | 100.0% | 81.5% | ⚠️ ↓ 18.4713% |
| 속도오차 X | 0.0871 | 0.0889 | ⚠️ ↑ 0.0018m/s |
| 토크포화 | 12.7% | 47.5% | ⚠️ ↑ 34.7932% |
| Roll | 0.9° | 3.8° | ⚠️ ↑ 2.8263° |
| Pitch | 0.9° | 6.1° | ⚠️ ↑ 5.2002° |
| 평균 전력 | 7.5602W | 18.4259W | ⚠️ ↑ 10.8657W |


## Tensorboard 학습 곡선 요약

| 스칼라 | 최종값 | 최대 | 최소 | 후반10% 평균 |
|--------|--------|------|------|-------------|
| rew_action_rate | -0.3311 | -0.0144 | -0.7980 | -0.3183 |
| rew_ang_vel_xy | -0.0886 | -0.0112 | -0.3351 | -0.0845 |
| rew_collision | 0.0000 | 0.0000 | -0.0041 | -0.0001 |
| rew_dof_acc | -0.0188 | -0.0011 | -0.0500 | -0.0183 |
| rew_dof_vel | -0.0233 | -0.0009 | -0.0426 | -0.0235 |
| rew_lin_vel_z | -0.0092 | -0.0006 | -0.0216 | -0.0085 |
| rew_orientation | -0.0508 | -0.0024 | -1.3041 | -0.0450 |
| rew_termination | 0.0000 | 0.0000 | -0.0100 | -0.0004 |
| rew_torques | -0.0658 | -0.0007 | -0.0659 | -0.0637 |
| rew_tracking_ang_vel | 1.5140 | 1.5196 | 0.0067 | 1.4581 |
| rew_tracking_ik | 0.0000 | 0.1271 | 0.0000 | 0.0000 |
| rew_tracking_lin_vel | 2.4078 | 2.4324 | 0.0218 | 2.3432 |
| rew_trot_contact | 0.4177 | 0.4370 | 0.0044 | 0.4139 |
| learning_rate | 0.0001 | 0.0076 | 0.0000 | 0.0002 |
| surrogate | -0.0016 | 0.0054 | -0.0094 | -0.0018 |
| value_function | 0.0762 | 3.9715 | 0.0017 | 0.0975 |
| collection time | 0.7621 | 0.9029 | 0.7270 | 0.7689 |
| learning_time | 0.2893 | 0.3728 | 0.2736 | 0.2872 |
| total_fps | 93498.0000 | 97185.0000 | 77559.0000 | 93124.1000 |
| mean_noise_std | 0.5442 | 0.9951 | 0.3745 | 0.5352 |
| mean_episode_length | 974.6300 | 1002.0000 | 13.0674 | 982.8525 |
| time | 974.6300 | 1002.0000 | 13.0674 | 982.8525 |
| mean_reward | 72.4435 | 75.1854 | 0.1394 | 73.2785 |
| time | 72.4435 | 75.1854 | 0.1394 | 73.2785 |

총 학습 iteration: 999


---

## 그래프

### Tensorboard 학습 곡선
![tb_training_curves.png](experiments/exp005_spotmicro_v1_1_3_vel_improve/tb_training_curves.png)
![tb_individual_rewards.png](experiments/exp005_spotmicro_v1_1_3_vel_improve/tb_individual_rewards.png)

### Diagnostic 결과
![diagnostic_report.png](experiments/exp005_spotmicro_v1_1_3_vel_improve/diagnostic_report.png)
![joint_detail.png](experiments/exp005_spotmicro_v1_1_3_vel_improve/joint_detail.png)
![action_smoothness.png](experiments/exp005_spotmicro_v1_1_3_vel_improve/action_smoothness.png)


---

## 결론 및 다음 실험 방향

**자동 판정:** ❌ FAIL (일부 기준 미달)

일부 기준을 통과하지 못했습니다. 조정이 필요합니다.
  - ❌ 토크포화: 47.5% (>40%)
**제안:** 토크 포화가 심합니다. action_scale을 줄이거나 Kp를 낮춰보세요.

> 💡 위 결론은 자동 생성되었습니다. 필요시 수정하세요.

---

## 메모

속도추종을 위해 자세를 버리면서 전체적으로 수치가 안좋아졌다. 가중치를 과하게 올리면서 tracking IK와의 비율이 깨져 생긴 문제라 판단. 우선 가중치를 이전 버전으로 되돌리고 조금씩 늘려가보며 얼마나 좋아지는지를 확인하는게 좋을것같다. 

