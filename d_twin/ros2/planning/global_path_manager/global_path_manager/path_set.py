"""
path_set.py

역할:
  - 기하학적 최소 이격 거리(SAFE_DISTANCE = 1.2) 적용 (END 및 START 포인트 부근 1.5m 배제)
  - 가중치 역전(PROXIMITY > CROSS)을 통한 우측 겹침(Funneling) 현상 완벽 해결
  - 맵 물리적 경계 기준 동적 Clearance(0.3m) 계산 및 커버리지 확장 지원
"""

from __future__ import annotations

import itertools
import math
from dataclasses import dataclass, field
from typing import Dict, FrozenSet, List, Optional, Set, Tuple

import numpy as np
import yaml

# ═══════════════════════════════════════════════════════════
# ── 공통 설정 (버전 간 반드시 동일, 여기서만 수정) ──────────
SEARCH_GRID_STEP   = 1.0    # 경로 탐색 격자 간격 (m) — 0.5로 바꾸면 더 세밀
GRID_STEP          = 1.0    # 커버리지 격자 크기 (m)
COVER_RADIUS       = 1.0    # 커버리지 판정 반경 (m)
# ═══════════════════════════════════════════════════════════

# ── 탐색 설정 ─────────────────────────────────────────────
MAX_WAYPOINTS      = 6

# ── 격자 / 반경 설정 ──────────────────────────────────────
FOOTPRINT_RADIUS  = 0.3    
SEPARATION_RADIUS = 0.6    
COLLINEAR_RADIUS  = 0.1
PATH_SAMPLE_D     = 0.2

# ── 안전 이격 거리 설정 ───────────────────────────────────
SAFE_DISTANCE     = 1.2    

# ── 선정 가중치 ───────────────────────────────────────────
COVERAGE_WEIGHT         = 15.0    
SEPARATION_WEIGHT       = 20.0   
COVERAGE_OVERLAP_WEIGHT = 5.0    
OVERLAP_WEIGHT          = 150.0  
COLLINEAR_WEIGHT        = 200.0  
CROSS_PENALTY           = 20000  # 교차 절대 금지 수준으로 강화   # 교차 허용 (물리적 병렬 충돌 방지를 위해 상대적으로 하향)
PROXIMITY_PENALTY       = 30000  # 근접 주행 절대 금지 강화  # 근접 주행 절대 금지 (선간 거리 강제 확보)
WP_COUNT_PENALTY        = 0.2    
WALL_RADIUS              = 1.5    # 벽/장애물 경계 인접 판단 반경 (m)
WALL_COVERAGE_WEIGHT     = 40.0   # 벽 담당 로봇: 벽 인접 셀 커버리지 가중치
INTERIOR_COVERAGE_WEIGHT = 40.0   # 내부 담당 로봇: 내부 셀 커버리지 가중치

Waypoint = Tuple[float, float]
Cell     = Tuple[int, int]


class MapConfig:
    def __init__(self, path: str):
        with open(path, "r") as f:
            cfg = yaml.safe_load(f)["map"]
        self.starts:      Dict[str, Waypoint] = {k: tuple(v) for k, v in cfg["starts"].items()}
        self.end:         Waypoint             = tuple(cfg["end"])
        self.room:        dict                 = cfg["room"]
        self.wp_sampling: dict                 = cfg["waypoint_sampling"]
        self.obstacles:   List[dict]           = cfg.get("obstacles", [])
        rt = cfg.get("robot_types", {})
        self.wall_robots:     List[str] = rt.get("wall", [])
        self.interior_robots: List[str] = rt.get("interior", [])

    def xs(self) -> List[float]:
        s = self.wp_sampling
        return [round(x, 2) for x in np.arange(s["x_min"], s["x_max"] + 1e-6, s["x_step"])]

    def ys(self) -> List[float]:
        s = self.wp_sampling
        return [round(y, 2) for y in np.arange(s["y_min"], s["y_max"] + 1e-6, s["y_step"])]


# ── 유효성 및 구역 판정 ─────────────────────────

def _is_in_map(x: float, y: float, cfg: MapConfig, use_clearance: bool = True) -> bool:
    r = cfg.room
    margin = 0.0 if use_clearance else 0.3
    return (r["x_min"] - margin <= x <= r["x_max"] + margin and
            r["y_min"] - margin <= y <= r["y_max"] + margin)

def _pt_blocked(x: float, y: float, cfg: MapConfig, use_clearance: bool = True) -> bool:
    if not _is_in_map(x, y, cfg, use_clearance):
        return True

    c_margin = 0.3 if use_clearance else 0.0
    if c_margin > 0:
        r = cfg.room
        if x < r["x_min"] + c_margin: return True
        if x > r["x_max"] - c_margin: return True
        if y < r["y_min"] + c_margin: return True
        if y > r["y_max"] - c_margin: return True

    for obs in cfg.obstacles:
        obs_c = obs.get("clearance", 0.0) if use_clearance else 0.0
        if (obs["x_min"] - obs_c <= x <= obs["x_max"] + obs_c and
                obs["y_min"] - obs_c <= y <= obs["y_max"] + obs_c):
            return True

    return False

def _seg_blocked(p1: Waypoint, p2: Waypoint, cfg: MapConfig, n: int = 12) -> bool:
    x1, y1 = p1; x2, y2 = p2
    for i in range(n + 1):
        t = i / n
        if _pt_blocked(x1 + t*(x2-x1), y1 + t*(y2-y1), cfg, use_clearance=True):
            return True
    return False

def _path_valid(wps: List[Waypoint], cfg: MapConfig) -> bool:
    return all(not _seg_blocked(wps[i], wps[i+1], cfg) for i in range(len(wps)-1))


# ── 격자 유틸 ─────────────────────────────────────────────
def _to_cell(x: float, y: float, x_min: float, y_min: float) -> Cell:
    return (math.floor((x - x_min) / GRID_STEP), math.floor((y - y_min) / GRID_STEP))

def _cell_center(cell: Cell, x_min: float, y_min: float) -> Tuple[float, float]:
    return (x_min + (cell[0] + 0.5) * GRID_STEP,
            y_min + (cell[1] + 0.5) * GRID_STEP)

def _build_map_cells(cfg: MapConfig) -> FrozenSet[Cell]:
    cells: Set[Cell] = set()
    x_min, y_min = 0.0, 0.0
    r = cfg.room
    x = r["x_min"] + GRID_STEP / 2
    while x < r["x_max"]:
        y = r["y_min"] + GRID_STEP / 2
        while y < r["y_max"]:
            if not _pt_blocked(x, y, cfg, use_clearance=False):
                cells.add(_to_cell(x, y, x_min, y_min))
            y += GRID_STEP
        x += GRID_STEP
    return frozenset(cells)

def _cells_within_radius(waypoints: List[Waypoint], radius: float,
                          all_cells: FrozenSet[Cell],
                          x_min: float, y_min: float) -> FrozenSet[Cell]:
    covered: Set[Cell] = set()
    r2 = radius ** 2
    r_idx = int(radius / GRID_STEP) + 1
    pts = []
    for i in range(len(waypoints) - 1):
        x1, y1 = waypoints[i]; x2, y2 = waypoints[i+1]
        length = math.hypot(x2-x1, y2-y1)
        steps  = max(1, int(length / PATH_SAMPLE_D))
        for s in range(steps + 1):
            t = s / steps
            pts.append((x1 + t*(x2-x1), y1 + t*(y2-y1)))
    for (px, py) in pts:
        bc = math.floor((px - x_min) / GRID_STEP)
        br = math.floor((py - y_min) / GRID_STEP)
        for dc in range(-r_idx, r_idx + 1):
            for dr in range(-r_idx, r_idx + 1):
                cell = (bc + dc, br + dr)
                if cell not in all_cells:
                    continue
                cx, cy = _cell_center(cell, x_min, y_min)
                if (cx - px)**2 + (cy - py)**2 <= r2:
                    covered.add(cell)
    return frozenset(covered)


# ── 교차 및 선간 거리 판정 ──────────────────────────────────
def _cross(o, a, b):
    return (a[0]-o[0])*(b[1]-o[1]) - (a[1]-o[1])*(b[0]-o[0])

def _point_on_segment(p, a, b) -> bool:
    if abs(_cross(a, b, p)) > 1e-5: return False
    if min(a[0], b[0]) - 1e-5 <= p[0] <= max(a[0], b[0]) + 1e-5 and \
       min(a[1], b[1]) - 1e-5 <= p[1] <= max(a[1], b[1]) + 1e-5:
        return True
    return False

def _segments_intersect_strict(a1, a2, b1, b2) -> bool:
    d1 = _cross(b1, b2, a1); d2 = _cross(b1, b2, a2)
    d3 = _cross(a1, a2, b1); d4 = _cross(a1, a2, b2)
    return (((d1 > 1e-5 and d2 < -1e-5) or (d1 < -1e-5 and d2 > 1e-5)) and
            ((d3 > 1e-5 and d4 < -1e-5) or (d3 < -1e-5 and d4 > 1e-5)))

def _paths_cross(wps_a: List[Waypoint], wps_b: List[Waypoint]) -> bool:
    segs_a = [(wps_a[i], wps_a[i+1]) for i in range(len(wps_a)-1)]
    segs_b = [(wps_b[i], wps_b[i+1]) for i in range(len(wps_b)-1)]
    for a1, a2 in segs_a:
        for b1, b2 in segs_b:
            if _segments_intersect_strict(a1, a2, b1, b2): return True
    for wp_a in wps_a[:-1]:
        for b1, b2 in segs_b:
            if _point_on_segment(wp_a, b1, b2): return True
    for wp_b in wps_b[:-1]:
        for a1, a2 in segs_a:
            if _point_on_segment(wp_b, a1, a2): return True
    return False

def _dist_pt_seg(p: Waypoint, a: Waypoint, b: Waypoint) -> float:
    l2 = (a[0]-b[0])**2 + (a[1]-b[1])**2
    if l2 == 0: return math.hypot(p[0]-a[0], p[1]-a[1])
    t = max(0, min(1, ((p[0]-a[0])*(b[0]-a[0]) + (p[1]-a[1])*(b[1]-a[1])) / l2))
    proj = (a[0] + t*(b[0]-a[0]), a[1] + t*(b[1]-a[1]))
    return math.hypot(p[0]-proj[0], p[1]-proj[1])

def _paths_min_dist(
    wps_a: List[Waypoint], wps_b: List[Waypoint],
    end_pt: Waypoint,
    all_starts: Optional[List[Waypoint]] = None,
) -> float:
    min_d = float('inf')

    def is_excluded(p: Waypoint) -> bool:
        if math.hypot(p[0] - end_pt[0], p[1] - end_pt[1]) < 1.5:
            return True
        if all_starts:
            for s in all_starts:
                if math.hypot(p[0] - s[0], p[1] - s[1]) < 1.5:
                    return True
        return False

    for p in wps_a:
        if is_excluded(p): continue
        for i in range(len(wps_b)-1):
            d = _dist_pt_seg(p, wps_b[i], wps_b[i+1])
            if d < min_d: min_d = d

    for p in wps_b:
        if is_excluded(p): continue
        for i in range(len(wps_a)-1):
            d = _dist_pt_seg(p, wps_a[i], wps_a[i+1])
            if d < min_d: min_d = d

    return min_d


def _turn_count(wps: List[Waypoint]) -> int:
    turns = 0
    for i in range(1, len(wps) - 1):
        a, b, c = wps[i - 1], wps[i], wps[i + 1]
        yaw_1 = math.atan2(b[1] - a[1], b[0] - a[0])
        yaw_2 = math.atan2(c[1] - b[1], c[0] - b[0])
        delta = abs((yaw_2 - yaw_1 + math.pi) % (2 * math.pi) - math.pi)
        if delta > 0.1:
            turns += 1
    return turns


def _selection_quality(
    selected: Dict[str, "GlobalPath"],
    cfg: Optional[MapConfig],
    starts: Dict[str, Waypoint],
) -> Tuple[int, int, int, int, int, int]:
    paths = list(selected.values())
    end_pt = cfg.end if cfg is not None else paths[0].waypoints[-1]
    start_pts = list(starts.values())

    cross_count = 0
    close_count = 0
    footprint_overlap = 0
    for i in range(len(paths)):
        for j in range(i + 1, len(paths)):
            if _paths_cross(paths[i].waypoints, paths[j].waypoints):
                cross_count += 1
            if _paths_min_dist(paths[i].waypoints, paths[j].waypoints,
                               end_pt, start_pts) < SAFE_DISTANCE:
                close_count += 1
            footprint_overlap += len(paths[i].footprint & paths[j].footprint)

    coverage = len(frozenset().union(*(p.coverage for p in paths)))
    turn_count = sum(_turn_count(p.waypoints) for p in paths)
    wp_count = sum(len(p.waypoints) for p in paths)

    return (
        cross_count,
        -coverage,
        footprint_overlap,
        turn_count,
        close_count,
        wp_count,
    )


# ── 데이터 클래스 ─────────────────────────────────────────
@dataclass
class GlobalPath:
    path_id:    str
    robot_key:  str
    waypoints:  List[Waypoint]
    pivot:      float = 0.0
    footprint:  FrozenSet[Cell] = field(default_factory=frozenset)
    separation: FrozenSet[Cell] = field(default_factory=frozenset)
    coverage:   FrozenSet[Cell] = field(default_factory=frozenset)
    collinear:  FrozenSet[Cell] = field(default_factory=frozenset)
    valid:      bool = True


def shape_of(path: GlobalPath) -> str:
    return 'dynamic'


def _build_wall_cells(cfg: MapConfig, all_cells: FrozenSet[Cell]) -> FrozenSet[Cell]:
    """장애물/맵 경계로부터 WALL_RADIUS 이내 셀을 반환."""
    x_min, y_min = 0.0, 0.0
    wall: Set[Cell] = set()
    r2 = WALL_RADIUS ** 2

    boundary_pts: List[Waypoint] = []
    r = cfg.room
    for t in np.arange(0, 1.01, 0.1):
        boundary_pts += [
            (r['x_min'] + t * (r['x_max'] - r['x_min']), r['y_min']),
            (r['x_min'] + t * (r['x_max'] - r['x_min']), r['y_max']),
            (r['x_min'], r['y_min'] + t * (r['y_max'] - r['y_min'])),
            (r['x_max'], r['y_min'] + t * (r['y_max'] - r['y_min'])),
        ]
    for obs in cfg.obstacles:
        for t in np.arange(0, 1.01, 0.1):
            boundary_pts += [
                (obs['x_min'] + t * (obs['x_max'] - obs['x_min']), obs['y_min']),
                (obs['x_min'] + t * (obs['x_max'] - obs['x_min']), obs['y_max']),
                (obs['x_min'], obs['y_min'] + t * (obs['y_max'] - obs['y_min'])),
                (obs['x_max'], obs['y_min'] + t * (obs['y_max'] - obs['y_min'])),
            ]

    r_idx = int(WALL_RADIUS / GRID_STEP) + 1
    for px, py in boundary_pts:
        bc = math.floor((px - x_min) / GRID_STEP)
        br = math.floor((py - y_min) / GRID_STEP)
        for dc in range(-r_idx, r_idx + 1):
            for dr in range(-r_idx, r_idx + 1):
                cell = (bc + dc, br + dr)
                if cell not in all_cells:
                    continue
                cx, cy = _cell_center(cell, x_min, y_min)
                if (cx - px) ** 2 + (cy - py) ** 2 <= r2:
                    wall.add(cell)

    return frozenset(wall)


# ── 로봇별 Path Set 동적 생성 ───────────────────────────────
def _build_paths_for(robot_key: str, start: Waypoint,
                     end: Waypoint, xs: List[float], ys: List[float],
                     cfg: MapConfig) -> List[GlobalPath]:
    paths: List[GlobalPath] = []
    
    grid_xs = [round(x, 2) for x in np.arange(cfg.room["x_min"] + SEARCH_GRID_STEP,
                                                cfg.room["x_max"], SEARCH_GRID_STEP)]
    grid_ys = [round(y, 2) for y in np.arange(cfg.room["y_min"] + SEARCH_GRID_STEP,
                                                cfg.room["y_max"], SEARCH_GRID_STEP)]
    
    queue = [([start], 'any')]
    seen_wps = set()

    while queue:
        wps, next_dir = queue.pop(0)
        curr = wps[-1]
        
        if _path_valid([curr, end], cfg):
            final_wps = wps + [end]
            if len(final_wps) <= MAX_WAYPOINTS:
                w_tuple = tuple(final_wps)
                if w_tuple not in seen_wps:
                    seen_wps.add(w_tuple)
                    pid = f"{robot_key}_dyn_{len(paths):04d}"
                    paths.append(GlobalPath(path_id=pid, robot_key=robot_key, waypoints=final_wps, pivot=0.0))
        
        if len(wps) >= MAX_WAYPOINTS - 1:
            continue
            
        if next_dir in ('x', 'any', 'ortho'):
            for nx in grid_xs:
                if nx > curr[0]:
                    nxt = (nx, curr[1])
                    if _path_valid([curr, nxt], cfg):
                        queue.append((wps + [nxt], 'y'))
                        
        if next_dir in ('y', 'any', 'ortho'):
            for ny in grid_ys:
                if ny > curr[1]:
                    nxt = (curr[0], ny)
                    if _path_valid([curr, nxt], cfg):
                        queue.append((wps + [nxt], 'x'))
                        
        if next_dir in ('diag', 'any'):
            for d in np.arange(SEARCH_GRID_STEP, 10.0, SEARCH_GRID_STEP):
                nx = round(curr[0] + d, 2)
                ny = round(curr[1] + d, 2)
                if nx <= cfg.room["x_max"] and ny <= cfg.room["y_max"]:
                    nxt = (nx, ny)
                    if _path_valid([curr, nxt], cfg):
                        queue.append((wps + [nxt], 'ortho'))
                        
    return paths


def build_path_set(cfg: MapConfig) -> Dict[str, List[GlobalPath]]:
    xs = cfg.xs(); ys = cfg.ys()
    return {k: _build_paths_for(k, start, cfg.end, xs, ys, cfg)
            for k, start in cfg.starts.items()}


# ── 유효성 + 셀 계산 ──────────────────────────────────────
def validate_path_set(
    path_sets: Dict[str, List[GlobalPath]],
    cfg: MapConfig,
) -> Dict[str, List[GlobalPath]]:
    x_min, y_min = 0.0, 0.0
    all_cells = _build_map_cells(cfg)

    for paths in path_sets.values():
        for path in paths:
            path.valid = _path_valid(path.waypoints, cfg)
            if path.valid:
                path.footprint  = _cells_within_radius(path.waypoints, FOOTPRINT_RADIUS,  all_cells, x_min, y_min)
                path.separation = _cells_within_radius(path.waypoints, SEPARATION_RADIUS, all_cells, x_min, y_min)
                path.coverage   = _cells_within_radius(path.waypoints, COVER_RADIUS,      all_cells, x_min, y_min)
                path.collinear  = _cells_within_radius(path.waypoints, COLLINEAR_RADIUS,  all_cells, x_min, y_min)
            else:
                path.footprint = path.separation = path.coverage = path.collinear = frozenset()
    return path_sets


# ── 경로 선정 ─────────────────────────────────────────────
def select_paths(
    path_sets: Dict[str, List[GlobalPath]],
    starts:    Optional[Dict[str, Waypoint]] = None,
    cfg:       Optional[MapConfig] = None,
) -> Dict[str, GlobalPath]:
    """
    y-x 기준 상위 절반 → 벽 담당 (wall_cells 우선 커버)
    y-x 기준 하위 절반 → 내부 담당 (interior_cells 우선 커버)
    벽 담당 먼저 선정 후 내부 담당 선정.
    """
    first_path = next((p[0] for p in path_sets.values() if p), None)
    if cfg is not None:
        end_pt = cfg.end
    elif first_path:
        end_pt = first_path.waypoints[-1]
    else:
        end_pt = (19.0, 14.0)

    if starts is None:
        starts = {k: paths[0].waypoints[0] for k, paths in path_sets.items() if paths}

    # 벽/내부 셀 계산. cfg가 있으면 실제 맵/장애물 경계를 사용하고,
    # 없을 때만 기존 coverage union 기반 heuristic으로 fallback한다.
    if cfg is not None:
        all_cells = _build_map_cells(cfg)
        wall_cells = _build_wall_cells(cfg, all_cells)
    else:
        all_cells = frozenset().union(
            *(p.coverage for paths in path_sets.values() for p in paths if p.valid)
        )
        wall_cells = _build_wall_cells_from_sets(path_sets, all_cells)
    interior_cells = all_cells - wall_cells

    def wall_priority(robot_key: str) -> Tuple[float, float, float]:
        x, y = starts[robot_key]
        return (y - x, y, -x)

    # 로봇 타입 배정: 위치 기준 정렬 → 상위 절반 wall, 하위 절반 interior.
    # y-x 동점도 좌표로 결정해서 robot id / yaml 순서가 결과를 흔들지 않게 한다.
    n = len(starts)
    sorted_robots = sorted(starts.keys(), key=wall_priority, reverse=True)
    n_wall = n // 2
    wall_robots     = set(sorted_robots[:n_wall])
    interior_robots = set(sorted_robots[n_wall:])

    # 벽 담당 먼저 선정. 내부 담당은 x가 큰 로봇을 먼저 고르게 해서
    # 바깥쪽 하단 경로를 선점하고, x가 작은 로봇은 대각 우회 후보를 타도록 유도한다.
    wall_order = [r for r in sorted_robots if r in wall_robots]
    interior_order = sorted(
        (r for r in sorted_robots if r in interior_robots),
        key=lambda k: (starts[k][0], starts[k][1]),
        reverse=True,
    )
    eval_order = wall_order + interior_order

    selected:          Dict[str, GlobalPath] = {}
    covered_cov:       Set[Cell] = set()
    covered_sep:       Set[Cell] = set()
    covered_foot:      Set[Cell] = set()
    covered_collinear: Set[Cell] = set()
    start_pts = list(starts.values())

    for robot_key in eval_order:
        candidates = [p for p in path_sets[robot_key] if p.valid]
        if not candidates:
            continue

        is_wall = robot_key in wall_robots

        def score(p: GlobalPath, _wall=is_wall,
                  _wc=wall_cells, _ic=interior_cells) -> float:
            new_cov = p.coverage - covered_cov

            if _wall:
                primary   = WALL_COVERAGE_WEIGHT     * len(new_cov & _wc)
                secondary = COVERAGE_WEIGHT           * len(new_cov - _wc)
            else:
                primary   = INTERIOR_COVERAGE_WEIGHT * len(new_cov & _ic)
                secondary = COVERAGE_WEIGHT           * len(new_cov - _ic)

            cross_p = sum(CROSS_PENALTY for sel in selected.values()
                          if _paths_cross(p.waypoints, sel.waypoints))
            prox_p  = sum(PROXIMITY_PENALTY for sel in selected.values()
                          if _paths_min_dist(p.waypoints, sel.waypoints,
                                             end_pt, start_pts) < SAFE_DISTANCE)
            sep_overlap     = len(p.separation & covered_sep)
            collinear_ratio = len(p.collinear & covered_collinear) / max(1, len(p.collinear))
            wp_penalty      = WP_COUNT_PENALTY * len(p.waypoints)

            return (
                primary
                + secondary
                - COVERAGE_OVERLAP_WEIGHT * len(p.coverage & covered_cov)
                - SEPARATION_WEIGHT       * sep_overlap
                - OVERLAP_WEIGHT          * len(p.footprint & covered_foot)
                - COLLINEAR_WEIGHT        * collinear_ratio
                - wp_penalty
                - cross_p
                - prox_p
            )

        best = max(candidates, key=score)
        best.pivot = float(robot_key in wall_robots)   # 범례 표시용
        selected[robot_key] = best

        covered_cov       |= best.coverage
        covered_sep       |= best.separation
        covered_foot      |= best.footprint
        covered_collinear |= best.collinear

    # Greedy 선정 뒤, 교차가 남아 있으면 로봇 하나씩 후보를 바꿔 보며
    # coverage를 크게 잃지 않는 더 단순한 조합으로 국소 보정한다.
    for _ in range(2):
        current_quality = _selection_quality(selected, cfg, starts)
        improved = False

        for robot_key in eval_order:
            original = selected[robot_key]
            best_path = original
            best_quality = current_quality

            for candidate in path_sets[robot_key]:
                if not candidate.valid or candidate is original:
                    continue

                selected[robot_key] = candidate
                quality = _selection_quality(selected, cfg, starts)
                if quality < best_quality:
                    best_quality = quality
                    best_path = candidate

            selected[robot_key] = best_path
            if best_path is not original:
                current_quality = best_quality
                improved = True

        if not improved:
            break

    for robot_key, path in selected.items():
        path.pivot = float(robot_key in wall_robots)

    return selected


def _build_wall_cells_from_sets(
    path_sets: Dict[str, List[GlobalPath]],
    all_cells: FrozenSet[Cell],
) -> FrozenSet[Cell]:
    """
    all_cells 중 맵 경계/장애물 인접 셀.
    cfg 없을 때 fallback용 heuristic. 실제 운용 시 _build_wall_cells(cfg) 사용.
    """
    wall: Set[Cell] = set()
    for cell in all_cells:
        cx = (cell[0] + 0.5) * GRID_STEP
        cy = (cell[1] + 0.5) * GRID_STEP
        if cx < 3.0 or cx > 17.0 or cy < 2.0 or cy > 13.0:
            wall.add(cell)
    return frozenset(wall)
