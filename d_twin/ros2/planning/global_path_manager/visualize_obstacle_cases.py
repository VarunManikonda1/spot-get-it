#!/usr/bin/env python3
"""
장애물 테스트 맵 5개를 3대/4대 로봇 구성으로 일괄 검증한다.

사용법:
  python3 visualize_obstacle_cases.py
  python3 visualize_obstacle_cases.py --maps map_obstacle_01.yaml map_obstacle_03.yaml
"""

import argparse
import os
import sys
from typing import Dict, Iterable

import matplotlib.pyplot as plt

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)

from global_path_manager.path_set import (  # noqa: E402
    GlobalPath,
    MapConfig,
    _build_map_cells,
    _paths_cross,
    _paths_min_dist,
    build_path_set,
    select_paths,
    validate_path_set,
)
from visualize_paths_4robots import draw_map, draw_paths_with_coverage  # noqa: E402


DEFAULT_MAPS = [
    'map_obstacle_01_3robots.yaml',
    'map_obstacle_01_4robots.yaml',
    'map_obstacle_02_3robots.yaml',
    'map_obstacle_02_4robots.yaml',
    'map_obstacle_03_3robots.yaml',
    'map_obstacle_03_4robots.yaml',
    'map_obstacle_04_3robots.yaml',
    'map_obstacle_04_4robots.yaml',
    'map_obstacle_05_3robots.yaml',
    'map_obstacle_05_4robots.yaml',
]


def count_crossings(selected: Dict[str, GlobalPath]) -> int:
    paths = list(selected.values())
    total = 0
    for i in range(len(paths)):
        for j in range(i + 1, len(paths)):
            if _paths_cross(paths[i].waypoints, paths[j].waypoints):
                total += 1
    return total


def min_pair_distance(selected: Dict[str, GlobalPath], cfg: MapConfig) -> float:
    paths = list(selected.values())
    starts = list(cfg.starts.values())
    min_dist = float('inf')
    for i in range(len(paths)):
        for j in range(i + 1, len(paths)):
            min_dist = min(
                min_dist,
                _paths_min_dist(paths[i].waypoints, paths[j].waypoints,
                                cfg.end, starts),
            )
    return min_dist if min_dist != float('inf') else 0.0


def render_case(map_name: str, config_dir: str, out_dir: str,
                prefix: str) -> None:
    map_path = os.path.join(config_dir, map_name)
    cfg = MapConfig(map_path)

    path_sets = build_path_set(cfg)
    validate_path_set(path_sets, cfg)
    selected = select_paths(path_sets, starts=cfg.starts, cfg=cfg)
    all_cells = _build_map_cells(cfg)

    fig, ax = plt.subplots(figsize=(14, 10))
    draw_map(ax, cfg)
    coverage_pct = draw_paths_with_coverage(ax, selected, all_cells, cfg)
    ax.set_xlim(-0.5, 13.5)
    ax.set_ylim(-0.5, 12.5)
    ax.set_title(
        f'Obstacle Case — {map_name}  Coverage {coverage_pct:.1f}%',
        fontweight='bold',
        fontsize=13,
    )

    os.makedirs(out_dir, exist_ok=True)
    label = os.path.splitext(os.path.basename(map_name))[0]
    out_file = os.path.join(out_dir, f'{prefix}_{label}.png')
    plt.tight_layout()
    plt.savefig(out_file, dpi=140, bbox_inches='tight')
    plt.close(fig)

    total_generated = sum(len(paths) for paths in path_sets.values())
    total_valid = sum(sum(p.valid for p in paths) for paths in path_sets.values())
    print(
        f'{map_name}: robots={len(cfg.starts)} '
        f'candidates={total_valid}/{total_generated} '
        f'coverage={coverage_pct:.1f}% '
        f'crossings={count_crossings(selected)} '
        f'min_dist={min_pair_distance(selected, cfg):.2f} '
        f'-> {out_file}'
    )


def existing_maps(map_names: Iterable[str], config_dir: str) -> list[str]:
    maps = []
    for map_name in map_names:
        path = os.path.join(config_dir, map_name)
        if not os.path.exists(path):
            print(f'[WARN] 맵 파일 없음: {path}')
            continue
        maps.append(map_name)
    return maps


def main():
    parser = argparse.ArgumentParser(description='장애물 케이스 일괄 시각화')
    parser.add_argument('--maps', nargs='*', default=DEFAULT_MAPS,
                        help='검증할 맵 yaml 목록')
    parser.add_argument('--config_dir', default=os.path.join(SCRIPT_DIR, 'config'),
                        help='맵 yaml 폴더')
    parser.add_argument('--out_dir', default=SCRIPT_DIR,
                        help='이미지 출력 폴더')
    parser.add_argument('--prefix', default='path_result_obstacle',
                        help='출력 파일 접두사')
    args = parser.parse_args()

    maps = existing_maps(args.maps, args.config_dir)
    if not maps:
        print('[ERROR] 검증할 맵이 없습니다.')
        sys.exit(1)

    for map_name in maps:
        render_case(map_name, args.config_dir, args.out_dir, args.prefix)


if __name__ == '__main__':
    main()
