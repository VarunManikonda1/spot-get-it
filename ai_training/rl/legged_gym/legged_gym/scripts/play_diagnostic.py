# play_diagnostic.py
# SpotMicro RL 학습 결과 종합 진단 스크립트 v3
# 사용법: python legged_gym/scripts/play_diagnostic.py --task=spotmicro_test
#         python legged_gym/scripts/play_diagnostic.py --task=spotmicro_test --checkpoint /path/to/model_500.pt --lightweight
#         python legged_gym/scripts/play_diagnostic.py --task=spotmicro_test --with_dr --prefall-eval
#         python legged_gym/scripts/play_diagnostic.py --task=spotmicro_test --flat-eval
#         python legged_gym/scripts/play_diagnostic.py --task=spotmicro_test --terrain-curriculum-eval
#         python legged_gym/scripts/play_diagnostic.py --task=spotmicro_test --walk-eval
#
# 출력:
#   1. 터미널에 종합 진단 리포트
#   2. diagnostics/{experiment_name}/ 에 상세 그래프 저장
#
# v2 추가 항목:
#   - [6] 회전 추종 분석 (Step 2용)
#   - [7] 자세 각도 분석 - roll/pitch (Step 3용)
#   - [8] 동작 부드러움 분석 (Step 4용)
#   - [9] 보행 패턴 분석 - 발 접촉/공중 시간 (Step 5용)
#   - [10] 에너지 효율 분석 (Step 4~6용)
#   - 자세 안정성 판정 버그 수정 (높이 목표 대비 편차 반영)
#
# v3 추가 항목:
#   - --checkpoint / --lightweight 옵션 (중간 iter 경량 스냅샷)
#   - Gait FFT 주파수 / L-R 비대칭 / 대각 동기화율
#   - 관절별 사용 범위/편향 JSON 저장
#   - 관절별 전력 분배 / 에너지 상세 JSON 저장
 
from legged_gym import LEGGED_GYM_ROOT_DIR
import os
import re
import isaacgym
from legged_gym.envs import *
from legged_gym.utils import get_args, task_registry
import numpy as np
import torch
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from collections import defaultdict
import argparse, sys 
 
def run_diagnostic(
    args,
    checkpoint_path=None,
    lightweight=False,
    with_dr=False,
    recovery_range_deg=None,
    flat_eval=False,
    terrain_curriculum_eval=False,
    walk_eval=False,
    ik_only=False,
):
    # ============ 환경 설정 ============
    env_cfg, train_cfg = task_registry.get_cfgs(name=args.task)
    env_cfg.env.num_envs = min(env_cfg.env.num_envs, 256)
    env_cfg.terrain.num_rows = 5
    env_cfg.terrain.num_cols = 5
    env_cfg.terrain.curriculum = bool(terrain_curriculum_eval)
    if flat_eval:
        env_cfg.terrain.mesh_type = 'plane'
        env_cfg.terrain.measure_heights = False
        print("[진단] flat_eval: plane 지형으로 진단합니다")
    else:
        terrain_profile = getattr(env_cfg.terrain, "terrain_profile", "default")
        print(
            "[진단] terrain_eval: "
            f"mesh={env_cfg.terrain.mesh_type}, profile={terrain_profile}, "
            f"measure_heights={env_cfg.terrain.measure_heights}, "
            f"curriculum={env_cfg.terrain.curriculum}")
    if not with_dr:
        env_cfg.noise.add_noise = False
        env_cfg.domain_rand.randomize_friction = False
        env_cfg.domain_rand.push_robots = False
    else:
        print("[진단] DR 활성화 상태로 진단합니다")
    if recovery_range_deg is not None:
        env_cfg.domain_rand.recovery_roll_pitch_range_deg = float(recovery_range_deg)
        print(
            "[진단] recovery_roll_pitch_range_deg override: "
            f"{env_cfg.domain_rand.recovery_roll_pitch_range_deg:.1f} deg")
    if walk_eval:
        env_cfg.domain_rand.recovery_roll_pitch_range_deg = 0.0
        env_cfg.domain_rand.recovery_yaw_range_deg = 0.0
        env_cfg.domain_rand.recovery_lin_vel_xy_range = 0.0
        env_cfg.domain_rand.recovery_lin_vel_z_range = 0.0
        env_cfg.domain_rand.recovery_ang_vel_xy_range = 0.0
        env_cfg.domain_rand.recovery_ang_vel_z_range = 0.0
        env_cfg.domain_rand.transition_tilt_push = False
        env_cfg.domain_rand.push_robots = False
        print("[진단] walk_eval: recovery reset/push 없이 보행만 진단합니다")
 
    env, _ = task_registry.make_env(name=args.task, args=args, env_cfg=env_cfg)
    obs = env.get_observations()
    
    if ik_only:
        print("[진단] IK-only: policy 미사용, action=0으로 평가합니다.")

        def policy(obs_tensor):
            return torch.zeros(
                (env.num_envs, env.num_actions),
                device=env.device,
                dtype=torch.float,
        )

    else:
 
        # --- Checkpoint 로드 (항목 1-A) ---
        train_cfg.runner.resume = True
        explicit_load_run = getattr(args, "load_run", None) is not None
        explicit_checkpoint = getattr(args, "checkpoint", None) is not None
        if not checkpoint_path and not explicit_load_run and not explicit_checkpoint:
            train_cfg.runner.load_run = -1
            train_cfg.runner.checkpoint = -1
        if checkpoint_path:
            # checkpoint 경로에서 run 디렉토리와 모델 번호 추출
            ck_dir = os.path.dirname(checkpoint_path)
            ck_basename = os.path.basename(checkpoint_path)  # e.g. model_500.pt
            train_cfg.runner.load_run = ck_dir
            # 모델 번호 추출 (model_500.pt → 500)
            m = re.search(r'model_(\d+)\.pt', ck_basename)
            if m:
                train_cfg.runner.checkpoint = int(m.group(1))

        ppo_runner, train_cfg = task_registry.make_alg_runner(
            env=env, name=args.task, args=args, train_cfg=train_cfg
        )
        policy = ppo_runner.get_inference_policy(device=env.device)
     
    # ============ 출력 폴더 (logs 바깥) ============
    diag_dir = os.path.join(
        LEGGED_GYM_ROOT_DIR, 'diagnostics',
        train_cfg.runner.experiment_name
    )
    os.makedirs(diag_dir, exist_ok=True)
 
    # ============ 관절 이름 & 한계 정보 ============
    joint_names = env.dof_names
    num_joints = env.num_dof
    num_envs = env.num_envs
    num_feet = env.feet_indices.shape[0]
 
    torque_limits = env.torque_limits.cpu().numpy()
    dof_pos_lower = env.dof_pos_limits[:, 0].cpu().numpy()
    dof_pos_upper = env.dof_pos_limits[:, 1].cpu().numpy()
 
    # URDF 원본 한계 역산
    soft_ratio = env.cfg.rewards.soft_dof_pos_limit
    urdf_lower = np.zeros_like(dof_pos_lower)
    urdf_upper = np.zeros_like(dof_pos_upper)
    for i in range(num_joints):
        mid = (dof_pos_lower[i] + dof_pos_upper[i]) / 2
        r_soft = dof_pos_upper[i] - dof_pos_lower[i]
        r_urdf = r_soft / soft_ratio if soft_ratio > 0 else r_soft
        urdf_lower[i] = mid - r_urdf / 2
        urdf_upper[i] = mid + r_urdf / 2
 
    # foot 이름 추출
    try:
        foot_names = [env.gym.get_actor_rigid_body_name(env.envs[0], env.actor_handles[0], idx)
                      for idx in env.feet_indices.cpu().numpy()]
    except:
        foot_names = [f'foot_{i}' for i in range(num_feet)]
 
    # --- 경량/풀 모드 헤더 ---
    iter_number = 0
    if checkpoint_path:
        m = re.search(r'model_(\d+)\.pt', os.path.basename(checkpoint_path))
        if m:
            iter_number = int(m.group(1))

    if not lightweight:
        print(f"\n{'='*60}")
        print(f"  SpotMicro RL 진단 v3")
        print(f"  Envs: {num_envs}, Joints: {num_joints}, Feet: {num_feet}")
        print(f"  Foot names: {foot_names}")
        if checkpoint_path:
            print(f"  Checkpoint: {os.path.basename(checkpoint_path)}")
        print(f"{'='*60}\n")
    else:
        print(f"  [경량] iter={iter_number}, checkpoint={os.path.basename(checkpoint_path) if checkpoint_path else 'latest'}")
 
    # ============ 데이터 수집 ============
    # 경량 모드: 1 에피소드 (max_episode_length steps)
    # 풀 모드: 3 에피소드 분량
    if lightweight:
        NUM_STEPS = int(env.max_episode_length)
    else:
        NUM_STEPS = int(env.max_episode_length) * 3
 
    data = defaultdict(list)
 
    # 관절별 누적 통계
    torque_saturation_count = np.zeros(num_joints)
    torque_max_seen = np.zeros(num_joints)
    dof_pos_min_seen = np.full(num_joints, 1e10)
    dof_pos_max_seen = np.full(num_joints, -1e10)
    dof_near_limit_count = np.zeros(num_joints)
 
    total_steps = 0
    episode_lengths = []
    episode_returns = []
    current_ep_return = torch.zeros(num_envs, device=env.device)
    current_ep_length = torch.zeros(num_envs, device=env.device)
 
    vel_errors_x = []
    vel_errors_y = []
    ang_vel_errors = []
    
    command_mode_stats = {
        'stand': {
            'label': '정지',
            'err_sum': 0.0,
            'cmd_sum': 0.0,
            'actual_sum': 0.0,
            'count': 0,
        },
        'forward': {
            'label': '직진/저회전',
            'err_sum': 0.0,
            'cmd_sum': 0.0,
            'actual_sum': 0.0,
            'count': 0,
        },
        'pure_turn': {
            'label': '제자리 회전',
            'err_sum': 0.0,
            'cmd_sum': 0.0,
            'actual_sum': 0.0,
            'count': 0,
        },
        'arc_turn': {
            'label': '전진+회전',
            'err_sum': 0.0,
            'cmd_sum': 0.0,
            'actual_sum': 0.0,
            'count': 0,
        },
        'high_wz': {
            'label': '큰 회전명령',
            'err_sum': 0.0,
            'cmd_sum': 0.0,
            'actual_sum': 0.0,
            'count': 0,
        },
    }
    
    def _accumulate_command_mode(name, mask, yaw_err, cmd_yaw, actual_yaw):
        """mode별 yaw error 누적"""
        count = int(np.sum(mask))
        if count == 0:
            return

        command_mode_stats[name]['err_sum'] += float(np.sum(yaw_err[mask]))
        command_mode_stats[name]['cmd_sum'] += float(np.sum(np.abs(cmd_yaw[mask])))
        command_mode_stats[name]['actual_sum'] += float(np.sum(np.abs(actual_yaw[mask])))
        command_mode_stats[name]['count'] += count
     
    # 동작 부드러움 추적
    action_rates = []
    prev_actions = None
 
    # 보행 패턴 추적 (발별 공중/접촉 시간)
    feet_contact_count = np.zeros(num_feet)
    feet_air_count = np.zeros(num_feet)
 
    # roll/pitch 추적
    roll_list = []
    pitch_list = []
 

    # ============================================================
    # Recovery assist 분석용 상태 추적
    # - reset 직후 일부러 기울어진 초기 상태가 1초 이내 안정화되는지 측정
    # - eligible trial: 초기 |roll| 또는 |pitch|가 설정 기준 이상인 trial
    # - success: 1초 이내 안정 기준에 도달했고, horizon 시점에도 안정 상태 유지
    # ============================================================
    recovery_horizon_s = 1.0
    recovery_horizon_steps = max(1, int(recovery_horizon_s / env.dt))
    recovery_initial_tilt_threshold = np.radians(
        float(getattr(
            env.cfg.rewards,
            "recovery_diagnostic_initial_tilt_threshold_deg",
            getattr(env.cfg.rewards, "recovery_reward_tilt_threshold_deg", 12.0),
        ))
    )
    recovery_stable_threshold = np.radians(5.0)
    recovery_min_height = float(
        getattr(
            env.cfg.rewards,
            "recovery_min_height",
            float(env.cfg.rewards.base_height_target) - 0.01,
        )
    )

    recovery_age = np.zeros(num_envs, dtype=np.int32)
    recovery_active = np.ones(num_envs, dtype=bool)
    recovery_init_roll = np.full(num_envs, np.nan)
    recovery_init_pitch = np.full(num_envs, np.nan)
    recovery_max_roll = np.zeros(num_envs, dtype=np.float32)
    recovery_max_pitch = np.zeros(num_envs, dtype=np.float32)
    recovery_first_stable_step = np.full(num_envs, -1, dtype=np.int32)
    recovery_trials = []

    transition_horizon_s = float(getattr(env.cfg.rewards, "transition_recovery_horizon_s", 0.75))
    transition_horizon_steps = max(1, int(transition_horizon_s / env.dt))
    transition_initial_tilt_threshold = np.radians(
        float(getattr(env.cfg.rewards, "transition_recovery_initial_tilt_threshold_deg", 4.0))
    )
    transition_stable_threshold = recovery_stable_threshold
    transition_min_height = recovery_min_height
    transition_age = np.zeros(num_envs, dtype=np.int32)
    transition_active = np.zeros(num_envs, dtype=bool)
    transition_init_roll = np.full(num_envs, np.nan)
    transition_init_pitch = np.full(num_envs, np.nan)
    transition_max_roll = np.zeros(num_envs, dtype=np.float32)
    transition_max_pitch = np.zeros(num_envs, dtype=np.float32)
    transition_first_stable_step = np.full(num_envs, -1, dtype=np.int32)
    transition_trials = []
    last_seen_transition_push_step = -1
    tilt_band_defs = [
        (12.0, 18.0, "12-18 deg"),
        (18.0, 25.0, "18-25 deg"),
        (25.0, 30.0, "25-30 deg"),
        (30.0, None, "30+ deg"),
    ]

    def _mean_or_none(values):
        return float(np.mean(values)) if values else None

    def _summarize_tilt_rows(rows, tilt_key):
        successes = [t for t in rows if t.get('success')]
        failures = [t for t in rows if not t.get('success')]
        times = [
            t['recovery_time_s'] for t in successes
            if t.get('recovery_time_s') is not None
        ]
        end_tilts = [
            max(t.get('end_roll_deg', 0.0), t.get('end_pitch_deg', 0.0))
            for t in rows
        ]
        return {
            'trials': int(len(rows)),
            'success_count': int(len(successes)),
            'failure_count': int(len(failures)),
            'success_rate_pct': (
                float(len(successes) / len(rows) * 100) if rows else None
            ),
            'early_failure_rate_pct': (
                float(sum(1 for t in rows if t.get('forced_failure')) / len(rows) * 100)
                if rows else None
            ),
            'mean_recovery_time_s': _mean_or_none(times),
            'mean_tilt_deg': _mean_or_none([t[tilt_key] for t in rows]),
            'mean_end_tilt_deg': _mean_or_none(end_tilts),
            'mean_end_roll_deg': _mean_or_none([t.get('end_roll_deg', 0.0) for t in rows]),
            'mean_end_pitch_deg': _mean_or_none([t.get('end_pitch_deg', 0.0) for t in rows]),
            'mean_end_height_m': _mean_or_none([t.get('end_height_m', 0.0) for t in rows]),
        }

    def _summarize_tilt_bands(trials, tilt_key):
        bands = []
        for low, high, label in tilt_band_defs:
            rows = [
                t for t in trials
                if t.get(tilt_key) is not None
                and t[tilt_key] >= low
                and (high is None or t[tilt_key] < high)
            ]
            band = _summarize_tilt_rows(rows, tilt_key)
            band.update({
                'label': label,
                'min_tilt_deg': float(low),
                'max_tilt_deg': float(high) if high is not None else None,
            })
            bands.append(band)
        return bands

    def _summarize_prefall(trials, tilt_key):
        rows = [
            t for t in trials
            if t.get(tilt_key) is not None and t[tilt_key] >= 18.0
        ]
        summary = _summarize_tilt_rows(rows, tilt_key)
        summary['min_tilt_deg'] = 18.0
        return summary

    def _start_recovery_trials(env_ids_np, roll_abs_np, pitch_abs_np, height_np):
        """새 episode/reset 직후 recovery trial 초기화"""
        if len(env_ids_np) == 0:
            return
        recovery_age[env_ids_np] = 0
        recovery_active[env_ids_np] = True
        recovery_init_roll[env_ids_np] = roll_abs_np[env_ids_np]
        recovery_init_pitch[env_ids_np] = pitch_abs_np[env_ids_np]
        recovery_max_roll[env_ids_np] = roll_abs_np[env_ids_np]
        recovery_max_pitch[env_ids_np] = pitch_abs_np[env_ids_np]
        recovery_first_stable_step[env_ids_np] = -1

    def _record_recovery_trial(env_i, success, end_roll, end_pitch, end_height, forced_failure=False):
        """한 recovery trial 결과 기록"""
        init_roll = recovery_init_roll[env_i]
        init_pitch = recovery_init_pitch[env_i]
        if np.isnan(init_roll) or np.isnan(init_pitch):
            return

        init_tilt = max(float(init_roll), float(init_pitch))
        eligible = init_tilt >= recovery_initial_tilt_threshold

        recovery_time_s = None
        if success and recovery_first_stable_step[env_i] >= 0:
            recovery_time_s = float(recovery_first_stable_step[env_i] * env.dt)

        recovery_trials.append({
            'eligible': bool(eligible),
            'success': bool(success) if eligible else False,
            'forced_failure': bool(forced_failure),
            'init_roll_deg': float(np.degrees(init_roll)),
            'init_pitch_deg': float(np.degrees(init_pitch)),
            'init_tilt_deg': float(np.degrees(init_tilt)),
            'max_roll_first_1s_deg': float(np.degrees(recovery_max_roll[env_i])),
            'max_pitch_first_1s_deg': float(np.degrees(recovery_max_pitch[env_i])),
            'end_roll_deg': float(np.degrees(end_roll)),
            'end_pitch_deg': float(np.degrees(end_pitch)),
            'end_height_m': float(end_height),
            'recovery_time_s': recovery_time_s,
        })

    def _get_initial_tilt_for_trials(roll_abs_np, pitch_abs_np):
        """env reset 때 저장한 roll/pitch가 있으면 그것을 initial로 사용"""
        if hasattr(env, "last_reset_roll") and hasattr(env, "last_reset_pitch"):
            init_roll = np.abs(env.last_reset_roll.cpu().numpy())
            init_pitch = np.abs(env.last_reset_pitch.cpu().numpy())
            return init_roll, init_pitch
        return roll_abs_np, pitch_abs_np
            
    def _summarize_recovery_trials():
        eligible = [t for t in recovery_trials if t['eligible']]
        successes = [t for t in eligible if t['success']]
        failures = [t for t in eligible if not t['success']]
        times = [t['recovery_time_s'] for t in successes if t['recovery_time_s'] is not None]
        tilt_bands = _summarize_tilt_bands(recovery_trials, 'init_tilt_deg')
        prefall = _summarize_prefall(recovery_trials, 'init_tilt_deg')

        def _mean(key, rows):
            return float(np.mean([r[key] for r in rows])) if rows else None

        def _median(values):
            return float(np.median(values)) if values else None

        return {
            'horizon_s': float(recovery_horizon_s),
            'initial_tilt_threshold_deg': float(np.degrees(recovery_initial_tilt_threshold)),
            'stable_threshold_deg': float(np.degrees(recovery_stable_threshold)),
            'min_height_m': float(recovery_min_height),
            'total_trials': int(len(recovery_trials)),
            'eligible_trials': int(len(eligible)),
            'success_count': int(len(successes)),
            'failure_count': int(len(failures)),
            'success_rate_pct': float(len(successes) / len(eligible) * 100) if eligible else None,
            'early_failure_rate_pct': float(
                sum(1 for t in eligible if t.get('forced_failure')) / len(eligible) * 100
            ) if eligible else None,
            'mean_recovery_time_s': float(np.mean(times)) if times else None,
            'median_recovery_time_s': _median(times),
            'mean_initial_tilt_deg': _mean('init_tilt_deg', eligible),
            'mean_initial_roll_deg': _mean('init_roll_deg', eligible),
            'mean_initial_pitch_deg': _mean('init_pitch_deg', eligible),
            'mean_max_roll_first_1s_deg': _mean('max_roll_first_1s_deg', eligible),
            'mean_max_pitch_first_1s_deg': _mean('max_pitch_first_1s_deg', eligible),
            'mean_end_roll_deg': _mean('end_roll_deg', eligible),
            'mean_end_pitch_deg': _mean('end_pitch_deg', eligible),
            'mean_end_height_m': _mean('end_height_m', eligible),
            'tilt_bands': tilt_bands,
            'prefall': prefall,
        }

    def _start_transition_trials(env_ids_np, roll_abs_np, pitch_abs_np, height_np, push_step):
        if len(env_ids_np) == 0:
            return
        transition_age[env_ids_np] = 0
        transition_active[env_ids_np] = True
        transition_init_roll[env_ids_np] = roll_abs_np[env_ids_np]
        transition_init_pitch[env_ids_np] = pitch_abs_np[env_ids_np]
        transition_max_roll[env_ids_np] = roll_abs_np[env_ids_np]
        transition_max_pitch[env_ids_np] = pitch_abs_np[env_ids_np]
        transition_first_stable_step[env_ids_np] = -1

    def _record_transition_trial(env_i, success, end_roll, end_pitch, end_height, forced_failure=False):
        init_roll = transition_init_roll[env_i]
        init_pitch = transition_init_pitch[env_i]
        if np.isnan(init_roll) or np.isnan(init_pitch):
            return

        max_tilt = max(float(transition_max_roll[env_i]), float(transition_max_pitch[env_i]))
        eligible = max_tilt >= transition_initial_tilt_threshold

        recovery_time_s = None
        if success and transition_first_stable_step[env_i] >= 0:
            recovery_time_s = float(transition_first_stable_step[env_i] * env.dt)

        transition_trials.append({
            'eligible': bool(eligible),
            'success': bool(success) if eligible else False,
            'forced_failure': bool(forced_failure),
            'init_roll_deg': float(np.degrees(init_roll)),
            'init_pitch_deg': float(np.degrees(init_pitch)),
            'max_roll_deg': float(np.degrees(transition_max_roll[env_i])),
            'max_pitch_deg': float(np.degrees(transition_max_pitch[env_i])),
            'max_tilt_deg': float(np.degrees(max_tilt)),
            'end_roll_deg': float(np.degrees(end_roll)),
            'end_pitch_deg': float(np.degrees(end_pitch)),
            'end_height_m': float(end_height),
            'recovery_time_s': recovery_time_s,
        })

    def _summarize_transition_trials():
        eligible = [t for t in transition_trials if t['eligible']]
        successes = [t for t in eligible if t['success']]
        failures = [t for t in eligible if not t['success']]
        times = [t['recovery_time_s'] for t in successes if t['recovery_time_s'] is not None]
        tilt_bands = _summarize_tilt_bands(transition_trials, 'max_tilt_deg')
        prefall = _summarize_prefall(transition_trials, 'max_tilt_deg')

        def _mean(key, rows):
            return float(np.mean([r[key] for r in rows])) if rows else None

        return {
            'horizon_s': float(transition_horizon_s),
            'initial_tilt_threshold_deg': float(np.degrees(transition_initial_tilt_threshold)),
            'stable_threshold_deg': float(np.degrees(transition_stable_threshold)),
            'min_height_m': float(transition_min_height),
            'total_trials': int(len(transition_trials)),
            'eligible_trials': int(len(eligible)),
            'success_count': int(len(successes)),
            'failure_count': int(len(failures)),
            'success_rate_pct': float(len(successes) / len(eligible) * 100) if eligible else None,
            'early_failure_rate_pct': float(
                sum(1 for t in eligible if t.get('forced_failure')) / len(eligible) * 100
            ) if eligible else None,
            'mean_recovery_time_s': float(np.mean(times)) if times else None,
            'mean_max_tilt_deg': _mean('max_tilt_deg', eligible),
            'mean_end_roll_deg': _mean('end_roll_deg', eligible),
            'mean_end_pitch_deg': _mean('end_pitch_deg', eligible),
            'mean_end_height_m': _mean('end_height_m', eligible),
            'tilt_bands': tilt_bands,
            'prefall': prefall,
        }
    # 에너지 추적
    power_list = []

    # --- 관절별 전력 추적 (항목 6-A) ---
    per_joint_power_sum = np.zeros(num_joints)
    peak_power = 0.0
 
    for step in range(NUM_STEPS):
        actions = policy(obs.detach())
        obs, _, rews, dones, infos = env.step(actions.detach())
        total_steps += 1
 
        # --- 토크 분석 ---
        torques_np = env.torques.cpu().numpy()
        torque_abs = np.abs(torques_np)
 
        for j in range(num_joints):
            col = torque_abs[:, j]
            torque_saturation_count[j] += np.sum(col > torque_limits[j] * 0.9)
            torque_max_seen[j] = max(torque_max_seen[j], col.max())
 
        saturated_ratio = np.mean(torque_abs > torque_limits[None, :] * 0.9)
        data['torque_saturation_ratio'].append(saturated_ratio)
 
        # --- 관절 위치 분석 ---
        dof_pos_np = env.dof_pos.cpu().numpy()
 
        for j in range(num_joints):
            col = dof_pos_np[:, j]
            dof_pos_min_seen[j] = min(dof_pos_min_seen[j], col.min())
            dof_pos_max_seen[j] = max(dof_pos_max_seen[j], col.max())
            margin = (dof_pos_upper[j] - dof_pos_lower[j]) * 0.05
            near_lower = np.sum(col < dof_pos_lower[j] + margin)
            near_upper = np.sum(col > dof_pos_upper[j] - margin)
            dof_near_limit_count[j] += near_lower + near_upper
 
        # --- 선속도 추종 ---
        cmd_x = env.commands[:, 0].cpu().numpy()
        cmd_y = env.commands[:, 1].cpu().numpy()
        vel_x = env.base_lin_vel[:, 0].cpu().numpy()
        vel_y = env.base_lin_vel[:, 1].cpu().numpy()
        vel_errors_x.append(np.mean(np.abs(cmd_x - vel_x)))
        vel_errors_y.append(np.mean(np.abs(cmd_y - vel_y)))
 
        data['cmd_vel_x'].append(np.mean(cmd_x))
        data['actual_vel_x'].append(np.mean(vel_x))
        data['cmd_abs_vel_x'].append(np.mean(np.abs(cmd_x)))
        data['actual_abs_vel_x'].append(np.mean(np.abs(vel_x)))
        data['cmd_forward_pct'].append(np.mean(cmd_x > 0.02) * 100.0)
        data['actual_forward_pct'].append(np.mean(vel_x > 0.02) * 100.0)
        data['cmd_zero_lin_pct'].append(np.mean(np.sqrt(cmd_x ** 2 + cmd_y ** 2) < 0.02) * 100.0)
 
        # --- 각속도 추종 ---
        cmd_yaw = env.commands[:, 2].cpu().numpy()
        actual_yaw = env.base_ang_vel[:, 2].cpu().numpy()

        yaw_err = np.abs(cmd_yaw - actual_yaw)
        ang_vel_errors.append(np.mean(yaw_err))

        data['cmd_ang_vel'].append(np.mean(np.abs(cmd_yaw)))
        data['actual_ang_vel'].append(np.mean(np.abs(actual_yaw)))

        # --- Command mode별 yaw 오차 ---
        cmd_v = np.sqrt(cmd_x ** 2 + cmd_y ** 2)
        cmd_w = np.abs(cmd_yaw)

        stand_mask = (cmd_v < 0.05) & (cmd_w < 0.05)
        forward_mask = (cmd_v >= 0.05) & (cmd_w < 0.15)
        pure_turn_mask = (cmd_v < 0.05) & (cmd_w >= 0.15)
        arc_turn_mask = (cmd_v >= 0.05) & (cmd_w >= 0.15)
        high_wz_mask = cmd_w >= 0.30

        _accumulate_command_mode('stand', stand_mask, yaw_err, cmd_yaw, actual_yaw)
        _accumulate_command_mode('forward', forward_mask, yaw_err, cmd_yaw, actual_yaw)
        _accumulate_command_mode('pure_turn', pure_turn_mask, yaw_err, cmd_yaw, actual_yaw)
        _accumulate_command_mode('arc_turn', arc_turn_mask, yaw_err, cmd_yaw, actual_yaw)
        _accumulate_command_mode('high_wz', high_wz_mask, yaw_err, cmd_yaw, actual_yaw)
 
        # --- Roll/Pitch ---
        projected_grav = env.projected_gravity.cpu().numpy()
        gx = projected_grav[:, 0]
        gy = projected_grav[:, 1]
        gz = projected_grav[:, 2]
        roll = np.arctan2(gy, -gz)
        pitch = np.arctan2(-gx, -gz)
        roll_list.append(np.mean(np.abs(roll)))
        pitch_list.append(np.mean(np.abs(pitch)))
        data['roll_abs'].append(np.mean(np.abs(roll)))
        data['pitch_abs'].append(np.mean(np.abs(pitch)))
 

        # --- 높이/수직속도 ---
        base_height_np = env.root_states[:, 2].cpu().numpy()
        base_vel_z_np = env.base_lin_vel[:, 2].cpu().numpy()
        data['base_height'].append(float(np.mean(base_height_np)))
        data['base_vel_z'].append(float(np.mean(base_vel_z_np)))

        # --- Recovery assist trial 추적 ---
        roll_abs_np = np.abs(roll)
        pitch_abs_np = np.abs(pitch)

        # --- Transition recovery trial 추적: 주행 중 push 이후 안정화 ---
        current_push_step = int(getattr(env, 'last_transition_push_step', -1))
        if current_push_step >= 0 and current_push_step != last_seen_transition_push_step:
            all_env_ids = np.arange(num_envs, dtype=np.int64)
            _start_transition_trials(
                all_env_ids, roll_abs_np, pitch_abs_np, base_height_np, current_push_step)
            last_seen_transition_push_step = current_push_step

        transition_active_mask = transition_active.copy()
        if np.any(transition_active_mask):
            transition_ids = np.where(transition_active_mask)[0]
            transition_age[transition_ids] += 1
            transition_max_roll[transition_ids] = np.maximum(
                transition_max_roll[transition_ids], roll_abs_np[transition_ids])
            transition_max_pitch[transition_ids] = np.maximum(
                transition_max_pitch[transition_ids], pitch_abs_np[transition_ids])

            transition_stable_now = (
                (roll_abs_np < transition_stable_threshold) &
                (pitch_abs_np < transition_stable_threshold) &
                (base_height_np > transition_min_height)
            )
            transition_first_stable_ids = transition_ids[
                transition_stable_now[transition_ids] &
                (transition_first_stable_step[transition_ids] < 0)
            ]
            transition_first_stable_step[transition_first_stable_ids] = transition_age[
                transition_first_stable_ids]

            transition_horizon_ids = transition_ids[
                transition_age[transition_ids] >= transition_horizon_steps]
            for env_i in transition_horizon_ids:
                success = bool(transition_stable_now[env_i])
                _record_transition_trial(
                    env_i,
                    success=success,
                    end_roll=roll_abs_np[env_i],
                    end_pitch=pitch_abs_np[env_i],
                    end_height=base_height_np[env_i],
                    forced_failure=False,
                )
            transition_active[transition_horizon_ids] = False

        # 최초 step 또는 reset 직후 아직 초기화되지 않은 env 초기화
       
            
        uninitialized = np.where(np.isnan(recovery_init_roll))[0]
        if len(uninitialized) > 0:
            init_roll_np, init_pitch_np = _get_initial_tilt_for_trials(roll_abs_np, pitch_abs_np)
            _start_recovery_trials(uninitialized, init_roll_np, init_pitch_np, base_height_np)

        active_mask = recovery_active.copy()
        if np.any(active_mask):
            active_ids = np.where(active_mask)[0]
            recovery_age[active_ids] += 1
            recovery_max_roll[active_ids] = np.maximum(recovery_max_roll[active_ids], roll_abs_np[active_ids])
            recovery_max_pitch[active_ids] = np.maximum(recovery_max_pitch[active_ids], pitch_abs_np[active_ids])

            stable_now = (
                (roll_abs_np < recovery_stable_threshold) &
                (pitch_abs_np < recovery_stable_threshold) &
                (base_height_np > recovery_min_height)
            )
            first_stable_ids = active_ids[
                stable_now[active_ids] & (recovery_first_stable_step[active_ids] < 0)
            ]
            recovery_first_stable_step[first_stable_ids] = recovery_age[first_stable_ids]

            horizon_ids = active_ids[recovery_age[active_ids] >= recovery_horizon_steps]
            for env_i in horizon_ids:
                success = bool(stable_now[env_i])
                _record_recovery_trial(
                    env_i,
                    success=success,
                    end_roll=roll_abs_np[env_i],
                    end_pitch=pitch_abs_np[env_i],
                    end_height=base_height_np[env_i],
                    forced_failure=False,
                )
            recovery_active[horizon_ids] = False
        # --- 동작 부드러움 ---
        current_actions = env.actions.cpu().numpy()
        if prev_actions is not None:
            rate = np.mean(np.square(current_actions - prev_actions))
            action_rates.append(rate)
            data['action_rate'].append(rate)
        prev_actions = current_actions.copy()
 
        # --- 보행 패턴 (발 접촉/공중) ---
        contact_forces = env.contact_forces[:, env.feet_indices, 2].cpu().numpy()
        in_contact = contact_forces > 1.0
        for f in range(num_feet):
            feet_contact_count[f] += np.sum(in_contact[:, f])
            feet_air_count[f] += np.sum(~in_contact[:, f])
 
        contact_pattern = in_contact[0]
        data['contact_pattern'].append(contact_pattern.copy())
 
        # --- 에너지 (Power = torque * velocity) ---
        dof_vel_np = env.dof_vel.cpu().numpy()
        power = np.mean(np.sum(np.abs(torques_np * dof_vel_np), axis=1))
        power_list.append(power)
        data['power'].append(power)

        # --- 관절별 전력 (항목 6-A) ---
        per_joint_power = np.mean(np.abs(torques_np * dof_vel_np), axis=0)  # (num_joints,)
        per_joint_power_sum += per_joint_power

        # peak_power 추적
        step_total_power = np.sum(np.abs(torques_np * dof_vel_np), axis=1).mean()
        peak_power = max(peak_power, step_total_power)
 

        # --- 에피소드 통계 ---
        current_ep_return += rews
        current_ep_length += 1
        done_ids = dones.nonzero(as_tuple=False).flatten()
        if len(done_ids) > 0:
            done_ids_np = done_ids.cpu().numpy().astype(np.int64)

            # horizon 전에 reset된 active recovery trial은 실패로 기록
            active_done_ids = done_ids_np[recovery_active[done_ids_np]]
            for env_i in active_done_ids:
                _record_recovery_trial(
                    env_i,
                    success=False,
                    end_roll=roll_abs_np[env_i],
                    end_pitch=pitch_abs_np[env_i],
                    end_height=base_height_np[env_i],
                    forced_failure=True,
                )

            transition_done_ids = done_ids_np[transition_active[done_ids_np]]
            for env_i in transition_done_ids:
                _record_transition_trial(
                    env_i,
                    success=False,
                    end_roll=roll_abs_np[env_i],
                    end_pitch=pitch_abs_np[env_i],
                    end_height=base_height_np[env_i],
                    forced_failure=True,
                )
            transition_active[transition_done_ids] = False

            # env.step() 내부에서 이미 reset_idx가 호출된 뒤이므로,
            # 현재 관측값을 새 trial의 시작 상태로 사용
            init_roll_np, init_pitch_np = _get_initial_tilt_for_trials(roll_abs_np, pitch_abs_np)
            _start_recovery_trials(done_ids_np, init_roll_np, init_pitch_np, base_height_np)

            episode_lengths.extend(current_ep_length[done_ids].cpu().tolist())
            episode_returns.extend(current_ep_return[done_ids].cpu().tolist())
            current_ep_return[done_ids] = 0
            current_ep_length[done_ids] = 0
        # --- alive 비율 ---
        alive = (env.reset_buf == 0).sum().item()
        data['alive_ratio'].append(alive / num_envs)

    # ============ 공통 후처리 ============
    total_samples = total_steps * num_envs

    # --- 속도 오차 요약 ---
    mean_err_x = np.mean(vel_errors_x)
    mean_err_y = np.mean(vel_errors_y)
    mean_ang_err = np.mean(ang_vel_errors)
    
    # --- Command mode별 yaw 추종 요약 ---
    command_mode_summary = {}

    for name, stat in command_mode_stats.items():
        count = stat['count']

        if count > 0:
            command_mode_summary[name] = {
                'label': stat['label'],
                'count': int(count),
                'ratio_pct': float(count / total_samples * 100),
                'mean_ang_error': float(stat['err_sum'] / count),
                'mean_abs_cmd_yaw': float(stat['cmd_sum'] / count),
                'mean_abs_actual_yaw': float(stat['actual_sum'] / count),
            }
        else:
            command_mode_summary[name] = {
                'label': stat['label'],
                'count': 0,
                'ratio_pct': 0.0,
                'mean_ang_error': None,
                'mean_abs_cmd_yaw': None,
                'mean_abs_actual_yaw': None,
            }

    # --- 자세 요약 ---
    mean_height = np.mean(data['base_height'])
    std_height = np.std(data['base_height'])
    mean_vel_z = np.mean(np.abs(data['base_vel_z']))
    height_target = env.cfg.rewards.base_height_target

    # --- Roll/Pitch 요약 ---
    mean_roll = np.mean(roll_list)
    mean_pitch = np.mean(pitch_list)
    max_roll = np.max(roll_list)
    max_pitch = np.max(pitch_list)

    # --- 토크 요약 ---
    overall_sat = np.mean(data['torque_saturation_ratio']) * 100

    # --- 에너지 요약 ---
    mean_power = np.mean(power_list)
    std_power = np.std(power_list)
    mean_vel = np.mean(np.abs(data['actual_vel_x']))
    mean_cmd_abs_x = np.mean(data['cmd_abs_vel_x'])
    mean_actual_abs_x = np.mean(data['actual_abs_vel_x'])
    mean_cmd_forward_pct = np.mean(data['cmd_forward_pct'])
    mean_actual_forward_pct = np.mean(data['actual_forward_pct'])
    mean_cmd_zero_lin_pct = np.mean(data['cmd_zero_lin_pct'])
    forward_tracking_ratio = (
        mean_actual_abs_x / mean_cmd_abs_x
        if mean_cmd_abs_x > 1.0e-6 else 0.0
    )
    robot_mass = 2.6
    cot = mean_power / (robot_mass * 9.81 * mean_actual_abs_x) if mean_actual_abs_x > 0.01 else 0.0

    # --- 에피소드 통계 ---
    timeout_rate = 0.0
    early_death_rate = 0.0
    if episode_lengths:
        timeout_rate = np.sum(np.array(episode_lengths) >= env.max_episode_length) / len(episode_lengths) * 100
        early_death_rate = np.sum(np.array(episode_lengths) < 50) / len(episode_lengths) * 100


    # --- Recovery assist 요약 ---
    recovery_summary = _summarize_recovery_trials()
    transition_recovery_summary = _summarize_transition_trials()
    # --- Gait FFT 주파수 (항목 3-A) ---
    contact_patterns = np.array(data['contact_pattern'])  # (steps, num_feet)
    gait_frequency = 0.0
    gait_period_steps = 0

    if len(contact_patterns) > 100:
        signal = contact_patterns[:, 0].astype(float)  # FL foot
        signal = signal - signal.mean()  # DC 제거

        dt_step = env.dt * env.cfg.control.decimation  # 1 step의 실제 시간(초)

        from numpy.fft import fft, fftfreq
        N = len(signal)
        yf = np.abs(fft(signal))[:N//2]
        xf = fftfreq(N, d=dt_step)[:N//2]

        # DC(0Hz) 제외하고 피크 찾기
        yf[0] = 0
        peak_idx = np.argmax(yf)
        gait_frequency = float(xf[peak_idx])  # Hz
        gait_period_steps = int(1.0 / (gait_frequency * dt_step)) if gait_frequency > 0 else 0

    # --- L/R 비대칭 (항목 3-B) ---
    left_contact = []
    right_contact = []
    for f in range(num_feet):
        total = feet_contact_count[f] + feet_air_count[f]
        pct = feet_contact_count[f] / total * 100 if total > 0 else 0
        fn_lower = foot_names[f].lower()
        if 'left' in fn_lower or '_l_' in fn_lower:
            left_contact.append(pct)
        elif 'right' in fn_lower or '_r_' in fn_lower:
            right_contact.append(pct)

    lr_asymmetry = abs(np.mean(left_contact) - np.mean(right_contact)) if left_contact and right_contact else 0.0

    # --- 대각 동기화율 (항목 3-C) ---
    overall_trot = 0.0
    diag1_sync = 0.0
    diag2_sync = 0.0
    all_ground = True
    for f in range(num_feet):
        total = feet_contact_count[f] + feet_air_count[f]
        air_pct = feet_air_count[f] / total * 100 if total > 0 else 0
        if air_pct > 5:
            all_ground = False

    if not all_ground and num_feet == 4 and len(contact_patterns) > 100:
        diag1_sync = float(np.mean(contact_patterns[:, 0] == contact_patterns[:, 3]))
        diag2_sync = float(np.mean(contact_patterns[:, 1] == contact_patterns[:, 2]))
        overall_trot = (diag1_sync + diag2_sync) / 2

    # ================================================================
    # 경량 모드: JSON 저장만 하고 리턴 (항목 1-B, 1-C)
    # ================================================================
    if lightweight:
        import json as _json

        # 발별 접촉 비율
        feet_contact_pct_dict = {}
        for f in range(num_feet):
            fname = foot_names[f]
            total = feet_contact_count[f] + feet_air_count[f]
            feet_contact_pct_dict[fname] = float(feet_contact_count[f] / total * 100) if total > 0 else 0.0

        snapshot = {
            'iter': iter_number,
            'timeout_pct': float(timeout_rate),
            'vel_error_x': float(mean_err_x),
            'torque_saturation_pct': float(overall_sat),
            'mean_roll_deg': float(np.degrees(mean_roll)),
            'mean_pitch_deg': float(np.degrees(mean_pitch)),
            'mean_power': float(mean_power),
            'episode_return': float(np.mean(episode_returns)) if episode_returns else 0.0,
            'recovery_success_rate_pct': recovery_summary.get('success_rate_pct'),
            'recovery_eligible_trials': recovery_summary.get('eligible_trials', 0),
            'mean_recovery_time_s': recovery_summary.get('mean_recovery_time_s'),
            'recovery_prefall_success_rate_pct': recovery_summary.get('prefall', {}).get('success_rate_pct'),
            'recovery_prefall_trials': recovery_summary.get('prefall', {}).get('trials', 0),
            'feet_contact_pct': feet_contact_pct_dict,
            'diagonal_sync': float(overall_trot * 100),
        }

        snapshot_path = os.path.join(diag_dir, f'snapshot_iter{iter_number:04d}.json')
        with open(snapshot_path, 'w', encoding='utf-8') as jf:
            _json.dump(snapshot, jf, indent=2, ensure_ascii=False)
        print(f"    -> 스냅샷 저장: {snapshot_path}")
        return  # 경량 모드 종료

    # ============ 리포트 출력 (풀 모드만) ============
 
    # --- [1] 토크 분석 ---
    print(f"\n{'='*60}")
    print(f"  [1] 토크 분석 (Torque Analysis)")
    print(f"{'='*60}")
    print(f"  {'Joint':<30} {'Limit':>7} {'MaxSeen':>8} {'Sat%':>7} {'상태':>6}")
    print(f"  {'-'*58}")
    for j in range(num_joints):
        sat_pct = torque_saturation_count[j] / total_samples * 100
        limit = torque_limits[j]
        max_seen = torque_max_seen[j]
        if sat_pct > 20:
            status = "⚠ 심각"
        elif sat_pct > 5:
            status = "⚠ 주의"
        else:
            status = "✓ 정상"
        print(f"  {joint_names[j]:<30} {limit:>7.3f} {max_seen:>8.3f} {sat_pct:>6.1f}% {status}")
 
    print(f"\n  전체 평균 토크 포화율: {overall_sat:.1f}%")
    if overall_sat > 10:
        print(f"  → 토크 한계에 자주 도달. PD 게인 또는 action_scale 확인 필요.")
    elif overall_sat > 3:
        print(f"  → 가끔 포화. 정상 범위이나 모니터링 필요.")
    else:
        print(f"  → 토크 여유 충분.")
 
    # --- [2] 관절 한계 ---
    print(f"\n{'='*60}")
    print(f"  [2] 관절 한계 분석 (Joint Limit Analysis)")
    print(f"{'='*60}")
    print(f"  {'Joint':<30} {'URDF Lo':>8} {'URDF Hi':>8} {'MinSeen':>8} {'MaxSeen':>8} {'Near%':>7}")
    print(f"  {'-'*69}")
    for j in range(num_joints):
        near_pct = dof_near_limit_count[j] / total_samples * 100
        print(f"  {joint_names[j]:<30} {urdf_lower[j]:>8.3f} {urdf_upper[j]:>8.3f} "
              f"{dof_pos_min_seen[j]:>8.3f} {dof_pos_max_seen[j]:>8.3f} {near_pct:>6.1f}%")
 
    # --- [3] 선속도 추종 ---
    print(f"\n{'='*60}")
    print(f"  [3] 선속도 추종 분석 (Linear Velocity Tracking)")
    print(f"{'='*60}")
    print(f"  평균 X속도 오차: {mean_err_x:.4f} m/s")
    print(f"  평균 Y속도 오차: {mean_err_y:.4f} m/s")
    print(f"  평균 |cmd_x|: {mean_cmd_abs_x:.4f} m/s")
    print(f"  평균 |actual_x|: {mean_actual_abs_x:.4f} m/s")
    print(f"  전진 추종 비율(|actual_x|/|cmd_x|): {forward_tracking_ratio:.2f}")
    print(f"  전진 command 비율(cmd_x > 0.02): {mean_cmd_forward_pct:.1f}%")
    print(f"  실제 전진 비율(actual_x > 0.02): {mean_actual_forward_pct:.1f}%")
    print(f"  선속도 zero command 비율(|cmd_xy| < 0.02): {mean_cmd_zero_lin_pct:.1f}%")
    if mean_cmd_abs_x < 0.02 and env.cfg.commands.ranges.lin_vel_x[1] > 0.05:
        print(f"  → ⚠ 전진 command가 거의 없습니다. command deadband/sampling 설정 확인 필요.")
    if mean_err_x < 0.03:
        print(f"  → ✓ X속도 추종 우수")
    elif mean_err_x < 0.08:
        print(f"  → X속도 추종 보통")
    else:
        print(f"  → ⚠ X속도 추종 미흡")
 
    # --- [4] 에피소드 통계 ---
    print(f"\n{'='*60}")
    print(f"  [4] 에피소드 통계 (Episode Stats)")
    print(f"{'='*60}")
    if episode_lengths:
        print(f"  에피소드 수: {len(episode_lengths)}")
        print(f"  평균 길이: {np.mean(episode_lengths):.1f} steps "
              f"(max: {env.max_episode_length} = timeout)")
        print(f"  최소/최대 길이: {np.min(episode_lengths):.0f} / {np.max(episode_lengths):.0f}")
        print(f"  평균 리턴: {np.mean(episode_returns):.3f}")
        print(f"  Timeout 비율: {timeout_rate:.1f}%")
        print(f"  조기 종료 비율 (<50 steps): {early_death_rate:.1f}%")
        if early_death_rate > 30:
            print(f"  → ⚠ 리셋 직후 넘어짐 빈번.")
    else:
        print(f"  에피소드 완료 없음")
 
    # --- [5] 자세 안정성 ---
    print(f"\n{'='*60}")
    print(f"  [5] 자세 안정성 (Posture Stability)")
    print(f"{'='*60}")
    height_error = abs(mean_height - height_target)
    print(f"  평균 높이: {mean_height:.4f} m (목표: {height_target} m, 편차: {height_error:.4f} m)")
    print(f"  높이 표준편차: {std_height:.4f} m")
    print(f"  평균 수직 속도(abs): {mean_vel_z:.4f} m/s")
    if mean_height < 0.13:
        print(f"  → ⚠ 주저앉음. Kp 부족 또는 토크 포화 확인.")
    elif height_error > 0.04:
        print(f"  → ⚠ 목표 높이와 차이 큼. base_height reward 추가 고려.")
    elif mean_vel_z > 0.1:
        print(f"  → ⚠ 수직 바운싱. lin_vel_z 페널티 추가 고려.")
    elif std_height > 0.03:
        print(f"  → ⚠ 높이 변동 큼. 안정성 reward 추가 고려.")
    else:
        print(f"  → ✓ 안정적.")
 
    # --- [6] 회전 추종 ---
    print(f"\n{'='*60}")
    print(f"  [6] 회전 추종 분석 (Angular Velocity Tracking)")
    print(f"{'='*60}")
    mean_cmd_yaw = np.mean(np.abs(data['cmd_ang_vel']))
    print(f"  평균 yaw 명령(abs): {mean_cmd_yaw:.4f} rad/s")
    print(f"  평균 yaw 오차: {mean_ang_err:.4f} rad/s")
    
    print("")
    print("  Command mode별 yaw 추종:")
    print(f"  {'Mode':<14} {'Ratio':>7} {'Count':>8} {'Cmd|wz|':>10} {'Actual|wz|':>12} {'Err':>10}")
    print(f"  {'-'*67}")

    for key in ['stand', 'forward', 'pure_turn', 'arc_turn', 'high_wz']:
        m = command_mode_summary[key]
        label = m['label']
        ratio = m['ratio_pct']
        count = m['count']

        if m['mean_ang_error'] is None:
            cmd_str = "N/A"
            actual_str = "N/A"
            err_str = "N/A"
        else:
            cmd_str = f"{m['mean_abs_cmd_yaw']:.4f}"
            actual_str = f"{m['mean_abs_actual_yaw']:.4f}"
            err_str = f"{m['mean_ang_error']:.4f}"

        print(f"  {label:<14} {ratio:>6.1f}% {count:>8} {cmd_str:>10} {actual_str:>12} {err_str:>10}")
    
    if mean_cmd_yaw < 0.01:
        print(f"  → yaw 명령 없음 (Step 2 이전이면 정상)")
    elif mean_ang_err < 0.05:
        print(f"  → ✓ 회전 추종 우수")
    elif mean_ang_err < 0.15:
        print(f"  → 회전 추종 보통")
    else:
        print(f"  → ⚠ 회전 추종 미흡")
 
    # --- [7] Roll/Pitch ---
    print(f"\n{'='*60}")
    print(f"  [7] 자세 각도 분석 (Roll/Pitch)")
    print(f"{'='*60}")
    print(f"  평균 |roll|:  {np.degrees(mean_roll):.2f}° (최대: {np.degrees(max_roll):.2f}°)")
    print(f"  평균 |pitch|: {np.degrees(mean_pitch):.2f}° (최대: {np.degrees(max_pitch):.2f}°)")
    if mean_roll > np.radians(15) or mean_pitch > np.radians(15):
        print(f"  → ⚠ 기울어짐 심각. orientation reward 강화 필요.")
    elif mean_roll > np.radians(8) or mean_pitch > np.radians(8):
        print(f"  → ⚠ 기울어짐 있음. orientation reward 고려.")
    else:
        print(f"  → ✓ 자세 안정적.")
 
    # --- [8] 동작 부드러움 ---
    print(f"\n{'='*60}")
    print(f"  [8] 동작 부드러움 분석 (Action Smoothness)")
    print(f"{'='*60}")
    if action_rates:
        mean_action_rate = np.mean(action_rates)
        max_action_rate = np.max(action_rates)
        print(f"  평균 action rate (MSE): {mean_action_rate:.6f}")
        print(f"  최대 action rate (MSE): {max_action_rate:.6f}")
        if mean_action_rate > 0.1:
            print(f"  → ⚠ 동작 급변 심함. 떨림 가능성. action_rate 페널티 추가 고려.")
        elif mean_action_rate > 0.01:
            print(f"  → 보통. action_rate 페널티로 개선 가능.")
        else:
            print(f"  → ✓ 동작 부드러움.")
 
    # --- [9] 보행 패턴 ---
    print(f"\n{'='*60}")
    print(f"  [9] 보행 패턴 분석 (Gait Pattern)")
    print(f"{'='*60}")
    print(f"  {'Foot':<25} {'접촉%':>8} {'공중%':>8} {'상태':>10}")
    print(f"  {'-'*51}")
    for f in range(num_feet):
        total = feet_contact_count[f] + feet_air_count[f]
        contact_pct = feet_contact_count[f] / total * 100 if total > 0 else 0
        air_pct = feet_air_count[f] / total * 100 if total > 0 else 0
        if air_pct < 2:
            status = "⚠ 끌림"
        elif air_pct < 10:
            status = "△ 약간 듦"
        elif air_pct < 50:
            status = "✓ 정상"
        else:
            status = "⚠ 과도"
        print(f"  {foot_names[f]:<25} {contact_pct:>7.1f}% {air_pct:>7.1f}% {status}")

    if all_ground:
        print(f"\n  → 모든 발이 바닥에 붙어 있음. Step 5 이전이면 정상.")
    else:
        if num_feet == 4 and len(contact_patterns) > 100:
            print(f"\n  대각선 동기화율: {overall_trot*100:.1f}%")
            print(f"    (FL-RR: {diag1_sync*100:.1f}%, FR-RL: {diag2_sync*100:.1f}%)")
            if overall_trot > 0.7:
                print(f"  → ✓ Trot gait 패턴 감지!")
            elif overall_trot > 0.5:
                print(f"  → 부분적 trot 패턴. 학습 더 필요.")
            else:
                print(f"  → Trot이 아닌 다른 gait. 안정적이면 OK.")

    # --- Gait FFT 출력 (항목 3) ---
    print(f"\n  Gait 주파수: {gait_frequency:.2f} Hz (주기: {gait_period_steps} steps)")
    print(f"  L/R 비대칭: {lr_asymmetry:.1f}%p")
 
    # --- [10] 에너지 효율 ---
    print(f"\n{'='*60}")
    print(f"  [10] 에너지 효율 분석 (Energy/Power)")
    print(f"{'='*60}")
    print(f"  평균 소비 전력: {mean_power:.4f} W")
    print(f"  전력 표준편차: {std_power:.4f} W")
    print(f"  피크 전력: {peak_power:.4f} W")
    if mean_vel > 0.01:
        print(f"  Cost of Transport: {cot:.2f}")
        if cot < 2.0:
            print(f"  → ✓ 에너지 효율 우수")
        elif cot < 10.0:
            print(f"  → 보통 (소형 로봇 일반적 수준)")
        else:
            print(f"  → ⚠ 비효율적. torques 페널티 추가 고려.")
    else:
        print(f"  → 속도 거의 0이라 CoT 계산 불가")

    # --- [11] Trot 상세 분석 ---
    print(f"\n{'='*60}")
    print(f"  [11] Trot 상세 분석 (Trot Detail)")
    print(f"{'='*60}")
    contact_history = np.array(data['contact_pattern'])
    for f in range(num_feet):
        air_durations = []
        ground_durations = []
        count = 0
        in_air = not contact_history[0][f]
        for t in range(len(contact_history)):
            is_air = not contact_history[t][f]
            if is_air == in_air:
                count += 1
            else:
                duration = count * env.dt * env.cfg.control.decimation
                if in_air:
                    air_durations.append(duration)
                else:
                    ground_durations.append(duration)
                count = 1
                in_air = is_air
        mean_air = np.mean(air_durations) if air_durations else 0
        mean_gnd = np.mean(ground_durations) if ground_durations else 0
        print(f"  {foot_names[f]}: 공중 {mean_air:.3f}s / 접지 {mean_gnd:.3f}s")

    print(f"\n  FL-RR 평균 공중시간 vs FR-RL 평균 공중시간")
    print(f"  비율이 1.0에 가까울수록 대칭")

    # --- [12] 속도 구간별 추종 ---
    print(f"\n{'='*60}")
    print(f"  [12] 속도 구간별 추종 (Per-speed Tracking)")
    print(f"{'='*60}")
    cmd_x_all = np.array(data['cmd_vel_x'])
    actual_x_all = np.array(data['actual_vel_x'])
    for low, high, label in [(0.0, 0.2, '저속'), (0.2, 0.3, '중속'), (0.3, 0.5, '고속')]:
        mask = (cmd_x_all >= low) & (cmd_x_all < high)
        if np.sum(mask) > 0:
            err = np.mean(np.abs(cmd_x_all[mask] - actual_x_all[mask]))
            print(f"  {label} ({low}~{high} m/s): 오차 {err:.4f} m/s, 샘플 {np.sum(mask)}")

    # --- [13] 넘어짐 원인 분석 ---
    print(f"\n{'='*60}")
    print(f"  [13] 넘어짐 원인 분석 (Fall Analysis)")
    print(f"{'='*60}")
    if 'fall_heights' not in data:
        data['fall_heights'] = []
        data['fall_rolls'] = []
        data['fall_pitches'] = []
    if data['fall_heights']:
        print(f"  넘어질 때 평균 높이: {np.mean(data['fall_heights']):.4f} m")
        print(f"  넘어질 때 평균 |roll|: {np.degrees(np.mean(data['fall_rolls'])):.1f}°")
        print(f"  넘어질 때 평균 |pitch|: {np.degrees(np.mean(data['fall_pitches'])):.1f}°")
        if np.mean(data['fall_rolls']) > np.mean(data['fall_pitches']):
            print(f"  → 좌우 전도가 주 원인. orientation 또는 ang_vel_xy 강화 고려")
        else:
            print(f"  → 전후 전도가 주 원인. 속도 범위 또는 lin_vel_z 확인")

    # --- [14] Shoulder 관절 분석 ---
    print(f"\n{'='*60}")
    print(f"  [14] Shoulder 관절 상세 (Shoulder Analysis)")
    print(f"{'='*60}")
    shoulder_indices = [j for j, name in enumerate(joint_names) if 'shoulder' in name]
    for j in shoulder_indices:
        range_used = dof_pos_max_seen[j] - dof_pos_min_seen[j]
        range_total = urdf_upper[j] - urdf_lower[j]
        usage_pct = range_used / range_total * 100
        bias = (dof_pos_max_seen[j] + dof_pos_min_seen[j]) / 2
        print(f"  {joint_names[j]}: 사용범위 {usage_pct:.0f}%, 편향 {bias:+.3f} rad")
        if abs(bias) > 0.1:
            print(f"    → ⚠ 한쪽으로 편향. 비대칭 보행 원인 가능")


    # --- [15] Recovery assist 분석 ---
    def _fmt_pct(value):
        return "N/A" if value is None else f"{value:.1f}%"

    def _fmt_num(value, suffix=""):
        return "N/A" if value is None else f"{value:.2f}{suffix}"

    def _print_tilt_band_summary(title, summary):
        bands = summary.get('tilt_bands', [])
        prefall = summary.get('prefall', {})
        print(f"\n  {title} tilt band summary")
        print(f"  {'Band':<10} {'Trials':>7} {'Success':>9} {'EndTilt':>9} {'Time':>8}")
        print(f"  {'-'*48}")
        for band in bands:
            print(
                f"  {band['label']:<10} "
                f"{band.get('trials', 0):>7} "
                f"{_fmt_pct(band.get('success_rate_pct')):>9} "
                f"{_fmt_num(band.get('mean_end_tilt_deg'), '°'):>9} "
                f"{_fmt_num(band.get('mean_recovery_time_s'), 's'):>8}"
            )
        print(
            f"  {'18+ deg':<10} "
            f"{prefall.get('trials', 0):>7} "
            f"{_fmt_pct(prefall.get('success_rate_pct')):>9} "
            f"{_fmt_num(prefall.get('mean_end_tilt_deg'), '°'):>9} "
            f"{_fmt_num(prefall.get('mean_recovery_time_s'), 's'):>8}"
        )

    print(f"\n{'='*60}")
    print(f"  [15] Recovery Assist 분석")
    print(f"{'='*60}")
    print(f"  평가 horizon: {recovery_summary['horizon_s']:.2f}s")
    print(f"  eligible 기준: 초기 |roll| 또는 |pitch| ≥ {recovery_summary['initial_tilt_threshold_deg']:.1f}°")
    print(f"  안정 기준: |roll|, |pitch| < {recovery_summary['stable_threshold_deg']:.1f}°, height > {recovery_summary['min_height_m']:.3f}m")
    print(f"  전체 trial: {recovery_summary['total_trials']}")
    print(f"  eligible trial: {recovery_summary['eligible_trials']}")
    if recovery_summary['success_rate_pct'] is None:
        print("  Recovery 성공률: N/A (eligible trial 없음)")
    else:
        print(f"  Recovery 성공률: {recovery_summary['success_rate_pct']:.1f}% "
              f"({recovery_summary['success_count']}/{recovery_summary['eligible_trials']})")
        print(f"  조기 실패율: {recovery_summary['early_failure_rate_pct']:.1f}%")
        if recovery_summary['mean_recovery_time_s'] is not None:
            print(f"  평균 회복 시간: {recovery_summary['mean_recovery_time_s']:.3f}s")
        print(f"  평균 초기 tilt: {recovery_summary['mean_initial_tilt_deg']:.2f}°")
        print(f"  1초 후 평균 |roll|/|pitch|: "
              f"{recovery_summary['mean_end_roll_deg']:.2f}° / "
              f"{recovery_summary['mean_end_pitch_deg']:.2f}°")
        if recovery_summary['success_rate_pct'] >= 80 and recovery_summary['early_failure_rate_pct'] < 10:
            print("  → ✓ Recovery assist 안정적")
        elif recovery_summary['success_rate_pct'] >= 50:
            print("  → △ 일부 회복 가능. perturbation curriculum 또는 reward 조정 필요")
        else:
            print("  → ⚠ Recovery 성공률 낮음. perturbation 강도/termination/reward 확인 필요")
    _print_tilt_band_summary("Recovery", recovery_summary)

    # --- [16] Transition recovery 분석 ---
    print(f"\n{'='*60}")
    print(f"  [16] Transition Recovery 분석")
    print(f"{'='*60}")
    print(f"  평가 horizon: {transition_recovery_summary['horizon_s']:.2f}s")
    print(f"  eligible 기준: push 후 최대 |roll| 또는 |pitch| ≥ "
          f"{transition_recovery_summary['initial_tilt_threshold_deg']:.1f}°")
    print(f"  안정 기준: |roll|, |pitch| < "
          f"{transition_recovery_summary['stable_threshold_deg']:.1f}°, "
          f"height > {transition_recovery_summary['min_height_m']:.3f}m")
    print(f"  전체 trial: {transition_recovery_summary['total_trials']}")
    print(f"  eligible trial: {transition_recovery_summary['eligible_trials']}")
    if transition_recovery_summary['success_rate_pct'] is None:
        print("  Transition recovery 성공률: N/A (eligible trial 없음)")
    else:
        print(f"  Transition recovery 성공률: "
              f"{transition_recovery_summary['success_rate_pct']:.1f}% "
              f"({transition_recovery_summary['success_count']}/"
              f"{transition_recovery_summary['eligible_trials']})")
        print(f"  조기 실패율: {transition_recovery_summary['early_failure_rate_pct']:.1f}%")
        if transition_recovery_summary['mean_recovery_time_s'] is not None:
            print(f"  평균 회복 시간: {transition_recovery_summary['mean_recovery_time_s']:.3f}s")
        print(f"  평균 최대 tilt: {transition_recovery_summary['mean_max_tilt_deg']:.2f}°")
        print(f"  horizon 후 평균 |roll|/|pitch|: "
              f"{transition_recovery_summary['mean_end_roll_deg']:.2f}° / "
              f"{transition_recovery_summary['mean_end_pitch_deg']:.2f}°")
    _print_tilt_band_summary("Transition", transition_recovery_summary)
    # ============ 그래프 1: 종합 대시보드 ============
    fig, axes = plt.subplots(4, 2, figsize=(16, 20))
    fig.suptitle('SpotMicro RL Diagnostic Report v3', fontsize=16, fontweight='bold')
    steps_range = range(len(data['cmd_vel_x']))

    ax = axes[0, 0]
    sat_pcts = torque_saturation_count / total_samples * 100
    colors = ['red' if s > 20 else 'orange' if s > 5 else 'green' for s in sat_pcts]
    short_names = [n.replace('front_', 'F').replace('rear_', 'R')
                    .replace('_left_', 'L_').replace('_right_', 'R_') for n in joint_names]
    ax.barh(short_names, sat_pcts, color=colors)
    ax.set_xlabel('Saturation %')
    ax.set_title('Torque Saturation per Joint (>90% limit)')
    ax.axvline(x=10, color='orange', linestyle='--', alpha=0.5)

    ax = axes[0, 1]
    for j in range(num_joints):
        ax.plot([j, j], [urdf_lower[j], urdf_upper[j]], 'b-', linewidth=6, alpha=0.2)
        ax.plot([j, j], [dof_pos_min_seen[j], dof_pos_max_seen[j]], 'r-', linewidth=3)
    ax.set_xticks(range(num_joints))
    ax.set_xticklabels(short_names, rotation=45, ha='right', fontsize=8)
    ax.set_ylabel('Angle (rad)')
    ax.set_title('Joint Range: URDF(blue) vs Actual(red)')

    ax = axes[1, 0]
    ax.plot(steps_range, data['cmd_vel_x'], 'b-', alpha=0.7, label='Command X')
    ax.plot(steps_range, data['actual_vel_x'], 'r-', alpha=0.7, label='Actual X')
    ax.set_xlabel('Step'); ax.set_ylabel('Velocity (m/s)')
    ax.set_title('Linear Velocity Tracking'); ax.legend()

    ax = axes[1, 1]
    ax.plot(steps_range, data['cmd_ang_vel'], 'b-', alpha=0.7, label='Command Yaw')
    ax.plot(steps_range, data['actual_ang_vel'], 'r-', alpha=0.7, label='Actual Yaw')
    ax.set_xlabel('Step'); ax.set_ylabel('Angular Vel (rad/s)')
    ax.set_title('Angular Velocity Tracking'); ax.legend()

    ax = axes[2, 0]
    ax.plot(steps_range, [np.degrees(r) for r in data['roll_abs']], 'r-', alpha=0.7, label='|Roll|')
    ax.plot(steps_range, [np.degrees(p) for p in data['pitch_abs']], 'b-', alpha=0.7, label='|Pitch|')
    ax.axhline(y=15, color='red', linestyle='--', alpha=0.3, label='Warning 15°')
    ax.set_xlabel('Step'); ax.set_ylabel('Angle (deg)')
    ax.set_title('Roll / Pitch Stability'); ax.legend()

    ax = axes[2, 1]
    ax.plot(steps_range, data['base_height'], 'g-', alpha=0.7, label='Height')
    ax.axhline(y=height_target, color='blue', linestyle='--', alpha=0.5, label=f'Target ({height_target}m)')
    ax.axhline(y=0.12, color='red', linestyle='--', alpha=0.5, label='Termination')
    ax.set_xlabel('Step'); ax.set_ylabel('Height (m)')
    ax.set_title('Base Height'); ax.legend()

    ax = axes[3, 0]
    show_steps = min(200, len(contact_patterns))
    if show_steps > 0:
        pattern_slice = contact_patterns[-show_steps:]
        for f in range(num_feet):
            y_vals = pattern_slice[:, f].astype(float) * (num_feet - f)
            ax.fill_between(range(show_steps), y_vals - 0.4, y_vals + 0.4,
                          alpha=0.6, label=foot_names[f])
        ax.set_xlabel('Step (last 200)'); ax.set_ylabel('Foot (filled=contact)')
        ax.set_title('Gait Pattern (Robot #0)'); ax.legend(fontsize=7); ax.set_yticks([])

    ax = axes[3, 1]
    ax.plot(steps_range, data['power'], 'orange', alpha=0.7)
    ax.set_xlabel('Step'); ax.set_ylabel('Power (W)')
    ax.set_title('Mechanical Power Consumption')

    plt.tight_layout()
    save_path = os.path.join(diag_dir, 'diagnostic_report.png')
    plt.savefig(save_path, dpi=150)
    print(f"\n  종합 그래프 저장: {save_path}")

    # ============ 그래프 2: 관절별 토크 상세 ============
    print(f"  관절별 토크 히스토리 수집 중...")
    torque_history = []
    dof_pos_history = []
    obs = env.get_observations()
    for _ in range(500):
        actions = policy(obs.detach())
        obs, _, rews, dones, infos = env.step(actions.detach())
        torque_history.append(env.torques[0].cpu().numpy())
        dof_pos_history.append(env.dof_pos[0].cpu().numpy())

    torque_history = np.array(torque_history)
    dof_pos_history = np.array(dof_pos_history)

    fig2, axes2 = plt.subplots(4, 3, figsize=(18, 16))
    fig2.suptitle('Per-Joint Torque & Position (Robot #0, last 500 steps)', fontsize=14)
    for j in range(min(num_joints, 12)):
        row = j // 3; col = j % 3
        ax = axes2[row, col]; ax_pos = ax.twinx()
        t_steps = range(len(torque_history))
        ax.plot(t_steps, torque_history[:, j], 'b-', alpha=0.7, linewidth=0.8)
        ax.axhline(y=torque_limits[j], color='red', linestyle='--', alpha=0.3)
        ax.axhline(y=-torque_limits[j], color='red', linestyle='--', alpha=0.3)
        ax.set_ylabel('Torque (Nm)', color='blue', fontsize=8)
        ax.set_ylim(-torque_limits[j] * 1.2, torque_limits[j] * 1.2)
        ax_pos.plot(t_steps, dof_pos_history[:, j], 'g-', alpha=0.5, linewidth=0.8)
        ax_pos.axhline(y=urdf_lower[j], color='orange', linestyle=':', alpha=0.3)
        ax_pos.axhline(y=urdf_upper[j], color='orange', linestyle=':', alpha=0.3)
        ax_pos.set_ylabel('Pos (rad)', color='green', fontsize=8)
        ax.set_title(joint_names[j], fontsize=9)
        ax.tick_params(axis='both', labelsize=7)

    plt.tight_layout()
    save_path2 = os.path.join(diag_dir, 'joint_detail.png')
    plt.savefig(save_path2, dpi=150)
    print(f"  관절 상세 그래프 저장: {save_path2}")

    # ============ 그래프 3: 동작 부드러움 ============
    if action_rates:
        fig3, axes3 = plt.subplots(1, 2, figsize=(14, 5))
        fig3.suptitle('Action Smoothness Analysis', fontsize=14)
        ax = axes3[0]
        ax.plot(action_rates, 'purple', alpha=0.7, linewidth=0.5)
        ax.set_xlabel('Step'); ax.set_ylabel('Action Rate (MSE)')
        ax.set_title('Action Rate Over Time')
        ax = axes3[1]
        ax.hist(action_rates, bins=50, color='purple', edgecolor='white', alpha=0.7)
        ax.set_xlabel('Action Rate (MSE)'); ax.set_ylabel('Count')
        ax.set_title('Action Rate Distribution')
        plt.tight_layout()
        save_path3 = os.path.join(diag_dir, 'action_smoothness.png')
        plt.savefig(save_path3, dpi=150)
        print(f"  동작 부드러움 그래프 저장: {save_path3}")


    # ============ 그래프 4: Recovery assist ============
    if recovery_trials:
        eligible_trials = [t for t in recovery_trials if t['eligible']]
        fig4, axes4 = plt.subplots(2, 2, figsize=(14, 10))
        fig4.suptitle('Recovery Assist Analysis', fontsize=14)

        ax = axes4[0, 0]
        ax.plot(steps_range, [np.degrees(r) for r in data['roll_abs']], 'r-', alpha=0.7, label='|Roll|')
        ax.plot(steps_range, [np.degrees(p) for p in data['pitch_abs']], 'b-', alpha=0.7, label='|Pitch|')
        ax.axhline(y=recovery_summary['stable_threshold_deg'], color='green', linestyle='--', alpha=0.5, label='Stable threshold')
        ax.axhline(y=recovery_summary['initial_tilt_threshold_deg'], color='orange', linestyle='--', alpha=0.5, label='Eligible threshold')
        ax.axhline(y=18, color='purple', linestyle=':', alpha=0.4, label='Pre-fall 18 deg')
        ax.axhline(y=25, color='red', linestyle=':', alpha=0.35, label='Pre-fall 25 deg')
        ax.set_xlabel('Step')
        ax.set_ylabel('Angle (deg)')
        ax.set_title('Roll/Pitch Stability During Diagnostic')
        ax.legend()

        ax = axes4[0, 1]
        if eligible_trials:
            init_tilts = [t['init_tilt_deg'] for t in eligible_trials]
            end_tilts = [max(t['end_roll_deg'], t['end_pitch_deg']) for t in eligible_trials]
            ax.hist(init_tilts, bins=20, alpha=0.6, label='Initial tilt')
            ax.hist(end_tilts, bins=20, alpha=0.6, label='Tilt at 1s/end')
            ax.axvline(18, color='purple', linestyle=':', alpha=0.6, label='18 deg')
            ax.axvline(25, color='red', linestyle=':', alpha=0.5, label='25 deg')
            ax.axvline(30, color='black', linestyle=':', alpha=0.4, label='30 deg')
            ax.set_xlabel('Tilt (deg)')
            ax.set_ylabel('Count')
            ax.set_title('Initial vs End Tilt Distribution')
            ax.legend()
        else:
            ax.text(0.5, 0.5, 'No eligible trials', ha='center', va='center')
            ax.set_axis_off()

        ax = axes4[1, 0]
        if eligible_trials:
            success_count = recovery_summary['success_count']
            failure_count = recovery_summary['failure_count']
            ax.bar(['Success', 'Failure'], [success_count, failure_count])
            ax.set_ylabel('Trials')
            ax.set_title(f"Recovery Success Rate: {recovery_summary['success_rate_pct']:.1f}%")
        else:
            ax.text(0.5, 0.5, 'No eligible trials', ha='center', va='center')
            ax.set_axis_off()

        ax = axes4[1, 1]
        times = [t['recovery_time_s'] for t in eligible_trials if t['success'] and t['recovery_time_s'] is not None]
        if times:
            ax.hist(times, bins=20, alpha=0.8)
            ax.axvline(np.mean(times), linestyle='--', alpha=0.7, label=f"mean={np.mean(times):.2f}s")
            ax.set_xlabel('Recovery time (s)')
            ax.set_ylabel('Count')
            ax.set_title('Recovery Time Distribution')
            ax.legend()
        else:
            ax.text(0.5, 0.5, 'No successful recovery times', ha='center', va='center')
            ax.set_axis_off()

        plt.tight_layout()
        save_path4 = os.path.join(diag_dir, 'recovery_report.png')
        plt.savefig(save_path4, dpi=150)
        print(f"  Recovery 그래프 저장: {save_path4}")
    print(f"\n{'='*60}")
    print(f"  진단 완료!")
    print(f"  그래프 위치: {diag_dir}/")
    print(f"{'='*60}\n")

    # ============ JSON 요약 저장 (experiment_report.py 연동용) ============
    import json as _json
    from datetime import datetime as _dt

    terrain_eval_mode = (
        'flat'
        if flat_eval else
        ('terrain_curriculum' if terrain_curriculum_eval else 'terrain_random')
    )
    if walk_eval:
        terrain_eval_mode = f"{terrain_eval_mode}_walk"

    diagnostic_summary = {
        'timestamp': _dt.now().isoformat(),
        'run_name': train_cfg.runner.run_name,
        'experiment_name': train_cfg.runner.experiment_name,
        'max_iterations': train_cfg.runner.max_iterations,
        'num_envs': num_envs,
        'metrics': {
            'timeout_pct': float(timeout_rate),
            'early_death_pct': float(early_death_rate),
            'vel_error_x': float(mean_err_x),
            'vel_error_y': float(mean_err_y),
            'mean_cmd_abs_x': float(mean_cmd_abs_x),
            'mean_actual_abs_x': float(mean_actual_abs_x),
            'forward_tracking_ratio': float(forward_tracking_ratio),
            'mean_cmd_forward_pct': float(mean_cmd_forward_pct),
            'mean_actual_forward_pct': float(mean_actual_forward_pct),
            'mean_cmd_zero_lin_pct': float(mean_cmd_zero_lin_pct),
            'ang_vel_error': float(mean_ang_err),
            'torque_saturation_pct': float(overall_sat),
            'mean_height': float(mean_height),
            'height_std': float(std_height),
            'height_target': float(height_target),
            'mean_vel_z_abs': float(mean_vel_z),
            'mean_roll_deg': float(np.degrees(mean_roll)),
            'mean_pitch_deg': float(np.degrees(mean_pitch)),
            'max_roll_deg': float(np.degrees(max_roll)),
            'max_pitch_deg': float(np.degrees(max_pitch)),
            'mean_action_rate': float(np.mean(action_rates)) if action_rates else 0.0,
            'max_action_rate': float(np.max(action_rates)) if action_rates else 0.0,
            'mean_power': float(mean_power),
            'power_std': float(std_power),
            'cost_of_transport': float(cot) if mean_vel > 0.01 else None,
            'mean_episode_length': float(np.mean(episode_lengths)) if episode_lengths else 0.0,
            'mean_episode_return': float(np.mean(episode_returns)) if episode_returns else 0.0,
            'num_episodes': len(episode_lengths),
            'recovery_success_rate_pct': recovery_summary.get('success_rate_pct'),
            'recovery_eligible_trials': recovery_summary.get('eligible_trials', 0),
            'mean_recovery_time_s': recovery_summary.get('mean_recovery_time_s'),
            'recovery_early_failure_rate_pct': recovery_summary.get('early_failure_rate_pct'),
            'recovery_prefall_success_rate_pct': recovery_summary.get('prefall', {}).get('success_rate_pct'),
            'recovery_prefall_trials': recovery_summary.get('prefall', {}).get('trials', 0),
            'recovery_prefall_mean_end_tilt_deg': recovery_summary.get('prefall', {}).get('mean_end_tilt_deg'),
            'transition_recovery_success_rate_pct': transition_recovery_summary.get('success_rate_pct'),
            'transition_recovery_eligible_trials': transition_recovery_summary.get('eligible_trials', 0),
            'mean_transition_recovery_time_s': transition_recovery_summary.get('mean_recovery_time_s'),
            'transition_recovery_early_failure_rate_pct': transition_recovery_summary.get('early_failure_rate_pct'),
            'transition_recovery_mean_max_tilt_deg': transition_recovery_summary.get('mean_max_tilt_deg'),
            'transition_prefall_success_rate_pct': transition_recovery_summary.get('prefall', {}).get('success_rate_pct'),
            'transition_prefall_trials': transition_recovery_summary.get('prefall', {}).get('trials', 0),
            'transition_prefall_mean_end_tilt_deg': transition_recovery_summary.get('prefall', {}).get('mean_end_tilt_deg'),
        },
        'recovery': recovery_summary,
        'transition_recovery': transition_recovery_summary,
        'command_mode_metrics': command_mode_summary,
        'config': {
            'control_type': env.cfg.control.control_type,
            'action_scale': float(env.cfg.control.action_scale),
            'decimation': int(env.cfg.control.decimation),
            'stiffness': {k: float(v) for k, v in env.cfg.control.stiffness.items()},
            'damping': {k: float(v) for k, v in env.cfg.control.damping.items()},
            'base_height_target': float(env.cfg.rewards.base_height_target),
            'soft_dof_pos_limit': float(env.cfg.rewards.soft_dof_pos_limit),
            'lin_vel_x': list(env.cfg.commands.ranges.lin_vel_x),
            'lin_vel_y': list(env.cfg.commands.ranges.lin_vel_y),
            'ang_vel_yaw': list(env.cfg.commands.ranges.ang_vel_yaw),
            'command_deadband': float(getattr(env.cfg.commands, 'command_deadband', 0.2)),
            'terrain': {
                'eval_mode': terrain_eval_mode,
                'walk_eval': bool(walk_eval),
                'mesh_type': str(getattr(env.cfg.terrain, 'mesh_type', 'unknown')),
                'terrain_profile': str(getattr(env.cfg.terrain, 'terrain_profile', 'default')),
                'measure_heights': bool(getattr(env.cfg.terrain, 'measure_heights', False)),
                'curriculum': bool(getattr(env.cfg.terrain, 'curriculum', False)),
                'num_rows': int(getattr(env.cfg.terrain, 'num_rows', 0)),
                'num_cols': int(getattr(env.cfg.terrain, 'num_cols', 0)),
                'terrain_length': float(getattr(env.cfg.terrain, 'terrain_length', 0.0)),
                'terrain_width': float(getattr(env.cfg.terrain, 'terrain_width', 0.0)),
                'terrain_proportions': list(getattr(env.cfg.terrain, 'terrain_proportions', [])),
                'spotmicro_slope_max': float(getattr(env.cfg.terrain, 'spotmicro_slope_max', 0.0)),
                'spotmicro_rolling_amp_max': float(getattr(env.cfg.terrain, 'spotmicro_rolling_amp_max', 0.0)),
            },
        },
        'reward_scales': {},
        'torque_per_joint': {},
        'joint_limit_near_pct': {},
        'gait': {
            'feet_contact_pct': {},
            'feet_air_pct': {},
        },
    }

    # reward scales 추출
    for attr in dir(env.cfg.rewards.scales):
        if not attr.startswith('_'):
            val = getattr(env.cfg.rewards.scales, attr)
            if isinstance(val, (int, float)):
                diagnostic_summary['reward_scales'][attr] = float(val)

    # 관절별 상세 (항목 5-A: 사용 범위/편향 추가)
    for j in range(num_joints):
        jname = joint_names[j]
        sat_pct = float(torque_saturation_count[j] / total_samples * 100)
        near_pct = float(dof_near_limit_count[j] / total_samples * 100)

        range_used = float(dof_pos_max_seen[j] - dof_pos_min_seen[j])
        range_total = float(urdf_upper[j] - urdf_lower[j])
        usage_pct = range_used / range_total * 100 if range_total > 0 else 0
        bias = float((dof_pos_max_seen[j] + dof_pos_min_seen[j]) / 2)

        diagnostic_summary['torque_per_joint'][jname] = {
            'saturation_pct': sat_pct,
            'max_torque_seen': float(torque_max_seen[j]),
            'torque_limit': float(torque_limits[j]),
            'range_used_rad': range_used,
            'range_total_rad': range_total,
            'range_usage_pct': float(usage_pct),
            'position_bias_rad': bias,
        }
        diagnostic_summary['joint_limit_near_pct'][jname] = near_pct

    # 보행 패턴
    for f in range(num_feet):
        fname = foot_names[f]
        total = feet_contact_count[f] + feet_air_count[f]
        diagnostic_summary['gait']['feet_contact_pct'][fname] = \
            float(feet_contact_count[f] / total * 100) if total > 0 else 0
        diagnostic_summary['gait']['feet_air_pct'][fname] = \
            float(feet_air_count[f] / total * 100) if total > 0 else 0

    # Gait 분석 데이터 (항목 3-D)
    diagnostic_summary['gait']['frequency_hz'] = float(gait_frequency)
    diagnostic_summary['gait']['period_steps'] = int(gait_period_steps)
    diagnostic_summary['gait']['diagonal_sync_pct'] = float(overall_trot * 100)
    diagnostic_summary['gait']['lr_asymmetry_pct'] = float(lr_asymmetry)

    # 에너지 상세 (항목 6-B)
    diagnostic_summary['energy'] = {
        'mean_power': float(mean_power),
        'peak_power': float(peak_power),
        'peak_mean_ratio': float(peak_power / mean_power) if mean_power > 0 else None,
        'cost_of_transport': float(cot) if mean_vel > 0.01 else None,
        'per_joint_power': {},
    }
    total_power_sum = per_joint_power_sum.sum()
    for j in range(num_joints):
        avg_power = per_joint_power_sum[j] / total_steps
        pct = per_joint_power_sum[j] / total_power_sum * 100 if total_power_sum > 0 else 0
        diagnostic_summary['energy']['per_joint_power'][joint_names[j]] = {
            'mean_power_w': float(avg_power),
            'share_pct': float(pct),
        }

    # JSON 저장
    json_save_path = os.path.join(diag_dir, 'diagnostic_summary.json')
    with open(json_save_path, 'w', encoding='utf-8') as jf:
        _json.dump(diagnostic_summary, jf, indent=2, ensure_ascii=False)
    print(f"  JSON 요약 저장: {json_save_path}")


# ============================================================
if __name__ == '__main__':
    import sys

    with_dr = '--with_dr' in sys.argv
    if with_dr:
        sys.argv.remove('--with_dr')

    prefall_eval = '--prefall-eval' in sys.argv
    if prefall_eval:
        sys.argv.remove('--prefall-eval')

    flat_eval = '--flat-eval' in sys.argv
    if flat_eval:
        sys.argv.remove('--flat-eval')

    terrain_curriculum_eval = '--terrain-curriculum-eval' in sys.argv
    if terrain_curriculum_eval:
        sys.argv.remove('--terrain-curriculum-eval')

    walk_eval = '--walk-eval' in sys.argv
    if walk_eval:
        sys.argv.remove('--walk-eval')
        
    ik_only = '--ik-only' in sys.argv
    if ik_only:
        sys.argv.remove('--ik-only')

    recovery_range_deg = 30.0 if prefall_eval else None
    i = 1
    while i < len(sys.argv):
        arg = sys.argv[i]
        if arg == '--recovery-range-deg' and i + 1 < len(sys.argv):
            recovery_range_deg = float(sys.argv[i + 1])
            del sys.argv[i:i + 2]
            continue
        if arg.startswith('--recovery-range-deg='):
            recovery_range_deg = float(arg.split('=', 1)[1])
            del sys.argv[i]
            continue
        i += 1

    args = get_args()

    # --- 항목 1-A: 추가 CLI 인자 파싱 ---
    checkpoint_path = None
    lightweight = False

    # get_args()가 처리하지 않는 인자를 수동으로 파싱
    argv = sys.argv[1:]
    i = 0
    while i < len(argv):
        if argv[i] == '--checkpoint' and i + 1 < len(argv):
            checkpoint_path = argv[i + 1]
            i += 2
        elif argv[i].startswith('--checkpoint='):
            checkpoint_path = argv[i].split('=', 1)[1]
            i += 1
        elif argv[i] == '--lightweight':
            lightweight = True
            i += 1
        else:
            i += 1

    run_diagnostic(
        args,
        checkpoint_path=checkpoint_path,
        lightweight=lightweight,
        with_dr=with_dr,
        recovery_range_deg=recovery_range_deg,
        flat_eval=flat_eval,
        terrain_curriculum_eval=terrain_curriculum_eval,
        walk_eval=walk_eval,
        ik_only=ik_only,
    )
