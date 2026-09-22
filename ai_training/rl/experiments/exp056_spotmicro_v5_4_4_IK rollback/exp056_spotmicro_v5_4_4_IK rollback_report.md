# 실험 056: spotmicro_v5_4_4_IK rollback

- **날짜:** 2026-05-13 09:46
- **Task:** spotmicro_test
- **Run name:** `spotmicro_v5_4_4_IK rollback`
- **판정:** ✅ PASS

---

## 실험 목적

V5.4.4: IK rollback 후 각 체크포인트마다 확인

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

- ✅ Timeout: 98.5% (≥80%)
- ✅ 속도오차 X: 0.0488 m/s (<0.08)
- ⚠️ 토크포화: 14.2% (10~40%)
- ✅ 자세: roll 1.4°, pitch 1.3° (안정)
- ✅ 조기종료: 0.0% (<5%)

### 상세 수치

| 지표 | 값 |
|------|-----|
| Timeout% | 98.46153846153847 |
| 조기종료% | 0.0 |
| 속도오차 X | 0.048789359629154205 m/s |
| 속도오차 Y | 0.027880022302269936 m/s |
| 각속도오차 | 0.08642600476741791 rad/s |
| 토크포화% | 14.19890873015873 |
| 평균 높이 | 0.2106753545793104 m |
| Roll (평균) | 1.4182981252670288° |
| Pitch (평균) | 1.3137667179107666° |
| Action Rate | 0.00466881413012743 |
| 평균 전력 | 5.9569573402404785 W |
| CoT | 1.9658350165040925 |

## Command Mode별 회전 추종 분석

| 모드 | 비율 | 샘플 수 | 평균 abs(cmd_wz) | 평균 abs(actual_wz) | yaw 오차 | 상태 |
|------|------|---------|------------------|---------------------|----------|------|
| 정지 | 2.1% | 4003 | 0.0200 | 0.0729 | 0.0749 | ❌ |
| 직진/저회전 | 41.7% | 80218 | 0.0698 | 0.1088 | 0.0863 | ⚠️ |
| 제자리 회전 | 8.6% | 16475 | 0.2138 | 0.1897 | 0.0820 | ⚠️ |
| 전진+회전 | 41.1% | 78995 | 0.2227 | 0.2148 | 0.0871 | ⚠️ |
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
| foot_0 | 76.5% | 23.5% |
| foot_1 | 77.3% | 22.7% |
| foot_2 | 70.3% | 29.7% |
| foot_3 | 66.5% | 33.5% |

### 관절별 토크 포화

| 관절 | 포화% | 최대토크 | 한계 | 상태 |
|------|------|---------|------|------|
| front_left_shoulder | 0.0% | 2.940 | 2.940 | ✅ |
| front_left_leg | 17.3% | 2.940 | 2.940 | ⚠️ |
| front_left_foot | 20.8% | 2.940 | 2.940 | ❌ |
| front_right_shoulder | 0.0% | 2.940 | 2.940 | ✅ |
| front_right_leg | 34.3% | 2.940 | 2.940 | ❌ |
| front_right_foot | 5.4% | 2.940 | 2.940 | ⚠️ |
| rear_left_shoulder | 0.0% | 2.940 | 2.940 | ✅ |
| rear_left_leg | 32.3% | 2.940 | 2.940 | ❌ |
| rear_left_foot | 24.5% | 2.940 | 2.940 | ❌ |
| rear_right_shoulder | 0.0% | 2.940 | 2.940 | ✅ |
| rear_right_leg | 31.9% | 2.940 | 2.940 | ❌ |
| rear_right_foot | 3.8% | 2.940 | 2.940 | ✅ |


## Gait 분석

| 지표 | 값 |
|------|-----|
| Gait 주파수 | 1.00 Hz |
| Gait 주기 | 50 steps |
| 대각 동기화율 | 86.5% |
| L/R 비대칭 | 0.0%p |
| 판정 | ✅ Trot 패턴 / ✅ 대칭 |


## 관절별 상세

| 관절 | 토크포화% | 사용범위% | 편향(rad) | 한계근접% | 상태 |
|------|----------|----------|----------|----------|------|
| front_left_shoulder | 0.0% | 48% | +0.137 | 0.0% | ⚠️ |
| front_left_leg | 17.3% | 21% | -0.656 | 0.0% | ⚠️ |
| front_left_foot | 20.8% | 43% | +1.135 | 0.0% | ⚠️ |
| front_right_shoulder | 0.0% | 49% | +0.079 | 0.0% | ✅ |
| front_right_leg | 34.3% | 30% | -0.802 | 0.0% | ⚠️ |
| front_right_foot | 5.4% | 52% | +1.273 | 0.0% | ⚠️ |
| rear_left_shoulder | 0.0% | 50% | +0.095 | 0.0% | ✅ |
| rear_left_leg | 32.3% | 26% | -0.787 | 0.0% | ⚠️ |
| rear_left_foot | 24.5% | 59% | +1.345 | 0.0% | ⚠️ |
| rear_right_shoulder | 0.0% | 69% | +0.166 | 0.0% | ⚠️ |
| rear_right_leg | 31.9% | 26% | -0.827 | 0.0% | ⚠️ |
| rear_right_foot | 3.8% | 49% | +1.213 | 0.0% | ⚠️ |


## 에너지 효율 상세

| 지표 | 값 |
|------|-----|
| 평균 전력 | 5.96 W |
| 피크 전력 | 29.86 W |
| 피크/평균 비율 | 5.0x |
| CoT | 1.97 |

### 관절별 전력 분배

| 관절 | 평균전력(W) | 비율% |
|------|-----------|-------|
| front_left_foot | 1.121 | 18.8% |
| rear_left_foot | 0.954 | 16.0% |
| front_right_foot | 0.854 | 14.3% |
| rear_right_foot | 0.696 | 11.7% |
| front_right_leg | 0.646 | 10.9% |
| front_left_leg | 0.558 | 9.4% |
| rear_right_leg | 0.486 | 8.2% |
| rear_left_leg | 0.450 | 7.6% |
| rear_right_shoulder | 0.066 | 1.1% |
| rear_left_shoulder | 0.046 | 0.8% |
| front_right_shoulder | 0.044 | 0.7% |
| front_left_shoulder | 0.037 | 0.6% |


## 이전 실험 대비 비교

| 지표 | 이전 (exp055) | 현재 (exp056) | 변화 |
|------|-------|-------|------|
| Timeout% | 27.3% | 98.5% | ✅ ↑ 71.1694% |
| 속도오차 X | 0.0579 | 0.0488 | ✅ ↓ 0.0091m/s |
| 토크포화 | 17.7% | 14.2% | ✅ ↓ 3.5324% |
| Roll | 1.7° | 1.4° | ✅ ↓ 0.3146° |
| Pitch | 2.2° | 1.3° | ✅ ↓ 0.8489° |
| 평균 전력 | 7.1789W | 5.9570W | ✅ ↓ 1.2219W |


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
  - ⚠️ 토크포화: 14.2% (10~40%)

> 💡 위 결론은 자동 생성되었습니다. 필요시 수정하세요.

---

## 메모

_(추가 메모가 있으면 여기에 작성)_

