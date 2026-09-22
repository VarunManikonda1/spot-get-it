#!/usr/bin/env python3

import argparse
import csv
import math
from pathlib import Path


NUM_JOINTS = 12


def load_rows(path: Path):
    with path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        return list(reader)


def f(row, name: str) -> float:
    value = row.get(name, "")
    if value == "":
        return float("nan")
    return float(value)


def metrics(rows, joint: int):
    times = [f(r, "t_sec") for r in rows]
    errors = [f(r, f"error_{joint}") for r in rows]
    targets = [f(r, f"target_{joint}") for r in rows]
    positions = [f(r, f"position_{joint}") for r in rows]
    velocities = [f(r, f"velocity_{joint}") for r in rows]
    times = [x for x in times if math.isfinite(x)]
    errors = [x for x in errors if math.isfinite(x)]
    targets = [x for x in targets if math.isfinite(x)]
    positions = [x for x in positions if math.isfinite(x)]
    velocities = [x for x in velocities if math.isfinite(x)]

    if not errors or not targets or not positions:
        return None

    abs_errors = [abs(x) for x in errors]
    rms_error = math.sqrt(sum(x * x for x in errors) / len(errors))
    mean_abs_error = sum(abs_errors) / len(abs_errors)
    target_range = max(targets) - min(targets)
    pos_range = max(positions) - min(positions)
    final_error = errors[-1]
    max_abs_error = max(abs_errors)
    rms_velocity = math.sqrt(sum(v * v for v in velocities) / len(velocities)) if velocities else 0.0
    lag_ms, lag_corr = estimate_lag(times, targets, positions)
    amp_ratio = pos_range / max(target_range, 1.0e-6)

    overshoot = 0.0
    if target_range > 1.0e-6:
        low = min(targets[0], targets[-1])
        high = max(targets[0], targets[-1])
        if targets[-1] >= targets[0]:
            overshoot = max(0.0, max(positions) - high)
        else:
            overshoot = max(0.0, low - min(positions))

    return {
        "joint": joint,
        "samples": len(errors),
        "target_range": target_range,
        "position_range": pos_range,
        "rms_error": rms_error,
        "mean_abs_error": mean_abs_error,
        "max_abs_error": max_abs_error,
        "final_error": final_error,
        "overshoot": overshoot,
        "rms_velocity": rms_velocity,
        "lag_ms": lag_ms,
        "lag_corr": lag_corr,
        "amp_ratio": amp_ratio,
    }


def estimate_lag(times, targets, positions):
    if len(times) < 4 or len(targets) != len(positions) or len(times) != len(targets):
        return float("nan"), 0.0
    dt = (times[-1] - times[0]) / max(len(times) - 1, 1)
    if dt <= 0.0:
        return float("nan"), 0.0

    target_mean = sum(targets) / len(targets)
    position_mean = sum(positions) / len(positions)
    target_centered = [x - target_mean for x in targets]
    position_centered = [x - position_mean for x in positions]
    if max(target_centered) - min(target_centered) < 1.0e-4:
        return float("nan"), 0.0

    max_lag = min(int(0.40 / dt), len(times) - 2)
    best_lag = 0
    best_corr = -1.0
    for lag in range(0, max_lag + 1):
        if lag == 0:
            a = target_centered
            b = position_centered
        else:
            a = target_centered[:-lag]
            b = position_centered[lag:]
        denom = math.sqrt(sum(x * x for x in a) * sum(x * x for x in b))
        corr = sum(x * y for x, y in zip(a, b)) / denom if denom > 1.0e-12 else 0.0
        if corr > best_corr:
            best_lag = lag
            best_corr = corr

    return best_lag * dt * 1000.0, best_corr


def recommendation(m):
    if m["target_range"] < 0.02:
        return "target motion too small for gain matching"
    ratio = m["amp_ratio"]
    overshoot_ratio = m["overshoot"] / max(m["target_range"], 1.0e-6)
    final_ratio = abs(m["final_error"]) / max(m["target_range"], 1.0e-6)

    if math.isfinite(m["lag_ms"]) and m["lag_ms"] > 100.0 and m["lag_corr"] > 0.70:
        return "dominant actuator lag: match sim with actuator_lag/tau before changing Kp/Kd"
    if ratio < 0.70 or final_ratio > 0.35:
        return "actual response is weak/slow: lower sim Kp or add delay, or raise real servo gain if possible"
    if overshoot_ratio > 0.20:
        return "actual response overshoots: raise sim damping or lower real Kp if possible"
    if m["rms_error"] > 0.08:
        return "tracking error high: compare with sim replay before training"
    return "response looks usable; match sim replay metrics near this joint"


def main():
    parser = argparse.ArgumentParser(
        description="Summarize target-feedback logs for Kp/Kd response matching."
    )
    parser.add_argument("csv_path", type=Path)
    args = parser.parse_args()

    rows = load_rows(args.csv_path)
    if not rows:
        raise SystemExit("CSV has no samples")

    print(f"samples: {len(rows)}")
    print(
        "joint,target_range,pos_range,rms_err,mean_abs_err,max_abs_err,"
        "final_err,overshoot,rms_vel,lag_ms,lag_corr,amp_ratio,recommendation"
    )
    for joint in range(NUM_JOINTS):
        m = metrics(rows, joint)
        if m is None:
            continue
        print(
            f"{joint},"
            f"{m['target_range']:.5f},"
            f"{m['position_range']:.5f},"
            f"{m['rms_error']:.5f},"
            f"{m['mean_abs_error']:.5f},"
            f"{m['max_abs_error']:.5f},"
            f"{m['final_error']:.5f},"
            f"{m['overshoot']:.5f},"
            f"{m['rms_velocity']:.5f},"
            f"{m['lag_ms']:.1f},"
            f"{m['lag_corr']:.3f},"
            f"{m['amp_ratio']:.2f},"
            f"{recommendation(m)}"
        )


if __name__ == "__main__":
    main()
