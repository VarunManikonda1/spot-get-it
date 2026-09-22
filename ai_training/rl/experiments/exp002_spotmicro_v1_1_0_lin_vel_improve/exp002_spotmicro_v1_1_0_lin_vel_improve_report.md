# 실험 002: spotmicro_v1_1_0_lin_vel_improve

- **날짜:** 2026-04-24 15:33
- **Task:** spotmicro_test
- **Run name:** `spotmicro_v1_1_0_lin_vel_improve`
- **판정:** ✅ PASS

---

## 실험 목적

V1.1.0: 직선속도 추종 정도 향상을 위한 reward 가중치 및 action_scale 조정

---

## 변경점 (이전 커밋 대비)

```diff
diff --git a/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py b/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
index 9f10d38..488fd48 100644
--- a/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
+++ b/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
@@ -34,7 +34,7 @@ class SpotmicroTestCfg(LeggedRobotCfg):
         control_type = 'P'
         stiffness = {'shoulder': 20.0, 'leg': 20.0, 'foot': 20.0}
         damping = {'shoulder': 0.5, 'leg': 0.5, 'foot': 0.5}
-        action_scale = 0.14
+        action_scale = 0.22
         decimation = 4
 
     class asset(LeggedRobotCfg.asset):
@@ -49,7 +49,7 @@ class SpotmicroTestCfg(LeggedRobotCfg):
 
     class rewards(LeggedRobotCfg.rewards):
         class scales:
-            tracking_lin_vel = 1.0
+            tracking_lin_vel = 1.5
             tracking_ang_vel = 0.9 
             termination = -10.0
             lin_vel_z = -2.0
@@ -118,7 +118,7 @@ class SpotmicroTestCfgPPO(LeggedRobotCfgPPO):
         entropy_coef = 0.01
 
     class runner(LeggedRobotCfgPPO.runner):
-        run_name = 'spotmicro_v1_0_ik_tracking'
+        run_name = 'spotmicro_v1_1_0_lin_vel_improve'
         experiment_name = 'spotmicro_test'
         max_iterations = 1000
         save_interval = 100
```

**변경 요약:**
  - action_scale = 0.14
  + action_scale = 0.22
  - tracking_lin_vel = 1.0
  + tracking_lin_vel = 1.5
  - run_name = 'spotmicro_v1_0_ik_tracking'
  + run_name = 'spotmicro_v1_1_0_lin_vel_improve'

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
| tracking_lin_vel | 1.5 |
| trot_contact | 0.5 |

---

## 진단 결과 (Diagnostic)

### 핵심 지표

- ✅ Timeout: 100.0% (≥80%)
- ✅ 속도오차 X: 0.0852 m/s (<0.12)
- ⚠️ 토크포화: 16.0% (10~40%)
- ✅ 자세: roll 2.5°, pitch 1.2° (안정)
- ✅ 조기종료: 0.0% (<5%)

### 상세 수치

| 지표 | 값 |
|------|-----|
| Timeout% | 100.0 |
| 조기종료% | 0.0 |
| 속도오차 X | 0.08518523722887039 m/s |
| 속도오차 Y | 0.022517140954732895 m/s |
| 각속도오차 | 0.10101324319839478 rad/s |
| 토크포화% | 15.982064810189812 |
| 평균 높이 | 0.2199133638090346 m |
| Roll (평균) | 2.474885940551758° |
| Pitch (평균) | 1.232933521270752° |
| Action Rate | 0.003358467947691679 |
| 평균 전력 | 8.126753807067871 W |
| CoT | 1.636116050071275 |


## 학습 곡선 분석

| 지표 | 값 | 판정 |
|------|-----|------|
| 수렴 iter (90%) | 213 | 빠름 |
| 후반 안정성 (CV) | 0.005 | 안정 |
| 정체 구간 | 있음 (iter 285, 49 iter) | |


## Per-leg 분석

### 발별 접촉/공중 비율

| 발 | 접촉% | 공중% |
|-----|-------|------|
| foot_0 | 51.2% | 48.8% |
| foot_1 | 57.8% | 42.2% |
| foot_2 | 55.0% | 45.0% |
| foot_3 | 50.1% | 49.9% |

### 관절별 토크 포화

| 관절 | 포화% | 최대토크 | 한계 | 상태 |
|------|------|---------|------|------|
| front_left_shoulder | 0.0% | 2.940 | 2.940 | ✅ |
| front_left_leg | 13.2% | 2.940 | 2.940 | ⚠️ |
| front_left_foot | 4.8% | 2.940 | 2.940 | ✅ |
| front_right_shoulder | 0.0% | 2.915 | 2.940 | ✅ |
| front_right_leg | 33.7% | 2.940 | 2.940 | ❌ |
| front_right_foot | 18.7% | 2.940 | 2.940 | ⚠️ |
| rear_left_shoulder | 0.0% | 2.940 | 2.940 | ✅ |
| rear_left_leg | 23.6% | 2.940 | 2.940 | ❌ |
| rear_left_foot | 4.7% | 2.940 | 2.940 | ✅ |
| rear_right_shoulder | 0.0% | 2.940 | 2.940 | ✅ |
| rear_right_leg | 67.4% | 2.940 | 2.940 | ❌ |
| rear_right_foot | 25.5% | 2.940 | 2.940 | ❌ |


## Gait 분석

| 지표 | 값 |
|------|-----|
| Gait 주파수 | 0.42 Hz |
| Gait 주기 | 30 steps |
| 대각 동기화율 | 95.9% |
| L/R 비대칭 | 0.0%p |
| 판정 | ✅ Trot 패턴 / ✅ 대칭 |


## 관절별 상세

| 관절 | 토크포화% | 사용범위% | 편향(rad) | 한계근접% | 상태 |
|------|----------|----------|----------|----------|------|
| front_left_shoulder | 0.0% | 19% | -0.030 | 0.0% | ✅ |
| front_left_leg | 13.2% | 11% | -0.661 | 0.0% | ⚠️ |
| front_left_foot | 4.8% | 25% | +1.220 | 0.0% | ⚠️ |
| front_right_shoulder | 0.0% | 18% | +0.025 | 0.0% | ✅ |
| front_right_leg | 33.7% | 11% | -0.762 | 0.0% | ⚠️ |
| front_right_foot | 18.7% | 25% | +1.252 | 0.0% | ⚠️ |
| rear_left_shoulder | 0.0% | 17% | +0.009 | 0.0% | ✅ |
| rear_left_leg | 23.6% | 10% | -0.737 | 0.0% | ⚠️ |
| rear_left_foot | 4.7% | 24% | +1.225 | 0.0% | ⚠️ |
| rear_right_shoulder | 0.0% | 23% | +0.024 | 0.0% | ✅ |
| rear_right_leg | 67.4% | 20% | -0.847 | 0.0% | ⚠️ |
| rear_right_foot | 25.5% | 23% | +1.235 | 0.0% | ⚠️ |


## 에너지 효율 상세

| 지표 | 값 |
|------|-----|
| 평균 전력 | 8.13 W |
| 피크 전력 | 28.50 W |
| 피크/평균 비율 | 3.5x |
| CoT | 1.64 |

### 관절별 전력 분배

| 관절 | 평균전력(W) | 비율% |
|------|-----------|-------|
| front_right_foot | 1.530 | 18.8% |
| rear_right_leg | 1.327 | 16.3% |
| rear_right_foot | 1.099 | 13.5% |
| front_left_foot | 1.096 | 13.5% |
| rear_left_leg | 0.786 | 9.7% |
| front_right_leg | 0.683 | 8.4% |
| front_left_leg | 0.632 | 7.8% |
| rear_left_foot | 0.584 | 7.2% |
| rear_left_shoulder | 0.176 | 2.2% |
| front_right_shoulder | 0.114 | 1.4% |
| rear_right_shoulder | 0.056 | 0.7% |
| front_left_shoulder | 0.043 | 0.5% |


## 이전 실험 대비 비교

| 지표 | 이전 (exp001) | 현재 (exp002) | 변화 |
|------|-------|-------|------|
| Timeout% | 100.0% | 100.0% | → 유지 |
| 속도오차 X | 0.1025 | 0.0852 | ✅ ↓ 0.0173m/s |
| 토크포화 | 16.4% | 16.0% | ✅ ↓ 0.4488% |
| Roll | 1.8° | 2.5° | ⚠️ ↑ 0.6894° |
| Pitch | 1.3° | 1.2° | ✅ ↓ 0.0737° |
| 평균 전력 | 7.7931W | 8.1268W | ⚠️ ↑ 0.3336W |


## Tensorboard 학습 곡선 요약

| 스칼라 | 최종값 | 최대 | 최소 | 후반10% 평균 |
|--------|--------|------|------|-------------|
| rew_action_rate | -0.0700 | -0.0144 | -0.6715 | -0.0706 |
| rew_ang_vel_xy | -0.0171 | -0.0028 | -0.0777 | -0.0181 |
| rew_collision | 0.0000 | 0.0000 | -0.0016 | 0.0000 |
| rew_dof_acc | -0.0122 | -0.0011 | -0.0550 | -0.0125 |
| rew_dof_vel | -0.0139 | -0.0009 | -0.0462 | -0.0146 |
| rew_lin_vel_z | -0.0041 | -0.0006 | -0.0211 | -0.0044 |
| rew_orientation | -0.0069 | -0.0006 | -0.4172 | -0.0067 |
| rew_termination | 0.0000 | 0.0000 | -0.0100 | -0.0000 |
| rew_torques | -0.0354 | -0.0007 | -0.0585 | -0.0362 |
| rew_tracking_ang_vel | 0.5684 | 0.5810 | 0.0030 | 0.5670 |
| rew_tracking_ik | 0.6361 | 0.6534 | 0.0011 | 0.6273 |
| rew_tracking_lin_vel | 1.4533 | 1.4562 | 0.0131 | 1.4434 |
| rew_trot_contact | 0.4517 | 0.4559 | 0.0044 | 0.4504 |
| learning_rate | 0.0006 | 0.0051 | 0.0000 | 0.0003 |
| surrogate | -0.0009 | 0.0056 | -0.0136 | -0.0020 |
| value_function | 0.0359 | 0.2772 | 0.0005 | 0.0370 |
| collection time | 0.7717 | 0.9063 | 0.6991 | 0.7734 |
| learning_time | 0.2831 | 0.3384 | 0.2714 | 0.2881 |
| total_fps | 93196.0000 | 99666.0000 | 81879.0000 | 92702.0200 |
| mean_noise_std | 0.2391 | 0.9932 | 0.2012 | 0.2338 |
| mean_episode_length | 996.9700 | 1002.0000 | 13.0674 | 1000.2197 |
| time | 996.9700 | 1002.0000 | 13.0674 | 1000.2197 |
| mean_reward | 58.4047 | 59.3963 | 0.0035 | 58.4653 |
| time | 58.4047 | 59.3963 | 0.0035 | 58.4653 |

총 학습 iteration: 999


---

## 그래프

### Tensorboard 학습 곡선
![tb_training_curves.png](experiments/exp002_spotmicro_v1_1_0_lin_vel_improve/tb_training_curves.png)
![tb_individual_rewards.png](experiments/exp002_spotmicro_v1_1_0_lin_vel_improve/tb_individual_rewards.png)

### Diagnostic 결과
![diagnostic_report.png](experiments/exp002_spotmicro_v1_1_0_lin_vel_improve/diagnostic_report.png)
![joint_detail.png](experiments/exp002_spotmicro_v1_1_0_lin_vel_improve/joint_detail.png)
![action_smoothness.png](experiments/exp002_spotmicro_v1_1_0_lin_vel_improve/action_smoothness.png)


---

## 결론 및 다음 실험 방향

**자동 판정:** ✅ PASS

대부분 기준을 통과했으나 일부 항목이 보통 수준입니다.
  - ⚠️ 토크포화: 16.0% (10~40%)

> 💡 위 결론은 자동 생성되었습니다. 필요시 수정하세요.

---

## 메모

이전 학습에 비해 직진 속도 추종도는 좋아졌으나 그만큼 각속도 추종도가 비슷하게 나빠지고 몸체의 흔들림 정도가 커짐. reward가중치 조정의 영향도 있겠으나 action scale 범위를 늘리면서 생긴 영향이 더 크다고 판단, 다음 학습에서 orientation, ang_vel_xy 수치를 조정하여 속도 추종도는 유지하며 흔들림 잡을 수 있는지 확인해보자

