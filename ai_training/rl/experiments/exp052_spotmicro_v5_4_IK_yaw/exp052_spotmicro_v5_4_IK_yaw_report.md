# 실험 052: spotmicro_v5_4_IK_yaw

- **날짜:** 2026-05-12 12:32
- **Task:** spotmicro_test
- **Run name:** `spotmicro_v5_4_IK_yaw`
- **판정:** ✅ PASS

---

## 실험 목적

V5.4: yaw_ik 수정

---

## 변경점 (이전 커밋 대비)

```diff
diff --git a/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test.py b/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test.py
index 44d7e17..c8b9d44 100644
--- a/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test.py
+++ b/ai_training/rl/legged_gym/legged_gym/envs/spotmicro_test/spotmicro_test.py
@@ -30,6 +30,30 @@ class SpotmicroTest(LeggedRobot):
             [self.obs_scales.lin_vel, self.obs_scales.lin_vel, self.obs_scales.ang_vel],
             device=self.device)
 
+        self.leg_origin_x = torch.tensor(
+            [0.093, 0.093, -0.093, -0.093],
+            device=self.device,
+            dtype=torch.float,
+        )
+
+        self.leg_origin_y = torch.tensor(
+            [0.036, -0.036, 0.036, -0.036],
+            device=self.device,
+            dtype=torch.float,
+        )
+
+        # shoulder 축 부호는 실제 play에서 확인 필요
+        # 오른쪽 shoulder 축이 반대라면 [1, -1, 1, -1]이 맞을 가능성이 있음
+        self.shoulder_sign = torch.tensor(
+            [1.0, -1.0, 1.0, -1.0],
+            device=self.device,
+            dtype=torch.float,
+        )
+
+        self.max_stride_x = 0.12
+        self.max_stride_y = 0.03
+        self.shoulder_y_gain = 1.0
+        self.shoulder_ref_limit = 0.10
         # ==== Step 5: 서보 응답 지연 (substep 단위, dt=5ms 해상도) ====
         if self.cfg.domain_rand.action_delay:
             delay_range = self.cfg.domain_rand.action_delay_range
@@ -48,6 +72,8 @@ class SpotmicroTest(LeggedRobot):
             print(f"[Action Delay] 활성화: "
                   f"substep 단위, dt={dt_ms:.1f}ms, "
                   f"range={delay_range[0]*dt_ms:.0f}~{delay_range[1]*dt_ms:.0f}ms")
+                  
+
 
     def step(self, actions):
         """서보 응답 지연을 substep 단위로 적용
@@ -270,6 +296,11 @@ class SpotmicroTest(LeggedRobot):
         reward *= (torch.norm(self.commands[:, :2], dim=1) > 0.1).float()
         return reward
         
+    
+    def _reward_stand_still(self):
+        cmd_norm = torch.norm(self.commands[:, :3], dim=1)
+        return torch.sum(torch.abs(self.dof_pos - self.default_dof_pos), dim=1) * (cmd_norm < 0.1)
+    '''
     def _reward_stand_still(self):
         cmd_norm = torch.norm(self.commands[:, :3], dim=1)
         is_stand = (cmd_norm < 0.1).float()
@@ -281,50 +312,116 @@ class SpotmicroTest(LeggedRobot):
         )
 
         return (lin_penalty + 0.5 * yaw_penalty + pose_penalty) * is_stand
-
+    '''
     def _get_ik_target(self):
-        vx = self.commands[:, 0]
-        wz = self.commands[:, 2] 
+        vx = self.commands[:, 0].unsqueeze(1)  # [N, 1]
+        vy = self.commands[:, 1].unsqueeze(1)  # [N, 1]
+        wz = self.commands[:, 2].unsqueeze(1)  # [N, 1]
+
+        leg_x = self.leg_origin_x.unsqueeze(0)  # [1, 4]
+        leg_y = self.leg_origin_y.unsqueeze(0)  # [1, 4]
+
+        # yaw 회전에 따른 다리별 목표 foot velocity
+        foot_vx = vx - wz * leg_y
+        foot_vy = vy + wz * leg_x
 
-        
-        v_left = vx - (wz * self.robot_
```

**변경 요약:**
  + self.leg_origin_x = torch.tensor(
  + [0.093, 0.093, -0.093, -0.093],
  + device=self.device,
  + dtype=torch.float,
  + )
  + self.leg_origin_y = torch.tensor(
  + [0.036, -0.036, 0.036, -0.036],
  + device=self.device,
  + dtype=torch.float,
  + )
  + self.shoulder_sign = torch.tensor(
  + [1.0, -1.0, 1.0, -1.0],
  + device=self.device,
  + dtype=torch.float,
  + )
  + self.max_stride_x = 0.12
  + self.max_stride_y = 0.03
  + self.shoulder_y_gain = 1.0
  + self.shoulder_ref_limit = 0.10
  + def _reward_stand_still(self):
  + cmd_norm = torch.norm(self.commands[:, :3], dim=1)
  + return torch.sum(torch.abs(self.dof_pos - self.default_dof_pos), dim=1) * (cmd_norm < 0.1)
  + '''
  + '''
  - vx = self.commands[:, 0]
  - wz = self.commands[:, 2]
  + vx = self.commands[:, 0].unsqueeze(1)  # [N, 1]
  + vy = self.commands[:, 1].unsqueeze(1)  # [N, 1]
  + wz = self.commands[:, 2].unsqueeze(1)  # [N, 1]
  + leg_x = self.leg_origin_x.unsqueeze(0)  # [1, 4]
  + leg_y = self.leg_origin_y.unsqueeze(0)  # [1, 4]
  + foot_vx = vx - wz * leg_y
  + foot_vy = vy + wz * leg_x
  - v_left = vx - (wz * self.robot_width / 2.0)
  - v_right = vx + (wz * self.robot_width / 2.0)
  - max_stride = 0.12
  - raw_stride_l = v_left * stance_time
  - stride_l = torch.clamp(raw_stride_l, -max_stride, max_stride)
  - raw_stride_r = v_right * stance_time
  - stride_r = torch.clamp(raw_stride_r, -max_stride, max_stride)
  - strides = torch.stack([stride_l, stride_r, stride_l, stride_r], dim=1)
  - offsets = torch.tensor([0.0, 0.5, 0.5, 0.0], device=self.device)
  + stride_x = foot_vx * stance_time
  + stride_y = foot_vy * stance_time
  + stride_x = torch.clamp(
  + stride_x,
  + -self.max_stride_x,
  + self.max_stride_x,
  + )
  + stride_y = torch.clamp(
  + stride_y,
  + -self.max_stride_y,
  + self.max_stride_y,
  + )
  + offsets = torch.tensor(
  + [0.0, 0.5, 0.5, 0.0],
  + device=self.device,
  + dtype=torch.float,
  + )
  - z = torch.full((self.num_envs, 4), -self.body_height, device=self.device)
  + y = torch.zeros((self.num_envs, 4), device=self.device)
  + z = torch.full(
  + (self.num_envs, 4),
  + -self.body_height,
  + device=self.device,
  + )
  - x[is_stance] = strides[is_stance] * (0.5 - t_stance[is_stance])
  - x[is_swing] = strides[is_swing] * (-0.5 + t_swing[is_swing])
  - z[is_swing] = -self.body_height + self.step_height * torch.sin(torch.pi * t_swing[is_swing])
  - d = torch.sqrt(x**2 + z**2)
  - cos_q2 = (d**2 - self.L1_EFF**2 - self.L2**2) / (2 * self.L1_EFF * self.L2)
  + x[is_stance] = stride_x[is_stance] * (0.5 - t_stance[is_stance])
  + y[is_stance] = stride_y[is_stance] * (0.5 - t_stance[is_stance])
  + x[is_swing] = stride_x[is_swing] * (-0.5 + t_swing[is_swing])
  + y[is_swing] = stride_y[is_swing] * (-0.5 + t_swing[is_swing])
  + z[is_swing] = (
  + -self.body_height
  + + self.step_height * torch.sin(torch.pi * t_swing[is_swing])
  + )
  + shoulder_raw = self.shoulder_y_gain * torch.atan2(y, -z)
  + shoulder_ref = torch.clamp(
  + shoulder_raw,
  + -self.shoulder_ref_limit,
  + self.shoulder_ref_limit,
  + )
  + shoulder_ref = shoulder_ref * self.shoulder_sign.unsqueeze(0)
  + z_eff = -torch.sqrt(torch.clamp(z * z + y * y, min=1e-6))
  + d = torch.sqrt(x**2 + z_eff**2)
  + cos_q2 = (d**2 - self.L1_EFF**2 - self.L2**2) / (
  + 2 * self.L1_EFF * self.L2
  + )
  - beta = torch.atan2(x, -z)
  - alpha_k = torch.atan2(self.L2 * torch.sin(q2), self.L1_EFF + self.L2 * torch.cos(q2))
  + beta = torch.atan2(x, -z_eff)
  + alpha_k = torch.atan2(
  + self.L2 * torch.sin(q2),
  + self.L1_EFF + self.L2 * torch.cos(q2),
  + )
  - ref_dof_pos[:, 1::3] = theta_leg
  - ref_dof_pos[:, 2::3] = theta_foot
  + ref_dof_pos[:, 0::3] = shoulder_ref
  + ref_dof_pos[:, 1::3] = theta_leg
  + ref_dof_pos[:, 2::3] = theta_foot
  - cmd_norm = torch.norm(self.commands[:, :3], dim=1, keepdim=True)  # [num_envs, 1]
  - blend = torch.clamp(cmd_norm / 0.1, 0.0, 1.0)  # 0=정지→default, 1=이동→IK
  + cmd_norm = torch.norm(self.commands[:, :3], dim=1, keepdim=True)
  + blend = torch.clamp(cmd_norm / 0.1, 0.0, 1.0)
  - run_name = 'spotmicro_v5_3_stand_still'
  + run_name = 'spotmicro_v5_4_IK_yaw'

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

| 지표 | 이전 (exp051) | 현재 (exp052) | 변화 |
|------|-------|-------|------|
| Timeout% | 89.5% | 98.5% | ✅ ↑ 8.9510% |
| 속도오차 X | 0.0458 | 0.0488 | ⚠️ ↑ 0.0030m/s |
| 토크포화 | 17.4% | 14.2% | ✅ ↓ 3.2352% |
| Roll | 1.8° | 1.4° | ✅ ↓ 0.4232° |
| Pitch | 2.6° | 1.3° | ✅ ↓ 1.2565° |
| 평균 전력 | 6.1258W | 5.9570W | ✅ ↓ 0.1688W |


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
| collection time | 0.8487 | 1.0131 | 0.7825 | 0.8562 |
| learning_time | 0.2817 | 0.3669 | 0.2690 | 0.2867 |
| total_fps | 86967.0000 | 92078.0000 | 73991.0000 | 86038.1267 |
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

