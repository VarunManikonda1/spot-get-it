from legged_gym.envs import *
from legged_gym.utils import get_args, task_registry
import torch
import numpy as np


def print_matrix(name, arr, precision=4):
    np.set_printoptions(precision=precision, suppress=True)
    print(f"{name}:")
    print(arr)


def main(args):
    # ============================================================
    # 1. 환경 생성
    # ============================================================
    env_cfg, train_cfg = task_registry.get_cfgs(name=args.task)

    # viewer에서 보기 쉽게 1개 환경만 사용
    env_cfg.env.num_envs = 1

    # 테스트에서는 외란/노이즈 끄기
    env_cfg.terrain.curriculum = False
    env_cfg.noise.add_noise = False

    env_cfg.domain_rand.randomize_friction = False
    env_cfg.domain_rand.randomize_base_mass = False
    env_cfg.domain_rand.push_robots = False
    env_cfg.domain_rand.action_delay = False

    # 평지 고정
    env_cfg.terrain.mesh_type = "plane"
    env_cfg.terrain.measure_heights = False

    env, _ = task_registry.make_env(
        name=args.task,
        args=args,
        env_cfg=env_cfg,
    )

    # ============================================================
    # 2. 확인용 이름 출력
    # ============================================================
    print("\n" + "=" * 80)
    print("DOF names")
    print("=" * 80)
    for i, name in enumerate(env.dof_names):
        print(f"{i:02d}: {name}")

    print("\n" + "=" * 80)
    print("Feet rigid body names")
    print("=" * 80)
    try:
        for i, idx in enumerate(env.feet_indices.cpu().numpy()):
            body_name = env.gym.get_actor_rigid_body_name(
                env.envs[0],
                env.actor_handles[0],
                int(idx),
            )
            print(f"foot_{i}: body index {idx}, name={body_name}")
    except Exception as e:
        print("feet body name 출력 실패:", e)

    print("\nRigid body names")
    print("=" * 80)

    try:
        body_names = env.body_names
    except AttributeError:
        body_names = env.gym.get_actor_rigid_body_names(
            env.envs[0],
            env.actor_handles[0],
        )

    for i, name in enumerate(body_names):
        print(f"{i:02d}: {name}")
    # ============================================================
    # 3. 테스트 명령 선택
    # ============================================================
    # A. 정지 테스트
    #FIXED_VX = 0.0
    #FIXED_VY = 0.0
    #FIXED_WZ = 0.0

    # B. 직진 IK 테스트를 보려면 위 대신 아래 사용
    FIXED_VX = 0.2
    FIXED_VY = 0.0
    FIXED_WZ = 0.0
    FIXED_PHASE = 0.75

    # C. 제자리 회전 IK 테스트
    # FIXED_VX = 0.0
    # FIXED_VY = 0.0
    # FIXED_WZ = 0.2

    # zero action
    zero_actions = torch.zeros(
        env.num_envs,
        env.num_actions,
        device=env.device,
        dtype=torch.float,
    )

    # ============================================================
    # 4. 시뮬레이션 루프
    # ============================================================
    num_steps = 3000

    for step in range(num_steps):
        # command는 매 step 강제로 고정
        env.commands[:, 0] = FIXED_VX
        env.commands[:, 1] = FIXED_VY
        env.commands[:, 2] = FIXED_WZ
        #env.gait_phase[:] = FIXED_PHASE

        # zero action으로만 step
        obs, _, rew, dones, infos = env.step(zero_actions)

        # reset되면 다시 command 고정
        env.commands[:, 0] = FIXED_VX
        env.commands[:, 1] = FIXED_VY
        env.commands[:, 2] = FIXED_WZ
        #env.gait_phase[:] = FIXED_PHASE

        if step % 100 == 0:
            robot = 0

            ref = env._get_ik_target()[robot].detach().cpu().numpy().reshape(4, 3)
            dof = env.dof_pos[robot].detach().cpu().numpy().reshape(4, 3)
            torque = env.torques[robot].detach().cpu().numpy().reshape(4, 3)

            contact_fz = (
                env.contact_forces[robot, env.feet_indices, 2]
                .detach()
                .cpu()
                .numpy()
            )

            root = env.root_states[robot].detach().cpu().numpy()
            base_lin_vel = env.base_lin_vel[robot].detach().cpu().numpy()
            base_ang_vel = env.base_ang_vel[robot].detach().cpu().numpy()
            projected_gravity = env.projected_gravity[robot].detach().cpu().numpy()

            # pitch/roll 추정
            gx, gy, gz = projected_gravity
            roll = np.arctan2(gy, -gz)
            pitch = np.arctan2(-gx, -gz)

            print("\n" + "=" * 80)
            print(
                f"[step {step}] cmd=({FIXED_VX:.2f}, {FIXED_VY:.2f}, {FIXED_WZ:.2f}) "
                f"done={bool(dones[robot].item())}"
            )
            print(f"base height z: {root[2]:.4f} m")
            print(f"roll: {np.degrees(roll):+.2f} deg, pitch: {np.degrees(pitch):+.2f} deg")
            print(f"base_lin_vel: {base_lin_vel}")
            print(f"base_ang_vel: {base_ang_vel}")
            print(f"projected_gravity: {projected_gravity}")
            print(f"gait_phase: {env.gait_phase[robot].item():.4f}")

            print_matrix("ref_dof_pos [FL, FR, RL, RR] x [shoulder, leg, foot]", ref)
            print_matrix("actual_dof_pos [FL, FR, RL, RR] x [shoulder, leg, foot]", dof)
            print_matrix("torques [FL, FR, RL, RR] x [shoulder, leg, foot]", torque)
            print_matrix("contact_fz [foot_0, foot_1, foot_2, foot_3]", contact_fz)

            print("torque_abs_sum_per_leg:")
            print(np.sum(np.abs(torque), axis=1))

            body_forces_z = env.contact_forces[robot, :, 2].detach().cpu().numpy()

            print("\nNon-zero body contact Fz:")
            for i, fz in enumerate(body_forces_z):
                if abs(fz) > 0.5:
                    print(f"{i:02d} {body_names[i]}: Fz={fz:.3f}")


if __name__ == "__main__":
    args = get_args()
    main(args)
