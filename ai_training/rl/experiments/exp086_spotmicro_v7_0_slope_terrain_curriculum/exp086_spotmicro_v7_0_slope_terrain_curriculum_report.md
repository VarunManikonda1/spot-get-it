# 실험 086: spotmicro_v7_0_slope_terrain_curriculum

- **날짜:** 2026-05-26 12:58
- **Task:** spotmicro_test
- **Run name:** `spotmicro_v7_0_slope_terrain_curriculum`
- **판정:** ❌ FAIL (일부 기준 미달)

---

## 실험 목적

v7.0: 지형 학습 시작

---

## 변경점 (이전 커밋 대비)

```diff
diff --git a/ai_training/rl/experiment_report.py b/ai_training/rl/experiment_report.py
index 3c6b10c..f325649 100644
--- a/ai_training/rl/experiment_report.py
+++ b/ai_training/rl/experiment_report.py
@@ -449,11 +449,12 @@ def auto_judge(metrics, run_name='', recovery_data=None, transition_recovery_dat
 
     judgments = []
     all_pass = True
+    terrain_mode = 'terrain' in run_name or 'slope' in run_name
     prefall_mode = (
         'prefall' in run_name
-        or bool(recovery_data)
-        or bool(transition_recovery_data)
+        or ('recovery' in run_name and not terrain_mode)
     )
+    recovery_metrics_present = bool(recovery_data) or bool(transition_recovery_data)
 
     timeout = metrics.get('timeout_pct', 0)
     timeout_pass = 95 if prefall_mode else 80
@@ -462,7 +463,7 @@ def auto_judge(metrics, run_name='', recovery_data=None, transition_recovery_dat
         judgments.append(f"✅ Timeout: {timeout:.1f}% (≥{timeout_pass}%)")
     elif timeout >= timeout_warn:
         judgments.append(f"⚠️ Timeout: {timeout:.1f}% ({timeout_warn}~{timeout_pass}%, 개선 필요)")
-        if prefall_mode:
+        if prefall_mode or terrain_mode:
             all_pass = False
     elif timeout >= 60:
         judgments.append(f"⚠️ Timeout: {timeout:.1f}% (60~{timeout_warn}%, 보통)")
@@ -491,12 +492,23 @@ def auto_judge(metrics, run_name='', recovery_data=None, transition_recovery_dat
 
     roll = metrics.get('mean_roll_deg', 0)
     pitch = metrics.get('mean_pitch_deg', 0)
-    if roll < 8 and pitch < 8:
-        judgments.append(f"✅ 자세: roll {roll:.1f}°, pitch {pitch:.1f}° (안정)")
-    elif roll < 15 and pitch < 15:
-        judgments.append(f"⚠️ 자세: roll {roll:.1f}°, pitch {pitch:.1f}° (보통)")
+    posture_pass = 10 if terrain_mode else 8
+    posture_warn = 18 if terrain_mode else 15
+    if roll < posture_pass and pitch < posture_pass:
+        judgments.append(
+            f"✅ 자세: roll {roll:.1f}°, pitch {pitch:.1f}° "
+            f"(<{posture_pass}°)"
+        )
+    elif roll < posture_warn and pitch < posture_warn:
+        judgments.append(
+            f"⚠️ 자세: roll {roll:.1f}°, pitch {pitch:.1f}° "
+            f"({posture_pass}~{posture_warn}°)"
+        )
     else:
-        judgments.append(f"❌ 자세: roll {roll:.1f}°, pitch {pitch:.1f}° (불안정)")
+        judgments.append(
+            f"❌ 자세: roll {roll:.1f}°, pitch {pitch:.1f}° "
+            f"(>{posture_warn}°)"
+        )
         all_pass = False
 
     early_death = metrics.get('early_death_pct', 0)
@@ -505,7 +517,7 @@ def auto_judge(metrics, run_name='', recovery_data=None, transition_recovery_dat
         judgments.append(f"✅ 조기종료: {early_death:.1f}% (<{early_death_pass}%)")
     elif early_death < 20:
         judgments.append(f"⚠️ 조기종료: {early_death:.1f}% (5~20%)")
-        if prefall_mode:
+        if prefall_mode or terrain_mode:
             all_pass = False
     else:
         judgments.append(f"❌ 조기종료: {early_death:.1f}% (>20%)")
@@ -517,9 +529,12 @@ def auto_judge(metrics, r
```

**변경 요약:**
  + terrain_mode = 'terrain' in run_name or 'slope' in run_name
  - or bool(recovery_data)
  - or bool(transition_recovery_data)
  + or ('recovery' in run_name and not terrain_mode)
  + recovery_metrics_present = bool(recovery_data) or bool(transition_recovery_data)
  - if prefall_mode:
  + if prefall_mode or terrain_mode:
  - if roll < 8 and pitch < 8:
  - judgments.append(f"✅ 자세: roll {roll:.1f}°, pitch {pitch:.1f}° (안정)")
  - elif roll < 15 and pitch < 15:
  - judgments.append(f"⚠️ 자세: roll {roll:.1f}°, pitch {pitch:.1f}° (보통)")
  + posture_pass = 10 if terrain_mode else 8
  + posture_warn = 18 if terrain_mode else 15
  + if roll < posture_pass and pitch < posture_pass:
  + judgments.append(
  + f"✅ 자세: roll {roll:.1f}°, pitch {pitch:.1f}° "
  + f"(<{posture_pass}°)"
  + )
  + elif roll < posture_warn and pitch < posture_warn:
  + judgments.append(
  + f"⚠️ 자세: roll {roll:.1f}°, pitch {pitch:.1f}° "
  + f"({posture_pass}~{posture_warn}°)"
  + )
  - judgments.append(f"❌ 자세: roll {roll:.1f}°, pitch {pitch:.1f}° (불안정)")
  + judgments.append(
  + f"❌ 자세: roll {roll:.1f}°, pitch {pitch:.1f}° "
  + f"(>{posture_warn}°)"
  + )
  - if prefall_mode:
  + if prefall_mode or terrain_mode:
  + if prefall_mode:
  + all_pass = False
  - all_pass = False
  + if prefall_mode:
  + all_pass = False
  - all_pass = False
  + if prefall_mode:
  + all_pass = False
  + elif terrain_mode and recovery_metrics_present:
  + judgments.append(
  + "ℹ️ Recovery/pre-fall 지표는 참고값입니다 "
  + "(terrain run 자동 PASS/FAIL 기준에서는 제외)"
  + )
  + terrain_cfg = config_snapshot.get('terrain', {}) if config_snapshot else {}
  + terrain_section = ""
  + if terrain_cfg:
  + terrain_section = f"""
  + | 항목 | 값 |
  + |------|-----|
  + | 평가 모드 | {terrain_cfg.get('eval_mode', 'N/A')} |
  + | walk_eval | {terrain_cfg.get('walk_eval', 'N/A')} |
  + | mesh_type | {terrain_cfg.get('mesh_type', 'N/A')} |
  + | terrain_profile | {terrain_cfg.get('terrain_profile', 'N/A')} |
  + | measure_heights | {terrain_cfg.get('measure_heights', 'N/A')} |
  + | grid | {terrain_cfg.get('num_rows', 'N/A')} x {terrain_cfg.get('num_cols', 'N/A')} |
  + | env 크기 | {terrain_cfg.get('terrain_length', 'N/A')} x {terrain_cfg.get('terrain_width', 'N/A')} m |
  + | terrain_proportions | {terrain_cfg.get('terrain_proportions', 'N/A')} |
  + | slope max | {terrain_cfg.get('spotmicro_slope_max', 'N/A')} |
  + | rolling amp max | {terrain_cfg.get('spotmicro_rolling_amp_max', 'N/A')} m |
  + """
  + {terrain_section}
  - move_up = distance > self.terrain.env_length / 2
  + move_up_distance = getattr(
  + self.cfg.terrain,
  + "curriculum_move_up_distance",
  + self.terrain.env_length / 2,
  + )
  + move_down_command_scale = getattr(
  + self.cfg.terrain,
  + "curriculum_move_down_command_scale",
  + 0.5,
  + )
  + move_up = distance > move_up_distance
  - move_down = (distance < torch.norm(self.commands[env_ids, :2], dim=1)*self.max_episode_length_s*0.5) * ~move_up
  + move_down = (
  + distance < torch.norm(self.commands[env_ids, :2], dim=1)
  + * self.max_episode_length_s
  + * move_down_command_scale
  + ) * ~move_up
  - self.recovery_yaw_range = math.pi
  + self.recovery_yaw_range = math.radians(
  + getattr(self.cfg.domain_rand, "recovery_yaw_range_deg", 180.0)
  + )
  - mesh_type = 'plane'
  - curriculum = False
  + mesh_type = 'trimesh'
  + terrain_profile = 'spotmicro_slope'
  + curriculum = True
  + horizontal_scale = 0.05
  + vertical_scale = 0.005
  + border_size = 8.0
  + max_init_terrain_level = 2
  + num_rows = 8
  + num_cols = 12
  + terrain_length = 6.0
  + terrain_width = 6.0
  + curriculum_move_up_distance = 0.90
  + curriculum_move_down_command_scale = 0.25
  + terrain_proportions = [0.40, 0.25, 0.35]
  + spotmicro_slope_min = 0.02
  + spotmicro_slope_max = 0.14
  + spotmicro_rough_height_max = 0.008
  + spotmicro_rolling_amp_max = 0.025
  + spotmicro_rolling_wavelength_min = 0.45
  + spotmicro_rolling_wavelength_max = 1.20
  + spotmicro_terrain_platform_size = 0.7
  + slope_treshold = 0.75
  - tilt_threshold_deg = 17.0
  - full_tilt_deg = 27.0
  + tilt_threshold_deg = 14.0
  + full_tilt_deg = 25.0
  - command_scale = 0.0
  + command_scale = 0.15
  - phase_scale = 0.0
  + phase_scale = 0.1
  - recovery_stance_contact = 0.25
  - recovery_reward_tilt_threshold_deg = 15.0
  + recovery_reward_tilt_threshold_deg = 12.0
  - recovery_relief_tilt_threshold_deg = 17.0
  - recovery_relief_full_tilt_deg = 27.0
  + recovery_relief_tilt_threshold_deg = 14.0
  + recovery_relief_full_tilt_deg = 25.0
  - transition_tilt_push_ang_vel_xy = 0.30
  + transition_tilt_push_ang_vel_xy = 0.40
  + recovery_yaw_range_deg = 180.0
  - run_name = 'spotmicro_v6_4_prefall_brace_mode'
  + run_name = 'spotmicro_v7_0_slope_terrain_curriculum'
  - max_iterations = 500
  + max_iterations = 800
  - def run_diagnostic(args, checkpoint_path=None, lightweight=False, with_dr=False, recovery_range_deg=None):
  + def run_diagnostic(
  + args,
  + checkpoint_path=None,
  + lightweight=False,
  + with_dr=False,
  + recovery_range_deg=None,
  + flat_eval=False,
  + terrain_curriculum_eval=False,
  + walk_eval=False,
  + ):
  - env_cfg.terrain.curriculum = False
  + env_cfg.terrain.curriculum = bool(terrain_curriculum_eval)
  + if flat_eval:
  + env_cfg.terrain.mesh_type = 'plane'
  + env_cfg.terrain.measure_heights = False
  + print("[진단] flat_eval: plane 지형으로 진단합니다")
  + else:
  + terrain_profile = getattr(env_cfg.terrain, "terrain_profile", "default")
  + print(
  + "[진단] terrain_eval: "
  + f"mesh={env_cfg.terrain.mesh_type}, profile={terrain_profile}, "
  + f"measure_heights={env_cfg.terrain.measure_heights}, "
  + f"curriculum={env_cfg.terrain.curriculum}")
  + if walk_eval:
  + env_cfg.domain_rand.recovery_roll_pitch_range_deg = 0.0
  + env_cfg.domain_rand.recovery_yaw_range_deg = 0.0
  + env_cfg.domain_rand.recovery_lin_vel_xy_range = 0.0
  + env_cfg.domain_rand.recovery_lin_vel_z_range = 0.0
  + env_cfg.domain_rand.recovery_ang_vel_xy_range = 0.0
  + env_cfg.domain_rand.recovery_ang_vel_z_range = 0.0
  + env_cfg.domain_rand.transition_tilt_push = False
  + env_cfg.domain_rand.push_robots = False
  + print("[진단] walk_eval: recovery reset/push 없이 보행만 진단합니다")
  + terrain_eval_mode = (
  + 'flat'
  + if flat_eval else
  + ('terrain_curriculum' if terrain_curriculum_eval else 'terrain_random')
  + )
  + if walk_eval:
  + terrain_eval_mode = f"{terrain_eval_mode}_walk"
  + 'terrain': {
  + 'eval_mode': terrain_eval_mode,
  + 'walk_eval': bool(walk_eval),
  + 'mesh_type': str(getattr(env.cfg.terrain, 'mesh_type', 'unknown')),
  + 'terrain_profile': str(getattr(env.cfg.terrain, 'terrain_profile', 'default')),
  + 'measure_heights': bool(getattr(env.cfg.terrain, 'measure_heights', False)),
  + 'curriculum': bool(getattr(env.cfg.terrain, 'curriculum', False)),
  + 'num_rows': int(getattr(env.cfg.terrain, 'num_rows', 0)),
  + 'num_cols': int(getattr(env.cfg.terrain, 'num_cols', 0)),
  + 'terrain_length': float(getattr(env.cfg.terrain, 'terrain_length', 0.0)),
  + 'terrain_width': float(getattr(env.cfg.terrain, 'terrain_width', 0.0)),
  + 'terrain_proportions': list(getattr(env.cfg.terrain, 'terrain_proportions', [])),
  + 'spotmicro_slope_max': float(getattr(env.cfg.terrain, 'spotmicro_slope_max', 0.0)),
  + 'spotmicro_rolling_amp_max': float(getattr(env.cfg.terrain, 'spotmicro_rolling_amp_max', 0.0)),
  + },
  + flat_eval = '--flat-eval' in sys.argv
  + if flat_eval:
  + sys.argv.remove('--flat-eval')
  + terrain_curriculum_eval = '--terrain-curriculum-eval' in sys.argv
  + if terrain_curriculum_eval:
  + sys.argv.remove('--terrain-curriculum-eval')
  + walk_eval = '--walk-eval' in sys.argv
  + if walk_eval:
  + sys.argv.remove('--walk-eval')
  + flat_eval=flat_eval,
  + terrain_curriculum_eval=terrain_curriculum_eval,
  + walk_eval=walk_eval,
  + if getattr(self.cfg, "terrain_profile", "") == "spotmicro_slope":
  + return self.make_spotmicro_slope_terrain(terrain, choice, difficulty)
  + def make_spotmicro_slope_terrain(self, terrain, choice, difficulty):
  + """Gentle blind-terrain curriculum for SpotMicro.
  + This profile intentionally excludes stairs, gaps, pits and stepping
  + stones. The policy keeps the same 47-D observation, so terrain should
  + stay smooth enough to infer from IMU/body response rather than local
  + height samples.
  + """
  + proportions = self.proportions
  + slope_min = float(getattr(self.cfg, "spotmicro_slope_min", 0.02))
  + slope_max = float(getattr(self.cfg, "spotmicro_slope_max", 0.14))
  + rough_max = float(getattr(self.cfg, "spotmicro_rough_height_max", 0.008))
  + rolling_amp_max = float(getattr(self.cfg, "spotmicro_rolling_amp_max", 0.025))
  + wavelength_min = float(getattr(self.cfg, "spotmicro_rolling_wavelength_min", 0.45))
  + wavelength_max = float(getattr(self.cfg, "spotmicro_rolling_wavelength_max", 1.20))
  + platform_size = float(getattr(self.cfg, "spotmicro_terrain_platform_size", 1.2))
  + difficulty = np.clip(float(difficulty), 0.0, 1.0)
  + slope_abs = slope_min + difficulty * (slope_max - slope_min)
  + if choice < proportions[0]:
  + slope = terrain_band_sign(choice, 0.0, proportions[0]) * slope_abs
  + directional_sloped_terrain(
  + terrain,
  + slope=slope,
  + platform_size=platform_size,
  + )
  + elif choice < proportions[1]:
  + slope = terrain_band_sign(choice, proportions[0], proportions[1]) * slope_abs
  + directional_sloped_terrain(
  + terrain,
  + slope=slope,
  + platform_size=platform_size,
  + )
  + rough_height = rough_max * (0.25 + 0.75 * difficulty)
  + terrain_utils.random_uniform_terrain(
  + terrain,
  + min_height=-rough_height,
  + max_height=rough_height,
  + step=self.cfg.vertical_scale,
  + downsampled_scale=0.20,
  + )
  + else:
  + slope = terrain_band_sign(choice, proportions[1], 1.0) * slope_abs
  + amplitude = rolling_amp_max * (0.25 + 0.75 * difficulty)
  + wavelength = wavelength_max - difficulty * (wavelength_max - wavelength_min)
  + rolling_sloped_terrain(
  + terrain,
  + slope=0.5 * slope,
  + amplitude=amplitude,
  + wavelength=wavelength,
  + platform_size=platform_size,
  + )
  + return terrain
  + def terrain_band_sign(choice, lo, hi):
  + mid = float(lo) + 0.5 * (float(hi) - float(lo))
  + return -1.0 if float(choice) < mid else 1.0
  + def directional_sloped_terrain(terrain, slope, platform_size=1.0):
  + """Creates a single-axis up/down ramp with a flat spawn platform."""
  + x = (
  + np.arange(terrain.length, dtype=np.float32)
  + - 0.5 * float(terrain.length - 1)
  + ) * terrain.horizontal_scale
  + platform_half = 0.5 * float(platform_size)
  + x_ramp = np.sign(x) * np.maximum(np.abs(x) - platform_half, 0.0)
  + heights = slope * x_ramp
  + raw = np.rint(heights / terrain.vertical_scale).astype(np.int16)
  + terrain.height_field_raw += raw[:, None]
  + def rolling_sloped_terrain(terrain, slope, amplitude, wavelength, platform_size=1.0):
  + """Creates repeated smooth inclines/declines without discrete steps."""
  + x = (
  + np.arange(terrain.length, dtype=np.float32)
  + - 0.5 * float(terrain.length - 1)
  + ) * terrain.horizontal_scale
  + y = (
  + np.arange(terrain.width, dtype=np.float32)
  + - 0.5 * float(terrain.width - 1)
  + ) * terrain.horizontal_scale
  + platform_half = 0.5 * float(platform_size)
  + outside = np.maximum(np.abs(x) - platform_half, 0.0)
  + x_ramp = np.sign(x) * outside
  + envelope = np.clip(outside / max(float(wavelength), 1.0e-6), 0.0, 1.0)
  + wave_x = np.sin(2.0 * np.pi * x / max(float(wavelength), 1.0e-6))
  + wave_y = np.sin(2.0 * np.pi * y / max(float(wavelength) * 1.7, 1.0e-6))
  + heights = (
  + slope * x_ramp[:, None]
  + + envelope[:, None] * float(amplitude) * wave_x[:, None]
  + + envelope[:, None] * float(amplitude) * 0.35 * wave_y[None, :]
  + )
  + raw = np.rint(heights / terrain.vertical_scale).astype(np.int16)
  + terrain.height_field_raw += raw

---

## 현재 Reward Scales

| Reward | Scale |
|--------|-------|
| action_rate | -0.05 |
| ang_vel_xy | -1.0 |
| ang_vel_xy_recovery | 0.5 |
| base_height | -2.0 |
| collision | -1.0 |
| dof_acc | -2.5e-07 |
| dof_vel | -0.0005 |
| feet_air_time | 0.04 |
| feet_clearance | 0.03 |
| lin_vel_z | -2.0 |
| no_stuck_feet | -0.2 |
| orientation | -10.0 |
| stand_still | -0.4 |
| swing_contact | -0.45 |
| termination | -20.0 |
| tilt_recovery | 4.0 |
| torques | -0.001 |
| tracking_ang_vel | 0.6 |
| tracking_ik | 0.6 |
| tracking_lin_vel | 1.0 |
| trot_contact | 0.35 |

---

## 진단 결과 (Diagnostic)


### 지형 설정

| 항목 | 값 |
|------|-----|
| 평가 모드 | terrain_random |
| walk_eval | False |
| mesh_type | trimesh |
| terrain_profile | spotmicro_slope |
| measure_heights | False |
| grid | 5 x 5 |
| env 크기 | 6.0 x 6.0 m |
| terrain_proportions | [0.4, 0.25, 0.35] |
| slope max | 0.14 |
| rolling amp max | 0.025 m |


### 핵심 지표

- ❌ Timeout: 58.6% (<60%, 미달)
- ✅ 속도오차 X: 0.0285 m/s (<0.08)
- ⚠️ 토크포화: 14.2% (10~40%)
- ✅ 자세: roll 2.1°, pitch 2.7° (<10°)
- ✅ 조기종료: 1.2% (<5%)
- ❌ 전환복구: 22.8% (<50%)
- ❌ 18도+ pre-fall 복구: 23.2% (<60%)
- ℹ️ Recovery/pre-fall 지표는 참고값입니다 (terrain run 자동 PASS/FAIL 기준에서는 제외)

### 상세 수치

| 지표 | 값 |
|------|-----|
| Timeout% | 58.648648648648646 |
| 조기종료% | 1.2162162162162162 |
| 속도오차 X | 0.02846538834273815 m/s |
| 속도오차 Y | 0.0171639584004879 m/s |
| 각속도오차 | 0.09048660844564438 rad/s |
| 토크포화% | 14.179971504190256 |
| 평균 높이 | 0.16910489916285396 m |
| Roll (평균) | 2.134605884552002° |
| Pitch (평균) | 2.7018816471099854° |
| Action Rate | 0.017601165920495987 |
| 평균 전력 | 4.519503116607666 W |
| CoT | 2.851966199825448 |
| Recovery 성공률 | 63.03317535545023% |
| Recovery eligible trials | 844 |
| 평균 회복 시간 | 0.45184209516369983 s |
| Recovery 조기 실패율 | 1.066350710900474% |
| Recovery 18도+ 성공률 | 62.808641975308646% |
| Recovery 18도+ trials | 648 |
| Recovery 18도+ horizon 후 tilt | 2.255327905194811° |
| Transition recovery 성공률 | 22.804878048780488% |
| Transition recovery eligible trials | 820 |
| 평균 Transition recovery 시간 | 0.29197860309943796 s |
| Transition 18도+ 성공률 | 23.159509202453986% |
| Transition 18도+ trials | 652 |
| Transition 18도+ horizon 후 tilt | 7.501647655320771° |

## Recovery Assist 분석

| 지표 | 값 |
|------|-----|
| 판정 | ⚠️ 일부 회복 가능 |
| Recovery 성공률 | 63.0% |
| 성공/실패 | 532 / 312 |
| Eligible trials | 844 / 990 |
| 평균 회복 시간 | 0.452s |
| 조기 실패율 | 1.1% |
| 평가 horizon | 1.0 s |
| 초기 tilt 기준 | 12.000000000000002° |
| 안정 기준 | roll/pitch < 5.0°, height > 0.165m |
| 평균 초기 tilt | 22.377504424458163° |
| 평균 초기 roll/pitch | 16.759099264594965° / 16.998999955616984° |
| 1초 후 평균 roll/pitch | 1.3449261310896274° / 1.6082436008081205° |
| 1초 내 최대 roll/pitch 평균 | 18.081756576825093° / 19.368489897364125° |

> 해석: `Recovery 성공률`은 초기 tilt가 기준 이상인 episode 중 1초 내 안정 자세로 복귀한 비율입니다. 조기 실패율이 높으면 reset 직후 바로 넘어지는 것이고, 평균 회복 시간이 짧을수록 위기 대응이 빠른 것입니다.

### Recovery Tilt Band 분석

| Tilt 구간 | Trials | 성공률 | Horizon 후 평균 tilt | 평균 회복 시간 |
|-----------|--------|--------|----------------------|----------------|
| 12-18 deg | 196 | 63.8% | 1.40° | 0.34s |
| 18-25 deg | 331 | 62.8% | 1.66° | 0.45s |
| 25-30 deg | 317 | 62.8% | 2.88° | 0.52s |
| 30+ deg | 0 | N/A | N/A | N/A |
| 18+ deg 전체 | 648 | 62.8% | 2.26° | 0.49s |

> 이번 pre-fall 목표는 특히 `18-25 deg`, `25-30 deg`, `18+ deg 전체` 행을 우선 봅니다. `25-30 deg` trial이 없으면 30도 근처 회복 성능은 아직 검증되지 않은 것입니다.



## Transition Recovery 분석

| 지표 | 값 |
|------|-----|
| 판정 | ❌ 전환 복구 부족 |
| Transition recovery 성공률 | 22.8% |
| 성공/실패 | 187 / 633 |
| Eligible trials | 820 / 3584 |
| 평균 회복 시간 | 0.292s |
| 조기 실패율 | 7.2% |
| 평가 horizon | 0.75 s |
| push 후 eligible 기준 | max roll/pitch > 12.000000000000002° |
| 안정 기준 | roll/pitch < 5.0°, height > 0.165m |
| 평균 최대 tilt | 21.873150268486615° |
| horizon 후 평균 roll/pitch | 4.195650885328821° / 5.5701326555713° |

> 해석: `Transition Recovery`는 학습 중 push/roll-pitch angular impulse가 들어간 뒤 일정 시간 안에 자세가 안정 기준으로 돌아오는지를 봅니다.

### Transition Recovery Tilt Band 분석

| Tilt 구간 | Trials | 성공률 | Horizon 후 평균 tilt | 평균 회복 시간 |
|-----------|--------|--------|----------------------|----------------|
| 12-18 deg | 168 | 21.4% | 5.29° | 0.28s |
| 18-25 deg | 488 | 22.1% | 6.22° | 0.26s |
| 25-30 deg | 125 | 30.4% | 6.57° | 0.39s |
| 30+ deg | 39 | 12.8% | 26.46° | 0.46s |
| 18+ deg 전체 | 652 | 23.2% | 7.50° | 0.30s |

> 이번 pre-fall 목표는 특히 `18-25 deg`, `25-30 deg`, `18+ deg 전체` 행을 우선 봅니다. `25-30 deg` trial이 없으면 30도 근처 회복 성능은 아직 검증되지 않은 것입니다.



## Command Mode별 회전 추종 분석

| 모드 | 비율 | 샘플 수 | 평균 abs(cmd_wz) | 평균 abs(actual_wz) | yaw 오차 | 상태 |
|------|------|---------|------------------|---------------------|----------|------|
| 정지 | 9.3% | 71334 | 0.0260 | 0.0818 | 0.0800 | ❌ |
| 직진/저회전 | 51.2% | 393891 | 0.0474 | 0.1076 | 0.0957 | ⚠️ |
| 제자리 회전 | 9.7% | 74684 | 0.1747 | 0.1568 | 0.0893 | ⚠️ |
| 전진+회전 | 11.3% | 86811 | 0.1743 | 0.1591 | 0.0918 | ⚠️ |
| 큰 회전명령 | 0.0% | 0 | N/A | N/A | N/A | N/A |

> 해석 기준: `제자리 회전`만 나쁘면 pure turn 학습/보행 패턴 문제, `큰 회전명령`만 나쁘면 yaw 명령 범위가 현재 토크/보폭 한계보다 큰 문제, `정지`가 나쁘면 stop drift 문제로 보면 됩니다.
        


## 학습 곡선 분석

| 지표 | 값 | 판정 |
|------|-----|------|
| 수렴 iter (90%) | 8138 | 느림 |
| 후반 안정성 (CV) | 0.029 | 안정 |
| 정체 구간 | 없음 | |


## Per-leg 분석

### 발별 접촉/공중 비율

| 발 | 접촉% | 공중% |
|-----|-------|------|
| foot_0 | 70.1% | 29.9% |
| foot_1 | 73.3% | 26.7% |
| foot_2 | 77.6% | 22.4% |
| foot_3 | 59.9% | 40.1% |

### 관절별 토크 포화

| 관절 | 포화% | 최대토크 | 한계 | 상태 |
|------|------|---------|------|------|
| front_left_shoulder | 0.3% | 2.940 | 2.940 | ✅ |
| front_left_leg | 0.9% | 2.940 | 2.940 | ✅ |
| front_left_foot | 22.9% | 2.940 | 2.940 | ❌ |
| front_right_shoulder | 0.3% | 2.940 | 2.940 | ✅ |
| front_right_leg | 1.6% | 2.940 | 2.940 | ✅ |
| front_right_foot | 23.4% | 2.940 | 2.940 | ❌ |
| rear_left_shoulder | 5.1% | 2.940 | 2.940 | ⚠️ |
| rear_left_leg | 49.4% | 2.940 | 2.940 | ❌ |
| rear_left_foot | 47.7% | 2.940 | 2.940 | ❌ |
| rear_right_shoulder | 0.4% | 2.940 | 2.940 | ✅ |
| rear_right_leg | 2.3% | 2.940 | 2.940 | ✅ |
| rear_right_foot | 15.8% | 2.940 | 2.940 | ⚠️ |


## Gait 분석

| 지표 | 값 |
|------|-----|
| Gait 주파수 | 0.22 Hz |
| Gait 주기 | 57 steps |
| 대각 동기화율 | 85.6% |
| L/R 비대칭 | 0.0%p |
| 판정 | ✅ Trot 패턴 / ✅ 대칭 |


## 관절별 상세

| 관절 | 토크포화% | 사용범위% | 편향(rad) | 한계근접% | 상태 |
|------|----------|----------|----------|----------|------|
| front_left_shoulder | 0.3% | 103% | -0.009 | 0.1% | ✅ |
| front_left_leg | 0.9% | 31% | -1.314 | 0.0% | ⚠️ |
| front_left_foot | 22.9% | 51% | +1.908 | 0.0% | ⚠️ |
| front_right_shoulder | 0.3% | 101% | -0.004 | 0.0% | ✅ |
| front_right_leg | 1.6% | 30% | -1.175 | 0.0% | ⚠️ |
| front_right_foot | 23.4% | 51% | +1.894 | 0.0% | ⚠️ |
| rear_left_shoulder | 5.1% | 102% | -0.006 | 0.2% | ✅ |
| rear_left_leg | 49.4% | 34% | -0.917 | 0.0% | ⚠️ |
| rear_left_foot | 47.7% | 52% | +1.882 | 0.0% | ⚠️ |
| rear_right_shoulder | 0.4% | 104% | -0.018 | 0.1% | ✅ |
| rear_right_leg | 2.3% | 46% | -1.109 | 0.0% | ⚠️ |
| rear_right_foot | 15.8% | 49% | +1.706 | 0.0% | ⚠️ |


## 에너지 효율 상세

| 지표 | 값 |
|------|-----|
| 평균 전력 | 4.52 W |
| 피크 전력 | 40.57 W |
| 피크/평균 비율 | 9.0x |
| CoT | 2.85 |

### 관절별 전력 분배

| 관절 | 평균전력(W) | 비율% |
|------|-----------|-------|
| rear_left_foot | 0.853 | 18.9% |
| front_right_foot | 0.680 | 15.0% |
| front_left_foot | 0.648 | 14.3% |
| rear_left_leg | 0.489 | 10.8% |
| rear_right_foot | 0.456 | 10.1% |
| front_right_leg | 0.332 | 7.4% |
| front_left_leg | 0.292 | 6.5% |
| rear_right_leg | 0.287 | 6.3% |
| rear_left_shoulder | 0.219 | 4.8% |
| rear_right_shoulder | 0.127 | 2.8% |
| front_right_shoulder | 0.075 | 1.7% |
| front_left_shoulder | 0.062 | 1.4% |


## 이전 실험 대비 비교

| 지표 | 이전 (exp085) | 현재 (exp086) | 변화 |
|------|-------|-------|------|
| Timeout% | 61.6% | 58.6% | ⚠️ ↓ 2.9715% |
| 속도오차 X | 0.0280 | 0.0285 | ⚠️ ↑ 0.0005m/s |
| 토크포화 | 5.4% | 14.2% | ⚠️ ↑ 8.7807% |
| Roll | 2.1° | 2.1° | ⚠️ ↑ 0.0690° |
| Pitch | 2.2° | 2.7° | ⚠️ ↑ 0.4884° |
| 평균 전력 | 3.9351W | 4.5195W | ⚠️ ↑ 0.5844W |


## Tensorboard 학습 곡선 요약

| 스칼라 | 최종값 | 최대 | 최소 | 후반10% 평균 |
|--------|--------|------|------|-------------|
| rew_action_rate | -0.0475 | -0.0005 | -0.0516 | -0.0483 |
| rew_ang_vel_xy | -0.1105 | -0.0266 | -0.1239 | -0.1116 |
| rew_ang_vel_xy_recovery | 0.0006 | 0.0014 | 0.0004 | 0.0006 |
| rew_base_height | -0.0005 | -0.0000 | -0.0007 | -0.0004 |
| rew_collision | -0.0006 | 0.0000 | -0.0074 | -0.0003 |
| rew_dof_acc | -0.0072 | -0.0003 | -0.0077 | -0.0071 |
| rew_dof_vel | -0.0036 | -0.0002 | -0.0039 | -0.0036 |
| rew_feet_air_time | -0.0001 | 0.0000 | -0.0002 | -0.0001 |
| rew_feet_clearance | 0.0000 | 0.0001 | 0.0000 | 0.0000 |
| rew_lin_vel_z | -0.0066 | -0.0013 | -0.0078 | -0.0070 |
| rew_no_stuck_feet | -0.0035 | -0.0000 | -0.0047 | -0.0039 |
| rew_orientation | -0.0418 | -0.0244 | -0.0659 | -0.0414 |
| rew_stand_still | -0.0210 | -0.0076 | -0.0555 | -0.0240 |
| rew_swing_contact | -0.0615 | -0.0002 | -0.0743 | -0.0656 |
| rew_termination | -0.0049 | -0.0013 | -0.0132 | -0.0047 |
| rew_tilt_recovery | 0.0007 | 0.0009 | 0.0003 | 0.0006 |
| rew_torques | -0.0252 | -0.0003 | -0.0259 | -0.0247 |
| rew_tracking_ang_vel | 0.3169 | 0.4309 | 0.0020 | 0.3205 |
| rew_tracking_ik | 0.2645 | 0.4075 | 0.0020 | 0.2793 |
| rew_tracking_lin_vel | 0.8348 | 0.9004 | 0.0039 | 0.8437 |
| rew_trot_contact | 0.2504 | 0.2716 | 0.0005 | 0.2485 |
| terrain_level | 4.6804 | 4.8173 | 1.1099 | 4.7033 |
| learning_rate | 0.0002 | 0.0005 | 0.0001 | 0.0004 |
| surrogate | -0.0030 | 0.0009 | -0.0048 | -0.0029 |
| value_function | 0.0231 | 0.1770 | 0.0069 | 0.0201 |
| collection time | 1.2884 | 4.6425 | 1.2173 | 1.2995 |
| learning_time | 0.2849 | 0.5918 | 0.2768 | 0.2866 |
| total_fps | 62485.0000 | 65192.0000 | 18845.0000 | 61990.3875 |
| mean_noise_std | 0.1814 | 0.1858 | 0.0765 | 0.1807 |
| mean_episode_length | 896.6400 | 982.6500 | 21.9200 | 912.1443 |
| time | 896.6400 | 982.6500 | 21.9200 | 912.1443 |
| mean_reward | 27.9067 | 35.3858 | -0.2999 | 28.3770 |
| time | 27.9067 | 35.3858 | -0.2999 | 28.3770 |

총 학습 iteration: 8899


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

**자동 판정:** ❌ FAIL (일부 기준 미달)

일부 기준을 통과하지 못했습니다. 조정이 필요합니다.
  - ❌ Timeout: 58.6% (<60%, 미달)
  - ❌ 전환복구: 22.8% (<50%)
  - ❌ 18도+ pre-fall 복구: 23.2% (<60%)

**제안:** Timeout이 낮습니다. 최근 추가한 페널티를 절반으로 줄여보세요.

> 💡 위 결론은 자동 생성되었습니다. 필요시 수정하세요.

---

## 메모

_(추가 메모가 있으면 여기에 작성)_

