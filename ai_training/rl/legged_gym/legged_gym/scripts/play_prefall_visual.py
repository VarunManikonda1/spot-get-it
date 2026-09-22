from legged_gym import LEGGED_GYM_ROOT_DIR
import os
import sys

import isaacgym
from isaacgym import gymtorch
from isaacgym.torch_utils import quat_from_euler_xyz
from legged_gym.envs import *
from legged_gym.utils import get_args, task_registry

import numpy as np
import torch


def _pop_flag(name):
    enabled = name in sys.argv
    if enabled:
        sys.argv.remove(name)
    return enabled


def _pop_value(name, default, cast):
    i = 1
    while i < len(sys.argv):
        arg = sys.argv[i]
        if arg == name and i + 1 < len(sys.argv):
            value = cast(sys.argv[i + 1])
            del sys.argv[i:i + 2]
            return value
        prefix = name + "="
        if arg.startswith(prefix):
            value = cast(arg.split("=", 1)[1])
            del sys.argv[i]
            return value
        i += 1
    return default


def _apply_prefall_tilt(env, tilt_deg, axis, ang_vel):
    roll = tilt_deg if axis == "roll" else 0.0
    pitch = tilt_deg if axis == "pitch" else 0.0
    roll_t = torch.tensor([np.radians(roll)], device=env.device)
    pitch_t = torch.tensor([np.radians(pitch)], device=env.device)
    yaw_t = torch.zeros(1, device=env.device)

    env.root_states[0, 3:7] = quat_from_euler_xyz(roll_t, pitch_t, yaw_t)[0]
    env.root_states[0, 7:10] = 0.0
    env.root_states[0, 10:13] = 0.0
    if axis == "roll":
        env.root_states[0, 10] = np.radians(ang_vel)
    else:
        env.root_states[0, 11] = np.radians(ang_vel)

    env_ids = torch.tensor([0], dtype=torch.int32, device=env.device)
    env.gym.set_actor_root_state_tensor_indexed(
        env.sim,
        gymtorch.unwrap_tensor(env.root_states),
        gymtorch.unwrap_tensor(env_ids),
        1,
    )


def _tilt_deg(env):
    tilt = torch.norm(env.projected_gravity[0, :2]).clamp(0.0, 1.0)
    return float(torch.asin(tilt).item() * 180.0 / np.pi)


def play_prefall_visual(args, opts):
    env_cfg, train_cfg = task_registry.get_cfgs(name=args.task)
    env_cfg.env.num_envs = 1
    env_cfg.terrain.num_rows = 1
    env_cfg.terrain.num_cols = 1
    env_cfg.terrain.curriculum = False
    env_cfg.domain_rand.push_robots = False
    if not opts["with_dr"]:
        env_cfg.noise.add_noise = False
        env_cfg.domain_rand.randomize_friction = False
        env_cfg.domain_rand.randomize_base_mass = False
        env_cfg.domain_rand.randomize_base_com = False
        env_cfg.domain_rand.randomize_motor_strength = False

    env, _ = task_registry.make_env(name=args.task, args=args, env_cfg=env_cfg)
    obs = env.get_observations()

    train_cfg.runner.resume = True
    ppo_runner, train_cfg = task_registry.make_alg_runner(
        env=env, name=args.task, args=args, train_cfg=train_cfg)
    policy = ppo_runner.get_inference_policy(device=env.device)

    frame_dir = os.path.join(
        LEGGED_GYM_ROOT_DIR,
        "logs",
        train_cfg.runner.experiment_name,
        "exported",
        "prefall_visual_frames",
    )
    if opts["record_frames"]:
        os.makedirs(frame_dir, exist_ok=True)

    camera_position = np.array([0.9, -0.9, 0.45], dtype=np.float64)
    camera_target = np.array([0.0, 0.0, 0.15], dtype=np.float64)
    env.set_camera(camera_position, camera_target)

    triggered = False
    frame_idx = 0
    total_steps = int(opts["duration_s"] / env.dt)
    trigger_step = int(opts["trigger_s"] / env.dt)

    print(
        "[prefall visual] "
        f"run={train_cfg.runner.load_run}, checkpoint={train_cfg.runner.checkpoint}, "
        f"cmd_x={opts['cmd_x']:.2f} m/s, tilt={opts['tilt_deg']:.1f} deg "
        f"axis={opts['axis']}, trigger={opts['trigger_s']:.2f}s"
    )

    for step in range(total_steps):
        env.commands[:, 0] = opts["cmd_x"]
        env.commands[:, 1] = 0.0
        env.commands[:, 2] = 0.0

        if not triggered and step >= trigger_step:
            _apply_prefall_tilt(
                env,
                opts["tilt_deg"],
                opts["axis"],
                opts["ang_vel_deg_s"],
            )
            obs = env.get_observations()
            triggered = True
            print(f"[prefall visual] injected tilt at t={step * env.dt:.2f}s")

        actions = policy(obs.detach())
        obs, _, _, _, _ = env.step(actions.detach())

        if opts["record_frames"] and step % 2 == 0 and getattr(env, "viewer", None) is not None:
            filename = os.path.join(frame_dir, f"{frame_idx:05d}.png")
            env.gym.write_viewer_image_to_file(env.viewer, filename)
            frame_idx += 1

        if step % max(1, int(0.25 / env.dt)) == 0:
            blend = 0.0
            if hasattr(env, "_recovery_blend"):
                blend = float(env._recovery_blend()[0].item())
            eff_cmd_x = opts["cmd_x"]
            if hasattr(env, "_get_effective_commands"):
                eff_cmd_x = float(env._get_effective_commands()[0, 0].item())
            print(
                f"t={step * env.dt:5.2f}s "
                f"tilt={_tilt_deg(env):5.1f}deg "
                f"blend={blend:4.2f} "
                f"cmd_x={opts['cmd_x']:.2f}->{eff_cmd_x:.2f}"
            )

    if opts["record_frames"]:
        print(f"[prefall visual] frames: {frame_dir}")


if __name__ == "__main__":
    opts = {
        "with_dr": _pop_flag("--with-dr"),
        "record_frames": _pop_flag("--record-frames"),
        "cmd_x": _pop_value("--cmd-x", 0.08, float),
        "tilt_deg": _pop_value("--tilt-deg", 28.0, float),
        "axis": _pop_value("--axis", "pitch", str),
        "trigger_s": _pop_value("--trigger-s", 1.5, float),
        "duration_s": _pop_value("--duration-s", 5.0, float),
        "ang_vel_deg_s": _pop_value("--ang-vel-deg-s", 120.0, float),
    }
    if opts["axis"] not in ("roll", "pitch"):
        raise ValueError("--axis must be 'roll' or 'pitch'")

    args = get_args()
    play_prefall_visual(args, opts)
