from legged_gym.envs import *
from legged_gym.utils import get_args, task_registry
import torch
import numpy as np

def main(args):
    env_cfg, train_cfg = task_registry.get_cfgs(name=args.task)

    # viewer 확인용: 환경 수 줄이기
    env_cfg.env.num_envs = 1
    env_cfg.terrain.curriculum = False
    env_cfg.noise.add_noise = False
    env_cfg.domain_rand.randomize_friction = False
    env_cfg.domain_rand.push_robots = False

    env, _ = task_registry.make_env(name=args.task, args=args, env_cfg=env_cfg)

    # 테스트 command
    FIXED_VX = 0.0
    FIXED_VY = 0.0
    FIXED_WZ = 0.2

    zero_actions = torch.zeros(
        env.num_envs,
        env.num_actions,
        device=env.device,
        dtype=torch.float
    )

    for i in range(3000):
        env.commands[:, 0] = FIXED_VX
        env.commands[:, 1] = FIXED_VY
        env.commands[:, 2] = FIXED_WZ

        # phase를 고정해서 특정 순간의 IK만 보고 싶으면 아래 줄 사용
        # env.gait_phase[:] = 0.25

        if i % 50 == 0:
            ref = env._get_ik_target()[0].detach().cpu().numpy()
            dof = env.dof_pos[0].detach().cpu().numpy()

            print("\n" + "=" * 80)
            print(f"[step {i}] cmd = vx:{FIXED_VX:.2f}, vy:{FIXED_VY:.2f}, wz:{FIXED_WZ:.2f}")
            print("ref_dof_pos [FL, FR, RL, RR] x [shoulder, leg, foot]:")
            print(ref.reshape(4, 3))
            print("actual dof_pos:")
            print(dof.reshape(4, 3))

        env.step(zero_actions)

if __name__ == "__main__":
    args = get_args()
    main(args)
