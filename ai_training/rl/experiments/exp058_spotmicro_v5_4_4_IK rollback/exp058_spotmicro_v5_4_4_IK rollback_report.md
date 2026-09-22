# 실험 058: spotmicro_v5_4_4_IK rollback

- **날짜:** 2026-05-13 10:05
- **Task:** spotmicro_test
- **Run name:** `spotmicro_v5_4_4_IK rollback`
- **판정:** ✅ PASS

---

## 실험 목적

V5.4.4.2: IK rollback 후 각 체크포인트마다 확인 - without dr

---

## 변경점 (이전 커밋 대비)

```diff
diff --git a/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test.py b/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test.py
index db0863b..f194b90 100644
--- a/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test.py
+++ b/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test.py
@@ -330,7 +330,7 @@ class SpotmicroTest(LeggedRobot):
         '''
         foot_vx = vx - wz * leg_y
         foot_vy = vy + wz * leg_x
-        foot_vy = torch.zeros_like(foot_vx)
+        #foot_vy = torch.zeros_like(foot_vx)
 
         stance_time = self.gait_period * self.duty_factor
 
diff --git a/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py b/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
index a06a297..455143b 100644
--- a/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
+++ b/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
@@ -123,7 +123,7 @@ class SpotmicroTestCfgPPO(LeggedRobotCfgPPO):
         entropy_coef = 0.01
 
     class runner(LeggedRobotCfgPPO.runner):
-        run_name = 'spotmicro_v5_4_3_turn_width'
+        run_name = 'spotmicro_v5_4_4_IK rollback'
         experiment_name = 'spotmicro_test'
         max_iterations = 1500
         save_interval = 100
```

**변경 요약:**
  - foot_vy = torch.zeros_like(foot_vx)
  - run_name = 'spotmicro_v5_4_3_turn_width'
  + run_name = 'spotmicro_v5_4_4_IK rollback'

---

## 현재 Reward Scales

| Reward | Scale |
|--------|-------|
| action_rate | -0.05 |
| ang_vel_xy | -0.4 |
| collision | -1.0 |
| dof_acc | -2.5e-07 |
| dof_vel | -0.001 |
| lin_vel_z | -2.0 |
| orientation | -6.0 |
| stand_still | -0.5 |
| termination | -10.0 |
| torques | -0.001 |
| tracking_ang_vel | 1.3 |
| tracking_ik | 1.0 |
| tracking_lin_vel | 1.5 |
| trot_contact | 0.5 |

---

## 진단 결과 (Diagnostic)

### 핵심 지표

- ✅ Timeout: 99.2% (≥80%)
- ✅ 속도오차 X: 0.0501 m/s (<0.08)
- ⚠️ 토크포화: 14.0% (10~40%)
- ✅ 자세: roll 1.4°, pitch 1.3° (안정)
- ✅ 조기종료: 0.0% (<5%)

### 상세 수치

| 지표 | 값 |
|------|-----|
| Timeout% | 99.2248062015504 |
| 조기종료% | 0.0 |
| 속도오차 X | 0.050075117498636246 m/s |
| 속도오차 Y | 0.027973704040050507 m/s |
| 각속도오차 | 0.07695126533508301 rad/s |
| 토크포화% | 13.951846764346765 |
| 평균 높이 | 0.21095113027028312 m |
| Roll (평균) | 1.4284101724624634° |
| Pitch (평균) | 1.3363662958145142° |
| Action Rate | 0.0027161051984876394 |
| 평균 전력 | 5.8682332038879395 W |
| CoT | 2.0187201618358808 |

## Command Mode별 회전 추종 분석

| 모드 | 비율 | 샘플 수 | 평균 abs(cmd_wz) | 평균 abs(actual_wz) | yaw 오차 | 상태 |
|------|------|---------|------------------|---------------------|----------|------|
| 정지 | 3.1% | 6016 | 0.0231 | 0.0530 | 0.0585 | ⚠️ |
| 직진/저회전 | 42.2% | 81077 | 0.0744 | 0.1074 | 0.0766 | ✅ |
| 제자리 회전 | 8.1% | 15510 | 0.2119 | 0.1802 | 0.0826 | ⚠️ |
| 전진+회전 | 40.9% | 78589 | 0.2255 | 0.2185 | 0.0745 | ✅ |
| 큰 회전명령 | 0.0% | 0 | N/A | N/A | N/A | N/A |

> 해석 기준: `제자리 회전`만 나쁘면 pure turn 학습/보행 패턴 문제, `큰 회전명령`만 나쁘면 yaw 명령 범위가 현재 토크/보폭 한계보다 큰 문제, `정지`가 나쁘면 stop drift 문제로 보면 됩니다.
        


## 학습 곡선 분석

| 지표 | 값 | 판정 |
|------|-----|------|
| 수렴 iter (90%) | 336 | 빠름 |
| 후반 안정성 (CV) | 0.247 | 불안정 |
| 정체 구간 | 없음 | |


## Per-leg 분석

### 발별 접촉/공중 비율

| 발 | 접촉% | 공중% |
|-----|-------|------|
| foot_0 | 77.1% | 22.9% |
| foot_1 | 78.2% | 21.8% |
| foot_2 | 70.7% | 29.3% |
| foot_3 | 67.1% | 32.9% |

### 관절별 토크 포화

| 관절 | 포화% | 최대토크 | 한계 | 상태 |
|------|------|---------|------|------|
| front_left_shoulder | 0.0% | 2.940 | 2.940 | ✅ |
| front_left_leg | 17.4% | 2.940 | 2.940 | ⚠️ |
| front_left_foot | 18.9% | 2.940 | 2.940 | ⚠️ |
| front_right_shoulder | 0.0% | 2.940 | 2.940 | ✅ |
| front_right_leg | 34.5% | 2.940 | 2.940 | ❌ |
| front_right_foot | 5.3% | 2.940 | 2.940 | ⚠️ |
| rear_left_shoulder | 0.1% | 2.940 | 2.940 | ✅ |
| rear_left_leg | 31.6% | 2.940 | 2.940 | ❌ |
| rear_left_foot | 24.3% | 2.940 | 2.940 | ❌ |
| rear_right_shoulder | 0.0% | 2.940 | 2.940 | ✅ |
| rear_right_leg | 31.5% | 2.940 | 2.940 | ❌ |
| rear_right_foot | 3.8% | 2.940 | 2.940 | ✅ |


## Gait 분석

| 지표 | 값 |
|------|-----|
| Gait 주파수 | 1.00 Hz |
| Gait 주기 | 50 steps |
| 대각 동기화율 | 85.2% |
| L/R 비대칭 | 0.0%p |
| 판정 | ✅ Trot 패턴 / ✅ 대칭 |


## 관절별 상세

| 관절 | 토크포화% | 사용범위% | 편향(rad) | 한계근접% | 상태 |
|------|----------|----------|----------|----------|------|
| front_left_shoulder | 0.0% | 33% | +0.049 | 0.0% | ✅ |
| front_left_leg | 17.4% | 21% | -0.653 | 0.0% | ⚠️ |
| front_left_foot | 18.9% | 44% | +1.145 | 0.0% | ⚠️ |
| front_right_shoulder | 0.0% | 46% | -0.004 | 0.0% | ✅ |
| front_right_leg | 34.5% | 27% | -0.804 | 0.0% | ⚠️ |
| front_right_foot | 5.3% | 55% | +1.306 | 0.0% | ⚠️ |
| rear_left_shoulder | 0.1% | 59% | +0.026 | 0.0% | ✅ |
| rear_left_leg | 31.6% | 26% | -0.793 | 0.0% | ⚠️ |
| rear_left_foot | 24.3% | 47% | +1.198 | 0.0% | ⚠️ |
| rear_right_shoulder | 0.0% | 64% | +0.197 | 0.0% | ⚠️ |
| rear_right_leg | 31.5% | 24% | -0.795 | 0.0% | ⚠️ |
| rear_right_foot | 3.8% | 46% | +1.145 | 0.0% | ⚠️ |


## 에너지 효율 상세

| 지표 | 값 |
|------|-----|
| 평균 전력 | 5.87 W |
| 피크 전력 | 33.83 W |
| 피크/평균 비율 | 5.8x |
| CoT | 2.02 |

### 관절별 전력 분배

| 관절 | 평균전력(W) | 비율% |
|------|-----------|-------|
| front_left_foot | 1.089 | 18.6% |
| rear_left_foot | 0.917 | 15.6% |
| front_right_foot | 0.852 | 14.5% |
| rear_right_foot | 0.701 | 11.9% |
| front_right_leg | 0.654 | 11.1% |
| front_left_leg | 0.554 | 9.4% |
| rear_right_leg | 0.476 | 8.1% |
| rear_left_leg | 0.448 | 7.6% |
| rear_right_shoulder | 0.063 | 1.1% |
| rear_left_shoulder | 0.044 | 0.7% |
| front_right_shoulder | 0.040 | 0.7% |
| front_left_shoulder | 0.031 | 0.5% |


## 이전 실험 대비 비교

| 지표 | 이전 (exp057) | 현재 (exp058) | 변화 |
|------|-------|-------|------|
| Timeout% | 95.5% | 99.2% | ✅ ↑ 3.7024% |
| 속도오차 X | 0.0485 | 0.0501 | ⚠️ ↑ 0.0016m/s |
| 토크포화 | 14.6% | 14.0% | ✅ ↓ 0.6330% |
| Roll | 1.3° | 1.4° | ⚠️ ↑ 0.1455° |
| Pitch | 1.5° | 1.3° | ✅ ↓ 0.1764° |
| 평균 전력 | 5.9716W | 5.8682W | ✅ ↓ 0.1033W |


## Tensorboard 학습 곡선 요약

| 스칼라 | 최종값 | 최대 | 최소 | 후반10% 평균 |
|--------|--------|------|------|-------------|
| rew_action_rate | -0.0105 | -0.0101 | -0.7437 | -0.0197 |
| rew_ang_vel_xy | -0.0414 | -0.0233 | -0.3490 | -0.0325 |
| rew_collision | 0.0000 | 0.0000 | -0.0020 | -0.0000 |
| rew_dof_acc | -0.0025 | -0.0013 | -0.0440 | -0.0045 |
| rew_dof_vel | -0.0039 | -0.0012 | -0.0366 | -0.0062 |
| rew_lin_vel_z | -0.0112 | -0.0010 | -0.0112 | -0.0063 |
| rew_orientation | -0.0030 | -0.0024 | -0.5148 | -0.0063 |
| rew_stand_still | -0.0369 | 0.0000 | -0.2409 | -0.0358 |
| rew_termination | -0.0069 | 0.0000 | -0.0100 | -0.0035 |
| rew_torques | -0.0095 | -0.0007 | -0.0539 | -0.0195 |
| rew_tracking_ang_vel | 0.2975 | 0.8643 | 0.0022 | 0.6123 |
| rew_tracking_ik | 0.2474 | 0.7206 | 0.0011 | 0.5118 |
| rew_tracking_lin_vel | 0.4574 | 1.4545 | 0.0071 | 0.9450 |
| rew_trot_contact | 0.1053 | 0.4027 | 0.0030 | 0.2415 |
| learning_rate | 0.0000 | 0.0100 | 0.0000 | 0.0000 |
| surrogate | -0.0012 | 0.0319 | -0.0077 | -0.0007 |
| value_function | 0.0062 | 0.1664 | 0.0032 | 0.0094 |
| collection time | 0.8194 | 0.9194 | 0.7696 | 0.8172 |
| learning_time | 0.2879 | 0.3401 | 0.2732 | 0.2883 |
| total_fps | 88777.0000 | 93145.0000 | 78401.0000 | 88949.3600 |
| mean_noise_std | 0.1344 | 0.9990 | 0.1344 | 0.1366 |
| mean_episode_length | 453.8800 | 1002.0000 | 21.8600 | 672.9192 |
| time | 453.8800 | 1002.0000 | 21.8600 | 672.9192 |
| mean_reward | 30.4661 | 63.6451 | -0.1590 | 45.1923 |
| time | 30.4661 | 63.6451 | -0.1590 | 45.1923 |

총 학습 iteration: 1499


---

## 그래프

### Tensorboard 학습 곡선
![tb_training_curves.png](./tb_training_curves.png)
![tb_individual_rewards.png](./tb_individual_rewards.png)

### Diagnostic 결과
![diagnostic_report.png](./diagnostic_report.png)
![joint_detail.png](./joint_detail.png)
![action_smoothness.png](./action_smoothness.png)


---

## 결론 및 다음 실험 방향

**자동 판정:** ✅ PASS

대부분 기준을 통과했으나 일부 항목이 보통 수준입니다.
  - ⚠️ 토크포화: 14.0% (10~40%)

> 💡 위 결론은 자동 생성되었습니다. 필요시 수정하세요.

---

## 메모

_(추가 메모가 있으면 여기에 작성)_

