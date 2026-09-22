#!/usr/bin/env python3
"""
SpotMicro RL 실험 보고서 자동 생성 + Git 자동 커밋 (v3)
=====================================================

사용법:
  python experiment_report.py --task=spotmicro_test \
      --purpose "Step4: torques/action_rate 페널티 추가로 동작 부드럽게"

v3 추가 섹션:
  - 학습 추이 (Checkpoint 스냅샷) — 항목 1
  - 학습 곡선 분석 — 항목 4
  - Per-leg 분석 — 항목 2
  - Gait 분석 — 항목 3
  - 관절별 상세 — 항목 5
  - 에너지 효율 상세 — 항목 6
"""

import os
import sys
import json
import glob
import re
import subprocess
import argparse
from datetime import datetime
from pathlib import Path
import numpy as np

# ============================================================
# 설정
# ============================================================
PROJECT_ROOT = os.path.dirname(os.path.abspath(__file__))

LOGS_DIR = os.path.join(PROJECT_ROOT, 'legged_gym', 'logs')
DIAG_DIR = os.path.join(PROJECT_ROOT, 'legged_gym', 'diagnostics')
EXPERIMENTS_DIR = os.path.join(PROJECT_ROOT, 'experiments')
PURPOSE_FILE = os.path.join(PROJECT_ROOT, '.experiment_purpose.txt')


def parse_args():
    parser = argparse.ArgumentParser(description='RL 실험 보고서 자동 생성')
    parser.add_argument('--task', type=str, default='spotmicro_test',
                        help='legged_gym task 이름')
    parser.add_argument('--purpose', type=str, default=None,
                        help='이번 실험의 목적 (한 줄)')
    parser.add_argument('--no-git', action='store_true',
                        help='Git commit/push 건너뛰기')
    parser.add_argument('--no-push', action='store_true',
                        help='Git push만 건너뛰기 (commit은 함)')
    parser.add_argument('--exp-id', type=str, default=None,
                        help='실험 ID 직접 지정 (미지정시 자동 채번)')
    return parser.parse_args()


# ============================================================
# 1. 실험 ID 채번
# ============================================================
def get_next_experiment_id(experiments_dir):
    """experiments/ 폴더에서 마지막 번호를 찾아 +1"""
    os.makedirs(experiments_dir, exist_ok=True)
    existing = glob.glob(os.path.join(experiments_dir, 'exp*'))
    if not existing:
        return 1

    max_id = 0
    for f in existing:
        basename = os.path.basename(f)
        match = re.match(r'exp(\d+)', basename)
        if match:
            max_id = max(max_id, int(match.group(1)))
    return max_id + 1


# ============================================================
# 2. 목적(purpose) 읽기
# ============================================================
def get_purpose(args):
    if args.purpose:
        return args.purpose
    if os.path.exists(PURPOSE_FILE):
        with open(PURPOSE_FILE, 'r', encoding='utf-8') as f:
            purpose = f.read().strip()
        if purpose:
            return purpose
    return "TODO: 실험 목적을 여기에 작성하세요"


# ============================================================
# 3. Diagnostic JSON 읽기
# ============================================================
def load_diagnostic_summary(experiment_name):
    json_path = os.path.join(DIAG_DIR, experiment_name, 'diagnostic_summary.json')
    if not os.path.exists(json_path):
        print(f"[경고] {json_path} 없음.")
        print(f"  play_diagnostic.py 에 JSON 저장 패치를 적용했는지 확인하세요.")
        return None
    with open(json_path, 'r', encoding='utf-8') as f:
        return json.load(f)


# ============================================================
# 3-1. 중간 checkpoint 스냅샷 로드 (항목 1-F)
# ============================================================
def load_snapshots(experiment_name):
    """중간 checkpoint 스냅샷 JSON들을 읽어서 iter 순 정렬 반환"""
    pattern = os.path.join(DIAG_DIR, experiment_name, 'snapshot_iter*.json')
    files = sorted(glob.glob(pattern))  # 파일명 순 = iter 순
    snapshots = []
    for f in files:
        with open(f, 'r') as fp:
            snapshots.append(json.load(fp))
    return snapshots


# ============================================================
# 4. Tensorboard 스칼라 추출 (항목 4-B: values/steps 추가)
# ============================================================
def extract_tensorboard_data(experiment_name, run_name=None):
    try:
        from tensorboard.backend.event_processing.event_accumulator import EventAccumulator
    except ImportError:
        print("[경고] tensorboard 패키지 없음. pip install tensorboard")
        return None

    log_base = os.path.join(LOGS_DIR, experiment_name)
    if not os.path.exists(log_base):
        print(f"[경고] 로그 디렉토리 없음: {log_base}")
        return None

    if run_name:
        run_dirs = [os.path.join(log_base, run_name)]
    else:
        all_runs = sorted(
            [os.path.join(log_base, d) for d in os.listdir(log_base)
             if os.path.isdir(os.path.join(log_base, d))],
            key=os.path.getmtime
        )
        if not all_runs:
            print(f"[경고] run 디렉토리 없음: {log_base}")
            return None
        run_dirs = [all_runs[-1]]

    run_dir = run_dirs[0]
    run_name_actual = os.path.basename(run_dir)
    print(f"  Tensorboard 읽는 중: {run_dir}")

    event_files = glob.glob(os.path.join(run_dir, 'events.out.tfevents.*'))
    if not event_files:
        event_files = glob.glob(os.path.join(run_dir, '**', 'events.out.tfevents.*'), recursive=True)
    if not event_files:
        print(f"[경고] event 파일 없음: {run_dir}")
        return None

    ea = EventAccumulator(run_dir)
    ea.Reload()

    tb_data = {
        'run_name': run_name_actual,
        'run_dir': run_dir,
        'scalars': {},
    }

    scalar_tags = ea.Tags().get('scalars', [])
    for tag in scalar_tags:
        events = ea.Scalars(tag)
        if events:
            values = [e.value for e in events]
            steps = [e.step for e in events]
            tb_data['scalars'][tag] = {
                'final': values[-1],
                'max': max(values),
                'min': min(values),
                'mean_last_10pct': sum(values[int(len(values)*0.9):]) / max(1, len(values) - int(len(values)*0.9)),
                'total_steps': steps[-1] if steps else 0,
                'values': values,   # 항목 4-B 추가
                'steps': steps,     # 항목 4-B 추가
            }

    return tb_data


# ============================================================
# 4-C. 학습 곡선 형상 분석 (항목 4)
# ============================================================
def analyze_training_curve(tb_data):
    """TB 스칼라에서 학습 곡선 형상 지표 추출"""
    if not tb_data or not tb_data.get('scalars'):
        return {}

    result = {}

    # mean_reward 키 찾기
    reward_key = None
    for key in tb_data.get('scalars', {}):
        short = key.split('/')[-1] if '/' in key else key
        if short in ('mean_reward', 'mean_return', 'mean_episode_reward'):
            reward_key = key
            break
    if not reward_key:
        return result

    scalar = tb_data['scalars'][reward_key]
    values = scalar.get('values', [])
    steps = scalar.get('steps', [])
    if len(values) < 10:
        return result

    values = np.array(values)
    steps = np.array(steps)
    final_val = scalar['max']  # 최고 reward 기준

    # 1) 수렴 iter: 최종값의 90%에 처음 도달한 iter
    threshold_90 = final_val * 0.9
    convergence_iter = None
    for i, v in enumerate(values):
        if v >= threshold_90:
            convergence_iter = int(steps[i])
            break
    result['convergence_iter_90pct'] = convergence_iter

    # 2) 후반 안정성: 후반 50% 구간의 표준편차 / 평균
    half = len(values) // 2
    latter_half = values[half:]
    if len(latter_half) > 0 and np.mean(latter_half) != 0:
        stability = np.std(latter_half) / abs(np.mean(latter_half))
    else:
        stability = None
    result['latter_half_cv'] = float(stability) if stability is not None else None

    # 3) 정체 구간 탐지
    window = max(10, len(values) // 20)
    plateau_start = None
    plateau_length = 0
    for i in range(len(values) - window):
        segment = values[i:i+window]
        if np.std(segment) < 0.01 * abs(np.mean(segment) + 1e-8):
            if plateau_start is None:
                plateau_start = int(steps[i])
            plateau_length = int(steps[i+window-1] - steps[i])
        else:
            plateau_start = None
    result['plateau_detected'] = plateau_start is not None
    result['plateau_start_iter'] = plateau_start
    result['plateau_length'] = plateau_length

    # total_steps for convergence judgment
    result['total_steps'] = int(steps[-1]) if len(steps) > 0 else 0

    return result


# ============================================================
# 5. Tensorboard 그래프 저장
# ============================================================
def save_tensorboard_graphs(tb_data, save_dir):
    try:
        from tensorboard.backend.event_processing.event_accumulator import EventAccumulator
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        print("[경고] matplotlib 또는 tensorboard 없음. 그래프 건너뜀.")
        return []

    if not tb_data or 'run_dir' not in tb_data:
        return []

    ea = EventAccumulator(tb_data['run_dir'])
    ea.Reload()

    scalar_tags = ea.Tags().get('scalars', [])
    if not scalar_tags:
        return []

    os.makedirs(save_dir, exist_ok=True)
    saved_files = []

    fig, axes = plt.subplots(2, 1, figsize=(12, 8))
    fig.suptitle(f"Training Curves: {tb_data['run_name']}", fontsize=14)

    reward_tags = [t for t in scalar_tags if 'reward' in t.lower() or 'return' in t.lower()]
    length_tags = [t for t in scalar_tags if 'length' in t.lower() or 'episode_length' in t.lower()]

    for tag in reward_tags[:3]:
        events = ea.Scalars(tag)
        steps = [e.step for e in events]
        values = [e.value for e in events]
        label = tag.split('/')[-1] if '/' in tag else tag
        axes[0].plot(steps, values, alpha=0.8, label=label)
    axes[0].set_xlabel('Iteration')
    axes[0].set_ylabel('Reward')
    axes[0].set_title('Training Reward')
    axes[0].legend(fontsize=8)
    axes[0].grid(True, alpha=0.3)

    for tag in length_tags[:2]:
        events = ea.Scalars(tag)
        steps = [e.step for e in events]
        values = [e.value for e in events]
        label = tag.split('/')[-1] if '/' in tag else tag
        axes[1].plot(steps, values, alpha=0.8, label=label)
    axes[1].set_xlabel('Iteration')
    axes[1].set_ylabel('Episode Length')
    axes[1].set_title('Episode Length')
    axes[1].legend(fontsize=8)
    axes[1].grid(True, alpha=0.3)

    plt.tight_layout()
    path1 = os.path.join(save_dir, 'tb_training_curves.png')
    plt.savefig(path1, dpi=150)
    plt.close()
    saved_files.append(path1)

    rew_tags = [t for t in scalar_tags if t.startswith('rew_') or '/rew_' in t]
    if rew_tags:
        n_plots = len(rew_tags)
        cols = 3
        rows = (n_plots + cols - 1) // cols
        fig, axes = plt.subplots(rows, cols, figsize=(15, 4 * rows))
        fig.suptitle(f"Individual Rewards: {tb_data['run_name']}", fontsize=14)

        if rows == 1 and cols == 1:
            axes = [[axes]]
        elif rows == 1:
            axes = [axes]

        for i, tag in enumerate(rew_tags):
            r, c = i // cols, i % cols
            ax = axes[r][c] if rows > 1 else axes[0][c]
            events = ea.Scalars(tag)
            steps = [e.step for e in events]
            values = [e.value for e in events]
            label = tag.split('/')[-1] if '/' in tag else tag
            label = label.replace('rew_', '')
            ax.plot(steps, values, alpha=0.8)
            ax.set_title(label, fontsize=10)
            ax.grid(True, alpha=0.3)

        for i in range(n_plots, rows * cols):
            r, c = i // cols, i % cols
            ax = axes[r][c] if rows > 1 else axes[0][c]
            ax.set_visible(False)

        plt.tight_layout()
        path2 = os.path.join(save_dir, 'tb_individual_rewards.png')
        plt.savefig(path2, dpi=150)
        plt.close()
        saved_files.append(path2)

    return saved_files


def copy_final_model(experiment_name, dest_dir):
    import shutil
    log_base = os.path.join(LOGS_DIR, experiment_name)
    if not os.path.exists(log_base):
        return None
    runs = sorted(
        [os.path.join(log_base, d) for d in os.listdir(log_base)
         if os.path.isdir(os.path.join(log_base, d))],
        key=os.path.getmtime
    )
    if not runs:
        return None
    latest_run = runs[-1]
    models = sorted(glob.glob(os.path.join(latest_run, 'model_*.pt')))
    if not models:
        return None
    final_model = models[-1]
    dest_path = os.path.join(dest_dir, 'model_final.pt')
    shutil.copy2(final_model, dest_path)
    return dest_path


# ============================================================
# 6. Git Diff로 변경점 자동 감지
# ============================================================
def get_config_diff():
    try:
        result = subprocess.run(
            ['git', 'diff', 'HEAD', '--', '*.py'],
            capture_output=True, text=True, cwd=PROJECT_ROOT
        )
        if result.returncode != 0:
            result = subprocess.run(
                ['git', 'diff', '--cached', '--', '*.py'],
                capture_output=True, text=True, cwd=PROJECT_ROOT
            )
        diff_text = result.stdout.strip()
        if not diff_text:
            result = subprocess.run(
                ['git', 'diff', '--', '*.py'],
                capture_output=True, text=True, cwd=PROJECT_ROOT
            )
            diff_text = result.stdout.strip()
        return diff_text if diff_text else "(변경 사항 없음 또는 git 미설정)"
    except FileNotFoundError:
        return "(git이 설치되지 않음)"


def parse_config_changes(diff_text):
    if diff_text.startswith("("):
        return diff_text
    changes = []
    lines = diff_text.split('\n')
    for i, line in enumerate(lines):
        if line.startswith('+') and not line.startswith('+++'):
            content = line[1:].strip()
            if content and not content.startswith('#'):
                changes.append(f"  + {content}")
        elif line.startswith('-') and not line.startswith('---'):
            content = line[1:].strip()
            if content and not content.startswith('#'):
                changes.append(f"  - {content}")
    if not changes:
        return "(코드 변경 없음)"
    return '\n'.join(changes)


# ============================================================
# 7. 이전 실험과 비교
# ============================================================
def load_previous_experiment(experiments_dir, current_id):
    prev_id = current_id - 1
    if prev_id < 1:
        return None
    pattern = os.path.join(experiments_dir, f'exp{prev_id:03d}_*/*metrics.json')
    files = glob.glob(pattern)
    if files:
        with open(files[0], 'r', encoding='utf-8') as f:
            return json.load(f)
    return None


# ============================================================
# 8. Pass/Fail 자동 판정
# ============================================================
def _find_tilt_band(summary, label):
    for band in summary.get('tilt_bands', []) if summary else []:
        if band.get('label') == label:
            return band
    return {}


def auto_judge(metrics, run_name='', recovery_data=None, transition_recovery_data=None):
    if not metrics:
        return "⚠ 수치 데이터 없음 — 수동 판정 필요", []

    judgments = []
    all_pass = True
    terrain_mode = 'terrain' in run_name or 'slope' in run_name
    prefall_mode = (
        'prefall' in run_name
        or ('recovery' in run_name and not terrain_mode)
    )
    recovery_metrics_present = bool(recovery_data) or bool(transition_recovery_data)

    timeout = metrics.get('timeout_pct', 0)
    timeout_pass = 95 if prefall_mode else 80
    timeout_warn = 90 if prefall_mode else 60
    if timeout >= timeout_pass:
        judgments.append(f"✅ Timeout: {timeout:.1f}% (≥{timeout_pass}%)")
    elif timeout >= timeout_warn:
        judgments.append(f"⚠️ Timeout: {timeout:.1f}% ({timeout_warn}~{timeout_pass}%, 개선 필요)")
        if prefall_mode or terrain_mode:
            all_pass = False
    elif timeout >= 60:
        judgments.append(f"⚠️ Timeout: {timeout:.1f}% (60~{timeout_warn}%, 보통)")
        all_pass = False
    else:
        judgments.append(f"❌ Timeout: {timeout:.1f}% (<60%, 미달)")
        all_pass = False

    vel_err = metrics.get('vel_error_x', 999)
    if vel_err < 0.08:
        judgments.append(f"✅ 속도오차 X: {vel_err:.4f} m/s (<0.08)")
    elif vel_err < 0.12:
        judgments.append(f"⚠️ 속도오차 X: {vel_err:.4f} m/s (0.08~0.12, 보통)")
    else:
        judgments.append(f"❌ 속도오차 X: {vel_err:.4f} m/s (>0.12, 미달)")
        all_pass = False

    torque_sat = metrics.get('torque_saturation_pct', 0)
    if torque_sat < 10:
        judgments.append(f"✅ 토크포화: {torque_sat:.1f}% (<10%)")
    elif torque_sat < 40:
        judgments.append(f"⚠️ 토크포화: {torque_sat:.1f}% (10~40%)")
    else:
        judgments.append(f"❌ 토크포화: {torque_sat:.1f}% (>40%)")
        all_pass = False

    roll = metrics.get('mean_roll_deg', 0)
    pitch = metrics.get('mean_pitch_deg', 0)
    posture_pass = 10 if terrain_mode else 8
    posture_warn = 18 if terrain_mode else 15
    if roll < posture_pass and pitch < posture_pass:
        judgments.append(
            f"✅ 자세: roll {roll:.1f}°, pitch {pitch:.1f}° "
            f"(<{posture_pass}°)"
        )
    elif roll < posture_warn and pitch < posture_warn:
        judgments.append(
            f"⚠️ 자세: roll {roll:.1f}°, pitch {pitch:.1f}° "
            f"({posture_pass}~{posture_warn}°)"
        )
    else:
        judgments.append(
            f"❌ 자세: roll {roll:.1f}°, pitch {pitch:.1f}° "
            f"(>{posture_warn}°)"
        )
        all_pass = False

    early_death = metrics.get('early_death_pct', 0)
    early_death_pass = 5 if prefall_mode else 5
    if early_death < early_death_pass:
        judgments.append(f"✅ 조기종료: {early_death:.1f}% (<{early_death_pass}%)")
    elif early_death < 20:
        judgments.append(f"⚠️ 조기종료: {early_death:.1f}% (5~20%)")
        if prefall_mode or terrain_mode:
            all_pass = False
    else:
        judgments.append(f"❌ 조기종료: {early_death:.1f}% (>20%)")
        all_pass = False

    transition_sr = metrics.get('transition_recovery_success_rate_pct')
    if transition_sr is not None:
        if transition_sr >= 80:
            judgments.append(f"✅ 전환복구: {transition_sr:.1f}% (≥80%)")
        elif transition_sr >= 50:
            judgments.append(f"⚠️ 전환복구: {transition_sr:.1f}% (50~80%)")
            if prefall_mode:
                all_pass = False
        else:
            judgments.append(f"❌ 전환복구: {transition_sr:.1f}% (<50%)")
            if prefall_mode:
                all_pass = False

    prefall_trials = metrics.get('transition_prefall_trials', 0)
    prefall_sr = metrics.get('transition_prefall_success_rate_pct')
    if prefall_trials:
        prefall_pass = 70 if prefall_mode else 85
        prefall_warn = 60
        if prefall_sr is not None and prefall_sr >= prefall_pass:
            judgments.append(f"✅ 18도+ pre-fall 복구: {prefall_sr:.1f}% (≥{prefall_pass}%)")
        elif prefall_sr is not None and prefall_sr >= 60:
            judgments.append(f"⚠️ 18도+ pre-fall 복구: {prefall_sr:.1f}% ({prefall_warn}~{prefall_pass}%, 개선 필요)")
            if prefall_mode:
                all_pass = False
        elif prefall_sr is not None:
            judgments.append(f"❌ 18도+ pre-fall 복구: {prefall_sr:.1f}% (<{prefall_warn}%)")
            if prefall_mode:
                all_pass = False
    else:
        judgments.append("⚠️ 18도+ pre-fall 복구: trial 없음 (30도 목표 미검증)")
        if prefall_mode:
            all_pass = False

    if prefall_mode:
        reset_25_30 = _find_tilt_band(recovery_data, '25-30 deg')
        reset_sr = reset_25_30.get('success_rate_pct')
        reset_trials = reset_25_30.get('trials', 0)
        if reset_trials <= 0:
            judgments.append("❌ Reset 25-30도 복구: trial 없음")
            all_pass = False
        elif reset_sr is not None and reset_sr >= 75:
            judgments.append(f"✅ Reset 25-30도 복구: {reset_sr:.1f}% (≥75%, n={reset_trials})")
        elif reset_sr is not None and reset_sr >= 65:
            judgments.append(f"⚠️ Reset 25-30도 복구: {reset_sr:.1f}% (65~75%, n={reset_trials})")
            all_pass = False
        elif reset_sr is not None:
            judgments.append(f"❌ Reset 25-30도 복구: {reset_sr:.1f}% (<65%, n={reset_trials})")
            all_pass = False

        transition_25_30 = _find_tilt_band(transition_recovery_data, '25-30 deg')
        transition_sr = transition_25_30.get('success_rate_pct')
        transition_trials = transition_25_30.get('trials', 0)
        if transition_trials < 20:
            judgments.append(f"❌ Transition 25-30도 복구: trial 부족 (n={transition_trials}, 최소 20)")
            all_pass = False
        elif transition_sr is not None and transition_sr >= 65:
            judgments.append(f"✅ Transition 25-30도 복구: {transition_sr:.1f}% (≥65%, n={transition_trials})")
        elif transition_sr is not None and transition_sr >= 55:
            judgments.append(f"⚠️ Transition 25-30도 복구: {transition_sr:.1f}% (55~65%, n={transition_trials})")
            all_pass = False
        elif transition_sr is not None:
            judgments.append(f"❌ Transition 25-30도 복구: {transition_sr:.1f}% (<55%, n={transition_trials})")
            all_pass = False
    elif terrain_mode and recovery_metrics_present:
        judgments.append(
            "ℹ️ Recovery/pre-fall 지표는 참고값입니다 "
            "(terrain run 자동 PASS/FAIL 기준에서는 제외)"
        )

    overall = "✅ PASS" if all_pass else "❌ FAIL (일부 기준 미달)"
    return overall, judgments


# ============================================================
# 9. 보고서 생성
# ============================================================
def generate_report(exp_id, purpose, diag_data, tb_data, diff_text,
                    prev_data, tb_graphs, task_name):
    """마크다운 보고서 생성"""

    run_name = ''
    if tb_data:
        run_name = tb_data.get('run_name', '')
    elif diag_data:
        run_name = diag_data.get('run_name', '')

    run_name = re.sub(r'^[A-Z][a-z]{2}\d{2}_\d{2}-\d{2}-\d{2}_', '', run_name)
    timestamp = datetime.now().strftime('%Y-%m-%d %H:%M')
    date_str = datetime.now().strftime('%Y-%m-%d')

    # 파일명
    safe_run = re.sub(r'[^\w\-]', '_', run_name) if run_name else 'unknown'
    filename = f"exp{exp_id:03d}_{run_name}_report.md"
    json_filename = f"exp{exp_id:03d}_{run_name}_metrics.json"

    metrics = diag_data.get('metrics', {}) if diag_data else {}
    config_snapshot = diag_data.get('config', {}) if diag_data else {}
    reward_scales = diag_data.get('reward_scales', {}) if diag_data else {}
    
    command_mode_metrics = diag_data.get('command_mode_metrics', {}) if diag_data else {}
    recovery_data = diag_data.get('recovery', {}) if diag_data else {}
    transition_recovery_data = diag_data.get('transition_recovery', {}) if diag_data else {}

    # 자동 판정
    overall_judge, judgments = auto_judge(
        metrics,
        run_name,
        recovery_data=recovery_data,
        transition_recovery_data=transition_recovery_data,
    )

    # 변경점 요약
    changes_summary = parse_config_changes(diff_text)

    terrain_cfg = config_snapshot.get('terrain', {}) if config_snapshot else {}
    terrain_section = ""
    if terrain_cfg:
        terrain_section = f"""
### 지형 설정

| 항목 | 값 |
|------|-----|
| 평가 모드 | {terrain_cfg.get('eval_mode', 'N/A')} |
| walk_eval | {terrain_cfg.get('walk_eval', 'N/A')} |
| mesh_type | {terrain_cfg.get('mesh_type', 'N/A')} |
| terrain_profile | {terrain_cfg.get('terrain_profile', 'N/A')} |
| measure_heights | {terrain_cfg.get('measure_heights', 'N/A')} |
| grid | {terrain_cfg.get('num_rows', 'N/A')} x {terrain_cfg.get('num_cols', 'N/A')} |
| env 크기 | {terrain_cfg.get('terrain_length', 'N/A')} x {terrain_cfg.get('terrain_width', 'N/A')} m |
| terrain_proportions | {terrain_cfg.get('terrain_proportions', 'N/A')} |
| slope max | {terrain_cfg.get('spotmicro_slope_max', 'N/A')} |
| rolling amp max | {terrain_cfg.get('spotmicro_rolling_amp_max', 'N/A')} m |
"""

    # --- 이전 대비 비교표 ---
    comparison = ""
    if prev_data and prev_data.get('metrics'):
        pm = prev_data['metrics']
        comparison = f"""
## 이전 실험 대비 비교

| 지표 | 이전 (exp{exp_id-1:03d}) | 현재 (exp{exp_id:03d}) | 변화 |
|------|-------|-------|------|
| Timeout% | {pm.get('timeout_pct', 'N/A'):.1f}% | {metrics.get('timeout_pct', 'N/A'):.1f}% | {_delta(metrics.get('timeout_pct'), pm.get('timeout_pct'), '%', higher_better=True)} |
| 속도오차 X | {pm.get('vel_error_x', 'N/A'):.4f} | {metrics.get('vel_error_x', 'N/A'):.4f} | {_delta(metrics.get('vel_error_x'), pm.get('vel_error_x'), 'm/s', higher_better=False)} |
| 토크포화 | {pm.get('torque_saturation_pct', 'N/A'):.1f}% | {metrics.get('torque_saturation_pct', 'N/A'):.1f}% | {_delta(metrics.get('torque_saturation_pct'), pm.get('torque_saturation_pct'), '%', higher_better=False)} |
| Roll | {pm.get('mean_roll_deg', 'N/A'):.1f}° | {metrics.get('mean_roll_deg', 'N/A'):.1f}° | {_delta(metrics.get('mean_roll_deg'), pm.get('mean_roll_deg'), '°', higher_better=False)} |
| Pitch | {pm.get('mean_pitch_deg', 'N/A'):.1f}° | {metrics.get('mean_pitch_deg', 'N/A'):.1f}° | {_delta(metrics.get('mean_pitch_deg'), pm.get('mean_pitch_deg'), '°', higher_better=False)} |
| 평균 전력 | {pm.get('mean_power', 'N/A'):.4f}W | {metrics.get('mean_power', 'N/A'):.4f}W | {_delta(metrics.get('mean_power'), pm.get('mean_power'), 'W', higher_better=False)} |
"""

    # --- Tensorboard 요약 ---
    tb_summary = ""
    if tb_data and tb_data.get('scalars'):
        tb_rows = []
        for tag, vals in sorted(tb_data['scalars'].items()):
            short_tag = tag.split('/')[-1] if '/' in tag else tag
            tb_rows.append(
                f"| {short_tag} | {vals['final']:.4f} | "
                f"{vals['max']:.4f} | {vals['min']:.4f} | "
                f"{vals['mean_last_10pct']:.4f} |"
            )
        tb_table = '\n'.join(tb_rows)
        tb_summary = f"""
## Tensorboard 학습 곡선 요약

| 스칼라 | 최종값 | 최대 | 최소 | 후반10% 평균 |
|--------|--------|------|------|-------------|
{tb_table}

총 학습 iteration: {tb_data['scalars'].get(list(tb_data['scalars'].keys())[0], {}).get('total_steps', 'N/A')}
"""

    # --- 그래프 참조 ---
    graph_refs = ""
    exp_dir_rel = f"."
    if tb_graphs:
        graph_lines = []
        for gp in tb_graphs:
            gname = os.path.basename(gp)
            graph_lines.append(f"![{gname}]({exp_dir_rel}/{gname})")
        graph_refs = '\n'.join(graph_lines)

    diag_graph_dir = os.path.join(DIAG_DIR, task_name)
    diag_graphs_ref = ""
    if os.path.exists(diag_graph_dir):
        for img_name in ['diagnostic_report.png', 'joint_detail.png', 'action_smoothness.png', 'recovery_report.png']:
            img_path = os.path.join(diag_graph_dir, img_name)
            if os.path.exists(img_path):
                diag_graphs_ref += f"![{img_name}]({exp_dir_rel}/{img_name})\n"

    # --- Reward Scales 스냅샷 ---
    reward_table = ""
    if reward_scales:
        reward_rows = []
        for k, v in sorted(reward_scales.items()):
            if v != 0.0:
                reward_rows.append(f"| {k} | {v} |")
        if reward_rows:
            reward_table = "| Reward | Scale |\n|--------|-------|\n" + '\n'.join(reward_rows)

    # =====================================================
    # 신규 섹션 생성
    # =====================================================

    # --- 항목 1: 학습 추이 (Checkpoint 스냅샷) ---
    snapshots = load_snapshots(task_name)
    snapshot_section = ""
    if snapshots:
        snap_rows = []
        for s in snapshots:
            snap_rows.append(
                f"| {s.get('iter', '?')} "
                f"| {s.get('timeout_pct', 0):.1f} "
                f"| {s.get('vel_error_x', 0):.4f} "
                f"| {s.get('torque_saturation_pct', 0):.1f} "
                f"| {s.get('mean_roll_deg', 0):.1f} "
                f"| {s.get('mean_pitch_deg', 0):.1f} "
                f"| {s.get('mean_power', 0):.2f} "
                f"| {s.get('recovery_success_rate_pct', 0) if s.get('recovery_success_rate_pct') is not None else 0:.1f} "
                f"| {s.get('mean_recovery_time_s', 0) if s.get('mean_recovery_time_s') is not None else 0:.2f} "
                f"| {s.get('episode_return', 0):.1f} |"
            )
        snap_table = '\n'.join(snap_rows)
        snapshot_section = f"""
## 학습 추이 (Checkpoint 스냅샷)

| iter | Timeout% | 속도오차X | 토크포화% | Roll° | Pitch° | 전력(W) | Recovery% | 회복시간(s) | Return |
|------|----------|----------|----------|-------|--------|---------|-----------|-------------|--------|
{snap_table}
"""

    # --- 항목 4: 학습 곡선 분석 ---
    curve_section = ""
    curve_analysis = analyze_training_curve(tb_data)
    if curve_analysis:
        conv_iter = curve_analysis.get('convergence_iter_90pct')
        total_iter = curve_analysis.get('total_steps', 1)
        cv = curve_analysis.get('latter_half_cv')
        plateau = curve_analysis.get('plateau_detected', False)
        plateau_start = curve_analysis.get('plateau_start_iter')
        plateau_len = curve_analysis.get('plateau_length', 0)

        # 수렴 판정
        if conv_iter is not None and total_iter > 0:
            ratio = conv_iter / total_iter
            if ratio < 0.3:
                conv_judge = "빠름"
            elif ratio < 0.7:
                conv_judge = "보통"
            else:
                conv_judge = "느림"
            conv_str = f"{conv_iter}"
        else:
            conv_judge = "미수렴"
            conv_str = "N/A"

        # CV 판정
        if cv is not None:
            if cv < 0.05:
                cv_judge = "안정"
            elif cv < 0.15:
                cv_judge = "보통"
            else:
                cv_judge = "불안정"
            cv_str = f"{cv:.3f}"
        else:
            cv_judge = "N/A"
            cv_str = "N/A"

        # 정체 판정
        if plateau:
            plateau_str = f"있음 (iter {plateau_start}, {plateau_len} iter)"
        else:
            plateau_str = "없음"

        curve_section = f"""
## 학습 곡선 분석

| 지표 | 값 | 판정 |
|------|-----|------|
| 수렴 iter (90%) | {conv_str} | {conv_judge} |
| 후반 안정성 (CV) | {cv_str} | {cv_judge} |
| 정체 구간 | {plateau_str} | |
"""

    # --- 항목 2: Per-leg 분석 ---
    per_leg_section = ""
    gait_data = diag_data.get('gait', {}) if diag_data else {}
    contact_pct = gait_data.get('feet_contact_pct', {})
    air_pct = gait_data.get('feet_air_pct', {})

    if contact_pct:
        leg_rows = []
        for foot in contact_pct:
            c = contact_pct[foot]
            a = air_pct.get(foot, 0)
            leg_rows.append(f"| {foot} | {c:.1f}% | {a:.1f}% |")
        per_leg_table = "| 발 | 접촉% | 공중% |\n|-----|-------|------|\n" + '\n'.join(leg_rows)

        # 관절별 토크 포화 테이블
        torque_per_joint = diag_data.get('torque_per_joint', {}) if diag_data else {}
        per_joint_torque_table = ""
        if torque_per_joint:
            jt_rows = []
            for jname, jdata in torque_per_joint.items():
                sat = jdata.get('saturation_pct', 0)
                max_t = jdata.get('max_torque_seen', 0)
                limit = jdata.get('torque_limit', 0)
                status = "❌" if sat > 20 else "⚠️" if sat > 5 else "✅"
                jt_rows.append(f"| {jname} | {sat:.1f}% | {max_t:.3f} | {limit:.3f} | {status} |")
            per_joint_torque_table = (
                "| 관절 | 포화% | 최대토크 | 한계 | 상태 |\n"
                "|------|------|---------|------|------|\n"
                + '\n'.join(jt_rows)
            )

        per_leg_section = f"""
## Per-leg 분석

### 발별 접촉/공중 비율

{per_leg_table}

### 관절별 토크 포화

{per_joint_torque_table}
"""
    # --- Command mode별 yaw 추종 분석 ---
    command_mode_section = ""

    if command_mode_metrics:
        mode_rows = []

        mode_order = ['stand', 'forward', 'pure_turn', 'arc_turn', 'high_wz']

        for key in mode_order:
            m = command_mode_metrics.get(key, {})
            label = m.get('label', key)
            count = m.get('count', 0)
            ratio = m.get('ratio_pct', 0.0)

            err = m.get('mean_ang_error')
            cmd = m.get('mean_abs_cmd_yaw')
            actual = m.get('mean_abs_actual_yaw')

            err_str = f"{err:.4f}" if err is not None else "N/A"
            cmd_str = f"{cmd:.4f}" if cmd is not None else "N/A"
            actual_str = f"{actual:.4f}" if actual is not None else "N/A"

            # 간단 판정
            if err is None:
                status = "N/A"
            elif key == 'stand':
                status = "✅" if err < 0.03 else "⚠️" if err < 0.06 else "❌"
            else:
                status = "✅" if err < 0.08 else "⚠️" if err < 0.15 else "❌"

            mode_rows.append(
                f"| {label} | {ratio:.1f}% | {count} | {cmd_str} | {actual_str} | {err_str} | {status} |"
            )

        mode_table = '\n'.join(mode_rows)
        
        command_mode_section = f"""
## Command Mode별 회전 추종 분석

| 모드 | 비율 | 샘플 수 | 평균 abs(cmd_wz) | 평균 abs(actual_wz) | yaw 오차 | 상태 |
|------|------|---------|------------------|---------------------|----------|------|
{mode_table}

> 해석 기준: `제자리 회전`만 나쁘면 pure turn 학습/보행 패턴 문제, `큰 회전명령`만 나쁘면 yaw 명령 범위가 현재 토크/보폭 한계보다 큰 문제, `정지`가 나쁘면 stop drift 문제로 보면 됩니다.
        """


    # --- Recovery assist 분석 ---
    def _fmt_pct(value):
        return "N/A" if value is None else f"{value:.1f}%"

    def _fmt_num(value, suffix=""):
        return "N/A" if value is None else f"{value:.2f}{suffix}"

    def _tilt_band_table(summary, title):
        bands = summary.get('tilt_bands', []) if summary else []
        prefall = summary.get('prefall', {}) if summary else {}
        if not bands and not prefall:
            return ""

        rows = []
        for band in bands:
            rows.append(
                f"| {band.get('label', 'N/A')} | "
                f"{band.get('trials', 0)} | "
                f"{_fmt_pct(band.get('success_rate_pct'))} | "
                f"{_fmt_num(band.get('mean_end_tilt_deg'), '°')} | "
                f"{_fmt_num(band.get('mean_recovery_time_s'), 's')} |"
            )
        rows.append(
            f"| 18+ deg 전체 | "
            f"{prefall.get('trials', 0)} | "
            f"{_fmt_pct(prefall.get('success_rate_pct'))} | "
            f"{_fmt_num(prefall.get('mean_end_tilt_deg'), '°')} | "
            f"{_fmt_num(prefall.get('mean_recovery_time_s'), 's')} |"
        )

        return f"""
### {title} Tilt Band 분석

| Tilt 구간 | Trials | 성공률 | Horizon 후 평균 tilt | 평균 회복 시간 |
|-----------|--------|--------|----------------------|----------------|
{chr(10).join(rows)}

> 이번 pre-fall 목표는 특히 `18-25 deg`, `25-30 deg`, `18+ deg 전체` 행을 우선 봅니다. `25-30 deg` trial이 없으면 30도 근처 회복 성능은 아직 검증되지 않은 것입니다.
"""

    recovery_section = ""
    if recovery_data:
        sr = recovery_data.get('success_rate_pct')
        ert = recovery_data.get('early_failure_rate_pct')
        mrt = recovery_data.get('mean_recovery_time_s')
        eligible = recovery_data.get('eligible_trials', 0)
        total = recovery_data.get('total_trials', 0)
        success_count = recovery_data.get('success_count', 0)
        failure_count = recovery_data.get('failure_count', 0)

        sr_str = f"{sr:.1f}%" if sr is not None else "N/A"
        ert_str = f"{ert:.1f}%" if ert is not None else "N/A"
        mrt_str = f"{mrt:.3f}s" if mrt is not None else "N/A"
        recovery_band_section = _tilt_band_table(recovery_data, "Recovery")

        if sr is None:
            recovery_judge = "⚠️ eligible trial 없음"
        elif sr >= 80 and (ert is not None and ert < 10):
            recovery_judge = "✅ Recovery 안정적"
        elif sr >= 50:
            recovery_judge = "⚠️ 일부 회복 가능"
        else:
            recovery_judge = "❌ Recovery 부족"

        recovery_section = f"""
## Recovery Assist 분석

| 지표 | 값 |
|------|-----|
| 판정 | {recovery_judge} |
| Recovery 성공률 | {sr_str} |
| 성공/실패 | {success_count} / {failure_count} |
| Eligible trials | {eligible} / {total} |
| 평균 회복 시간 | {mrt_str} |
| 조기 실패율 | {ert_str} |
| 평가 horizon | {recovery_data.get('horizon_s', 'N/A')} s |
| 초기 tilt 기준 | {recovery_data.get('initial_tilt_threshold_deg', 'N/A')}° |
| 안정 기준 | roll/pitch < {recovery_data.get('stable_threshold_deg', 'N/A')}°, height > {recovery_data.get('min_height_m', 'N/A')}m |
| 평균 초기 tilt | {recovery_data.get('mean_initial_tilt_deg', 'N/A')}° |
| 평균 초기 roll/pitch | {recovery_data.get('mean_initial_roll_deg', 'N/A')}° / {recovery_data.get('mean_initial_pitch_deg', 'N/A')}° |
| 1초 후 평균 roll/pitch | {recovery_data.get('mean_end_roll_deg', 'N/A')}° / {recovery_data.get('mean_end_pitch_deg', 'N/A')}° |
| 1초 내 최대 roll/pitch 평균 | {recovery_data.get('mean_max_roll_first_1s_deg', 'N/A')}° / {recovery_data.get('mean_max_pitch_first_1s_deg', 'N/A')}° |

> 해석: `Recovery 성공률`은 초기 tilt가 기준 이상인 episode 중 1초 내 안정 자세로 복귀한 비율입니다. 조기 실패율이 높으면 reset 직후 바로 넘어지는 것이고, 평균 회복 시간이 짧을수록 위기 대응이 빠른 것입니다.
{recovery_band_section}
"""
    # --- 항목 3: Gait 분석 ---
    transition_recovery_section = ""
    if transition_recovery_data:
        sr = transition_recovery_data.get('success_rate_pct')
        ert = transition_recovery_data.get('early_failure_rate_pct')
        mrt = transition_recovery_data.get('mean_recovery_time_s')
        eligible = transition_recovery_data.get('eligible_trials', 0)
        total = transition_recovery_data.get('total_trials', 0)
        success_count = transition_recovery_data.get('success_count', 0)
        failure_count = transition_recovery_data.get('failure_count', 0)

        sr_str = f"{sr:.1f}%" if sr is not None else "N/A"
        ert_str = f"{ert:.1f}%" if ert is not None else "N/A"
        mrt_str = f"{mrt:.3f}s" if mrt is not None else "N/A"
        transition_band_section = _tilt_band_table(transition_recovery_data, "Transition Recovery")

        if sr is None:
            transition_judge = "⚠️ eligible trial 없음"
        elif sr >= 80 and (ert is not None and ert < 10):
            transition_judge = "✅ 전환 복구 안정적"
        elif sr >= 50:
            transition_judge = "⚠️ 일부 전환 복구 가능"
        else:
            transition_judge = "❌ 전환 복구 부족"

        transition_recovery_section = f"""
## Transition Recovery 분석

| 지표 | 값 |
|------|-----|
| 판정 | {transition_judge} |
| Transition recovery 성공률 | {sr_str} |
| 성공/실패 | {success_count} / {failure_count} |
| Eligible trials | {eligible} / {total} |
| 평균 회복 시간 | {mrt_str} |
| 조기 실패율 | {ert_str} |
| 평가 horizon | {transition_recovery_data.get('horizon_s', 'N/A')} s |
| push 후 eligible 기준 | max roll/pitch > {transition_recovery_data.get('initial_tilt_threshold_deg', 'N/A')}° |
| 안정 기준 | roll/pitch < {transition_recovery_data.get('stable_threshold_deg', 'N/A')}°, height > {transition_recovery_data.get('min_height_m', 'N/A')}m |
| 평균 최대 tilt | {transition_recovery_data.get('mean_max_tilt_deg', 'N/A')}° |
| horizon 후 평균 roll/pitch | {transition_recovery_data.get('mean_end_roll_deg', 'N/A')}° / {transition_recovery_data.get('mean_end_pitch_deg', 'N/A')}° |

> 해석: `Transition Recovery`는 학습 중 push/roll-pitch angular impulse가 들어간 뒤 일정 시간 안에 자세가 안정 기준으로 돌아오는지를 봅니다.
{transition_band_section}
"""

    # --- 항목 3: Gait 분석 ---
    gait_section = ""
    if gait_data:
        gait_freq = gait_data.get('frequency_hz', 0)
        gait_period = gait_data.get('period_steps', 0)
        diag_sync = gait_data.get('diagonal_sync_pct', 0)
        lr_asym = gait_data.get('lr_asymmetry_pct', 0)

        # 대각 동기화 판정
        if diag_sync >= 70:
            sync_judge = "✅ Trot 패턴"
        elif diag_sync >= 50:
            sync_judge = "⚠️ 부분적 Trot"
        else:
            sync_judge = "다른 Gait"

        # L/R 비대칭 판정
        if lr_asym < 3:
            asym_judge = "✅ 대칭"
        elif lr_asym < 10:
            asym_judge = "⚠️ 약간 비대칭"
        else:
            asym_judge = "❌ 비대칭"

        gait_judge = f"{sync_judge} / {asym_judge}"

        freq_str = f"{gait_freq:.2f}" if gait_freq > 0 else "측정 불가"
        period_str = f"{gait_period}" if gait_period > 0 else "측정 불가"

        gait_section = f"""
## Gait 분석

| 지표 | 값 |
|------|-----|
| Gait 주파수 | {freq_str} Hz |
| Gait 주기 | {period_str} steps |
| 대각 동기화율 | {diag_sync:.1f}% |
| L/R 비대칭 | {lr_asym:.1f}%p |
| 판정 | {gait_judge} |
"""

    # --- 항목 5: 관절별 상세 ---
    joint_detail_section = ""
    torque_per_joint = diag_data.get('torque_per_joint', {}) if diag_data else {}
    joint_limit_near = diag_data.get('joint_limit_near_pct', {}) if diag_data else {}
    if torque_per_joint:
        jd_rows = []
        for jname, jdata in torque_per_joint.items():
            sat = jdata.get('saturation_pct', 0)
            usage = jdata.get('range_usage_pct', 0)
            bias = jdata.get('position_bias_rad', 0)
            near = joint_limit_near.get(jname, 0)

            # 상태 판정
            if sat > 20 or abs(bias) > 0.1 or near > 10:
                status = "⚠️"
            else:
                status = "✅"

            jd_rows.append(
                f"| {jname} | {sat:.1f}% | {usage:.0f}% | {bias:+.3f} | {near:.1f}% | {status} |"
            )
        jd_table = '\n'.join(jd_rows)
        joint_detail_section = f"""
## 관절별 상세

| 관절 | 토크포화% | 사용범위% | 편향(rad) | 한계근접% | 상태 |
|------|----------|----------|----------|----------|------|
{jd_table}
"""

    # --- 항목 6: 에너지 효율 상세 ---
    energy_section = ""
    energy_data = diag_data.get('energy', {}) if diag_data else {}
    if energy_data:
        e_mean = energy_data.get('mean_power', 0)
        e_peak = energy_data.get('peak_power', 0)
        e_ratio = energy_data.get('peak_mean_ratio')
        e_cot = energy_data.get('cost_of_transport')

        ratio_str = f"{e_ratio:.1f}x" if e_ratio is not None else "N/A"
        cot_str = f"{e_cot:.2f}" if e_cot is not None else "N/A"

        # 관절별 전력 분배
        per_jp = energy_data.get('per_joint_power', {})
        jp_table = ""
        if per_jp:
            jp_rows = []
            # 전력 비율 순으로 정렬
            sorted_jp = sorted(per_jp.items(), key=lambda x: x[1].get('share_pct', 0), reverse=True)
            for jname, jpdata in sorted_jp:
                jp_rows.append(
                    f"| {jname} | {jpdata.get('mean_power_w', 0):.3f} | {jpdata.get('share_pct', 0):.1f}% |"
                )
            jp_table = (
                "\n### 관절별 전력 분배\n\n"
                "| 관절 | 평균전력(W) | 비율% |\n"
                "|------|-----------|-------|\n"
                + '\n'.join(jp_rows)
            )

        energy_section = f"""
## 에너지 효율 상세

| 지표 | 값 |
|------|-----|
| 평균 전력 | {e_mean:.2f} W |
| 피크 전력 | {e_peak:.2f} W |
| 피크/평균 비율 | {ratio_str} |
| CoT | {cot_str} |
{jp_table}
"""

    # --- 보고서 조립 ---
    report = f"""# 실험 {exp_id:03d}: {run_name}

- **날짜:** {timestamp}
- **Task:** {task_name}
- **Run name:** `{run_name}`
- **판정:** {overall_judge}

---

## 실험 목적

{purpose}

---

## 변경점 (이전 커밋 대비)

```diff
{diff_text[:3000]}
```

**변경 요약:**
{changes_summary}

---

## 현재 Reward Scales

{reward_table if reward_table else "(데이터 없음 — diagnostic JSON 확인)"}

---

## 진단 결과 (Diagnostic)

{terrain_section}

### 핵심 지표

{chr(10).join('- ' + j for j in judgments) if judgments else "(데이터 없음)"}

### 상세 수치

| 지표 | 값 |
|------|-----|
| Timeout% | {metrics.get('timeout_pct', 'N/A')} |
| 조기종료% | {metrics.get('early_death_pct', 'N/A')} |
| 속도오차 X | {metrics.get('vel_error_x', 'N/A')} m/s |
| 속도오차 Y | {metrics.get('vel_error_y', 'N/A')} m/s |
| 각속도오차 | {metrics.get('ang_vel_error', 'N/A')} rad/s |
| 토크포화% | {metrics.get('torque_saturation_pct', 'N/A')} |
| 평균 높이 | {metrics.get('mean_height', 'N/A')} m |
| Roll (평균) | {metrics.get('mean_roll_deg', 'N/A')}° |
| Pitch (평균) | {metrics.get('mean_pitch_deg', 'N/A')}° |
| Action Rate | {metrics.get('mean_action_rate', 'N/A')} |
| 평균 전력 | {metrics.get('mean_power', 'N/A')} W |
| CoT | {metrics.get('cost_of_transport', 'N/A')} |
| Recovery 성공률 | {metrics.get('recovery_success_rate_pct', 'N/A')}% |
| Recovery eligible trials | {metrics.get('recovery_eligible_trials', 'N/A')} |
| 평균 회복 시간 | {metrics.get('mean_recovery_time_s', 'N/A')} s |
| Recovery 조기 실패율 | {metrics.get('recovery_early_failure_rate_pct', 'N/A')}% |
| Recovery 18도+ 성공률 | {metrics.get('recovery_prefall_success_rate_pct', 'N/A')}% |
| Recovery 18도+ trials | {metrics.get('recovery_prefall_trials', 'N/A')} |
| Recovery 18도+ horizon 후 tilt | {metrics.get('recovery_prefall_mean_end_tilt_deg', 'N/A')}° |
| Transition recovery 성공률 | {metrics.get('transition_recovery_success_rate_pct', 'N/A')}% |
| Transition recovery eligible trials | {metrics.get('transition_recovery_eligible_trials', 'N/A')} |
| 평균 Transition recovery 시간 | {metrics.get('mean_transition_recovery_time_s', 'N/A')} s |
| Transition 18도+ 성공률 | {metrics.get('transition_prefall_success_rate_pct', 'N/A')}% |
| Transition 18도+ trials | {metrics.get('transition_prefall_trials', 'N/A')} |
| Transition 18도+ horizon 후 tilt | {metrics.get('transition_prefall_mean_end_tilt_deg', 'N/A')}° |
{recovery_section}
{transition_recovery_section}
{command_mode_section}
{snapshot_section}
{curve_section}
{per_leg_section}
{gait_section}
{joint_detail_section}
{energy_section}
{comparison}
{tb_summary}

---

## 그래프

### Tensorboard 학습 곡선
{graph_refs if graph_refs else "(그래프 없음)"}

### Diagnostic 결과
{diag_graphs_ref if diag_graphs_ref else "(그래프 없음)"}

---

## 결론 및 다음 실험 방향

**자동 판정:** {overall_judge}

{_auto_conclusion(metrics, judgments)}

> 💡 위 결론은 자동 생성되었습니다. 필요시 수정하세요.

---

## 메모

_(추가 메모가 있으면 여기에 작성)_

"""
    return report, filename, json_filename


def _delta(current, previous, unit, higher_better=True):
    if current is None or previous is None:
        return "N/A"
    try:
        diff = float(current) - float(previous)
        if abs(diff) < 0.0001:
            return "→ 유지"
        arrow = "↑" if diff > 0 else "↓"
        good = (diff > 0) == higher_better
        emoji = "✅" if good else "⚠️"
        return f"{emoji} {arrow} {abs(diff):.4f}{unit}"
    except (TypeError, ValueError):
        return "N/A"


def _auto_conclusion(metrics, judgments):
    if not metrics:
        return "데이터 없음. play_diagnostic.py를 실행 후 다시 시도하세요."

    issues = [j for j in judgments if j.startswith('❌')]
    warnings = [j for j in judgments if j.startswith('⚠️')]
    passes = [j for j in judgments if j.startswith('✅')]

    lines = []

    if not issues and not warnings:
        lines.append("모든 기준을 통과했습니다. 다음 Step으로 진행 가능합니다.")
    elif not issues:
        lines.append("대부분 기준을 통과했으나 일부 항목이 보통 수준입니다.")
        for w in warnings:
            lines.append(f"  - {w}")
    else:
        lines.append("일부 기준을 통과하지 못했습니다. 조정이 필요합니다.")
        for i in issues:
            lines.append(f"  - {i}")

        timeout = metrics.get('timeout_pct', 100)
        if timeout < 60:
            lines.append("")
            lines.append("**제안:** Timeout이 낮습니다. 최근 추가한 페널티를 절반으로 줄여보세요.")

        vel_err = metrics.get('vel_error_x', 0)
        if vel_err > 0.2:
            lines.append("**제안:** 속도 추종이 미흡합니다. tracking_lin_vel 가중치를 높여보세요.")

        torque_sat = metrics.get('torque_saturation_pct', 0)
        if torque_sat > 40:
            lines.append("**제안:** 토크 포화가 심합니다. action_scale을 줄이거나 Kp를 낮춰보세요.")

    return '\n'.join(lines)


# ============================================================
# 10. EXPERIMENT_LOG.md 업데이트
# ============================================================
def update_experiment_log(exp_id, run_name, purpose, metrics, overall_judge):
    log_path = os.path.join(EXPERIMENTS_DIR, 'EXPERIMENT_LOG.md')
    date_str = datetime.now().strftime('%m/%d')
    timeout = f"{metrics.get('timeout_pct', 'N/A'):.1f}%" if metrics else "N/A"
    vel_err = f"{metrics.get('vel_error_x', 'N/A'):.3f}" if metrics else "N/A"
    torque = f"{metrics.get('torque_saturation_pct', 'N/A'):.1f}%" if metrics else "N/A"
    result = "✅" if "PASS" in overall_judge else "❌"

    short_purpose = purpose[:40] + "..." if len(purpose) > 40 else purpose
    new_row = (
        f"| {exp_id:03d} | {date_str} | {run_name} | {short_purpose} "
        f"| {timeout} | {vel_err} | {torque} | {result} |"
    )

    if os.path.exists(log_path):
        with open(log_path, 'r', encoding='utf-8') as f:
            content = f.read()
        content = content.rstrip() + '\n' + new_row + '\n'
    else:
        header = """# SpotMicro RL 실험 로그

| # | 날짜 | Run Name | 핵심 변경 | Timeout% | 속도오차X | 토크포화 | 결과 |
|---|------|----------|----------|----------|----------|---------|------|
"""
        content = header + new_row + '\n'

    with open(log_path, 'w', encoding='utf-8') as f:
        f.write(content)
    print(f"  실험 로그 업데이트: {log_path}")


# ============================================================
# 11. Git Commit & Push
# ============================================================
def git_commit_push(exp_id, run_name, overall_judge, no_push=False):
    try:
        subprocess.run(['git', 'add', '-A'], cwd=PROJECT_ROOT, check=True)

        result_tag = "PASS" if "PASS" in overall_judge else "FAIL"
        msg = f"[exp{exp_id:03d}] {run_name} — {result_tag}"

        subprocess.run(
            ['git', 'commit', '-m', msg],
            cwd=PROJECT_ROOT, check=True
        )
        print(f"  Git commit: {msg}")

        if not no_push:
            result = subprocess.run(
                ['git', 'push'],
                cwd=PROJECT_ROOT, capture_output=True, text=True
            )
            if result.returncode == 0:
                print(f"  Git push 완료!")
            else:
                print(f"  Git push 실패: {result.stderr}")
                print(f"  수동으로 'git push' 해주세요.")
        else:
            print(f"  Git push 건너뜀 (--no-push)")

    except subprocess.CalledProcessError as e:
        print(f"  Git 에러: {e}")
        print(f"  수동으로 커밋해주세요.")
    except FileNotFoundError:
        print(f"  Git이 설치되지 않았습니다.")


# ============================================================
# MAIN
# ============================================================
def main():
    args = parse_args()

    print("\n" + "=" * 60)
    print("  SpotMicro RL 실험 보고서 자동 생성 v3")
    print("=" * 60)

    # --- 실험 ID ---
    if args.exp_id:
        exp_id = int(args.exp_id)
    else:
        exp_id = get_next_experiment_id(EXPERIMENTS_DIR)
    print(f"\n  실험 ID: exp{exp_id:03d}")

    # --- 목적 ---
    purpose = get_purpose(args)
    print(f"  목적: {purpose}")

    # --- Diagnostic JSON ---
    print(f"\n[1/6] Diagnostic 데이터 로드...")
    diag_data = load_diagnostic_summary(args.task)

    # --- Tensorboard ---
    print(f"\n[2/6] Tensorboard 데이터 추출...")
    tb_data = extract_tensorboard_data(args.task)

    # --- 보고서용 그래프 저장 ---
    print(f"\n[3/6] 그래프 저장...")
    metrics = diag_data.get('metrics', {}) if diag_data else {}
    run_name = ''
    if tb_data:
        run_name = tb_data.get('run_name', '')
    elif diag_data:
        run_name = diag_data.get('run_name', '')

    run_name = re.sub(r'^[A-Z][a-z]{2}\d{2}_\d{2}-\d{2}-\d{2}_', '', run_name)
    safe_run = re.sub(r'[^\w\-]', '_', run_name) if run_name else 'unknown'
    exp_assets_dir = os.path.join(EXPERIMENTS_DIR, f"exp{exp_id:03d}_{run_name}")
    os.makedirs(exp_assets_dir, exist_ok=True)

    tb_graphs = save_tensorboard_graphs(tb_data, exp_assets_dir)

    # 최종 모델 복사
    model_path = copy_final_model(args.task, exp_assets_dir)
    if model_path:
        print(f"    최종 모델 복사: {os.path.basename(model_path)}")

    # diagnostic 그래프 복사
    diag_graph_dir = os.path.join(DIAG_DIR, args.task)
    if os.path.exists(diag_graph_dir):
        import shutil
        for img_name in ['diagnostic_report.png', 'joint_detail.png', 'action_smoothness.png']:
            src = os.path.join(diag_graph_dir, img_name)
            if os.path.exists(src):
                shutil.copy2(src, os.path.join(exp_assets_dir, img_name))
                print(f"    복사: {img_name}")

    # --- Config Diff ---
    print(f"\n[4/6] Config 변경점 분석...")
    diff_text = get_config_diff()

    # --- 이전 실험 비교 ---
    prev_data = load_previous_experiment(EXPERIMENTS_DIR, exp_id)

    # --- 보고서 생성 ---
    print(f"\n[5/6] 보고서 생성...")
    report, md_filename, json_filename = generate_report(
        exp_id, purpose, diag_data, tb_data, diff_text,
        prev_data, tb_graphs, args.task
    )

    # 보고서 저장
    md_path = os.path.join(exp_assets_dir, md_filename)
    with open(md_path, 'w', encoding='utf-8') as f:
        f.write(report)
    print(f"    보고서: {md_path}")

    # JSON 사이드카 저장
    json_path = os.path.join(exp_assets_dir, json_filename)
    sidecar = {
        'exp_id': exp_id,
        'run_name': run_name,
        'purpose': purpose,
        'timestamp': datetime.now().isoformat(),
        'metrics': metrics,
        'recovery': diag_data.get('recovery', {}) if diag_data else {},
        'transition_recovery': diag_data.get('transition_recovery', {}) if diag_data else {},
    }
    with open(json_path, 'w', encoding='utf-8') as f:
        json.dump(sidecar, f, indent=2, ensure_ascii=False)

    # 실험 로그 업데이트
    overall_judge, _ = auto_judge(metrics, run_name)
    update_experiment_log(exp_id, run_name, purpose, metrics, overall_judge)

    # 임시 파일 정리
    if os.path.exists(PURPOSE_FILE):
        os.remove(PURPOSE_FILE)
        print(f"    .experiment_purpose.txt 삭제")

    # --- Git ---
    if not args.no_git:
        print(f"\n[6/6] Git commit & push...")
        git_commit_push(exp_id, run_name, overall_judge, no_push=args.no_push)
    else:
        print(f"\n[6/6] Git 건너뜀 (--no-git)")

    # --- 완료 ---
    print(f"\n{'=' * 60}")
    print(f"  ✅ 완료! 보고서: {md_path}")
    print(f"{'=' * 60}\n")


if __name__ == '__main__':
    main()
