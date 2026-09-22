#!/usr/bin/env python3
"""
visualize_candidates.py

특정 로봇의 모든 경로 후보를 한 화면에 시각화한다.
유효 경로(valid=True)  : 컬러맵 + 실선
무효 경로(valid=False) : 회색 점선 (기본 숨김, --show_invalid 로 표시)

선택 모드 (기본: 전체 표시):
  --top N      pivot 상위 N개
  --diverse N  waypoint 무게중심 기반 Farthest-First 로 공간적으로 분산된 N개

사용법:
  python3 visualize_candidates.py
  python3 visualize_candidates.py --robot spot_02
  python3 visualize_candidates.py --diverse 8
  python3 visualize_candidates.py --top 10
  python3 visualize_candidates.py --show_invalid
"""

import argparse
import math
import os
import sys

import matplotlib.cm as cm
import matplotlib.patches as patches
import matplotlib.pyplot as plt
import numpy as np

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)

from global_path_manager.path_set import (
    GRID_STEP,
    MapConfig,
    build_path_set,
    validate_path_set,
)

INTERP_MAX_DIST = 0.5


# ──────────────────────────────────────────────
# 경로 선택 전략
# ──────────────────────────────────────────────

def _centroid(path) -> np.ndarray:
    """경로 waypoint 들의 무게중심 (x, y)."""
    wps = np.array(path.waypoints)   # (N, 2)
    return wps.mean(axis=0)



def filter_diagonal(paths, lo: float = 25.0, hi: float = 65.0):
    """
    첫 번째 구간(start → waypoints[1]) 방향각이 lo°~hi° 사이인
    대각선 경로를 제외하고 반환.
    waypoint 가 2개뿐인 경로(구간 1개)는 중간점 기준으로 판정.
    """
    def _first_seg_angle(path) -> float:
        wps = path.waypoints
        if len(wps) >= 3:
            p0, p1 = wps[0], wps[1]
        else:
            # 중간점 사용
            p0 = wps[0]
            p1 = ((wps[0][0] + wps[-1][0]) / 2,
                  (wps[0][1] + wps[-1][1]) / 2)
        dx = p1[0] - p0[0]
        dy = p1[1] - p0[1]
        if dx == 0 and dy == 0:
            return 45.0
        return math.degrees(math.atan2(abs(dy), abs(dx)))

    return [p for p in paths if not (lo <= _first_seg_angle(p) <= hi)]


def select_top(valid_paths, n: int):
    """pivot 내림차순 상위 N개. n=0 이면 전부."""
    ranked = sorted(valid_paths, key=lambda p: p.pivot, reverse=True)
    return ranked[:n] if n > 0 else ranked


def select_diverse(valid_paths, n: int):
    """
    Farthest-First Traversal (공간 분산 최대화).

    1. pivot 최고 경로를 seed 로 선택.
    2. 매 반복마다 '이미 선택된 경로들과의 최소 거리'가 가장 큰 경로를 추가.
    거리 = 두 경로 무게중심 간 유클리드 거리.
    """
    if not valid_paths:
        return []

    n = min(n, len(valid_paths))
    centroids = np.array([_centroid(p) for p in valid_paths])  # (M, 2)

    # seed: pivot 최고
    seed_idx = max(range(len(valid_paths)),
                   key=lambda i: valid_paths[i].pivot)
    selected_idx = [seed_idx]
    remaining    = set(range(len(valid_paths))) - {seed_idx}

    while len(selected_idx) < n and remaining:
        sel_c = centroids[selected_idx]          # (k, 2)
        best_idx, best_dist = -1, -1.0

        for i in remaining:
            # 선택된 경로들과의 최소 거리
            dists    = np.linalg.norm(sel_c - centroids[i], axis=1)
            min_dist = dists.min()
            if min_dist > best_dist:
                best_dist, best_idx = min_dist, i

        selected_idx.append(best_idx)
        remaining.discard(best_idx)

    return [valid_paths[i] for i in selected_idx]


# ──────────────────────────────────────────────
# 맵 배경
# ──────────────────────────────────────────────

def draw_map(ax, cfg: MapConfig):
    ax.set_aspect('equal')
    ax.grid(True, alpha=0.12, linewidth=0.5)
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
    ax.plot(ex, ey, '*', color='#17202A', markersize=16, zorder=10,
            label='End', clip_on=False)


# ──────────────────────────────────────────────
# 경로 렌더
# ──────────────────────────────────────────────

def draw_candidates(ax, paths, show_invalid: bool,
                    selected_valid, target_robot: str, mode_label: str):

    invalid_paths = [p for p in paths if not p.valid]

    # ── 무효 경로 (배경) ──────────────────────
    if show_invalid:
        for path in invalid_paths:
            wps = path.waypoints
            xs, ys = [w[0] for w in wps], [w[1] for w in wps]
            ax.plot(xs, ys, '--', color='#AEB6BF', lw=1.0,
                    alpha=0.35, zorder=3)
            ax.plot(xs, ys, 'o', color='#AEB6BF', markersize=3,
                    alpha=0.35, zorder=3)

    # ── 선택된 유효 경로 ──────────────────────
    n = len(selected_valid)
    cmap = cm.get_cmap('plasma', max(n, 1))

    for rank, path in enumerate(selected_valid):
        wps   = path.waypoints
        color = cmap(rank / max(n - 1, 1))
        alpha = max(0.35, 1.0 - rank / max(n, 1) * 0.55)

        xs, ys = [w[0] for w in wps], [w[1] for w in wps]
        ax.plot(xs, ys, '-', color=color, lw=2.2, alpha=alpha, zorder=5)
        ax.plot(xs, ys, 'o', color=color, markersize=5,
                alpha=alpha, zorder=6)



    # ── 출발지 ────────────────────────────────
    if selected_valid:
        sx, sy = selected_valid[0].waypoints[0]
        ax.plot(sx, sy, 's', color='#E74C3C', markersize=10,
                markeredgecolor='white', markeredgewidth=1.5,
                zorder=10, label=f'{target_robot} start')


# ──────────────────────────────────────────────
# 컬러바
# ──────────────────────────────────────────────

def add_colorbar(fig, ax, n: int, label: str):
    cmap = cm.get_cmap('plasma', max(n, 1))
    sm   = plt.cm.ScalarMappable(
        cmap=cmap,
        norm=plt.Normalize(vmin=1, vmax=max(n, 1)))
    sm.set_array([])
    cbar = fig.colorbar(sm, ax=ax, fraction=0.03, pad=0.02)
    cbar.set_label(label, fontsize=9)


# ──────────────────────────────────────────────
# 진입점
# ──────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description='로봇 경로 후보 시각화')
    parser.add_argument('--robot',        default='spot_01',
                        help='대상 로봇 ID (기본: spot_01)')
    parser.add_argument('--map',          default='map.yaml')
    parser.add_argument('--config_dir',   default=os.path.join(SCRIPT_DIR, 'config'))
    parser.add_argument('--out',          default='',
                        help='출력 PNG 파일명 (기본 자동 생성)')
    parser.add_argument('--show_invalid', action='store_true',
                        help='무효 경로(회색 점선)도 표시')

    group = parser.add_mutually_exclusive_group()
    group.add_argument('--top',     type=int, default=0,
                       metavar='N', help='pivot 상위 N개 표시')
    group.add_argument('--diverse', type=int, default=0,
                       metavar='N', help='공간 분산 최대화 (Farthest-First) N개 표시')
    parser.add_argument('--no_diagonal', action='store_true',
                        help='대각선 경로(25°~65°) 제외')
    args = parser.parse_args()

    target_robot = args.robot

    map_path = os.path.join(args.config_dir, args.map)
    if not os.path.exists(map_path):
        print(f'[ERROR] 맵 파일 없음: {map_path}')
        sys.exit(1)

    cfg = MapConfig(map_path)
    if target_robot not in cfg.starts:
        print(f'[ERROR] "{target_robot}" 이 map.yaml starts 에 없음')
        print(f'        사용 가능: {list(cfg.starts.keys())}')
        sys.exit(1)

    print(f'맵: {args.map}  로봇: {target_robot}')
    path_sets = build_path_set(cfg)
    validate_path_set(path_sets, cfg)

    paths     = path_sets[target_robot]
    valid_all = [p for p in paths if p.valid]
    if args.no_diagonal:
        before   = len(valid_all)
        valid_all = filter_diagonal(valid_all)
        print(f'  → 대각선 제외: {before}개 → {len(valid_all)}개')
    n_valid   = len(valid_all)
    n_invalid = len(paths) - n_valid
    print(f'{target_robot}: 전체 {len(paths)}개  유효 {n_valid}  무효 {n_invalid}')

    # ── 선택 모드 결정 ────────────────────────
    if args.diverse > 0:
        selected = select_diverse(valid_all, args.diverse)
        mode_label = f'diverse {len(selected)}'
        cbar_label = 'Selection order (1 = seed)'
        print(f'  → diverse 선택: {len(selected)}개')
        for i, p in enumerate(selected):
            cx, cy = _centroid(p)
            print(f'     #{i+1:2d}  pivot={p.pivot:.1f}  centroid=({cx:.1f}, {cy:.1f})')
    elif args.top > 0:
        selected = select_top(valid_all, args.top)
        mode_label = f'top {len(selected)}'
        cbar_label = 'Rank (1 = highest pivot)'
        print(f'  → top 선택: {len(selected)}개')
    else:
        selected = select_top(valid_all, 0)   # 전부, pivot 정렬
        mode_label = f'all {n_valid}'
        cbar_label = 'Rank (1 = highest pivot)'

    # ── 렌더 ─────────────────────────────────
    fig, ax = plt.subplots(figsize=(14, 10))
    margin  = 1.0
    ax.set_xlim(cfg.room['x_min'] - margin, cfg.room['x_max'] + margin)
    ax.set_ylim(cfg.room['y_min'] - margin, cfg.room['y_max'] + margin)

    draw_map(ax, cfg)
    draw_candidates(ax, paths,
                    show_invalid=args.show_invalid,
                    selected_valid=selected,
                    target_robot=target_robot,
                    mode_label=mode_label)

    inv_label = f'  +{n_invalid} invalid' if args.show_invalid else ''
    ax.set_title(
        f'{target_robot} — Path Candidates  '
        f'({mode_label}{inv_label})  [{args.map}]',
        fontweight='bold', fontsize=12, pad=10)

    if selected:
        add_colorbar(fig, ax, len(selected), cbar_label)

    ax.legend(loc='upper left', fontsize=9, framealpha=0.9)
    plt.tight_layout()

    map_label = os.path.splitext(os.path.basename(args.map))[0]
    out_file  = args.out or f'{target_robot}_candidates_{map_label}.png'
    plt.savefig(out_file, dpi=140, bbox_inches='tight')
    plt.close(fig)
    print(f'저장: {out_file}')


if __name__ == '__main__':
    main()
