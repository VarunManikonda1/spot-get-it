"""
core/visualizer.py

Map and path visualization rendering.
Returns PIL Images — no tkinter, no ROS2 dependencies.
"""

import io
import math
from typing import Dict, List, Optional, Tuple

import matplotlib
matplotlib.use('Agg')
import matplotlib.patches as mpatches
import matplotlib.pyplot as plt
import yaml

try:
    from PIL import Image
    PIL_OK = True
except ImportError:
    PIL_OK = False

WALL_IDS     = {'wall_bottom', 'wall_top', 'wall_left', 'wall_right'}
ROBOT_COLORS = ['#E74C3C', '#E67E22', '#F1C40F', '#2ECC71', '#8E44AD']
BG_CANVAS    = '#DFE6E9'


# ── Map render ─────────────────────────────────────────────────────────────────

def render_map(yaml_path: str, width_px: int, height_px: int,
               dpi: int = 96) -> 'Image.Image':
    """
    Load map.yaml and render a static map image.
    Shows: field boundary, waypoint sampling area, obstacles, starts, end.
    Returns a PIL Image.
    """
    with open(yaml_path) as f:
        cfg = yaml.safe_load(f)['map']

    fld = cfg.get('field', {})
    x1, x2 = fld.get('x_min', 0.0), fld.get('x_max', 20.0)
    y1, y2 = fld.get('y_min', 0.0), fld.get('y_max', 15.0)
    M = 1.5

    fig, ax = plt.subplots(figsize=(max(4, width_px / dpi),
                                    max(3, height_px / dpi)))
    fig.patch.set_facecolor(BG_CANVAS)
    ax.set_facecolor('#ECF0F1')
    ax.set_aspect('equal')
    ax.set_xlim(x1 - M, x2 + M)
    ax.set_ylim(y1 - M, y2 + M)
    ax.grid(True, alpha=0.3, lw=0.6)
    ax.set_xlabel('X (m)', fontsize=8)
    ax.set_ylabel('Y (m)', fontsize=8)
    ax.tick_params(labelsize=7)
    ax.set_title(
        f'field  {x2-x1:.1f} x {y2-y1:.1f} m'
        f'  |  {len(cfg.get("obstacles", []))} obstacles',
        fontsize=9,
    )

    # Field boundary
    ax.add_patch(mpatches.Rectangle(
        (x1, y1), x2 - x1, y2 - y1,
        lw=1.5, ec='#2C3E50', fc='#ECF0F1', zorder=1))

    # Waypoint sampling area
    ws = cfg.get('waypoint_sampling', {})
    if ws:
        ax.add_patch(mpatches.Rectangle(
            (ws['x_min'], ws['y_min']),
            ws['x_max'] - ws['x_min'], ws['y_max'] - ws['y_min'],
            lw=1.2, ec='#27AE60', fc='none', ls='--', zorder=2,
            label='sampling area'))

    # Obstacles
    for obs in cfg.get('obstacles', []):
        wall = obs.get('id', '') in WALL_IDS
        ax.add_patch(mpatches.Rectangle(
            (obs['x_min'], obs['y_min']),
            obs['x_max'] - obs['x_min'], obs['y_max'] - obs['y_min'],
            lw=1.0,
            ec='#5D6D7E' if wall else '#7D3C98',
            fc='#AEB6BF' if wall else '#D2B4DE',
            alpha=0.85, zorder=3,
            hatch=None if wall else '////'))

    # Start positions
    for i, (rid, pos) in enumerate(cfg.get('starts', {}).items()):
        c = ROBOT_COLORS[i % len(ROBOT_COLORS)]
        ax.plot(pos[0], pos[1], 'o', color=c, ms=7, zorder=5)
        ax.annotate(rid, (pos[0], pos[1]),
                    xytext=(4, 4), textcoords='offset points',
                    fontsize=6, fontweight='bold', color=c, zorder=6)

    # End point
    end = cfg.get('end')
    if end:
        ax.plot(end[0], end[1], '*', color='#2980B9', ms=13, zorder=5,
                label='end')

    plt.tight_layout(pad=0.5)
    return _fig_to_pil(fig, dpi)


# ── Path render ────────────────────────────────────────────────────────────────

def render_paths(
    yaml_path:  str,
    selected:   dict,
    all_cells:  frozenset,
    cfg,                     # MapConfig instance from core/path_set.py
    width_px:   int,
    height_px:  int,
    dpi:        int = 96,
    cover_radius: float = 2.0,
) -> 'Image.Image':
    """
    Render selected paths on top of the map.
    Shows: coverage zones, path lines, waypoints, interpolated points.
    Returns a PIL Image.
    """
    # Import here to avoid circular dependency at module level
    from sar_planner.core.path_set import _cells_within_radius, _interpolate_path

    with open(yaml_path) as f:
        map_cfg = yaml.safe_load(f)['map']

    fld = map_cfg.get('field', {})
    x1, x2 = fld.get('x_min', 0.0), fld.get('x_max', 20.0)
    y1, y2 = fld.get('y_min', 0.0), fld.get('y_max', 15.0)
    M = 1.5
    gs = cfg.grid_step

    fig, ax = plt.subplots(figsize=(max(4, width_px / dpi),
                                    max(3, height_px / dpi)))
    fig.patch.set_facecolor(BG_CANVAS)
    ax.set_facecolor('#ECF0F1')
    ax.set_aspect('equal')
    ax.set_xlim(x1 - M, x2 + M)
    ax.set_ylim(y1 - M, y2 + M)
    ax.grid(True, alpha=0.15, lw=0.5)
    ax.set_xlabel('X (m)', fontsize=8)
    ax.set_ylabel('Y (m)', fontsize=8)
    ax.tick_params(labelsize=7)

    # Field + obstacles (same as render_map)
    ax.add_patch(mpatches.Rectangle(
        (x1, y1), x2 - x1, y2 - y1,
        lw=1.5, ec='#2C3E50', fc='#ECF0F1', zorder=1))

    for obs in map_cfg.get('obstacles', []):
        wall = obs.get('id', '') in WALL_IDS
        ax.add_patch(mpatches.Rectangle(
            (obs['x_min'], obs['y_min']),
            obs['x_max'] - obs['x_min'], obs['y_max'] - obs['y_min'],
            lw=1.0,
            ec='#5D6D7E' if wall else '#7D3C98',
            fc='#AEB6BF' if wall else '#D2B4DE',
            alpha=0.85, zorder=2,
            hatch=None if wall else '////'))

    end = map_cfg.get('end')
    if end:
        ax.plot(end[0], end[1], '*', color='#17202A', ms=14, zorder=8)

    # Coverage + paths
    x_min_c, y_min_c = 0.0, 0.0
    seen = set()
    total_pct = 0.0

    for i, (robot_key, path) in enumerate(selected.items()):
        color = ROBOT_COLORS[i % len(ROBOT_COLORS)]
        wps   = path.waypoints

        # Coverage cells
        cov = _cells_within_radius(wps, cover_radius, all_cells,
                                   x_min_c, y_min_c, gs)
        for cell in cov:
            cx = x_min_c + (cell[0] + 0.5) * gs
            cy = y_min_c + (cell[1] + 0.5) * gs
            fc = '#F0B27A' if cell in seen else color
            ax.add_patch(mpatches.Rectangle(
                (cx - gs / 2, cy - gs / 2), gs, gs,
                fc=fc, ec='none', alpha=0.22, zorder=3))
        seen |= cov

        # Path line + waypoints
        xs, ys = [w[0] for w in wps], [w[1] for w in wps]
        ax.plot(xs, ys, '-', color=color, lw=2.2, zorder=5, alpha=0.9)
        ax.plot(xs, ys, 'o', color=color, ms=6,   zorder=7)

        # Interpolated points
        interp = _interpolate_path(wps)
        xi = [w[0] for w in interp]
        yi = [w[1] for w in interp]
        ax.plot(xi, yi, '.', color=color, ms=3, zorder=6, alpha=0.6)

        # Start label
        ax.annotate(robot_key, (wps[0][0], wps[0][1]),
                    xytext=(-18, 6), textcoords='offset points',
                    fontsize=7, color=color, fontweight='bold', zorder=9)

        ax.plot([], [], '-o', color=color, lw=2.2,
                label=f'{robot_key}  {len(interp)} wp')

    if all_cells:
        total_pct = 100.0 * len(seen) / len(all_cells)

    ax.legend(loc='upper left', fontsize=7, framealpha=0.9)
    ax.set_title(
        f'Global Paths  —  {len(selected)} robots'
        f'  |  coverage {total_pct:.1f}%'
        f'  |  grid {gs:.1f} m',
        fontsize=9,
    )

    plt.tight_layout(pad=0.5)
    return _fig_to_pil(fig, dpi)


# ── Helper ─────────────────────────────────────────────────────────────────────

def _fig_to_pil(fig, dpi: int) -> 'Image.Image':
    buf = io.BytesIO()
    plt.savefig(buf, format='png', dpi=dpi, bbox_inches='tight')
    plt.close(fig)
    buf.seek(0)
    return Image.open(buf).copy()
