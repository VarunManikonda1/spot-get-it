import argparse
import math
import os
import re
import sys
import time

import isaacgym  # torch보다 먼저 import
import numpy as np
import torch
from isaacgym import gymapi, gymtorch, gymutil
from isaacgym.torch_utils import quat_from_euler_xyz

from legged_gym.envs import *  # noqa: F401,F403
from legged_gym.utils import get_args, task_registry


def parse_custom_args():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--checkpoint-path", type=str, required=True)
    parser.add_argument("--cmd-vx", type=float, default=0.05)
    parser.add_argument("--cmd-vy", type=float, default=0.0)
    parser.add_argument("--cmd-wz", type=float, default=0.0)
    parser.add_argument("--roll-deg", type=float, default=12.0)
    parser.add_argument("--pitch-deg", type=float, default=0.0)
    parser.add_argument("--ang-vel-x", type=float, default=2.0)
    parser.add_argument("--ang-vel-y", type=float, default=0.0)
    parser.add_argument("--lin-vel-y", type=float, default=0.0)
    parser.add_argument(
        "--tilt-mode",
        choices=["impulse", "ramp", "instant"],
        default="impulse",
    )
    parser.add_argument("--impulse-steps", type=int, default=40)
    parser.add_argument("--ramp-steps", type=int, default=20)
    parser.add_argument("--start-step", type=int, default=150)
    parser.add_argument("--tilt-every-steps", type=int, default=350)
    parser.add_argument("--randomize-tilt", action="store_true")
    parser.add_argument("--demo-seed", type=int, default=None)
    parser.add_argument("--duration-steps", type=int, default=5000)
    parser.add_argument("--alternate", action="store_true")
    parser.add_argument("--no-real-time", action="store_true")
    parser.add_argument("--time-scale", type=float, default=1.0)
    parser.add_argument("--no-tilt-marker", action="store_true")
    parser.add_argument(
        "--marker-style",
        choices=["warning", "arrow", "sphere", "both", "none"],
        default="warning",
    )
    parser.add_argument("--marker-steps", type=int, default=35)
    parser.add_argument("--marker-height", type=float, default=0.22)
    parser.add_argument("--marker-radius", type=float, default=0.055)
    parser.add_argument("--warning-size", type=float, default=0.18)
    parser.add_argument("--warning-fill-lines", type=int, default=9)
    parser.add_argument("--warning-mark-lines", type=int, default=13)
    parser.add_argument("--arrow-length", type=float, default=0.34)
    parser.add_argument("--arrow-contact-offset", type=float, default=0.075)
    parser.add_argument("--arrow-clearance", type=float, default=0.060)
    parser.add_argument("--arrow-head", type=float, default=0.090)
    parser.add_argument("--arrow-spread", type=float, default=0.075)
    parser.add_argument("--arrow-thickness", type=float, default=0.060)
    custom_args, remaining = parser.parse_known_args()

    sys.argv = [sys.argv[0]] + remaining
    lg_args = get_args()
    return custom_args, lg_args


def set_checkpoint(train_cfg, checkpoint_path: str):
    ckpt = os.path.abspath(checkpoint_path)
    if not os.path.exists(ckpt):
        raise FileNotFoundError(f"checkpoint not found: {ckpt}")

    ck_dir = os.path.dirname(ckpt)
    ck_name = os.path.basename(ckpt)
    match = re.search(r"model_(\d+)\.pt", ck_name)
    if not match:
        raise ValueError(f"checkpoint filename must look like model_4100.pt: {ck_name}")

    train_cfg.runner.resume = True
    train_cfg.runner.load_run = ck_dir
    train_cfg.runner.checkpoint = int(match.group(1))


def apply_root_tilt(env, roll_deg: float, pitch_deg: float, ang_vel_x: float, ang_vel_y: float):
    roll = torch.full((env.num_envs,), math.radians(roll_deg), device=env.device)
    pitch = torch.full((env.num_envs,), math.radians(pitch_deg), device=env.device)
    yaw = torch.zeros(env.num_envs, device=env.device)

    env.root_states[:, 3:7] = quat_from_euler_xyz(roll, pitch, yaw)
    env.root_states[:, 7:13] = 0.0
    env.root_states[:, 10] = float(ang_vel_x)
    env.root_states[:, 11] = float(ang_vel_y)
    env.root_states[:, 2] = torch.maximum(
        env.root_states[:, 2],
        env.base_init_state[2] + env.env_origins[:, 2],
    )

    env.gym.set_actor_root_state_tensor(env.sim, gymtorch.unwrap_tensor(env.root_states))
    env.gym.refresh_actor_root_state_tensor(env.sim)


def apply_root_velocity(env, lin_vel_y: float, ang_vel_x: float, ang_vel_y: float):
    env.root_states[:, 8] = float(lin_vel_y)
    env.root_states[:, 10] = float(ang_vel_x)
    env.root_states[:, 11] = float(ang_vel_y)
    env.gym.set_actor_root_state_tensor(env.sim, gymtorch.unwrap_tensor(env.root_states))
    env.gym.refresh_actor_root_state_tensor(env.sim)


def smoothstep(x: float):
    x = max(0.0, min(1.0, x))
    return x * x * (3.0 - 2.0 * x)


def draw_tilt_marker(env, marker_geom, marker_remaining: int, marker_height: float):
    if env.viewer is None or marker_remaining <= 0:
        return

    root = env.root_states[0, :3].detach().cpu().numpy()
    pose = gymapi.Transform(
        gymapi.Vec3(
            float(root[0]),
            float(root[1]),
            float(root[2] + marker_height),
        ),
        gymapi.Quat(0.0, 0.0, 0.0, 1.0),
    )
    gymutil.draw_lines(marker_geom, env.gym, env.viewer, env.envs[0], pose)


def draw_warning_marker(
    env,
    marker_remaining: int,
    marker_height: float,
    warning_size: float,
    warning_fill_lines: int,
    warning_mark_lines: int,
):
    if env.viewer is None or marker_remaining <= 0:
        return

    root = env.root_states[0, :3].detach().cpu().numpy()
    center = np.array(
        [
            float(root[0]),
            float(root[1]),
            float(root[2] + marker_height),
        ],
        dtype=np.float32,
    )

    fill_color = [1.0, 0.0, 0.0]
    mark_color = [1.0, 1.0, 1.0]
    vertices = []
    colors = []

    def add_line(a, b, c):
        vertices.extend([float(a[0]), float(a[1]), float(a[2])])
        vertices.extend([float(b[0]), float(b[1]), float(b[2])])
        colors.extend([float(c[0]), float(c[1]), float(c[2])])

    def add_filled_exclamation(axis):
        z_axis = np.array([0.0, 0.0, 1.0], dtype=np.float32)
        side_axis = np.cross(axis, z_axis).astype(np.float32)
        bar_half_width = warning_size * 0.055
        bar_bottom = -warning_size * 0.11
        bar_top = warning_size * 0.24
        rows = max(5, int(warning_mark_lines))

        for offset in np.linspace(-bar_half_width, bar_half_width, rows):
            start = center + side_axis * float(offset) + z_axis * bar_bottom
            end = center + side_axis * float(offset) + z_axis * bar_top
            add_line(start, end, mark_color)
        for z in np.linspace(bar_bottom, bar_top, max(4, rows // 2)):
            start = center - side_axis * bar_half_width + z_axis * float(z)
            end = center + side_axis * bar_half_width + z_axis * float(z)
            add_line(start, end, mark_color)

        dot_center = center - z_axis * (warning_size * 0.30)
        dot_half = warning_size * 0.055
        for offset in np.linspace(-dot_half, dot_half, rows):
            chord_half = math.sqrt(max(0.0, dot_half * dot_half - float(offset) * float(offset)))
            start = dot_center + z_axis * float(offset) - side_axis * chord_half
            end = dot_center + z_axis * float(offset) + side_axis * chord_half
            add_line(start, end, mark_color)

    def add_filled_triangle(axis):
        z_axis = np.array([0.0, 0.0, 1.0], dtype=np.float32)
        half_width = warning_size * 0.45
        half_height = warning_size * 0.50
        top = center + z_axis * half_height
        left = center - axis * half_width - z_axis * half_height
        right = center + axis * half_width - z_axis * half_height

        rows = max(6, int(warning_fill_lines))
        for i in range(rows):
            t = i / max(1, rows - 1)
            z = -half_height + t * (2.0 * half_height)
            row_half_width = half_width * (1.0 - t)
            row_center = center + z_axis * z
            add_line(row_center - axis * row_half_width, row_center + axis * row_half_width, fill_color)

        for i in range(rows // 2):
            t = (i + 0.5) / max(1, rows // 2)
            start = left * (1.0 - t) + top * t
            end = right * (1.0 - t) + top * t
            add_line(start, end, fill_color)

        add_line(top, left, fill_color)
        add_line(left, right, fill_color)
        add_line(right, top, fill_color)
        add_filled_exclamation(axis)

    add_filled_triangle(np.array([1.0, 0.0, 0.0], dtype=np.float32))

    num_lines = len(colors) // 3
    env.gym.add_lines(env.viewer, env.envs[0], num_lines, vertices, colors)


def draw_push_arrow(
    env,
    sign: float,
    marker_remaining: int,
    marker_height: float,
    arrow_length: float,
    arrow_contact_offset: float,
    arrow_clearance: float,
    arrow_head: float,
    arrow_spread: float,
    arrow_thickness: float,
):
    if env.viewer is None or marker_remaining <= 0:
        return

    root = env.root_states[0, :3].detach().cpu().numpy()
    direction = np.array([0.0, 1.0 if sign >= 0.0 else -1.0, 0.0], dtype=np.float32)
    x_axis = np.array([1.0, 0.0, 0.0], dtype=np.float32)
    z_axis = np.array([0.0, 0.0, 1.0], dtype=np.float32)

    base = np.array(
        [
            float(root[0]),
            float(root[1]),
            float(root[2] + marker_height),
        ],
        dtype=np.float32,
    )

    # The arrow tail is outside the body, and its tip stays just outside the side being hit.
    outside_offset = float(arrow_contact_offset) + max(0.0, float(arrow_clearance))
    tip = base - direction * outside_offset
    tail = tip - direction * float(arrow_length)
    head_base = tip - direction * float(arrow_head)

    color = [1.0, 0.0, 0.0]

    vertices = []
    colors = []

    def add_line(a, b, c):
        vertices.extend([float(a[0]), float(a[1]), float(a[2])])
        vertices.extend([float(b[0]), float(b[1]), float(b[2])])
        colors.extend([float(c[0]), float(c[1]), float(c[2])])

    half_thickness = max(0.01, float(arrow_thickness) * 0.5)
    head_half = max(float(arrow_spread), half_thickness * 1.4)

    shaft_offsets = []
    for dx in np.linspace(-half_thickness, half_thickness, 5):
        for dz in np.linspace(-half_thickness, half_thickness, 5):
            shaft_offsets.append(x_axis * float(dx) + z_axis * float(dz))

    for offset in shaft_offsets:
        add_line(tail + offset, head_base + offset, color)

    shaft_corners = [
        x_axis * half_thickness + z_axis * half_thickness,
        x_axis * half_thickness - z_axis * half_thickness,
        -x_axis * half_thickness - z_axis * half_thickness,
        -x_axis * half_thickness + z_axis * half_thickness,
    ]
    for center in (tail, head_base):
        for i, corner in enumerate(shaft_corners):
            add_line(center + corner, center + shaft_corners[(i + 1) % len(shaft_corners)], color)

    head_grid = np.linspace(-head_half, head_half, 7)
    for dx in head_grid:
        for dz in head_grid:
            point = head_base + x_axis * float(dx) + z_axis * float(dz)
            add_line(point, tip, color)

    for dx in head_grid:
        add_line(
            head_base + x_axis * float(dx) - z_axis * head_half,
            head_base + x_axis * float(dx) + z_axis * head_half,
            color,
        )
    for dz in head_grid:
        add_line(
            head_base - x_axis * head_half + z_axis * float(dz),
            head_base + x_axis * head_half + z_axis * float(dz),
            color,
        )

    head_corners = [
        head_base + x_axis * head_half + z_axis * head_half,
        head_base + x_axis * head_half - z_axis * head_half,
        head_base - x_axis * head_half - z_axis * head_half,
        head_base - x_axis * head_half + z_axis * head_half,
    ]
    for corner in head_corners:
        add_line(corner, tip, color)
    for i, corner in enumerate(head_corners):
        add_line(corner, head_corners[(i + 1) % len(head_corners)], color)

    impact_half = max(0.035, head_half * 0.65)
    impact_corners = [
        tip + x_axis * impact_half + z_axis * impact_half,
        tip + x_axis * impact_half - z_axis * impact_half,
        tip - x_axis * impact_half - z_axis * impact_half,
        tip - x_axis * impact_half + z_axis * impact_half,
    ]
    for i, corner in enumerate(impact_corners):
        add_line(corner, impact_corners[(i + 1) % len(impact_corners)], color)
    add_line(tip - x_axis * impact_half, tip + x_axis * impact_half, color)
    add_line(tip - z_axis * impact_half, tip + z_axis * impact_half, color)

    num_lines = len(colors) // 3
    env.gym.add_lines(env.viewer, env.envs[0], num_lines, vertices, colors)


def roll_pitch_from_projected_gravity(env):
    gravity = env.projected_gravity[0].detach().cpu().numpy()
    gx, gy, gz = gravity
    roll = math.degrees(math.atan2(float(gy), -float(gz)))
    pitch = math.degrees(math.atan2(-float(gx), -float(gz)))
    return roll, pitch


def resolve_demo_seed(custom_args, args, train_cfg):
    if custom_args.demo_seed == -1:
        return int(time.time_ns() % (2**32 - 1))
    if custom_args.demo_seed is not None:
        return int(custom_args.demo_seed)
    if args.seed is not None:
        return int(args.seed)
    return int(train_cfg.seed)


def sample_tilt_event(custom_args, tilt_count: int, rng):
    if not custom_args.randomize_tilt:
        sign = -1.0 if custom_args.alternate and (tilt_count % 2 == 1) else 1.0
        return (
            sign,
            float(custom_args.roll_deg),
            float(custom_args.pitch_deg),
            float(custom_args.ang_vel_x),
            float(custom_args.ang_vel_y),
            float(custom_args.lin_vel_y),
        )

    sign = float(rng.choice([-1.0, 1.0]))
    roll_deg = float(abs(custom_args.roll_deg) * rng.uniform(0.55, 1.0))
    pitch_limit = abs(float(custom_args.pitch_deg))
    pitch_deg = float(rng.uniform(-pitch_limit, pitch_limit)) if pitch_limit > 0.0 else 0.0
    ang_vel_x = float(abs(custom_args.ang_vel_x) * rng.uniform(0.70, 1.35))
    ang_vel_y = float(custom_args.ang_vel_y * rng.uniform(0.70, 1.35))
    lin_vel_y = float(abs(custom_args.lin_vel_y) * rng.uniform(0.70, 1.35))
    return sign, roll_deg, pitch_deg, ang_vel_x, ang_vel_y, lin_vel_y


def main():
    custom_args, args = parse_custom_args()

    env_cfg, train_cfg = task_registry.get_cfgs(name=args.task)
    if args.seed is not None:
        env_cfg.seed = int(args.seed)
        train_cfg.seed = int(args.seed)
    demo_seed = resolve_demo_seed(custom_args, args, train_cfg)
    demo_rng = np.random.default_rng(demo_seed)

    env_cfg.env.num_envs = 1
    env_cfg.terrain.num_rows = 1
    env_cfg.terrain.num_cols = 1
    env_cfg.terrain.curriculum = False
    env_cfg.terrain.measure_heights = False
    env_cfg.noise.add_noise = False
    env_cfg.domain_rand.randomize_friction = False
    env_cfg.domain_rand.randomize_base_mass = False
    env_cfg.domain_rand.push_robots = False
    env_cfg.domain_rand.recovery_roll_pitch_range_deg = 0.0
    env_cfg.domain_rand.recovery_lin_vel_xy_range = 0.0
    env_cfg.domain_rand.recovery_lin_vel_z_range = 0.0
    env_cfg.domain_rand.recovery_ang_vel_xy_range = 0.0
    env_cfg.domain_rand.recovery_ang_vel_z_range = 0.0

    set_checkpoint(train_cfg, custom_args.checkpoint_path)

    env, _ = task_registry.make_env(name=args.task, args=args, env_cfg=env_cfg)
    obs = env.get_observations()

    ppo_runner, train_cfg = task_registry.make_alg_runner(
        env=env,
        name=args.task,
        args=args,
        train_cfg=train_cfg,
    )
    policy = ppo_runner.get_inference_policy(device=env.device)

    print("[INFO] recovery demo")
    print(f"[INFO] checkpoint: {os.path.abspath(custom_args.checkpoint_path)}")
    print(
        "[INFO] command: "
        f"vx={custom_args.cmd_vx:.3f}, vy={custom_args.cmd_vy:.3f}, wz={custom_args.cmd_wz:.3f}"
    )
    print(
        "[INFO] tilt: "
        f"roll={custom_args.roll_deg:.1f} deg, pitch={custom_args.pitch_deg:.1f} deg, "
        f"ang_vel_x={custom_args.ang_vel_x:.2f}, ang_vel_y={custom_args.ang_vel_y:.2f}, "
        f"lin_vel_y={custom_args.lin_vel_y:.2f}, mode={custom_args.tilt_mode}"
    )
    print(
        f"[INFO] seed: env={env_cfg.seed}, demo={demo_seed}, "
        f"randomize_tilt={custom_args.randomize_tilt}"
    )
    print(
        f"[INFO] env.dt={env.dt:.4f}s, sim_dt={env.sim_params.dt:.4f}s, "
        f"decimation={env.cfg.control.decimation}, gait_period={env.gait_period:.3f}s"
    )
    if custom_args.no_real_time:
        print("[INFO] real-time pacing: disabled")
    else:
        print(f"[INFO] real-time pacing: enabled, time_scale={custom_args.time_scale:.2f}")
    print("[INFO] viewer shortcut: V toggles sync, ESC exits")

    tilt_count = 0
    impulse_remaining = 0
    impulse_sign = 1.0
    impulse_target_abs_roll_deg = 0.0
    impulse_ang_vel_x = float(custom_args.ang_vel_x)
    impulse_ang_vel_y = float(custom_args.ang_vel_y)
    impulse_lin_vel_y = float(custom_args.lin_vel_y)
    ramp_remaining = 0
    ramp_sign = 1.0
    ramp_roll_deg = float(custom_args.roll_deg)
    ramp_pitch_deg = float(custom_args.pitch_deg)
    ramp_ang_vel_x = float(custom_args.ang_vel_x)
    ramp_ang_vel_y = float(custom_args.ang_vel_y)
    marker_remaining = 0
    marker_sign = 1.0
    marker_geom = gymutil.WireframeSphereGeometry(
        custom_args.marker_radius,
        12,
        12,
        None,
        color=(1.0, 0.15, 0.05),
    )
    for step in range(int(custom_args.duration_steps)):
        loop_start = time.perf_counter()

        env.commands[:, 0] = float(custom_args.cmd_vx)
        env.commands[:, 1] = float(custom_args.cmd_vy)
        env.commands[:, 2] = float(custom_args.cmd_wz)

        should_tilt = (
            step >= custom_args.start_step
            and (step - custom_args.start_step) % custom_args.tilt_every_steps == 0
        )
        if should_tilt:
            (
                sign,
                event_roll_deg,
                event_pitch_deg,
                event_ang_vel_x,
                event_ang_vel_y,
                event_lin_vel_y,
            ) = sample_tilt_event(custom_args, tilt_count, demo_rng)
            if custom_args.tilt_mode == "instant":
                apply_root_tilt(
                    env,
                    roll_deg=sign * event_roll_deg,
                    pitch_deg=event_pitch_deg,
                    ang_vel_x=sign * event_ang_vel_x,
                    ang_vel_y=event_ang_vel_y,
                )
            elif custom_args.tilt_mode == "ramp":
                ramp_remaining = max(1, int(custom_args.ramp_steps))
                ramp_sign = sign
                ramp_roll_deg = event_roll_deg
                ramp_pitch_deg = event_pitch_deg
                ramp_ang_vel_x = event_ang_vel_x
                ramp_ang_vel_y = event_ang_vel_y
            else:
                impulse_remaining = max(1, int(custom_args.impulse_steps))
                impulse_sign = sign
                impulse_target_abs_roll_deg = abs(event_roll_deg)
                impulse_ang_vel_x = event_ang_vel_x
                impulse_ang_vel_y = event_ang_vel_y
                impulse_lin_vel_y = event_lin_vel_y
            tilt_count += 1
            print(
                f"[TILT] step={step}, mode={custom_args.tilt_mode}, "
                f"sign={sign:+.0f}, target_roll={sign * event_roll_deg:+.1f} deg, "
                f"pitch={event_pitch_deg:+.1f} deg, ang_vel_x={event_ang_vel_x:.2f}"
            )
            if not custom_args.no_tilt_marker:
                marker_remaining = max(marker_remaining, int(custom_args.marker_steps))
                marker_sign = sign

        if ramp_remaining > 0:
            ramp_total = max(1, int(custom_args.ramp_steps))
            ramp_i = ramp_total - ramp_remaining + 1
            frac = smoothstep(ramp_i / ramp_total)
            apply_root_tilt(
                env,
                roll_deg=ramp_sign * ramp_roll_deg * frac,
                pitch_deg=ramp_pitch_deg * frac,
                ang_vel_x=ramp_sign * ramp_ang_vel_x,
                ang_vel_y=ramp_ang_vel_y,
            )
            ramp_remaining -= 1

        if impulse_remaining > 0:
            current_roll, _ = roll_pitch_from_projected_gravity(env)
            if abs(current_roll) < impulse_target_abs_roll_deg:
                apply_root_velocity(
                    env,
                    lin_vel_y=impulse_sign * impulse_lin_vel_y,
                    ang_vel_x=impulse_sign * impulse_ang_vel_x,
                    ang_vel_y=impulse_ang_vel_y,
                )
                impulse_remaining -= 1
            else:
                impulse_remaining = 0
                print(f"[TILT] reached roll target: current_roll={current_roll:+.2f} deg")

        if env.viewer is not None and not custom_args.no_tilt_marker:
            env.gym.clear_lines(env.viewer)
            marker_style = "none" if custom_args.no_tilt_marker else custom_args.marker_style
            if marker_style in ("warning", "both"):
                draw_warning_marker(
                    env,
                    marker_remaining=marker_remaining,
                    marker_height=custom_args.marker_height,
                    warning_size=custom_args.warning_size,
                    warning_fill_lines=custom_args.warning_fill_lines,
                    warning_mark_lines=custom_args.warning_mark_lines,
                )
            if marker_style == "arrow":
                draw_push_arrow(
                    env,
                    sign=marker_sign,
                    marker_remaining=marker_remaining,
                    marker_height=custom_args.marker_height,
                    arrow_length=custom_args.arrow_length,
                    arrow_contact_offset=custom_args.arrow_contact_offset,
                    arrow_clearance=custom_args.arrow_clearance,
                    arrow_head=custom_args.arrow_head,
                    arrow_spread=custom_args.arrow_spread,
                    arrow_thickness=custom_args.arrow_thickness,
                )
            if marker_style in ("sphere", "both"):
                draw_tilt_marker(
                    env,
                    marker_geom=marker_geom,
                    marker_remaining=marker_remaining,
                    marker_height=custom_args.marker_height,
                )
            marker_remaining = max(0, marker_remaining - 1)

        actions = policy(obs.detach())
        obs, _, _, _, _ = env.step(actions.detach())

        if step % 25 == 0:
            roll, pitch = roll_pitch_from_projected_gravity(env)
            height = float(env.root_states[0, 2].detach().cpu())
            phase = float(env.gait_phase[0, 0].detach().cpu())
            print(
                f"[step {step:04d} t={step * env.dt:.2f}s phase={phase:.3f}] "
                f"roll={roll:+.2f} deg, "
                f"pitch={pitch:+.2f} deg, height={height:.3f} m"
            )

        if not custom_args.no_real_time:
            target_dt = env.dt / max(custom_args.time_scale, 1e-6)
            elapsed = time.perf_counter() - loop_start
            sleep_s = target_dt - elapsed
            if sleep_s > 0.0:
                time.sleep(sleep_s)


if __name__ == "__main__":
    main()
