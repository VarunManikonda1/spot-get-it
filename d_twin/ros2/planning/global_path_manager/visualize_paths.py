#!/usr/bin/env python3
"""
visualize_paths.py

사용법:
  python3 visualize_paths.py
  (config/map_case_01.yaml ~ 05.yaml을 순차적으로 읽어 이미지 5장을 자동 생성)
  파일명 형식: path_result_{session}_{case}.png
"""

import argparse
import math
import os
import sys
from typing import List, Tuple

import matplotlib.patches as patches
import matplotlib.pyplot as plt

# global_path_manager 패키지 경로 자동 탐색
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)

from global_path_manager.path_set import (
    COVER_RADIUS,
    GRID_STEP,
    GlobalPath,
    MapConfig,
    _build_map_cells,
    _cells_within_radius,
    _paths_cross,
    build_path_set,
    select_paths,
    validate_path_set,
)

INTERP_MAX_DIST = 0.5
Waypoint = Tuple[float, float]

COLORS = {
    'spot_01': '#E74C3C',
    'spot_02': '#27AE60',
    'spot_03': '#2980B9',
    'spot_04': '#E67E22',
    'spot_05': '#8E44AD',
}
DEFAULT_COLOR = '#7F8C8D'


def _yaw_between(p1, p2) -> float:
    return math.atan2(p2[1] - p1[1], p2[0] - p1[0])


def _interpolate(wps: List[Waypoint]) -> List[Waypoint]:
    """선분마다 INTERP_MAX_DIST 이하로 보간한 점 목록 반환."""
    result: List[Waypoint] = []
    for i in range(len(wps) - 1):
        p1, p2 = wps[i], wps[i + 1]
        length = math.hypot(p2[0] - p1[0], p2[1] - p1[1])
        n = math.ceil(length / INTERP_MAX_DIST)
        for k in range(n):
            t = k / n
            result.append((round(p1[0] + t * (p2[0] - p1[0]), 2),
                            round(p1[1] + t * (p2[1] - p1[1]), 2)))
    result.append(wps[-1])
    return result


def draw_map(ax, cfg: MapConfig):
    ax.set_aspect('equal')
    ax.grid(True, alpha=0.15, linewidth=0.5)
    ax.set_xlabel('X (m)', fontsize=10)
    ax.set_ylabel('Y (m)', fontsize=10)

    lobby = cfg.lobby
    ax.add_patch(patches.Rectangle(
        (0, 0), 12.22, 7.54,
        lw=2, ec='#2C3E50', fc='#ECF0F1', zorder=1))

    if hasattr(cfg, 'corridor_1') and cfg.corridor_1:
        c = cfg.corridor_1
        ax.add_patch(patches.Rectangle(
            (c['x_min'], c['y_min']),
            c['x_max'] - c['x_min'], c['y_max'] - c['y_min'],
            lw=2, ec='#2C3E50', fc='#ECF0F1', zorder=1))

    ax.add_patch(patches.Rectangle(
        (lobby['x_min'], lobby['y_min']),
        lobby['x_max'] - lobby['x_min'],
        lobby['y_max'] - lobby['y_min'],
        lw=1, ec='#27AE60', fc='none', ls='--', alpha=0.35, zorder=2))

    for obs in cfg.obstacles:
        ax.add_patch(patches.Rectangle(
            (obs['x_min'], obs['y_min']),
            obs['x_max'] - obs['x_min'],
            obs['y_max'] - obs['y_min'],
            lw=1.5, ec='#7D3C98', fc='#D2B4DE', alpha=0.9,
            zorder=2, hatch='////'))

    ex, ey = cfg.end
    ax.plot(ex, ey, '*', color='#17202A', markersize=15, zorder=8)


def draw_paths_with_coverage(ax, selected, all_cells, cfg):
    x_min, y_min = 0.0, 0.0
    seen = set()

    for robot_key, path in selected.items():
        color = COLORS.get(robot_key, DEFAULT_COLOR)
        wps = path.waypoints
        interp_wps = _interpolate(wps)

        # 커버리지는 원본 waypoints 기준
        cov = _cells_within_radius(wps, COVER_RADIUS, all_cells, x_min, y_min)
        for cell in cov:
            cx = x_min + (cell[0] + 0.5) * GRID_STEP
            cy = y_min + (cell[1] + 0.5) * GRID_STEP
            ax.add_patch(patches.Rectangle(
                (cx - GRID_STEP / 2, cy - GRID_STEP / 2),
                GRID_STEP, GRID_STEP,
                fc='#F0B27A' if cell in seen else color,
                ec='none', alpha=0.22, zorder=2))
        seen |= cov

        # 경로선 및 원본 waypoint (큰 원)
        xs, ys = [w[0] for w in wps], [w[1] for w in wps]
        ax.plot(xs, ys, '-', color=color, lw=2.5, zorder=5, alpha=0.9)
        ax.plot(xs, ys, 'o', color=color, markersize=7, zorder=7)

        # 보간점 (작은 점)
        xi, yi = [w[0] for w in interp_wps], [w[1] for w in interp_wps]
        ax.plot(xi, yi, '.', color=color, markersize=4, zorder=6, alpha=0.7)

        ax.plot([], [], '-o', color=color, lw=2.5,
                label=f'{robot_key} pivot={path.pivot:.2f} ({len(interp_wps)}wp)')

    total_pct = 100 * len(seen) / len(all_cells) if all_cells else 0
    ax.legend(loc='upper left', fontsize=8, framealpha=0.9)
    return total_pct


def get_session_number(prefix):
    session = 1
    while any(f.startswith(f"{prefix}_{session:02d}_") for f in os.listdir('.') if f.endswith('.png')):
        session += 1
    return session


def main():
    parser = argparse.ArgumentParser(description='Global Path 5개 케이스 일괄 시각화')
    parser.add_argument('--config_dir', default=os.path.join(SCRIPT_DIR, 'config'), help='맵 폴더')
    parser.add_argument('--prefix', default='path_result', help='파일 접두사')
    args = parser.parse_args()

    session_idx = get_session_number(args.prefix)
    print(f"🚀 세션 번호 '{session_idx:02d}'번으로 배치를 시작합니다.")

    for i in range(1, 6):
        map_filename = f"map_case_{i:02d}.yaml"
        map_path = os.path.join(args.config_dir, map_filename)

        if not os.path.exists(map_path):
            continue

        print(f"\n[{i}/5] 분석 시작: {map_filename}")
        cfg = MapConfig(map_path)

        path_sets = build_path_set(cfg)
        total_gen = sum(len(v) for v in path_sets.values())
        print(f"   ㄴ 경로 후보 생성: {total_gen}개")

        validate_path_set(path_sets, cfg)
        for rk, paths in path_sets.items():
            valid_count = sum(1 for p in paths if p.valid)
            print(f"      - {rk}: 유효 경로 {valid_count}/{len(paths)}개")

        selected = select_paths(path_sets, starts=cfg.starts)
        all_cells = _build_map_cells(cfg)

        fig, ax = plt.subplots(figsize=(14, 10))
        ax.set_title(f'Global Path (Session {session_idx:02d}, Case {i:02d})', fontweight='bold')
        ax.set_xlim(-0.5, 13.5)
        ax.set_ylim(-0.5, 12.5)

        draw_map(ax, cfg)
        draw_paths_with_coverage(ax, selected, all_cells, cfg)
        plt.tight_layout()

        out_file = f"{args.prefix}_{session_idx:02d}_{i:02d}.png"
        plt.savefig(out_file, dpi=140, bbox_inches='tight')
        plt.close(fig)
        print(f"   ㄴ 저장 완료: {out_file}")

    print(f"\n🎉 모든 작업이 완료되었습니다.")


if __name__ == '__main__':
    main()
