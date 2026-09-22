#!/usr/bin/env python3
import argparse
import os
import sys
import matplotlib.patches as patches
import matplotlib.pyplot as plt
import yaml

SCRIPT_DIR  = os.path.dirname(os.path.abspath(__file__))
DEFAULT_MAP = os.path.join(SCRIPT_DIR, 'config', 'map.yaml')
DEFAULT_OUT = 'sweep_result.png'
ROBOT_COLOR = '#E74C3C'
ARROW_EVERY = 1

WAYPOINTS = [
    (2, 1), (2, 7), (8, 7), (8, 4), (4, 4),
    (4, 2), (9, 1), (11, 1), (11, 7),
]


def draw_map(ax, cfg):
    ax.set_aspect('equal')
    ax.grid(True, alpha=0.15, linewidth=0.5)
    ax.set_xlabel('X (m)', fontsize=10)
    ax.set_ylabel('Y (m)', fontsize=10)

    ax.add_patch(patches.Rectangle(
        (0, 0), 12.22, 7.54,
        lw=2, ec='#2C3E50', fc='#ECF0F1', zorder=1))

    if cfg.get('corridor_1'):
        c = cfg['corridor_1']
        ax.add_patch(patches.Rectangle(
            (c['x_min'], c['y_min']),
            c['x_max'] - c['x_min'], c['y_max'] - c['y_min'],
            lw=2, ec='#2C3E50', fc='#ECF0F1', zorder=1))

    for o in cfg.get('obstacles', []):
        ax.add_patch(patches.Rectangle(
            (o['x_min'], o['y_min']),
            o['x_max'] - o['x_min'], o['y_max'] - o['y_min'],
            lw=1.5, ec='#7D3C98', fc='#D2B4DE', alpha=0.9,
            zorder=2, hatch='////'))


def draw_path(ax, wps):
    xs = [w[0] for w in wps]
    ys = [w[1] for w in wps]

    ax.plot(xs, ys, '-', color=ROBOT_COLOR, lw=2.0, alpha=0.9, zorder=5)

    for i in range(0, len(wps) - 1, ARROW_EVERY):
        ax.annotate('', xy=wps[i + 1], xytext=wps[i],
                    arrowprops=dict(arrowstyle='->', color=ROBOT_COLOR, lw=1.4),
                    zorder=6)

    for i, (x, y) in enumerate(wps):
        ax.plot(x, y, 'o', color=ROBOT_COLOR, markersize=7, zorder=7)

    ax.plot(xs[0], ys[0], 'o', color=ROBOT_COLOR, markersize=11, zorder=8,
            label=f'Start {wps[0]}')
    ax.plot(xs[-1], ys[-1], 's', color=ROBOT_COLOR, markersize=11, zorder=8,
            label=f'End {wps[-1]}')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--config', default=DEFAULT_MAP)
    parser.add_argument('--out',    default=DEFAULT_OUT)
    args = parser.parse_args()

    if not os.path.exists(args.config):
        print(f'[ERROR] 맵 파일 없음: {args.config}')
        sys.exit(1)

    with open(args.config) as f:
        cfg = yaml.safe_load(f)['map']

    fig, ax = plt.subplots(figsize=(14, 10))
    ax.set_xlim(-0.5, 13.5)
    ax.set_ylim(-0.5, 12.5)
    ax.set_title('Sweep Path', fontweight='bold', fontsize=13)

    draw_map(ax, cfg)
    draw_path(ax, WAYPOINTS)
    ax.legend(loc='upper left', fontsize=9, framealpha=0.9)

    plt.tight_layout()
    plt.savefig(args.out, dpi=140, bbox_inches='tight')
    plt.close(fig)
    print(f'저장: {args.out}')


if __name__ == '__main__':
    main()
