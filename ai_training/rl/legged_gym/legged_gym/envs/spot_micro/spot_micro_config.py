from legged_gym.envs.base.legged_robot_config import LeggedRobotCfg, LeggedRobotCfgPPO


class SpotMicroCfg(LeggedRobotCfg):
    """Spot Micro 환경 설정"""

    class env(LeggedRobotCfg.env):
        num_envs = 1024          # RTX 4070 8GB → 1024로 시작
        num_observations = 45    # 문서 기준: 3+6+12+12+12
        num_actions = 12         # 관절 목표 각도 오프셋 × 12

    class terrain(LeggedRobotCfg.terrain):
        # Phase 4에서는 평지만 사용. Phase 6에서 curriculum 활성화
        mesh_type = 'plane'      # 'plane', 'heightfield', 'trimesh'
        curriculum = False       # 나중에 True로 변경

    class init_state(LeggedRobotCfg.init_state):
        # 로봇 초기 위치 (지면 위 약간 띄워서 스폰)
        pos = [0.0, 0.0, 0.30]  # [x, y, z] — z는 로봇 다리 길이 고려
        # 초기 자세 (기본값, 모두 0이면 다리가 쭉 펴진 상태)
        default_joint_angles = {
            # 앞다리
            'front_left_shoulder': 0.0,
            'front_left_leg': -0.8,      # 상완 뒤로 접힘
            'front_left_foot': 1.6,      # 하완 앞으로 접힘 (무릎)
            'front_right_shoulder': 0.0,
            'front_right_leg': -0.8,
            'front_right_foot': 1.6,
            # 뒷다리
            'rear_left_shoulder': 0.0,
            'rear_left_leg': -0.8,
            'rear_left_foot': 1.6,
            'rear_right_shoulder': 0.0,
            'rear_right_leg': -0.8,
            'rear_right_foot': 1.6,
        }

    class control(LeggedRobotCfg.control):
        control_type = 'P'       # 'P' = position target (legged_gym 표준)
        # PD 게인 — 시뮬레이터 내부 PD 제어기용
        # Spot Micro는 소형 로봇이므로 ANYmal보다 낮게 시작
        stiffness = {'shoulder': 25.0, 'leg': 25.0, 'foot': 25.0}  # Kp
        damping = {'shoulder': 0.6, 'leg': 0.6, 'foot': 0.6}      # Kd
        # action scale: RL 출력(-1~1)을 실제 각도 오프셋으로 변환
        action_scale = 0.25      # 0.25 rad ≈ 14.3° — 보수적으로 시작
        decimation = 4           # 시뮬 4스텝마다 1번 정책 실행
        # dt=0.005 * decimation=4 → 정책 주기 = 0.02s = 50Hz

    class asset(LeggedRobotCfg.asset):
        file = '{LEGGED_GYM_ROOT_DIR}/resources/robots/spot_micro/urdf/spot_micro.urdf'
        name = 'spot_micro'
        foot_name = 'toe'        # 발 끝 링크 이름에 포함된 문자열
        # Isaac Gym이 이 문자열을 포함하는 링크를 "발"로 인식
        penalize_contacts_on = ['leg_link', 'shoulder_link', 'base_link']
        terminate_after_contacts_on = ['base_link']  # 몸체가 바닥에 닿으면 종료
        self_collisions = 1      # 1 = self-collision 활성화
        flip_visual_attachments = False

    class rewards(LeggedRobotCfg.rewards):
        # Phase 4 초기: 최소 reward만 활성화
        class scales:
            tracking_lin_vel = 1.0      # 속도 추종 (가장 높은 가중치)
            tracking_ang_vel = 0.5      # yaw rate 추종
            termination = -0.0          # 넘어짐 (base 설정에서 이미 처리)
            lin_vel_z = -2.0            # z축 속도 페널티 (위아래 튀는 것 방지)
            ang_vel_xy = -0.05          # roll/pitch 각속도 페널티
            orientation = -0.0          # 처음엔 0으로 두고 나중에 활성화
            torques = -0.0001           # 에너지 효율 (약하게)
            dof_vel = -0.0              # 관절 속도 페널티
            dof_acc = -2.5e-7           # 관절 가속도 페널티
            action_rate = -0.01         # 급격한 액션 변화 페널티
            feet_air_time = 1.0         # swing time 보상 → trot 유도
            dof_pos_limits = -5.0       # 관절 한계 접근 페널티
            collision = -1.0            # 몸체/다리 접촉 페널티
        # 속도 추종 reward 계산 시 허용 오차
        soft_dof_pos_limit = 0.9   # 관절 한계의 90% 넘으면 페널티 시작
        base_height_target = 0.20  # 목표 몸체 높이 (m) — Spot Micro 크기 고려

    class normalization(LeggedRobotCfg.normalization):
        class obs_scales:
            lin_vel = 2.0
            ang_vel = 0.25
            dof_pos = 1.0
            dof_vel = 0.05
            height_measurements = 5.0

    class noise(LeggedRobotCfg.noise):
        add_noise = True
        noise_level = 1.0    # 1.0 = 기본, Phase 5에서 조절
        class noise_scales:
            dof_pos = 0.01
            dof_vel = 1.5
            lin_vel = 0.1
            ang_vel = 0.2
            gravity = 0.05

    class commands(LeggedRobotCfg.commands):
        curriculum = False    # 나중에 True
        max_curriculum = 1.0
        num_commands = 4      # vx, vy, yaw_rate, heading
        resampling_time = 10.0  # 10초마다 새 명령 샘플링
        class ranges:
            lin_vel_x = [-0.3, 0.3]    # m/s — Spot Micro 소형이므로 보수적
            lin_vel_y = [-0.2, 0.2]
            ang_vel_yaw = [-0.5, 0.5]  # rad/s
            heading = [-3.14, 3.14]

    class domain_rand(LeggedRobotCfg.domain_rand):
        # Phase 5에서 활성화. 처음엔 전부 False
        randomize_friction = False
        friction_range = [0.4, 1.2]
        randomize_base_mass = False
        added_mass_range = [-0.5, 0.5]
        push_robots = False
        push_interval_s = 15
        max_push_vel_xy = 0.5


class SpotMicroCfgPPO(LeggedRobotCfgPPO):
    """PPO 학습 하이퍼파라미터 — 대부분 기본값 유지"""

    class algorithm(LeggedRobotCfgPPO.algorithm):
        entropy_coef = 0.01

    class runner(LeggedRobotCfgPPO.runner):
        run_name = 'spot_micro_flat'
        experiment_name = 'spot_micro'
        max_iterations = 1500    # 평지 기본 보행은 300~500에서 수렴 시작
        # 체크포인트 저장 주기
        save_interval = 100
