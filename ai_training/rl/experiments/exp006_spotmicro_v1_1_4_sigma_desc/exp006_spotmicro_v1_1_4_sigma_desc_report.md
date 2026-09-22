# 실험 006: spotmicro_v1_1_4_sigma_desc

- **날짜:** 2026-04-27 09:51
- **Task:** spotmicro_test
- **Run name:** `spotmicro_v1_1_4_sigma_desc`
- **판정:** ✅ PASS

---

## 실험 목적

V1.1.4: tracking sigma 조정으로 민감도를 올림, 속도 추종도 얼마나 좋아지는지 확인

---

## 변경점 (이전 커밋 대비)

```diff
diff --git a/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py b/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
index 5938093..86d6f70 100644
--- a/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
+++ b/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
@@ -49,8 +49,8 @@ class SpotmicroTestCfg(LeggedRobotCfg):
 
     class rewards(LeggedRobotCfg.rewards):
         class scales:
-            tracking_lin_vel = 2.5
-            tracking_ang_vel = 2.0 
+            tracking_lin_vel = 1.5
+            tracking_ang_vel = 0.9 
             termination = -10.0
             lin_vel_z = -2.0
             ang_vel_xy = -0.2
@@ -70,6 +70,7 @@ class SpotmicroTestCfg(LeggedRobotCfg):
             tracking_ik = 1.0
         soft_dof_pos_limit = 0.9
         base_height_target = 0.206
+        tracking_sigma = 0.15
 
     class normalization(LeggedRobotCfg.normalization):
         class obs_scales:
@@ -118,7 +119,7 @@ class SpotmicroTestCfgPPO(LeggedRobotCfgPPO):
         entropy_coef = 0.01
 
     class runner(LeggedRobotCfgPPO.runner):
-        run_name = 'spotmicro_v1_1_3_vel_improve'
+        run_name = 'spotmicro_v1_1_4_sigma_desc'
         experiment_name = 'spotmicro_test'
         max_iterations = 1000
         save_interval = 100
```

**변경 요약:**
  - tracking_lin_vel = 2.5
  - tracking_ang_vel = 2.0
  + tracking_lin_vel = 1.5
  + tracking_ang_vel = 0.9
  + tracking_sigma = 0.15
  - run_name = 'spotmicro_v1_1_3_vel_improve'
  + run_name = 'spotmicro_v1_1_4_sigma_desc'

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
- ✅ 속도오차 X: 0.0600 m/s (<0.12)
- ⚠️ 토크포화: 13.6% (10~40%)
- ✅ 자세: roll 0.9°, pitch 1.1° (안정)
- ✅ 조기종료: 0.0% (<5%)

### 상세 수치

| 지표 | 값 |
|------|-----|
| Timeout% | 100.0 |
| 조기종료% | 0.0 |
| 속도오차 X | 0.059980712831020355 m/s |
| 속도오차 Y | 0.026150239631533623 m/s |
| 각속도오차 | 0.09492485970258713 rad/s |
| 토크포화% | 13.59417492229992 |
| 평균 높이 | 0.21942754760707095 m |
| Roll (평균) | 0.9189366698265076° |
| Pitch (평균) | 1.0870587825775146° |
| Action Rate | 0.0032488955184817314 |
| 평균 전력 | 8.484457969665527 W |
| CoT | 1.505792148531939 |


## 학습 곡선 분석

| 지표 | 값 | 판정 |
|------|-----|------|
| 수렴 iter (90%) | 213 | 빠름 |
| 후반 안정성 (CV) | 0.010 | 안정 |
| 정체 구간 | 있음 (iter 212, 49 iter) | |


## Per-leg 분석

### 발별 접촉/공중 비율

| 발 | 접촉% | 공중% |
|-----|-------|------|
| foot_0 | 55.7% | 44.3% |
| foot_1 | 57.0% | 43.0% |
| foot_2 | 53.1% | 46.9% |
| foot_3 | 49.3% | 50.7% |

### 관절별 토크 포화

| 관절 | 포화% | 최대토크 | 한계 | 상태 |
|------|------|---------|------|------|
| front_left_shoulder | 0.0% | 1.584 | 2.940 | ✅ |
| front_left_leg | 23.3% | 2.940 | 2.940 | ❌ |
| front_left_foot | 13.1% | 2.940 | 2.940 | ⚠️ |
| front_right_shoulder | 0.0% | 2.413 | 2.940 | ✅ |
| front_right_leg | 25.1% | 2.940 | 2.940 | ❌ |
| front_right_foot | 20.2% | 2.940 | 2.940 | ❌ |
| rear_left_shoulder | 0.0% | 2.181 | 2.940 | ✅ |
| rear_left_leg | 23.5% | 2.940 | 2.940 | ❌ |
| rear_left_foot | 2.3% | 2.940 | 2.940 | ✅ |
| rear_right_shoulder | 0.0% | 1.423 | 2.940 | ✅ |
| rear_right_leg | 48.5% | 2.940 | 2.940 | ❌ |
| rear_right_foot | 7.1% | 2.940 | 2.940 | ⚠️ |


## Gait 분석

| 지표 | 값 |
|------|-----|
| Gait 주파수 | 0.42 Hz |
| Gait 주기 | 30 steps |
| 대각 동기화율 | 93.8% |
| L/R 비대칭 | 0.0%p |
| 판정 | ✅ Trot 패턴 / ✅ 대칭 |


## 관절별 상세

| 관절 | 토크포화% | 사용범위% | 편향(rad) | 한계근접% | 상태 |
|------|----------|----------|----------|----------|------|
| front_left_shoulder | 0.0% | 11% | -0.016 | 0.0% | ✅ |
| front_left_leg | 23.3% | 11% | -0.721 | 0.0% | ⚠️ |
| front_left_foot | 13.1% | 29% | +1.163 | 0.0% | ⚠️ |
| front_right_shoulder | 0.0% | 19% | -0.003 | 0.0% | ✅ |
| front_right_leg | 25.1% | 10% | -0.683 | 0.0% | ⚠️ |
| front_right_foot | 20.2% | 24% | +1.272 | 0.0% | ⚠️ |
| rear_left_shoulder | 0.0% | 14% | +0.008 | 0.0% | ✅ |
| rear_left_leg | 23.5% | 9% | -0.701 | 0.0% | ⚠️ |
| rear_left_foot | 2.3% | 23% | +1.253 | 0.0% | ⚠️ |
| rear_right_shoulder | 0.0% | 12% | -0.007 | 0.0% | ✅ |
| rear_right_leg | 48.5% | 15% | -0.732 | 0.0% | ⚠️ |
| rear_right_foot | 7.1% | 22% | +1.242 | 0.0% | ⚠️ |


## 에너지 효율 상세

| 지표 | 값 |
|------|-----|
| 평균 전력 | 8.48 W |
| 피크 전력 | 19.86 W |
| 피크/평균 비율 | 2.3x |
| CoT | 1.51 |

### 관절별 전력 분배

| 관절 | 평균전력(W) | 비율% |
|------|-----------|-------|
| front_right_foot | 1.827 | 21.5% |
| front_left_foot | 1.665 | 19.6% |
| rear_right_leg | 1.139 | 13.4% |
| rear_left_leg | 0.888 | 10.5% |
| rear_right_foot | 0.824 | 9.7% |
| front_left_leg | 0.701 | 8.3% |
| front_right_leg | 0.681 | 8.0% |
| rear_left_foot | 0.509 | 6.0% |
| rear_left_shoulder | 0.091 | 1.1% |
| front_right_shoulder | 0.070 | 0.8% |
| rear_right_shoulder | 0.055 | 0.7% |
| front_left_shoulder | 0.034 | 0.4% |


## 이전 실험 대비 비교

| 지표 | 이전 (exp005) | 현재 (exp006) | 변화 |
|------|-------|-------|------|
| Timeout% | 81.5% | 100.0% | ✅ ↑ 18.4713% |
| 속도오차 X | 0.0889 | 0.0600 | ✅ ↓ 0.0290m/s |
| 토크포화 | 47.5% | 13.6% | ✅ ↓ 33.8640% |
| Roll | 3.8° | 0.9° | ✅ ↓ 2.8395° |
| Pitch | 6.1° | 1.1° | ✅ ↓ 5.0464° |
| 평균 전력 | 18.4259W | 8.4845W | ✅ ↓ 9.9415W |


## Tensorboard 학습 곡선 요약

| 스칼라 | 최종값 | 최대 | 최소 | 후반10% 평균 |
|--------|--------|------|------|-------------|
| rew_action_rate | -0.0443 | -0.0144 | -0.6744 | -0.0432 |
| rew_ang_vel_xy | -0.0436 | -0.0112 | -0.2807 | -0.0455 |
| rew_collision | 0.0000 | 0.0000 | -0.0011 | 0.0000 |
| rew_dof_acc | -0.0093 | -0.0011 | -0.0534 | -0.0091 |
| rew_dof_vel | -0.0117 | -0.0009 | -0.0447 | -0.0118 |
| rew_lin_vel_z | -0.0039 | -0.0006 | -0.0212 | -0.0039 |
| rew_orientation | -0.0069 | -0.0024 | -1.4870 | -0.0071 |
| rew_termination | 0.0000 | 0.0000 | -0.0100 | -0.0000 |
| rew_torques | -0.0313 | -0.0007 | -0.0586 | -0.0321 |
| rew_tracking_ang_vel | 0.5595 | 0.5698 | 0.0025 | 0.5597 |
| rew_tracking_ik | 0.7183 | 0.7308 | 0.0010 | 0.7169 |
| rew_tracking_lin_vel | 1.4346 | 1.4557 | 0.0104 | 1.4278 |
| rew_trot_contact | 0.4646 | 0.4684 | 0.0044 | 0.4650 |
| learning_rate | 0.0001 | 0.0051 | 0.0000 | 0.0003 |
| surrogate | -0.0008 | 0.0021 | -0.0114 | -0.0021 |
| value_function | 0.0626 | 0.5002 | 0.0005 | 0.0515 |
| collection time | 0.7359 | 0.8732 | 0.6965 | 0.7444 |
| learning_time | 0.2878 | 0.4091 | 0.2764 | 0.2877 |
| total_fps | 96025.0000 | 99974.0000 | 76665.0000 | 95262.5400 |
| mean_noise_std | 0.1927 | 0.9981 | 0.1550 | 0.1802 |
| mean_episode_length | 1002.0000 | 1002.0000 | 13.0674 | 1001.8286 |
| time | 1002.0000 | 1002.0000 | 13.0674 | 1001.8286 |
| mean_reward | 60.2600 | 60.7537 | -0.1348 | 60.3344 |
| time | 60.2600 | 60.7537 | -0.1348 | 60.3344 |

총 학습 iteration: 999


---

## 그래프

### Tensorboard 학습 곡선
![tb_training_curves.png](experiments/exp006_spotmicro_v1_1_4_sigma_desc/tb_training_curves.png)
![tb_individual_rewards.png](experiments/exp006_spotmicro_v1_1_4_sigma_desc/tb_individual_rewards.png)

### Diagnostic 결과
![diagnostic_report.png](experiments/exp006_spotmicro_v1_1_4_sigma_desc/diagnostic_report.png)
![joint_detail.png](experiments/exp006_spotmicro_v1_1_4_sigma_desc/joint_detail.png)
![action_smoothness.png](experiments/exp006_spotmicro_v1_1_4_sigma_desc/action_smoothness.png)


---

## 결론 및 다음 실험 방향

**자동 판정:** ✅ PASS

대부분 기준을 통과했으나 일부 항목이 보통 수준입니다.
  - ⚠️ 토크포화: 13.6% (10~40%)

> 💡 위 결론은 자동 생성되었습니다. 필요시 수정하세요.

---

## 메모

안정성은 유지하면서 속도 오차는 눈에 띄게 감소함. 

