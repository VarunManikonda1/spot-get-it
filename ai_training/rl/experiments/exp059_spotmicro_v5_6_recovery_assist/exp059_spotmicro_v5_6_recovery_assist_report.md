# 실험 059: spotmicro_v5_6_recovery_assist

- **날짜:** 2026-05-15 16:44
- **Task:** spotmicro_test
- **Run name:** `spotmicro_v5_6_recovery_assist`
- **판정:** ✅ PASS

---

## 실험 목적

V5.6: recovery test

---

## 변경점 (이전 커밋 대비)

```diff
diff --git a/ai_training/rl/experiment_report.py b/ai_training/rl/experiment_report.py
index 8c8e66b..0667969 100644
--- a/ai_training/rl/experiment_report.py
+++ b/ai_training/rl/experiment_report.py
@@ -521,6 +521,7 @@ def generate_report(exp_id, purpose, diag_data, tb_data, diff_text,
     reward_scales = diag_data.get('reward_scales', {}) if diag_data else {}
     
     command_mode_metrics = diag_data.get('command_mode_metrics', {}) if diag_data else {}
+    recovery_data = diag_data.get('recovery', {}) if diag_data else {}
 
     # 자동 판정
     overall_judge, judgments = auto_judge(metrics, run_name)
@@ -580,7 +581,7 @@ def generate_report(exp_id, purpose, diag_data, tb_data, diff_text,
     diag_graph_dir = os.path.join(DIAG_DIR, task_name)
     diag_graphs_ref = ""
     if os.path.exists(diag_graph_dir):
-        for img_name in ['diagnostic_report.png', 'joint_detail.png', 'action_smoothness.png']:
+        for img_name in ['diagnostic_report.png', 'joint_detail.png', 'action_smoothness.png', 'recovery_report.png']:
             img_path = os.path.join(diag_graph_dir, img_name)
             if os.path.exists(img_path):
                 diag_graphs_ref += f"![{img_name}]({exp_dir_rel}/{img_name})\n"
@@ -613,14 +614,16 @@ def generate_report(exp_id, purpose, diag_data, tb_data, diff_text,
                 f"| {s.get('mean_roll_deg', 0):.1f} "
                 f"| {s.get('mean_pitch_deg', 0):.1f} "
                 f"| {s.get('mean_power', 0):.2f} "
+                f"| {s.get('recovery_success_rate_pct', 0) if s.get('recovery_success_rate_pct') is not None else 0:.1f} "
+                f"| {s.get('mean_recovery_time_s', 0) if s.get('mean_recovery_time_s') is not None else 0:.2f} "
                 f"| {s.get('episode_return', 0):.1f} |"
             )
         snap_table = '\n'.join(snap_rows)
         snapshot_section = f"""
 ## 학습 추이 (Checkpoint 스냅샷)
 
-| iter | Timeout% | 속도오차X | 토크포화% | Roll° | Pitch° | 전력(W) | Return |
-|------|----------|----------|----------|-------|--------|---------|--------|
+| iter | Timeout% | 속도오차X | 토크포화% | Roll° | Pitch° | 전력(W) | Recovery% | 회복시간(s) | Return |
+|------|----------|----------|----------|-------|--------|---------|-----------|-------------|--------|
 {snap_table}
 """
 
@@ -766,6 +769,52 @@ def generate_report(exp_id, purpose, diag_data, tb_data, diff_text,
 > 해석 기준: `제자리 회전`만 나쁘면 pure turn 학습/보행 패턴 문제, `큰 회전명령`만 나쁘면 yaw 명령 범위가 현재 토크/보폭 한계보다 큰 문제, `정지`가 나쁘면 stop drift 문제로 보면 됩니다.
         """
 
+
+    # --- Recovery assist 분석 ---
+    recovery_section = ""
+    if recovery_data:
+        sr = recovery_data.get('success_rate_pct')
+        ert = recovery_data.get('early_failure_rate_pct')
+        mrt = recovery_data.get('mean_recovery_time_s')
+        eligible = recovery_data.get('eligible_trials', 0)
+        total = recovery_data.get('total_trials', 0)
+        success_count = recovery_data.get('success_count', 0)
+        failure_count = recovery_data.get('failure_count', 0)
+
+        sr_s
```

**변경 요약:**
  + recovery_data = diag_data.get('recovery', {}) if diag_data else {}
  - for img_name in ['diagnostic_report.png', 'joint_detail.png', 'action_smoothness.png']:
  + for img_name in ['diagnostic_report.png', 'joint_detail.png', 'action_smoothness.png', 'recovery_report.png']:
  + f"| {s.get('recovery_success_rate_pct', 0) if s.get('recovery_success_rate_pct') is not None else 0:.1f} "
  + f"| {s.get('mean_recovery_time_s', 0) if s.get('mean_recovery_time_s') is not None else 0:.2f} "
  - | iter | Timeout% | 속도오차X | 토크포화% | Roll° | Pitch° | 전력(W) | Return |
  - |------|----------|----------|----------|-------|--------|---------|--------|
  + | iter | Timeout% | 속도오차X | 토크포화% | Roll° | Pitch° | 전력(W) | Recovery% | 회복시간(s) | Return |
  + |------|----------|----------|----------|-------|--------|---------|-----------|-------------|--------|
  + recovery_section = ""
  + if recovery_data:
  + sr = recovery_data.get('success_rate_pct')
  + ert = recovery_data.get('early_failure_rate_pct')
  + mrt = recovery_data.get('mean_recovery_time_s')
  + eligible = recovery_data.get('eligible_trials', 0)
  + total = recovery_data.get('total_trials', 0)
  + success_count = recovery_data.get('success_count', 0)
  + failure_count = recovery_data.get('failure_count', 0)
  + sr_str = f"{sr:.1f}%" if sr is not None else "N/A"
  + ert_str = f"{ert:.1f}%" if ert is not None else "N/A"
  + mrt_str = f"{mrt:.3f}s" if mrt is not None else "N/A"
  + if sr is None:
  + recovery_judge = "⚠️ eligible trial 없음"
  + elif sr >= 80 and (ert is not None and ert < 10):
  + recovery_judge = "✅ Recovery 안정적"
  + elif sr >= 50:
  + recovery_judge = "⚠️ 일부 회복 가능"
  + else:
  + recovery_judge = "❌ Recovery 부족"
  + recovery_section = f"""
  + | 지표 | 값 |
  + |------|-----|
  + | 판정 | {recovery_judge} |
  + | Recovery 성공률 | {sr_str} |
  + | 성공/실패 | {success_count} / {failure_count} |
  + | Eligible trials | {eligible} / {total} |
  + | 평균 회복 시간 | {mrt_str} |
  + | 조기 실패율 | {ert_str} |
  + | 평가 horizon | {recovery_data.get('horizon_s', 'N/A')} s |
  + | 초기 tilt 기준 | {recovery_data.get('initial_tilt_threshold_deg', 'N/A')}° |
  + | 안정 기준 | roll/pitch < {recovery_data.get('stable_threshold_deg', 'N/A')}°, height > {recovery_data.get('min_height_m', 'N/A')}m |
  + | 평균 초기 tilt | {recovery_data.get('mean_initial_tilt_deg', 'N/A')}° |
  + | 평균 초기 roll/pitch | {recovery_data.get('mean_initial_roll_deg', 'N/A')}° / {recovery_data.get('mean_initial_pitch_deg', 'N/A')}° |
  + | 1초 후 평균 roll/pitch | {recovery_data.get('mean_end_roll_deg', 'N/A')}° / {recovery_data.get('mean_end_pitch_deg', 'N/A')}° |
  + | 1초 내 최대 roll/pitch 평균 | {recovery_data.get('mean_max_roll_first_1s_deg', 'N/A')}° / {recovery_data.get('mean_max_pitch_first_1s_deg', 'N/A')}° |
  + > 해석: `Recovery 성공률`은 초기 tilt가 기준 이상인 episode 중 1초 내 안정 자세로 복귀한 비율입니다. 조기 실패율이 높으면 reset 직후 바로 넘어지는 것이고, 평균 회복 시간이 짧을수록 위기 대응이 빠른 것입니다.
  + """
  + | Recovery 성공률 | {metrics.get('recovery_success_rate_pct', 'N/A')}% |
  + | Recovery eligible trials | {metrics.get('recovery_eligible_trials', 'N/A')} |
  + | 평균 회복 시간 | {metrics.get('mean_recovery_time_s', 'N/A')} s |
  + | Recovery 조기 실패율 | {metrics.get('recovery_early_failure_rate_pct', 'N/A')}% |
  + {recovery_section}
  + 'recovery': diag_data.get('recovery', {}) if diag_data else {},
  - from isaacgym.torch_utils import torch_rand_float
  + from isaacgym.torch_utils import torch_rand_float, quat_from_euler_xyz
  + self.recovery_roll_pitch_range = 10.0 * torch.pi / 180.0  # ±10 deg
  + self.recovery_yaw_range = 3.14159                        # yaw는 자유
  + self.recovery_lin_vel_xy_range = 0.10                    # ±0.10 m/s
  + self.recovery_lin_vel_z_range = 0.03                     # ±0.03 m/s
  + self.recovery_ang_vel_xy_range = 0.60                    # ±0.60 rad/s
  + self.recovery_ang_vel_z_range = 0.30                     # ±0.30 rad/s
  + def check_termination(self):
  + super().check_termination()
  + base_height = self.root_states[:, 2]
  + self.reset_buf |= (base_height < 0.125)
  + self.reset_buf |= (self.projected_gravity[:, 2] > 0.0)
  - self.dof_pos[env_ids] = self.default_dof_pos * torch_rand_float(
  - 0.5, 1.5, (len(env_ids), self.num_dof), device=self.device)
  + joint_noise = torch_rand_float(
  + -0.05, 0.05,
  + (len(env_ids), self.num_dof),
  + device=self.device,
  + )
  + self.dof_pos[env_ids] = self.default_dof_pos + joint_noise
  - gymtorch.unwrap_tensor(env_ids_int32), len(env_ids_int32))
  + gymtorch.unwrap_tensor(env_ids_int32),
  + len(env_ids_int32),
  + )
  + self.gait_phase[env_ids] = torch_rand_float(
  + 0.0, 1.0,
  + (len(env_ids), 1),
  + device=self.device,
  + )
  - delay_range[0], delay_range[1] + 1,
  - (len(env_ids),), device=self.device)
  + delay_range[0],
  + delay_range[1] + 1,
  + (len(env_ids),),
  + device=self.device,
  + )
  - """base 속도를 0으로 리셋"""
  + """Recovery assist용 reset:
  + - base를 살짝 기울어진 상태로 시작
  + - roll/pitch angular velocity 부여
  + - 아직 완전히 넘어진 상태는 만들지 않음
  + """
  + num = len(env_ids)
  - self.root_states[env_ids, 7:13] = torch_rand_float(
  - -0.3, 0.3, (len(env_ids), 6), device=self.device)
  + self.root_states[env_ids, 2] = self.base_init_state[2] + self.env_origins[env_ids, 2]
  + roll = torch_rand_float(
  + -self.recovery_roll_pitch_range,
  + self.recovery_roll_pitch_range,
  + (num, 1),
  + device=self.device,
  + ).squeeze(1)
  + pitch = torch_rand_float(
  + -self.recovery_roll_pitch_range,
  + self.recovery_roll_pitch_range,
  + (num, 1),
  + device=self.device,
  + ).squeeze(1)
  + yaw = torch_rand_float(
  + -self.recovery_yaw_range,
  + self.recovery_yaw_range,
  + (num, 1),
  + device=self.device,
  + ).squeeze(1)
  + self.root_states[env_ids, 3:7] = quat_from_euler_xyz(roll, pitch, yaw)
  + self.root_states[env_ids, 7:9] = torch_rand_float(
  + -self.recovery_lin_vel_xy_range,
  + self.recovery_lin_vel_xy_range,
  + (num, 2),
  + device=self.device,
  + )
  + self.root_states[env_ids, 9] = torch_rand_float(
  + -self.recovery_lin_vel_z_range,
  + self.recovery_lin_vel_z_range,
  + (num, 1),
  + device=self.device,
  + ).squeeze(1)
  + self.root_states[env_ids, 10:12] = torch_rand_float(
  + -self.recovery_ang_vel_xy_range,
  + self.recovery_ang_vel_xy_range,
  + (num, 2),
  + device=self.device,
  + )
  + self.root_states[env_ids, 12] = torch_rand_float(
  + -self.recovery_ang_vel_z_range,
  + self.recovery_ang_vel_z_range,
  + (num, 1),
  + device=self.device,
  + ).squeeze(1)
  - gymtorch.unwrap_tensor(env_ids_int32), len(env_ids_int32))
  - def check_termination(self):
  - super().check_termination()
  - base_height = self.root_states[:, 2]
  - self.reset_buf |= (base_height < 0.155)
  - self.reset_buf |= (self.projected_gravity[:, 2] > 0.0)
  - def _resample_commands(self, env_ids):
  - self.commands[env_ids, 0] = torch_rand_float(
  - self.command_ranges["lin_vel_x"][0], self.command_ranges["lin_vel_x"][1],
  - (len(env_ids), 1), device=self.device).squeeze(1)
  - self.commands[env_ids, 1] = torch_rand_float(
  - self.command_ranges["lin_vel_y"][0], self.command_ranges["lin_vel_y"][1],
  - (len(env_ids), 1), device=self.device).squeeze(1)
  - if self.cfg.commands.heading_command:
  - self.commands[env_ids, 3] = torch_rand_float(
  - self.command_ranges["heading"][0], self.command_ranges["heading"][1],
  - (len(env_ids), 1), device=self.device).squeeze(1)
  - else:
  - self.commands[env_ids, 2] = torch_rand_float(
  - self.command_ranges["ang_vel_yaw"][0], self.command_ranges["ang_vel_yaw"][1],
  - (len(env_ids), 1), device=self.device).squeeze(1)
  - self.commands[env_ids, :2] *= (torch.norm(self.commands[env_ids, :2], dim=1) > 0.05).unsqueeze(1)
  + gymtorch.unwrap_tensor(env_ids_int32),
  + len(env_ids_int32),
  + )
  - tracking_lin_vel = 1.5
  - tracking_ang_vel = 1.3
  + tracking_lin_vel = 0.5
  + tracking_ang_vel = 0.5
  - ang_vel_xy = -0.4
  - orientation = -6.0
  + ang_vel_xy = -1.0
  + orientation = -10.0
  - trot_contact = 0.5
  - tracking_ik = 1.0
  + trot_contact = 0.2
  + tracking_ik = 0.8
  - randomize_friction = True
  + randomize_friction = False
  - randomize_base_mass = True
  + randomize_base_mass = False
  - push_robots = True
  - push_interval_s = 15
  + push_robots = False
  + push_interval_s = 8
  - entropy_coef = 0.01
  + entropy_coef = 0.005
  + learning_rate = 1e-4
  - run_name = 'spotmicro_v5_4_4_IK rollback'
  + run_name = 'spotmicro_v5_6_recovery_assist'
  - max_iterations = 1500
  + max_iterations = 1000
  + resume = True
  + load_run = "May13_09-16-55_spotmicro_v5_4_4_IK_rollback"
  + checkpoint = 700
  + recovery_horizon_s = 1.0
  + recovery_horizon_steps = max(1, int(recovery_horizon_s / env.dt))
  + recovery_initial_tilt_threshold = np.radians(7.0)
  + recovery_stable_threshold = np.radians(5.0)
  + recovery_min_height = max(0.18, float(env.cfg.rewards.base_height_target) - 0.03)
  + recovery_age = np.zeros(num_envs, dtype=np.int32)
  + recovery_active = np.ones(num_envs, dtype=bool)
  + recovery_init_roll = np.full(num_envs, np.nan)
  + recovery_init_pitch = np.full(num_envs, np.nan)
  + recovery_max_roll = np.zeros(num_envs, dtype=np.float32)
  + recovery_max_pitch = np.zeros(num_envs, dtype=np.float32)
  + recovery_first_stable_step = np.full(num_envs, -1, dtype=np.int32)
  + recovery_trials = []
  + def _start_recovery_trials(env_ids_np, roll_abs_np, pitch_abs_np, height_np):
  + """새 episode/reset 직후 recovery trial 초기화"""
  + if len(env_ids_np) == 0:
  + return
  + recovery_age[env_ids_np] = 0
  + recovery_active[env_ids_np] = True
  + recovery_init_roll[env_ids_np] = roll_abs_np[env_ids_np]
  + recovery_init_pitch[env_ids_np] = pitch_abs_np[env_ids_np]
  + recovery_max_roll[env_ids_np] = roll_abs_np[env_ids_np]
  + recovery_max_pitch[env_ids_np] = pitch_abs_np[env_ids_np]
  + recovery_first_stable_step[env_ids_np] = -1
  + def _record_recovery_trial(env_i, success, end_roll, end_pitch, end_height, forced_failure=False):
  + """한 recovery trial 결과 기록"""
  + init_roll = recovery_init_roll[env_i]
  + init_pitch = recovery_init_pitch[env_i]
  + if np.isnan(init_roll) or np.isnan(init_pitch):
  + return
  + init_tilt = max(float(init_roll), float(init_pitch))
  + eligible = init_tilt >= recovery_initial_tilt_threshold
  + recovery_time_s = None
  + if success and recovery_first_stable_step[env_i] >= 0:
  + recovery_time_s = float(recovery_first_stable_step[env_i] * env.dt)
  + recovery_trials.append({
  + 'eligible': bool(eligible),
  + 'success': bool(success) if eligible else False,
  + 'forced_failure': bool(forced_failure),
  + 'init_roll_deg': float(np.degrees(init_roll)),
  + 'init_pitch_deg': float(np.degrees(init_pitch)),
  + 'init_tilt_deg': float(np.degrees(init_tilt)),
  + 'max_roll_first_1s_deg': float(np.degrees(recovery_max_roll[env_i])),
  + 'max_pitch_first_1s_deg': float(np.degrees(recovery_max_pitch[env_i])),
  + 'end_roll_deg': float(np.degrees(end_roll)),
  + 'end_pitch_deg': float(np.degrees(end_pitch)),
  + 'end_height_m': float(end_height),
  + 'recovery_time_s': recovery_time_s,
  + })
  + def _summarize_recovery_trials():
  + eligible = [t for t in recovery_trials if t['eligible']]
  + successes = [t for t in eligible if t['success']]
  + failures = [t for t in eligible if not t['success']]
  + times = [t['recovery_time_s'] for t in successes if t['recovery_time_s'] is not None]
  + def _mean(key, rows):
  + return float(np.mean([r[key] for r in rows])) if rows else None
  + def _median(values):
  + return float(np.median(values)) if values else None
  + return {
  + 'horizon_s': float(recovery_horizon_s),
  + 'initial_tilt_threshold_deg': float(np.degrees(recovery_initial_tilt_threshold)),
  + 'stable_threshold_deg': float(np.degrees(recovery_stable_threshold)),
  + 'min_height_m': float(recovery_min_height),
  + 'total_trials': int(len(recovery_trials)),
  + 'eligible_trials': int(len(eligible)),
  + 'success_count': int(len(successes)),
  + 'failure_count': int(len(failures)),
  + 'success_rate_pct': float(len(successes) / len(eligible) * 100) if eligible else None,
  + 'early_failure_rate_pct': float(
  + sum(1 for t in eligible if t.get('forced_failure')) / len(eligible) * 100
  + ) if eligible else None,
  + 'mean_recovery_time_s': float(np.mean(times)) if times else None,
  + 'median_recovery_time_s': _median(times),
  + 'mean_initial_tilt_deg': _mean('init_tilt_deg', eligible),
  + 'mean_initial_roll_deg': _mean('init_roll_deg', eligible),
  + 'mean_initial_pitch_deg': _mean('init_pitch_deg', eligible),
  + 'mean_max_roll_first_1s_deg': _mean('max_roll_first_1s_deg', eligible),
  + 'mean_max_pitch_first_1s_deg': _mean('max_pitch_first_1s_deg', eligible),
  + 'mean_end_roll_deg': _mean('end_roll_deg', eligible),
  + 'mean_end_pitch_deg': _mean('end_pitch_deg', eligible),
  + 'mean_end_height_m': _mean('end_height_m', eligible),
  + }
  - data['base_height'].append(env.root_states[:, 2].mean().item())
  - data['base_vel_z'].append(env.base_lin_vel[:, 2].mean().item())
  + base_height_np = env.root_states[:, 2].cpu().numpy()
  + base_vel_z_np = env.base_lin_vel[:, 2].cpu().numpy()
  + data['base_height'].append(float(np.mean(base_height_np)))
  + data['base_vel_z'].append(float(np.mean(base_vel_z_np)))
  + roll_abs_np = np.abs(roll)
  + pitch_abs_np = np.abs(pitch)
  + uninitialized = np.where(np.isnan(recovery_init_roll))[0]
  + if len(uninitialized) > 0:
  + _start_recovery_trials(uninitialized, roll_abs_np, pitch_abs_np, base_height_np)
  + active_mask = recovery_active.copy()
  + if np.any(active_mask):
  + active_ids = np.where(active_mask)[0]
  + recovery_age[active_ids] += 1
  + recovery_max_roll[active_ids] = np.maximum(recovery_max_roll[active_ids], roll_abs_np[active_ids])
  + recovery_max_pitch[active_ids] = np.maximum(recovery_max_pitch[active_ids], pitch_abs_np[active_ids])
  + stable_now = (
  + (roll_abs_np < recovery_stable_threshold) &
  + (pitch_abs_np < recovery_stable_threshold) &
  + (base_height_np > recovery_min_height)
  + )
  + first_stable_ids = active_ids[
  + stable_now[active_ids] & (recovery_first_stable_step[active_ids] < 0)
  + ]
  + recovery_first_stable_step[first_stable_ids] = recovery_age[first_stable_ids]
  + horizon_ids = active_ids[recovery_age[active_ids] >= recovery_horizon_steps]
  + for env_i in horizon_ids:
  + success = bool(stable_now[env_i])
  + _record_recovery_trial(
  + env_i,
  + success=success,
  + end_roll=roll_abs_np[env_i],
  + end_pitch=pitch_abs_np[env_i],
  + end_height=base_height_np[env_i],
  + forced_failure=False,
  + )
  + recovery_active[horizon_ids] = False
  + done_ids_np = done_ids.cpu().numpy().astype(np.int64)
  + active_done_ids = done_ids_np[recovery_active[done_ids_np]]
  + for env_i in active_done_ids:
  + _record_recovery_trial(
  + env_i,
  + success=False,
  + end_roll=roll_abs_np[env_i],
  + end_pitch=pitch_abs_np[env_i],
  + end_height=base_height_np[env_i],
  + forced_failure=True,
  + )
  + _start_recovery_trials(done_ids_np, roll_abs_np, pitch_abs_np, base_height_np)
  + recovery_summary = _summarize_recovery_trials()
  - dt_step = env.dt  # 1 step의 실제 시간(초)
  + dt_step = env.dt * env.cfg.control.decimation  # 1 step의 실제 시간(초)
  + 'recovery_success_rate_pct': recovery_summary.get('success_rate_pct'),
  + 'recovery_eligible_trials': recovery_summary.get('eligible_trials', 0),
  + 'mean_recovery_time_s': recovery_summary.get('mean_recovery_time_s'),
  + print(f"\n{'='*60}")
  + print(f"  [15] Recovery Assist 분석")
  + print(f"{'='*60}")
  + print(f"  평가 horizon: {recovery_summary['horizon_s']:.2f}s")
  + print(f"  eligible 기준: 초기 |roll| 또는 |pitch| ≥ {recovery_summary['initial_tilt_threshold_deg']:.1f}°")
  + print(f"  안정 기준: |roll|, |pitch| < {recovery_summary['stable_threshold_deg']:.1f}°, height > {recovery_summary['min_height_m']:.3f}m")
  + print(f"  전체 trial: {recovery_summary['total_trials']}")
  + print(f"  eligible trial: {recovery_summary['eligible_trials']}")
  + if recovery_summary['success_rate_pct'] is None:
  + print("  Recovery 성공률: N/A (eligible trial 없음)")
  + else:
  + print(f"  Recovery 성공률: {recovery_summary['success_rate_pct']:.1f}% "
  + f"({recovery_summary['success_count']}/{recovery_summary['eligible_trials']})")
  + print(f"  조기 실패율: {recovery_summary['early_failure_rate_pct']:.1f}%")
  + if recovery_summary['mean_recovery_time_s'] is not None:
  + print(f"  평균 회복 시간: {recovery_summary['mean_recovery_time_s']:.3f}s")
  + print(f"  평균 초기 tilt: {recovery_summary['mean_initial_tilt_deg']:.2f}°")
  + print(f"  1초 후 평균 |roll|/|pitch|: "
  + f"{recovery_summary['mean_end_roll_deg']:.2f}° / "
  + f"{recovery_summary['mean_end_pitch_deg']:.2f}°")
  + if recovery_summary['success_rate_pct'] >= 80 and recovery_summary['early_failure_rate_pct'] < 10:
  + print("  → ✓ Recovery assist 안정적")
  + elif recovery_summary['success_rate_pct'] >= 50:
  + print("  → △ 일부 회복 가능. perturbation curriculum 또는 reward 조정 필요")
  + else:
  + print("  → ⚠ Recovery 성공률 낮음. perturbation 강도/termination/reward 확인 필요")
  + if recovery_trials:
  + eligible_trials = [t for t in recovery_trials if t['eligible']]
  + fig4, axes4 = plt.subplots(2, 2, figsize=(14, 10))
  + fig4.suptitle('Recovery Assist Analysis', fontsize=14)
  + ax = axes4[0, 0]
  + ax.plot(steps_range, [np.degrees(r) for r in data['roll_abs']], 'r-', alpha=0.7, label='|Roll|')
  + ax.plot(steps_range, [np.degrees(p) for p in data['pitch_abs']], 'b-', alpha=0.7, label='|Pitch|')
  + ax.axhline(y=recovery_summary['stable_threshold_deg'], color='green', linestyle='--', alpha=0.5, label='Stable threshold')
  + ax.axhline(y=recovery_summary['initial_tilt_threshold_deg'], color='orange', linestyle='--', alpha=0.5, label='Eligible threshold')
  + ax.set_xlabel('Step')
  + ax.set_ylabel('Angle (deg)')
  + ax.set_title('Roll/Pitch Stability During Diagnostic')
  + ax.legend()
  + ax = axes4[0, 1]
  + if eligible_trials:
  + init_tilts = [t['init_tilt_deg'] for t in eligible_trials]
  + end_tilts = [max(t['end_roll_deg'], t['end_pitch_deg']) for t in eligible_trials]
  + ax.hist(init_tilts, bins=20, alpha=0.6, label='Initial tilt')
  + ax.hist(end_tilts, bins=20, alpha=0.6, label='Tilt at 1s/end')
  + ax.set_xlabel('Tilt (deg)')
  + ax.set_ylabel('Count')
  + ax.set_title('Initial vs End Tilt Distribution')
  + ax.legend()
  + else:
  + ax.text(0.5, 0.5, 'No eligible trials', ha='center', va='center')
  + ax.set_axis_off()
  + ax = axes4[1, 0]
  + if eligible_trials:
  + success_count = recovery_summary['success_count']
  + failure_count = recovery_summary['failure_count']
  + ax.bar(['Success', 'Failure'], [success_count, failure_count])
  + ax.set_ylabel('Trials')
  + ax.set_title(f"Recovery Success Rate: {recovery_summary['success_rate_pct']:.1f}%")
  + else:
  + ax.text(0.5, 0.5, 'No eligible trials', ha='center', va='center')
  + ax.set_axis_off()
  + ax = axes4[1, 1]
  + times = [t['recovery_time_s'] for t in eligible_trials if t['success'] and t['recovery_time_s'] is not None]
  + if times:
  + ax.hist(times, bins=20, alpha=0.8)
  + ax.axvline(np.mean(times), linestyle='--', alpha=0.7, label=f"mean={np.mean(times):.2f}s")
  + ax.set_xlabel('Recovery time (s)')
  + ax.set_ylabel('Count')
  + ax.set_title('Recovery Time Distribution')
  + ax.legend()
  + else:
  + ax.text(0.5, 0.5, 'No successful recovery times', ha='center', va='center')
  + ax.set_axis_off()
  + plt.tight_layout()
  + save_path4 = os.path.join(diag_dir, 'recovery_report.png')
  + plt.savefig(save_path4, dpi=150)
  + print(f"  Recovery 그래프 저장: {save_path4}")
  + 'recovery_success_rate_pct': recovery_summary.get('success_rate_pct'),
  + 'recovery_eligible_trials': recovery_summary.get('eligible_trials', 0),
  + 'mean_recovery_time_s': recovery_summary.get('mean_recovery_time_s'),
  + 'recovery_early_failure_rate_pct': recovery_summary.get('early_failure_rate_pct'),
  + 'recovery': recovery_summary,

---

## 현재 Reward Scales

| Reward | Scale |
|--------|-------|
| action_rate | -0.05 |
| ang_vel_xy | -1.0 |
| collision | -1.0 |
| dof_acc | -2.5e-07 |
| dof_vel | -0.001 |
| lin_vel_z | -2.0 |
| orientation | -10.0 |
| stand_still | -0.5 |
| termination | -10.0 |
| torques | -0.001 |
| tracking_ang_vel | 0.5 |
| tracking_ik | 0.8 |
| tracking_lin_vel | 0.5 |
| trot_contact | 0.2 |

---

## 진단 결과 (Diagnostic)

### 핵심 지표

- ✅ Timeout: 100.0% (≥80%)
- ✅ 속도오차 X: 0.0480 m/s (<0.08)
- ⚠️ 토크포화: 11.5% (10~40%)
- ✅ 자세: roll 1.4°, pitch 1.6° (안정)
- ✅ 조기종료: 0.0% (<5%)

### 상세 수치

| 지표 | 값 |
|------|-----|
| Timeout% | 100.0 |
| 조기종료% | 0.0 |
| 속도오차 X | 0.04798661917448044 m/s |
| 속도오차 Y | 0.026284771040081978 m/s |
| 각속도오차 | 0.09793634712696075 rad/s |
| 토크포화% | 11.545052170052172 |
| 평균 높이 | 0.21160255695060218 m |
| Roll (평균) | 1.4055005311965942° |
| Pitch (평균) | 1.6028794050216675° |
| Action Rate | 0.004450474865734577 |
| 평균 전력 | 4.882387161254883 W |
| CoT | 2.4461582956172494 |
| Recovery 성공률 | 77.77777777777779% |
| Recovery eligible trials | 9 |
| 평균 회복 시간 | 0.0828571410051414 s |
| Recovery 조기 실패율 | 0.0% |

## Recovery Assist 분석

| 지표 | 값 |
|------|-----|
| 판정 | ⚠️ 일부 회복 가능 |
| Recovery 성공률 | 77.8% |
| 성공/실패 | 7 / 2 |
| Eligible trials | 9 / 192 |
| 평균 회복 시간 | 0.083s |
| 조기 실패율 | 0.0% |
| 평가 horizon | 1.0 s |
| 초기 tilt 기준 | 7.0° |
| 안정 기준 | roll/pitch < 5.0°, height > 0.18m |
| 평균 초기 tilt | 7.782566273034803° |
| 평균 초기 roll/pitch | 7.387166954005849° / 2.971262145555548° |
| 1초 후 평균 roll/pitch | 3.4247761170069375° / 1.9886263410250347° |
| 1초 내 최대 roll/pitch 평균 | 7.918982187906901° / 5.231283876630995° |

> 해석: `Recovery 성공률`은 초기 tilt가 기준 이상인 episode 중 1초 내 안정 자세로 복귀한 비율입니다. 조기 실패율이 높으면 reset 직후 바로 넘어지는 것이고, 평균 회복 시간이 짧을수록 위기 대응이 빠른 것입니다.


## Command Mode별 회전 추종 분석

| 모드 | 비율 | 샘플 수 | 평균 abs(cmd_wz) | 평균 abs(actual_wz) | yaw 오차 | 상태 |
|------|------|---------|------------------|---------------------|----------|------|
| 정지 | 10.9% | 21006 | 0.0224 | 0.0821 | 0.0846 | ❌ |
| 직진/저회전 | 16.7% | 32044 | 0.0679 | 0.1084 | 0.0882 | ⚠️ |
| 제자리 회전 | 34.4% | 66053 | 0.2190 | 0.1924 | 0.1083 | ⚠️ |
| 전진+회전 | 18.0% | 34536 | 0.2332 | 0.2231 | 0.0921 | ⚠️ |
| 큰 회전명령 | 0.0% | 0 | N/A | N/A | N/A | N/A |

> 해석 기준: `제자리 회전`만 나쁘면 pure turn 학습/보행 패턴 문제, `큰 회전명령`만 나쁘면 yaw 명령 범위가 현재 토크/보폭 한계보다 큰 문제, `정지`가 나쁘면 stop drift 문제로 보면 됩니다.
        


## 학습 곡선 분석

| 지표 | 값 | 판정 |
|------|-----|------|
| 수렴 iter (90%) | 111 | 빠름 |
| 후반 안정성 (CV) | 0.013 | 안정 |
| 정체 구간 | 없음 | |


## Per-leg 분석

### 발별 접촉/공중 비율

| 발 | 접촉% | 공중% |
|-----|-------|------|
| foot_0 | 80.2% | 19.8% |
| foot_1 | 82.2% | 17.8% |
| foot_2 | 74.9% | 25.1% |
| foot_3 | 75.2% | 24.8% |

### 관절별 토크 포화

| 관절 | 포화% | 최대토크 | 한계 | 상태 |
|------|------|---------|------|------|
| front_left_shoulder | 0.0% | 2.428 | 2.940 | ✅ |
| front_left_leg | 16.2% | 2.940 | 2.940 | ⚠️ |
| front_left_foot | 13.0% | 2.940 | 2.940 | ⚠️ |
| front_right_shoulder | 0.0% | 2.649 | 2.940 | ✅ |
| front_right_leg | 27.5% | 2.940 | 2.940 | ❌ |
| front_right_foot | 4.6% | 2.940 | 2.940 | ✅ |
| rear_left_shoulder | 0.0% | 2.940 | 2.940 | ✅ |
| rear_left_leg | 26.4% | 2.940 | 2.940 | ❌ |
| rear_left_foot | 19.1% | 2.940 | 2.940 | ⚠️ |
| rear_right_shoulder | 0.0% | 2.647 | 2.940 | ✅ |
| rear_right_leg | 27.6% | 2.940 | 2.940 | ❌ |
| rear_right_foot | 4.2% | 2.940 | 2.940 | ✅ |


## Gait 분석

| 지표 | 값 |
|------|-----|
| Gait 주파수 | 0.25 Hz |
| Gait 주기 | 49 steps |
| 대각 동기화율 | 87.4% |
| L/R 비대칭 | 0.0%p |
| 판정 | ✅ Trot 패턴 / ✅ 대칭 |


## 관절별 상세

| 관절 | 토크포화% | 사용범위% | 편향(rad) | 한계근접% | 상태 |
|------|----------|----------|----------|----------|------|
| front_left_shoulder | 0.0% | 31% | +0.046 | 0.0% | ✅ |
| front_left_leg | 16.2% | 21% | -0.736 | 0.0% | ⚠️ |
| front_left_foot | 13.0% | 32% | +1.281 | 0.0% | ⚠️ |
| front_right_shoulder | 0.0% | 33% | +0.016 | 0.0% | ✅ |
| front_right_leg | 27.5% | 20% | -0.752 | 0.0% | ⚠️ |
| front_right_foot | 4.6% | 30% | +1.216 | 0.0% | ⚠️ |
| rear_left_shoulder | 0.0% | 46% | -0.090 | 0.0% | ✅ |
| rear_left_leg | 26.4% | 27% | -0.762 | 0.0% | ⚠️ |
| rear_left_foot | 19.1% | 45% | +1.301 | 0.0% | ⚠️ |
| rear_right_shoulder | 0.0% | 32% | +0.029 | 0.0% | ✅ |
| rear_right_leg | 27.6% | 20% | -0.725 | 0.0% | ⚠️ |
| rear_right_foot | 4.2% | 36% | +1.143 | 0.0% | ⚠️ |


## 에너지 효율 상세

| 지표 | 값 |
|------|-----|
| 평균 전력 | 4.88 W |
| 피크 전력 | 14.06 W |
| 피크/평균 비율 | 2.9x |
| CoT | 2.45 |

### 관절별 전력 분배

| 관절 | 평균전력(W) | 비율% |
|------|-----------|-------|
| front_left_foot | 0.840 | 17.2% |
| rear_left_foot | 0.749 | 15.3% |
| front_right_foot | 0.715 | 14.6% |
| rear_right_foot | 0.698 | 14.3% |
| front_right_leg | 0.499 | 10.2% |
| front_left_leg | 0.439 | 9.0% |
| rear_right_leg | 0.398 | 8.2% |
| rear_left_leg | 0.357 | 7.3% |
| rear_right_shoulder | 0.069 | 1.4% |
| front_right_shoulder | 0.042 | 0.9% |
| rear_left_shoulder | 0.042 | 0.9% |
| front_left_shoulder | 0.034 | 0.7% |


## 이전 실험 대비 비교

| 지표 | 이전 (exp058) | 현재 (exp059) | 변화 |
|------|-------|-------|------|
| Timeout% | 99.2% | 100.0% | ✅ ↑ 0.7752% |
| 속도오차 X | 0.0501 | 0.0480 | ✅ ↓ 0.0021m/s |
| 토크포화 | 14.0% | 11.5% | ✅ ↓ 2.4068% |
| Roll | 1.4° | 1.4° | ✅ ↓ 0.0229° |
| Pitch | 1.3° | 1.6° | ⚠️ ↑ 0.2665° |
| 평균 전력 | 5.8682W | 4.8824W | ✅ ↓ 0.9858W |


## Tensorboard 학습 곡선 요약

| 스칼라 | 최종값 | 최대 | 최소 | 후반10% 평균 |
|--------|--------|------|------|-------------|
| rew_action_rate | -0.0133 | -0.0007 | -0.0433 | -0.0134 |
| rew_ang_vel_xy | -0.0357 | -0.0195 | -0.1468 | -0.0377 |
| rew_collision | 0.0000 | 0.0000 | -0.0001 | -0.0000 |
| rew_dof_acc | -0.0034 | -0.0003 | -0.0087 | -0.0037 |
| rew_dof_vel | -0.0049 | -0.0003 | -0.0089 | -0.0052 |
| rew_lin_vel_z | -0.0020 | -0.0002 | -0.0028 | -0.0020 |
| rew_orientation | -0.0062 | -0.0007 | -0.0485 | -0.0075 |
| rew_stand_still | -0.0795 | -0.0010 | -0.1392 | -0.0807 |
| rew_termination | 0.0000 | 0.0000 | -0.0005 | -0.0000 |
| rew_torques | -0.0218 | -0.0001 | -0.0270 | -0.0224 |
| rew_tracking_ang_vel | 0.3929 | 0.3963 | 0.0023 | 0.3904 |
| rew_tracking_ik | 0.6736 | 0.6837 | 0.0063 | 0.6736 |
| rew_tracking_lin_vel | 0.4784 | 0.4827 | 0.0050 | 0.4760 |
| rew_trot_contact | 0.1194 | 0.1408 | 0.0016 | 0.1229 |
| learning_rate | 0.0003 | 0.0011 | 0.0000 | 0.0002 |
| surrogate | -0.0028 | 0.0023 | -0.0049 | -0.0029 |
| value_function | 0.0040 | 0.1524 | 0.0029 | 0.0050 |
| collection time | 0.8317 | 0.9823 | 0.8046 | 0.8608 |
| learning_time | 0.2855 | 0.3673 | 0.2673 | 0.2886 |
| total_fps | 87994.0000 | 90246.0000 | 76967.0000 | 85669.1100 |
| mean_noise_std | 0.0935 | 0.1880 | 0.0900 | 0.0935 |
| mean_episode_length | 1002.0000 | 1002.0000 | 12.5000 | 1000.6587 |
| time | 1002.0000 | 1002.0000 | 12.5000 | 1000.6587 |
| mean_reward | 29.7685 | 30.9258 | 0.1141 | 29.8851 |
| time | 29.7685 | 30.9258 | 0.1141 | 29.8851 |

총 학습 iteration: 999


---

## 그래프

### Tensorboard 학습 곡선
![tb_training_curves.png](./tb_training_curves.png)
![tb_individual_rewards.png](./tb_individual_rewards.png)

### Diagnostic 결과
![diagnostic_report.png](./diagnostic_report.png)
![joint_detail.png](./joint_detail.png)
![action_smoothness.png](./action_smoothness.png)
![recovery_report.png](./recovery_report.png)


---

## 결론 및 다음 실험 방향

**자동 판정:** ✅ PASS

대부분 기준을 통과했으나 일부 항목이 보통 수준입니다.
  - ⚠️ 토크포화: 11.5% (10~40%)

> 💡 위 결론은 자동 생성되었습니다. 필요시 수정하세요.

---

## 메모

_(추가 메모가 있으면 여기에 작성)_

