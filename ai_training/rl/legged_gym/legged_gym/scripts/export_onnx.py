# legged_gym/scripts/export_exp043_onnx.py
# exp043 PyTorch policy -> ONNX export + test_obs/test_action 생성

import argparse
import json
import os
import re
import sys

import isaacgym  # torch보다 먼저 import
import numpy as np
import torch

from legged_gym.envs import *  # noqa: F401,F403
from legged_gym.utils import get_args, task_registry


class ActorInferenceWrapper(torch.nn.Module):
    """rsl_rl ActorCritic에서 actor inference만 ONNX로 export하기 위한 wrapper."""

    def __init__(self, actor_critic):
        super().__init__()
        self.actor_critic = actor_critic

    def forward(self, obs: torch.Tensor) -> torch.Tensor:
        return self.actor_critic.act_inference(obs)


def parse_custom_args():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--checkpoint", type=str, required=True)
    parser.add_argument("--onnx-out", type=str, required=True)
    parser.add_argument("--vector-out-dir", type=str, required=True)
    parser.add_argument("--num-vectors", type=int, default=128)
    parser.add_argument("--opset", type=int, default=11)
    custom_args, remaining = parser.parse_known_args()

    # legged_gym get_args()가 모르는 인자를 제거
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
        raise ValueError(f"checkpoint filename must look like model_1500.pt: {ck_name}")

    train_cfg.runner.resume = True
    train_cfg.runner.load_run = ck_dir
    train_cfg.runner.checkpoint = int(match.group(1))


def main():
    custom_args, args = parse_custom_args()

    env_cfg, train_cfg = task_registry.get_cfgs(name=args.task)

    # export/test vector는 deterministic하게 만든다.
    env_cfg.env.num_envs = 1
    env_cfg.terrain.curriculum = False
    env_cfg.terrain.measure_heights = False
    env_cfg.noise.add_noise = False
    env_cfg.domain_rand.randomize_friction = False
    env_cfg.domain_rand.randomize_base_mass = False
    env_cfg.domain_rand.push_robots = False

    set_checkpoint(train_cfg, custom_args.checkpoint)

    env, _ = task_registry.make_env(name=args.task, args=args, env_cfg=env_cfg)
    obs = env.get_observations()

    ppo_runner, train_cfg = task_registry.make_alg_runner(
        env=env,
        name=args.task,
        args=args,
        train_cfg=train_cfg,
    )

    actor = ActorInferenceWrapper(ppo_runner.alg.actor_critic).to(env.device)
    actor.eval()

    obs_dim = int(env.num_obs)
    action_dim = int(env.num_actions)

    if obs_dim != 47:
        raise RuntimeError(f"unexpected obs_dim: {obs_dim}, expected 47")
    if action_dim != 12:
        raise RuntimeError(f"unexpected action_dim: {action_dim}, expected 12")

    os.makedirs(os.path.dirname(os.path.abspath(custom_args.onnx_out)), exist_ok=True)
    os.makedirs(custom_args.vector_out_dir, exist_ok=True)

    dummy_obs = torch.zeros(1, obs_dim, device=env.device, dtype=torch.float32)

    with torch.no_grad():
        torch.onnx.export(
            actor,
            dummy_obs,
            custom_args.onnx_out,
            input_names=["obs"],
            output_names=["action"],
            dynamic_axes={
                "obs": {0: "batch"},
                "action": {0: "batch"},
            },
            opset_version=custom_args.opset,
            do_constant_folding=True,
        )

    test_obs = []
    test_action = []

    with torch.no_grad():
        for _ in range(custom_args.num_vectors):
            action = actor(obs.detach())

            test_obs.append(obs[0].detach().cpu().numpy().astype(np.float32))
            test_action.append(action[0].detach().cpu().numpy().astype(np.float32))

            obs, _, _, _, _ = env.step(action.detach())

    test_obs = np.stack(test_obs, axis=0).astype(np.float32)
    test_action = np.stack(test_action, axis=0).astype(np.float32)

    obs_path = os.path.join(custom_args.vector_out_dir, "test_obs.npy")
    action_path = os.path.join(custom_args.vector_out_dir, "test_action.npy")
    meta_path = os.path.join(custom_args.vector_out_dir, "metadata.json")

    np.save(obs_path, test_obs)
    np.save(action_path, test_action)

    metadata = {
        "task": args.task,
        "checkpoint": os.path.abspath(custom_args.checkpoint),
        "onnx": os.path.abspath(custom_args.onnx_out),
        "obs_dim": obs_dim,
        "action_dim": action_dim,
        "num_vectors": int(custom_args.num_vectors),
        "obs_dtype": "float32",
        "action_dtype": "float32",
        "input_name": "obs",
        "output_name": "action",
        "note": "test_action is raw PyTorch actor output before ROS2 action clipping/postprocess.",
    }

    with open(meta_path, "w", encoding="utf-8") as f:
        json.dump(metadata, f, indent=2, ensure_ascii=False)

    print("[OK] exported ONNX:", custom_args.onnx_out)
    print("[OK] saved:", obs_path)
    print("[OK] saved:", action_path)
    print("[OK] saved:", meta_path)
    print("[INFO] test_obs shape:", test_obs.shape)
    print("[INFO] test_action shape:", test_action.shape)


if __name__ == "__main__":
    main()
