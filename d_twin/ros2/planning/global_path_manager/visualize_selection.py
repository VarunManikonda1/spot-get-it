#!/usr/bin/env python3
"""
visualize_selection.py

spot_01 후보 전체(흐림) 위에 4대 로봇 최종 선정 경로를 강조 표시한다.

사용법:
  python3 visualize_selection.py
  python3 visualize_selection.py --map map.yaml --config_dir ./config
  python3 visualize_selection.py --no_diagonal
"""

import argparse
import math
import os
import sys

import matplotlib.patches as patches
import matplotlib.pyplot as plt

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)

from global_path_manager.path_set import (
    MapConfig,
    build_path_set,
    select_paths,
    validate_path_set,
)

ROBOT_COLORS = {
    'spot_01': '#E74C3C',
    'spot_02': '#27AE60',
    'spot_03': '#2980B9',
    'spot_04': '#E67E22',
}

CANDIDATE_ROBOT = 'spot_01'


# ──────────────────────────────────────────────
# 대각선 필터
# ──────────────────────────────────────────────

def _first_seg_angle(path) -> float:
    wps = path.waypoints
    p0  = wps[0]
    p1  = wps[1] if len(wps) >= 3 else (
          (wps[0][0] + wps[-1][0]) / 2,
          (wps[0][1] + wps[-1][1]) / 2)
    dx, dy = p1[0] - p0[0], p1[1] - p0[1]
    return math.degrees(math.atan2(abs(dy), abs(dx))) if (dx or dy) else 45.0


def filter_diagonal(paths, lo=25.0, hi=65.0):
    return [p for p in paths if not (lo <= _first_seg_angle(p) <= hi)]


# ──────────────────────────────────────────────
# 맵 배경
# ──────────────────────────────────────────────

def draw_map(ax, cfg):
    ax.set_aspect('equal')
    ax.grid(True, alpha=0.10, linewidth=0.5)
    ax.set_xlabel('X (m)', fontsize=10)
    ax.set_ylabel('Y (m)', fontsize=10)

    r = cfg.room
    ax.add_patch(patches.Rectangle(
        (r['x_min'], r['y_min']),
        r['x_max'] - r['x_min'], r['y_max'] - r['y_min'],
        lw=2.5, ec='#2C3E50', fc='#F4F6F7', zorder=1))

    for obs in cfg.obstacles:
        ax.add_patch(patches.Rectangle(
            (obs['x_min'], obs['y_min']),
            obs['x_max'] - obs['x_min'],
            obs['y_max'] - obs['y_min'],
            lw=1.5, ec='#6C3483', fc='#D2B4DE', alpha=0.9,
            zorder=2, hatch='////'))

    ex, ey = cfg.end
    ax.plot(ex, ey, '*', color='#17202A', markersize=16, zorder=10, label='End')


# ──────────────────────────────────────────────
# 렌더
# ──────────────────────────────────────────────

def draw_candidates(ax, candidates):
    """spot_01 후보 전체를 흐리게."""
    for path in candidates:
        wps = path.waypoints
        xs  = [w[0] for w in wps]
        ys  = [w[1] for w in wps]
        ax.plot(xs, ys, '-', color='#95A5A6', lw=0.8, alpha=0.22, zorder=3)
        ax.plot(xs, ys, 'o', color='#95A5A6', markersize=2, alpha=0.18, zorder=3)


def draw_selected(ax, selected_all):
    """4대 로봇 선정 경로를 색상별로 강조."""
    for robot_id, path in selected_all.items():
        color = ROBOT_COLORS.get(robot_id, '#7F8C8D')
        wps   = path.waypoints
        xs    = [w[0] for w in wps]
        ys    = [w[1] for w in wps]

        # 글로우
        ax.plot(xs, ys, '-', color=color, lw=10, alpha=0.15, zorder=6)
        # 실선
        ax.plot(xs, ys, '-', color=color, lw=3.0, alpha=1.0,  zorder=7,
                label=f'{robot_id}  pivot={path.pivot:.2f}')
        # waypoint
        ax.plot(xs, ys, 'o', color=color, markersize=7,
                markeredgecolor='white', markeredgewidth=1.5, zorder=8)
        # 출발지 레이블
        sx, sy = wps[0]
        ax.annotate(robot_id, (sx, sy),
                    textcoords='offset points', xytext=(-20, 6),
                    fontsize=8, color=color, fontweight='bold', zorder=9)


# ──────────────────────────────────────────────
# 진입점
# ──────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--map',         default='map.yaml')
    parser.add_argument('--config_dir',  default=os.path.join(SCRIPT_DIR, 'config'))
    parser.add_argument('--out',         default='')
    parser.add_argument('--no_diagonal', action='store_true',
                        help='spot_01 후보에서 대각선 경로 제외')
    args = parser.parse_args()

    map_path = os.path.join(args.config_dir, args.map)
    if not os.path.exists(map_path):
        print(f'[ERROR] 맵 파일 없음: {map_path}')
        sys.exit(1)

    cfg = MapConfig(map_path)
    print(f'맵: {args.map}  로봇: {list(cfg.starts.keys())}')

    path_sets = build_path_set(cfg)
    validate_path_set(path_sets, cfg)

    # spot_01 후보
    candidates = [p for p in path_sets[CANDIDATE_ROBOT] if p.valid]
    if args.no_diagonal:
        before     = len(candidates)
        candidates = filter_diagonal(candidates)
        print(f'  → {CANDIDATE_ROBOT} 대각선 제외: {before} → {len(candidates)}개')
    else:
        print(f'  → {CANDIDATE_ROBOT} 후보: {len(candidates)}개')

    # 4대 선정
    selected_all = select_paths(path_sets, starts=cfg.starts, cfg=cfg)
    for rid, p in selected_all.items():
        print(f'  → {rid} 선정  pivot={p.pivot:.2f}  wp={len(p.waypoints)}')

    # 렌더
    fig, ax = plt.subplots(figsize=(14, 10))
    margin  = 1.0
    ax.set_xlim(cfg.room['x_min'] - margin, cfg.room['x_max'] + margin)
    ax.set_ylim(cfg.room['y_min'] - margin, cfg.room['y_max'] + margin)

    draw_map(ax, cfg)
    draw_candidates(ax, candidates)
    draw_selected(ax, selected_all)

    nd = '  (대각선 제외)' if args.no_diagonal else ''
    ax.set_title(
        f'{CANDIDATE_ROBOT} candidates ({len(candidates)}{nd}) + 4 robots selected',
        fontweight='bold', fontsize=13, pad=10)

    ax.legend(loc='upper left', fontsize=9, framealpha=0.9)
    plt.tight_layout()

    map_label = os.path.splitext(os.path.basename(args.map))[0]
    out_file  = args.out or f'selection_{map_label}.png'
    plt.savefig(out_file, dpi=150, bbox_inches='tight')
    plt.close(fig)
    print(f'저장: {out_file}')


if __name__ == '__main__':
    main()
