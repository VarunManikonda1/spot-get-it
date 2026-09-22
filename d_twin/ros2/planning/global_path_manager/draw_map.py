#!/usr/bin/env python3
import argparse
import os
import sys
import matplotlib.patches as patches
import matplotlib.pyplot as plt
import yaml

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_MAP = os.path.join(SCRIPT_DIR, 'config', 'map.yaml')
DEFAULT_OUT = 'map.png'

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
    ax.set_aspect('equal')
    ax.set_xlim(-0.5, 13.5)
    ax.set_ylim(-0.5, 12.5)
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

    for obs in cfg.get('obstacles', []):
        ax.add_patch(patches.Rectangle(
            (obs['x_min'], obs['y_min']),
            obs['x_max'] - obs['x_min'], obs['y_max'] - obs['y_min'],
            lw=1.5, ec='#7D3C98', fc='#D2B4DE', alpha=0.9,
            zorder=2, hatch='////'))

    plt.tight_layout()
    plt.savefig(args.out, dpi=140, bbox_inches='tight')
    plt.close(fig)
    print(f'저장: {args.out}')

if __name__ == '__main__':
    main()
