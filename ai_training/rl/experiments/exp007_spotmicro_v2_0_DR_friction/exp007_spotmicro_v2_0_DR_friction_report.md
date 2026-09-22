# 실험 007: spotmicro_v2_0_DR_friction

- **날짜:** 2026-04-27 11:02
- **Task:** spotmicro_test
- **Run name:** `spotmicro_v2_0_DR_friction`
- **판정:** ✅ PASS

---

## 실험 목적

V2.0: 도메인 랜덤화 적용. 우선 지면 마찰계수만 적용

---

## 변경점 (이전 커밋 대비)

```diff
diff --git a/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py b/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
index 86d6f70..27e468a 100644
--- a/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
+++ b/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
@@ -104,7 +104,7 @@ class SpotmicroTestCfg(LeggedRobotCfg):
             heading = [-3.14, 3.14]
 
     class domain_rand(LeggedRobotCfg.domain_rand):
-        randomize_friction = False
+        randomize_friction = True
         friction_range = [0.4, 1.2]
         randomize_base_mass = False
         added_mass_range = [-0.5, 0.5]
@@ -119,7 +119,7 @@ class SpotmicroTestCfgPPO(LeggedRobotCfgPPO):
         entropy_coef = 0.01
 
     class runner(LeggedRobotCfgPPO.runner):
-        run_name = 'spotmicro_v1_1_4_sigma_desc'
+        run_name = 'spotmicro_v2_0_DR_friction'
         experiment_name = 'spotmicro_test'
         max_iterations = 1000
         save_interval = 100
diff --git a/ai_training/rl/legged_gym/legged_gym/scripts/play_diagnostic.py b/ai_training/rl/legged_gym/legged_gym/scripts/play_diagnostic.py
index 3ae5b49..c54351d 100644
--- a/ai_training/rl/legged_gym/legged_gym/scripts/play_diagnostic.py
+++ b/ai_training/rl/legged_gym/legged_gym/scripts/play_diagnostic.py
@@ -33,7 +33,7 @@ import matplotlib
 matplotlib.use('Agg')
 import matplotlib.pyplot as plt
 from collections import defaultdict
- 
+import argparse, sys 
  
 def run_diagnostic(args, checkpoint_path=None, lightweight=False):
     # ============ 환경 설정 ============
```

**변경 요약:**
  - randomize_friction = False
  + randomize_friction = True
  - run_name = 'spotmicro_v1_1_4_sigma_desc'
  + run_name = 'spotmicro_v2_0_DR_friction'
  + import argparse, sys

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
- ✅ 속도오차 X: 0.0686 m/s (<0.08)
- ⚠️ 토크포화: 12.8% (10~40%)
- ✅ 자세: roll 1.0°, pitch 0.9° (안정)
- ✅ 조기종료: 0.0% (<5%)

### 상세 수치

| 지표 | 값 |
|------|-----|
| Timeout% | 100.0 |
| 조기종료% | 0.0 |
| 속도오차 X | 0.06864430010318756 m/s |
| 속도오차 Y | 0.01955048181116581 m/s |
| 각속도오차 | 0.0987924262881279 rad/s |
| 토크포화% | 12.781012390387392 |
| 평균 높이 | 0.21990395080554972 m |
| Roll (평균) | 0.9798154234886169° |
| Pitch (평균) | 0.8534088730812073° |
| Action Rate | 0.00307936011813581 |
| 평균 전력 | 7.881471157073975 W |
| CoT | 1.4829569099054856 |


## 학습 곡선 분석

| 지표 | 값 | 판정 |
|------|-----|------|
| 수렴 iter (90%) | 190 | 빠름 |
| 후반 안정성 (CV) | 0.009 | 안정 |
| 정체 구간 | 없음 | |


## Per-leg 분석

### 발별 접촉/공중 비율

| 발 | 접촉% | 공중% |
|-----|-------|------|
| foot_0 | 54.8% | 45.2% |
| foot_1 | 57.8% | 42.2% |
| foot_2 | 52.6% | 47.4% |
| foot_3 | 50.4% | 49.6% |

### 관절별 토크 포화

| 관절 | 포화% | 최대토크 | 한계 | 상태 |
|------|------|---------|------|------|
| front_left_shoulder | 0.0% | 1.874 | 2.940 | ✅ |
| front_left_leg | 18.9% | 2.940 | 2.940 | ⚠️ |
| front_left_foot | 8.4% | 2.940 | 2.940 | ⚠️ |
| front_right_shoulder | 0.0% | 2.725 | 2.940 | ✅ |
| front_right_leg | 31.0% | 2.940 | 2.940 | ❌ |
| front_right_foot | 12.8% | 2.940 | 2.940 | ⚠️ |
| rear_left_shoulder | 0.0% | 2.290 | 2.940 | ✅ |
| rear_left_leg | 23.3% | 2.940 | 2.940 | ❌ |
| rear_left_foot | 5.1% | 2.940 | 2.940 | ⚠️ |
| rear_right_shoulder | 0.0% | 2.590 | 2.940 | ✅ |
| rear_right_leg | 48.8% | 2.940 | 2.940 | ❌ |
| rear_right_foot | 5.1% | 2.940 | 2.940 | ⚠️ |


## Gait 분석

| 지표 | 값 |
|------|-----|
| Gait 주파수 | 0.42 Hz |
| Gait 주기 | 30 steps |
| 대각 동기화율 | 95.2% |
| L/R 비대칭 | 0.0%p |
| 판정 | ✅ Trot 패턴 / ✅ 대칭 |


## 관절별 상세

| 관절 | 토크포화% | 사용범위% | 편향(rad) | 한계근접% | 상태 |
|------|----------|----------|----------|----------|------|
| front_left_shoulder | 0.0% | 24% | +0.023 | 0.0% | ✅ |
| front_left_leg | 18.9% | 10% | -0.692 | 0.0% | ⚠️ |
| front_left_foot | 8.4% | 24% | +1.221 | 0.0% | ⚠️ |
| front_right_shoulder | 0.0% | 20% | -0.026 | 0.0% | ✅ |
| front_right_leg | 31.0% | 10% | -0.746 | 0.0% | ⚠️ |
| front_right_foot | 12.8% | 23% | +1.275 | 0.0% | ⚠️ |
| rear_left_shoulder | 0.0% | 17% | -0.014 | 0.0% | ✅ |
| rear_left_leg | 23.3% | 11% | -0.673 | 0.0% | ⚠️ |
| rear_left_foot | 5.1% | 21% | +1.253 | 0.0% | ⚠️ |
| rear_right_shoulder | 0.0% | 15% | +0.003 | 0.0% | ✅ |
| rear_right_leg | 48.8% | 14% | -0.741 | 0.0% | ⚠️ |
| rear_right_foot | 5.1% | 22% | +1.230 | 0.0% | ⚠️ |


## 에너지 효율 상세

| 지표 | 값 |
|------|-----|
| 평균 전력 | 7.88 W |
| 피크 전력 | 28.19 W |
| 피크/평균 비율 | 3.6x |
| CoT | 1.48 |

### 관절별 전력 분배

| 관절 | 평균전력(W) | 비율% |
|------|-----------|-------|
| front_right_foot | 1.681 | 21.3% |
| front_left_foot | 1.412 | 17.9% |
| rear_right_leg | 1.047 | 13.3% |
| rear_left_leg | 0.817 | 10.4% |
| rear_right_foot | 0.760 | 9.6% |
| front_right_leg | 0.678 | 8.6% |
| front_left_leg | 0.668 | 8.5% |
| rear_left_foot | 0.560 | 7.1% |
| rear_left_shoulder | 0.087 | 1.1% |
| front_right_shoulder | 0.067 | 0.9% |
| rear_right_shoulder | 0.064 | 0.8% |
| front_left_shoulder | 0.041 | 0.5% |


## 이전 실험 대비 비교

| 지표 | 이전 (exp006) | 현재 (exp007) | 변화 |
|------|-------|-------|------|
| Timeout% | 100.0% | 100.0% | → 유지 |
| 속도오차 X | 0.0600 | 0.0686 | ⚠️ ↑ 0.0087m/s |
| 토크포화 | 13.6% | 12.8% | ✅ ↓ 0.8132% |
| Roll | 0.9° | 1.0° | ⚠️ ↑ 0.0609° |
| Pitch | 1.1° | 0.9° | ✅ ↓ 0.2336° |
| 평균 전력 | 8.4845W | 7.8815W | ✅ ↓ 0.6030W |


## Tensorboard 학습 곡선 요약

| 스칼라 | 최종값 | 최대 | 최소 | 후반10% 평균 |
|--------|--------|------|------|-------------|
| rew_action_rate | -0.0558 | -0.0143 | -0.7279 | -0.0504 |
| rew_ang_vel_xy | -0.0502 | -0.0105 | -0.3470 | -0.0489 |
| rew_collision | 0.0000 | 0.0000 | -0.0016 | 0.0000 |
| rew_dof_acc | -0.0101 | -0.0011 | -0.0488 | -0.0096 |
| rew_dof_vel | -0.0118 | -0.0009 | -0.0410 | -0.0120 |
| rew_lin_vel_z | -0.0033 | -0.0006 | -0.0213 | -0.0038 |
| rew_orientation | -0.0063 | -0.0026 | -1.2292 | -0.0078 |
| rew_termination | 0.0000 | 0.0000 | -0.0100 | 0.0000 |
| rew_torques | -0.0328 | -0.0007 | -0.0594 | -0.0332 |
| rew_tracking_ang_vel | 0.5177 | 0.5553 | 0.0025 | 0.5385 |
| rew_tracking_ik | 0.6737 | 0.7161 | 0.0012 | 0.6913 |
| rew_tracking_lin_vel | 1.4332 | 1.4454 | 0.0095 | 1.4292 |
| rew_trot_contact | 0.4642 | 0.4658 | 0.0044 | 0.4632 |
| learning_rate | 0.0002 | 0.0051 | 0.0000 | 0.0003 |
| surrogate | -0.0011 | 0.0018 | -0.0116 | -0.0018 |
| value_function | 0.0749 | 0.2437 | 0.0005 | 0.0401 |
| collection time | 0.7667 | 1.0371 | 0.7033 | 0.7682 |
| learning_time | 0.2868 | 0.3795 | 0.2739 | 0.2910 |
| total_fps | 93311.0000 | 99320.0000 | 73256.0000 | 92851.3000 |
| mean_noise_std | 0.2143 | 1.0009 | 0.1625 | 0.1950 |
| mean_episode_length | 1002.0000 | 1002.0000 | 12.9545 | 1002.0000 |
| time | 1002.0000 | 1002.0000 | 12.9545 | 1002.0000 |
| mean_reward | 58.1814 | 59.9726 | -0.1601 | 59.1231 |
| time | 58.1814 | 59.9726 | -0.1601 | 59.1231 |

총 학습 iteration: 999


---

## 그래프

### Tensorboard 학습 곡선
![tb_training_curves.png](experiments/exp007_spotmicro_v2_0_DR_friction/tb_training_curves.png)
![tb_individual_rewards.png](experiments/exp007_spotmicro_v2_0_DR_friction/tb_individual_rewards.png)

### Diagnostic 결과
![diagnostic_report.png](experiments/exp007_spotmicro_v2_0_DR_friction/diagnostic_report.png)
![joint_detail.png](experiments/exp007_spotmicro_v2_0_DR_friction/joint_detail.png)
![action_smoothness.png](experiments/exp007_spotmicro_v2_0_DR_friction/action_smoothness.png)


---

## 결론 및 다음 실험 방향

**자동 판정:** ✅ PASS

대부분 기준을 통과했으나 일부 항목이 보통 수준입니다.
  - ⚠️ 토크포화: 12.8% (10~40%)

> 💡 위 결론은 자동 생성되었습니다. 필요시 수정하세요.

---

## 메모

속도 오차와 안정성이 거의 유지됨. 다음 랜덤화 적용하면 될듯

