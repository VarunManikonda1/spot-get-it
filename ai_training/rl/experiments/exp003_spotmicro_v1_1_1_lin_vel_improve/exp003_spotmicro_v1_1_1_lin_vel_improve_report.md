# 실험 003: spotmicro_v1_1_1_lin_vel_improve

- **날짜:** 2026-04-24 16:17
- **Task:** spotmicro_test
- **Run name:** `spotmicro_v1_1_1_lin_vel_improve`
- **판정:** ✅ PASS

---

## 실험 목적

V1.1.1: 직선속도 추종 정도는 유지하며 몸체의 흔들림 감소

---

## 변경점 (이전 커밋 대비)

```diff
diff --git a/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py b/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
index 488fd48..196707a 100644
--- a/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
+++ b/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
@@ -53,8 +53,8 @@ class SpotmicroTestCfg(LeggedRobotCfg):
             tracking_ang_vel = 0.9 
             termination = -10.0
             lin_vel_z = -2.0
-            ang_vel_xy = -0.05
-            orientation = -1.0
+            ang_vel_xy = -0.1
+            orientation = -2.0
             torques = -0.001
             dof_vel = -0.001
             dof_acc = -2.5e-7
@@ -118,7 +118,7 @@ class SpotmicroTestCfgPPO(LeggedRobotCfgPPO):
         entropy_coef = 0.01
 
     class runner(LeggedRobotCfgPPO.runner):
-        run_name = 'spotmicro_v1_1_0_lin_vel_improve'
+        run_name = 'spotmicro_v1_1_1_lin_vel_improve'
         experiment_name = 'spotmicro_test'
         max_iterations = 1000
         save_interval = 100
```

**변경 요약:**
  - ang_vel_xy = -0.05
  - orientation = -1.0
  + ang_vel_xy = -0.1
  + orientation = -2.0
  - run_name = 'spotmicro_v1_1_0_lin_vel_improve'
  + run_name = 'spotmicro_v1_1_1_lin_vel_improve'

---

## 현재 Reward Scales

| Reward | Scale |
|--------|-------|
| action_rate | -0.05 |
| ang_vel_xy | -0.1 |
| collision | -1.0 |
| dof_acc | -2.5e-07 |
| dof_vel | -0.001 |
| lin_vel_z | -2.0 |
| orientation | -2.0 |
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
- ✅ 속도오차 X: 0.0825 m/s (<0.12)
- ⚠️ 토크포화: 14.2% (10~40%)
- ✅ 자세: roll 2.1°, pitch 0.9° (안정)
- ✅ 조기종료: 0.0% (<5%)

### 상세 수치

| 지표 | 값 |
|------|-----|
| Timeout% | 100.0 |
| 조기종료% | 0.0 |
| 속도오차 X | 0.08248990029096603 m/s |
| 속도오차 Y | 0.024708250537514687 m/s |
| 각속도오차 | 0.0904223620891571 rad/s |
| 토크포화% | 14.159668456543455 |
| 평균 높이 | 0.21938748620983922 m |
| Roll (평균) | 2.059039354324341° |
| Pitch (평균) | 0.8953388333320618° |
| Action Rate | 0.003087573917582631 |
| 평균 전력 | 7.906877040863037 W |
| CoT | 1.5946281159127014 |


## 학습 곡선 분석

| 지표 | 값 | 판정 |
|------|-----|------|
| 수렴 iter (90%) | 212 | 빠름 |
| 후반 안정성 (CV) | 0.009 | 안정 |
| 정체 구간 | 있음 (iter 513, 49 iter) | |


## Per-leg 분석

### 발별 접촉/공중 비율

| 발 | 접촉% | 공중% |
|-----|-------|------|
| foot_0 | 51.5% | 48.5% |
| foot_1 | 60.1% | 39.9% |
| foot_2 | 53.9% | 46.1% |
| foot_3 | 49.1% | 50.9% |

### 관절별 토크 포화

| 관절 | 포화% | 최대토크 | 한계 | 상태 |
|------|------|---------|------|------|
| front_left_shoulder | 0.0% | 1.722 | 2.940 | ✅ |
| front_left_leg | 17.4% | 2.940 | 2.940 | ⚠️ |
| front_left_foot | 5.0% | 2.940 | 2.940 | ⚠️ |
| front_right_shoulder | 0.0% | 2.508 | 2.940 | ✅ |
| front_right_leg | 37.0% | 2.940 | 2.940 | ❌ |
| front_right_foot | 12.5% | 2.940 | 2.940 | ⚠️ |
| rear_left_shoulder | 0.0% | 2.109 | 2.940 | ✅ |
| rear_left_leg | 22.7% | 2.940 | 2.940 | ❌ |
| rear_left_foot | 3.1% | 2.940 | 2.940 | ✅ |
| rear_right_shoulder | 0.0% | 1.652 | 2.940 | ✅ |
| rear_right_leg | 60.1% | 2.940 | 2.940 | ❌ |
| rear_right_foot | 12.0% | 2.940 | 2.940 | ⚠️ |


## Gait 분석

| 지표 | 값 |
|------|-----|
| Gait 주파수 | 0.42 Hz |
| Gait 주기 | 30 steps |
| 대각 동기화율 | 94.6% |
| L/R 비대칭 | 0.0%p |
| 판정 | ✅ Trot 패턴 / ✅ 대칭 |


## 관절별 상세

| 관절 | 토크포화% | 사용범위% | 편향(rad) | 한계근접% | 상태 |
|------|----------|----------|----------|----------|------|
| front_left_shoulder | 0.0% | 16% | -0.013 | 0.0% | ✅ |
| front_left_leg | 17.4% | 11% | -0.676 | 0.0% | ⚠️ |
| front_left_foot | 5.0% | 26% | +1.234 | 0.0% | ⚠️ |
| front_right_shoulder | 0.0% | 19% | -0.001 | 0.0% | ✅ |
| front_right_leg | 37.0% | 13% | -0.771 | 0.0% | ⚠️ |
| front_right_foot | 12.5% | 24% | +1.268 | 0.0% | ⚠️ |
| rear_left_shoulder | 0.0% | 15% | +0.011 | 0.0% | ✅ |
| rear_left_leg | 22.7% | 12% | -0.748 | 0.0% | ⚠️ |
| rear_left_foot | 3.1% | 24% | +1.244 | 0.0% | ⚠️ |
| rear_right_shoulder | 0.0% | 14% | -0.003 | 0.0% | ✅ |
| rear_right_leg | 60.1% | 18% | -0.834 | 0.0% | ⚠️ |
| rear_right_foot | 12.0% | 22% | +1.242 | 0.0% | ⚠️ |


## 에너지 효율 상세

| 지표 | 값 |
|------|-----|
| 평균 전력 | 7.91 W |
| 피크 전력 | 24.41 W |
| 피크/평균 비율 | 3.1x |
| CoT | 1.59 |

### 관절별 전력 분배

| 관절 | 평균전력(W) | 비율% |
|------|-----------|-------|
| front_right_foot | 1.615 | 20.4% |
| rear_right_leg | 1.189 | 15.0% |
| front_left_foot | 1.138 | 14.4% |
| rear_right_foot | 0.925 | 11.7% |
| rear_left_leg | 0.823 | 10.4% |
| front_right_leg | 0.730 | 9.2% |
| rear_left_foot | 0.633 | 8.0% |
| front_left_leg | 0.602 | 7.6% |
| rear_left_shoulder | 0.093 | 1.2% |
| front_right_shoulder | 0.083 | 1.0% |
| rear_right_shoulder | 0.048 | 0.6% |
| front_left_shoulder | 0.028 | 0.3% |


## 이전 실험 대비 비교

| 지표 | 이전 (exp002) | 현재 (exp003) | 변화 |
|------|-------|-------|------|
| Timeout% | 100.0% | 100.0% | → 유지 |
| 속도오차 X | 0.0852 | 0.0825 | ✅ ↓ 0.0027m/s |
| 토크포화 | 16.0% | 14.2% | ✅ ↓ 1.8224% |
| Roll | 2.5° | 2.1° | ✅ ↓ 0.4158° |
| Pitch | 1.2° | 0.9° | ✅ ↓ 0.3376° |
| 평균 전력 | 8.1268W | 7.9069W | ✅ ↓ 0.2199W |


## Tensorboard 학습 곡선 요약

| 스칼라 | 최종값 | 최대 | 최소 | 후반10% 평균 |
|--------|--------|------|------|-------------|
| rew_action_rate | -0.0526 | -0.0144 | -0.6880 | -0.0558 |
| rew_ang_vel_xy | -0.0284 | -0.0056 | -0.1442 | -0.0290 |
| rew_collision | 0.0000 | 0.0000 | -0.0019 | 0.0000 |
| rew_dof_acc | -0.0099 | -0.0011 | -0.0523 | -0.0104 |
| rew_dof_vel | -0.0116 | -0.0009 | -0.0413 | -0.0125 |
| rew_lin_vel_z | -0.0031 | -0.0006 | -0.0190 | -0.0036 |
| rew_orientation | -0.0067 | -0.0012 | -0.8258 | -0.0065 |
| rew_termination | 0.0000 | 0.0000 | -0.0100 | -0.0001 |
| rew_torques | -0.0326 | -0.0007 | -0.0600 | -0.0335 |
| rew_tracking_ang_vel | 0.6071 | 0.6224 | 0.0030 | 0.5966 |
| rew_tracking_ik | 0.6932 | 0.7026 | 0.0011 | 0.6754 |
| rew_tracking_lin_vel | 1.4515 | 1.4599 | 0.0131 | 1.4408 |
| rew_trot_contact | 0.4604 | 0.4643 | 0.0044 | 0.4584 |
| learning_rate | 0.0000 | 0.0100 | 0.0000 | 0.0003 |
| surrogate | 0.0001 | 0.0027 | -0.0136 | -0.0023 |
| value_function | 0.0650 | 0.7781 | 0.0004 | 0.0655 |
| collection time | 0.7609 | 0.9752 | 0.6941 | 0.7630 |
| learning_time | 0.2872 | 0.3662 | 0.2666 | 0.2885 |
| total_fps | 93797.0000 | 100386.0000 | 74739.0000 | 93670.3500 |
| mean_noise_std | 0.2075 | 0.9944 | 0.1707 | 0.2036 |
| mean_episode_length | 995.0000 | 1002.0000 | 13.0674 | 1000.4748 |
| time | 995.0000 | 1002.0000 | 13.0674 | 1000.4748 |
| mean_reward | 60.6431 | 61.6547 | -0.0522 | 60.3908 |
| time | 60.6431 | 61.6547 | -0.0522 | 60.3908 |

총 학습 iteration: 999


---

## 그래프

### Tensorboard 학습 곡선
![tb_training_curves.png](experiments/exp003_spotmicro_v1_1_1_lin_vel_improve/tb_training_curves.png)
![tb_individual_rewards.png](experiments/exp003_spotmicro_v1_1_1_lin_vel_improve/tb_individual_rewards.png)

### Diagnostic 결과
![diagnostic_report.png](experiments/exp003_spotmicro_v1_1_1_lin_vel_improve/diagnostic_report.png)
![joint_detail.png](experiments/exp003_spotmicro_v1_1_1_lin_vel_improve/joint_detail.png)
![action_smoothness.png](experiments/exp003_spotmicro_v1_1_1_lin_vel_improve/action_smoothness.png)


---

## 결론 및 다음 실험 방향

**자동 판정:** ✅ PASS

대부분 기준을 통과했으나 일부 항목이 보통 수준입니다.
  - ⚠️ 토크포화: 14.2% (10~40%)

> 💡 위 결론은 자동 생성되었습니다. 필요시 수정하세요.

---

## 메모

페널티 증가로 안정성은 확실히 증가. 각속도 오차도 소폭 감소함을 확인. 우선 지금 증가시킨 페널티로 개선이 됨을 확인. 페널티 수치를 조금 더 크게 줘서 어느 정도 선이 추종을 잃지 않고 줄 수 있는 페널티 범위인지 확인해보자

