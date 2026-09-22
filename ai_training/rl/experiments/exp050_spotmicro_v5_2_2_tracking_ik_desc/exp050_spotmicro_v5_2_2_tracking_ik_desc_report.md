# 실험 050: spotmicro_v5_2_2_tracking_ik_desc

- **날짜:** 2026-05-12 10:12
- **Task:** spotmicro_test
- **Run name:** `spotmicro_v5_2_2_tracking_ik_desc`
- **판정:** ✅ PASS

---

## 실험 목적

V5.2.1: gait period 조정

---

## 변경점 (이전 커밋 대비)

```diff
diff --git a/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py b/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
index c9225b2..602830f 100644
--- a/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
+++ b/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test_config.py
@@ -123,7 +123,7 @@ class SpotmicroTestCfgPPO(LeggedRobotCfgPPO):
         entropy_coef = 0.01
 
     class runner(LeggedRobotCfgPPO.runner):
-        run_name = 'spotmicro_v5_2_1_gait_perioid'
+        run_name = 'spotmicro_v5_2_2_tracking_ik_desc'
         experiment_name = 'spotmicro_test'
         max_iterations = 1500
         save_interval = 100
```

**변경 요약:**
  - run_name = 'spotmicro_v5_2_1_gait_perioid'
  + run_name = 'spotmicro_v5_2_2_tracking_ik_desc'

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

- ✅ Timeout: 94.8% (≥80%)
- ✅ 속도오차 X: 0.0502 m/s (<0.08)
- ⚠️ 토크포화: 14.5% (10~40%)
- ✅ 자세: roll 1.6°, pitch 1.6° (안정)
- ✅ 조기종료: 1.5% (<5%)

### 상세 수치

| 지표 | 값 |
|------|-----|
| Timeout% | 94.81481481481482 |
| 조기종료% | 1.4814814814814816 |
| 속도오차 X | 0.05018283799290657 m/s |
| 속도오차 Y | 0.02928675338625908 m/s |
| 각속도오차 | 0.09159762412309647 rad/s |
| 토크포화% | 14.534770784770783 |
| 평균 높이 | 0.2109056534596058 m |
| Roll (평균) | 1.6472604274749756° |
| Pitch (평균) | 1.614355444908142° |
| Action Rate | 0.005165026523172855 |
| 평균 전력 | 6.08314323425293 W |
| CoT | 1.966930114003438 |

## Command Mode별 회전 추종 분석

| 모드 | 비율 | 샘플 수 | 평균 abs(cmd_wz) | 평균 abs(actual_wz) | yaw 오차 | 상태 |
|------|------|---------|------------------|---------------------|----------|------|
| 정지 | 1.6% | 3003 | 0.0173 | 0.0675 | 0.0696 | ❌ |
| 직진/저회전 | 44.8% | 86135 | 0.0688 | 0.1164 | 0.0933 | ⚠️ |
| 제자리 회전 | 8.8% | 16893 | 0.2359 | 0.2248 | 0.0839 | ⚠️ |
| 전진+회전 | 40.2% | 77224 | 0.2183 | 0.2206 | 0.0922 | ⚠️ |
| 큰 회전명령 | 0.0% | 0 | N/A | N/A | N/A | N/A |

> 해석 기준: `제자리 회전`만 나쁘면 pure turn 학습/보행 패턴 문제, `큰 회전명령`만 나쁘면 yaw 명령 범위가 현재 토크/보폭 한계보다 큰 문제, `정지`가 나쁘면 stop drift 문제로 보면 됩니다.
        


## 학습 곡선 분석

| 지표 | 값 | 판정 |
|------|-----|------|
| 수렴 iter (90%) | 562 | 보통 |
| 후반 안정성 (CV) | 0.038 | 안정 |
| 정체 구간 | 없음 | |


## Per-leg 분석

### 발별 접촉/공중 비율

| 발 | 접촉% | 공중% |
|-----|-------|------|
| foot_0 | 73.6% | 26.4% |
| foot_1 | 77.1% | 22.9% |
| foot_2 | 71.3% | 28.7% |
| foot_3 | 66.0% | 34.0% |

### 관절별 토크 포화

| 관절 | 포화% | 최대토크 | 한계 | 상태 |
|------|------|---------|------|------|
| front_left_shoulder | 0.0% | 2.940 | 2.940 | ✅ |
| front_left_leg | 15.2% | 2.940 | 2.940 | ⚠️ |
| front_left_foot | 18.6% | 2.940 | 2.940 | ⚠️ |
| front_right_shoulder | 0.1% | 2.940 | 2.940 | ✅ |
| front_right_leg | 33.6% | 2.940 | 2.940 | ❌ |
| front_right_foot | 6.8% | 2.940 | 2.940 | ⚠️ |
| rear_left_shoulder | 0.0% | 2.940 | 2.940 | ✅ |
| rear_left_leg | 35.0% | 2.940 | 2.940 | ❌ |
| rear_left_foot | 26.0% | 2.940 | 2.940 | ❌ |
| rear_right_shoulder | 0.0% | 2.940 | 2.940 | ✅ |
| rear_right_leg | 34.3% | 2.940 | 2.940 | ❌ |
| rear_right_foot | 4.7% | 2.940 | 2.940 | ✅ |


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
| front_left_shoulder | 0.0% | 54% | -0.021 | 0.0% | ✅ |
| front_left_leg | 15.2% | 22% | -0.630 | 0.0% | ⚠️ |
| front_left_foot | 18.6% | 46% | +1.174 | 0.0% | ⚠️ |
| front_right_shoulder | 0.1% | 78% | -0.047 | 0.0% | ✅ |
| front_right_leg | 33.6% | 24% | -0.772 | 0.0% | ⚠️ |
| front_right_foot | 6.8% | 46% | +1.187 | 0.0% | ⚠️ |
| rear_left_shoulder | 0.0% | 62% | +0.048 | 0.0% | ✅ |
| rear_left_leg | 35.0% | 29% | -0.741 | 0.0% | ⚠️ |
| rear_left_foot | 26.0% | 56% | +1.310 | 0.0% | ⚠️ |
| rear_right_shoulder | 0.0% | 74% | +0.141 | 0.0% | ⚠️ |
| rear_right_leg | 34.3% | 22% | -0.738 | 0.0% | ⚠️ |
| rear_right_foot | 4.7% | 61% | +1.336 | 0.0% | ⚠️ |


## 에너지 효율 상세

| 지표 | 값 |
|------|-----|
| 평균 전력 | 6.08 W |
| 피크 전력 | 26.31 W |
| 피크/평균 비율 | 4.3x |
| CoT | 1.97 |

### 관절별 전력 분배

| 관절 | 평균전력(W) | 비율% |
|------|-----------|-------|
| front_left_foot | 1.092 | 17.9% |
| rear_left_foot | 0.985 | 16.2% |
| front_right_foot | 0.890 | 14.6% |
| rear_right_foot | 0.706 | 11.6% |
| front_right_leg | 0.699 | 11.5% |
| front_left_leg | 0.544 | 8.9% |
| rear_right_leg | 0.500 | 8.2% |
| rear_left_leg | 0.450 | 7.4% |
| rear_right_shoulder | 0.077 | 1.3% |
| front_right_shoulder | 0.054 | 0.9% |
| rear_left_shoulder | 0.049 | 0.8% |
| front_left_shoulder | 0.038 | 0.6% |


## 이전 실험 대비 비교

| 지표 | 이전 (exp049) | 현재 (exp050) | 변화 |
|------|-------|-------|------|
| Timeout% | 94.8% | 94.8% | → 유지 |
| 속도오차 X | 0.0486 | 0.0502 | ⚠️ ↑ 0.0016m/s |
| 토크포화 | 16.8% | 14.5% | ✅ ↓ 2.2913% |
| Roll | 2.4° | 1.6° | ✅ ↓ 0.7291° |
| Pitch | 2.3° | 1.6° | ✅ ↓ 0.6701° |
| 평균 전력 | 5.7790W | 6.0831W | ⚠️ ↑ 0.3042W |


## Tensorboard 학습 곡선 요약

| 스칼라 | 최종값 | 최대 | 최소 | 후반10% 평균 |
|--------|--------|------|------|-------------|
| rew_action_rate | -0.0606 | -0.0146 | -0.7779 | -0.0609 |
| rew_ang_vel_xy | -0.0409 | -0.0342 | -0.3772 | -0.0400 |
| rew_collision | 0.0000 | 0.0000 | -0.0008 | -0.0000 |
| rew_dof_acc | -0.0103 | -0.0013 | -0.0462 | -0.0096 |
| rew_dof_vel | -0.0119 | -0.0012 | -0.0384 | -0.0109 |
| rew_lin_vel_z | -0.0030 | -0.0010 | -0.0107 | -0.0029 |
| rew_orientation | -0.0244 | -0.0122 | -0.3421 | -0.0233 |
| rew_stand_still | -0.0434 | 0.0000 | -0.4034 | -0.0455 |
| rew_termination | -0.0002 | 0.0000 | -0.0100 | -0.0003 |
| rew_torques | -0.0336 | -0.0007 | -0.0538 | -0.0316 |
| rew_tracking_ang_vel | 0.8285 | 0.8536 | 0.0022 | 0.8113 |
| rew_tracking_ik | 0.2727 | 0.2803 | 0.0006 | 0.2642 |
| rew_tracking_lin_vel | 1.4322 | 1.4659 | 0.0071 | 1.4149 |
| rew_trot_contact | 0.3840 | 0.4159 | 0.0030 | 0.3726 |
| learning_rate | 0.0004 | 0.0100 | 0.0000 | 0.0004 |
| surrogate | -0.0034 | 0.0013 | -0.0070 | -0.0033 |
| value_function | 0.0110 | 0.0853 | 0.0031 | 0.0118 |
| collection time | 0.7876 | 1.1609 | 0.7466 | 0.7828 |
| learning_time | 0.2890 | 0.3510 | 0.2733 | 0.2878 |
| total_fps | 91310.0000 | 95031.0000 | 67026.0000 | 91839.2733 |
| mean_noise_std | 0.2022 | 0.9997 | 0.1995 | 0.2063 |
| mean_episode_length | 967.7200 | 1002.0000 | 21.7100 | 977.5769 |
| time | 967.7200 | 1002.0000 | 21.7100 | 977.5769 |
| mean_reward | 51.5748 | 55.3453 | -0.1535 | 53.0629 |
| time | 51.5748 | 55.3453 | -0.1535 | 53.0629 |

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
  - ⚠️ 토크포화: 14.5% (10~40%)

> 💡 위 결론은 자동 생성되었습니다. 필요시 수정하세요.

---

## 메모

_(추가 메모가 있으면 여기에 작성)_

