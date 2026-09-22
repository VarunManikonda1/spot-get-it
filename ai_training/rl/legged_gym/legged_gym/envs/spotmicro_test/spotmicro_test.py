from legged_gym.envs.base.legged_robot import LeggedRobot
from isaacgym.torch_utils import torch_rand_float, quat_from_euler_xyz
from isaacgym import gymtorch
from pathlib import Path
import math
import numpy as np
import sys
import torch


def _ensure_locomotion_common_on_path():
    try:
        from locomotion_common import SharedTrotReference  # noqa: F401
        return
    except ImportError:
        pass

    for parent in Path(__file__).resolve().parents:
        candidate = parent / "robot_ws" / "src" / "control" / "locomotion_common"
        if candidate.exists():
            sys.path.insert(0, str(candidate))
            return


_ensure_locomotion_common_on_path()


class TorchSharedTrotReference:
    def __init__(
        self,
        device,
        dtype=torch.float,
        gait_period=1.0,
        duty_factor=0.55,
        body_height=(0.190, 0.190, 0.190, 0.190),
        step_height=(0.025, 0.025, 0.025, 0.025),
        default_foot_x=(-0.040, -0.040, -0.040, -0.040),
        default_foot_y=(0.052, -0.052, 0.052, -0.052),
        leg_origin_x=(0.093, 0.093, -0.093, -0.093),
        leg_origin_y=(0.036, -0.036, 0.036, -0.036),
        shoulder_sign=(1.0, -1.0, 1.0, -1.0),
        shoulder_offset_y=(0.052, -0.052, 0.052, -0.052),
        phase_offsets=(0.0, 0.5, 0.5, 0.0),
        max_stride_x=0.085,
        max_stride_y=0.024,
        soft_stride_limit=True,
        upper_link_x=0.010,
        upper_link_z=0.120,
        lower_link=0.115,
        toe_radius=0.015,
        shoulder_y_gain=1.0,
        shoulder_limit=0.548,
        joint_min=None,
        joint_max=None,
    ):
        self.device = device
        self.dtype = dtype
        self.gait_period = float(gait_period)
        self.duty_factor = float(duty_factor)
        self.max_stride_x = float(max_stride_x)
        self.max_stride_y = float(max_stride_y)
        self.soft_stride_limit = bool(soft_stride_limit)
        self.upper_link_x = float(upper_link_x)
        self.upper_link_z = float(upper_link_z)
        self.lower_link = float(lower_link)
        self.toe_radius = float(toe_radius)
        self.shoulder_y_gain = float(shoulder_y_gain)
        self.shoulder_limit = float(shoulder_limit)

        self.body_height = self._tensor4(body_height)
        self.step_height = self._tensor4(step_height)
        self.default_foot_x = self._tensor4(default_foot_x)
        self.default_foot_y = self._tensor4(default_foot_y)
        self.leg_origin_x = self._tensor4(leg_origin_x)
        self.leg_origin_y = self._tensor4(leg_origin_y)
        self.shoulder_sign = self._tensor4(shoulder_sign)
        self.shoulder_offset_y = self._tensor4(shoulder_offset_y)
        self.phase_offsets = self._tensor4(phase_offsets)

        if joint_min is not None and joint_max is not None:
            self.joint_min = torch.tensor(
                joint_min, device=device, dtype=dtype).view(1, 12)
            self.joint_max = torch.tensor(
                joint_max, device=device, dtype=dtype).view(1, 12)
        else:
            self.joint_min = None
            self.joint_max = None

    def _tensor4(self, values):
        return torch.tensor(values, device=self.device, dtype=self.dtype).view(1, 4)

    def get_reference(self, phase, commands):
        if phase.dim() == 1:
            phase = phase.unsqueeze(1)

        cmd_vx = commands[:, 0:1]
        cmd_vy = commands[:, 1:2]
        cmd_wz = commands[:, 2:3]
        stance_time = self.gait_period * self.duty_factor

        foot_vx = cmd_vx - cmd_wz * self.leg_origin_y
        foot_vy = cmd_vy + cmd_wz * self.leg_origin_x
        stride_x = self._limit_stride(foot_vx * stance_time, self.max_stride_x)
        stride_y = self._limit_stride(foot_vy * stance_time, self.max_stride_y)

        leg_phase = torch.remainder(phase + self.phase_offsets, 1.0)
        stance = leg_phase < self.duty_factor

        s_stance = torch.clamp(leg_phase / self.duty_factor, 0.0, 1.0)
        s_swing = torch.clamp(
            (leg_phase - self.duty_factor) / (1.0 - self.duty_factor),
            0.0,
            1.0,
        )
        smooth_swing = s_swing * s_swing * (3.0 - 2.0 * s_swing)

        x_stance = stride_x * (0.5 - s_stance)
        y_stance = stride_y * (0.5 - s_stance)
        x_swing = stride_x * (-0.5 + smooth_swing)
        y_swing = stride_y * (-0.5 + smooth_swing)
        lift = torch.sin(math.pi * s_swing)
        z_swing = self.step_height * lift * lift

        x = self.default_foot_x + torch.where(stance, x_stance, x_swing)
        y = self.default_foot_y + torch.where(stance, y_stance, y_swing)
        z_contact = -self.body_height + torch.where(
            stance,
            torch.zeros_like(z_swing),
            z_swing,
        )
        z = z_contact + self.toe_radius

        shoulder_axis = self.shoulder_sign
        shoulder_y = self.shoulder_offset_y
        yz_radius2 = y * y + z * z
        sagittal_z = -torch.sqrt(torch.clamp(
            yz_radius2 - shoulder_y * shoulder_y,
            min=1.0e-9,
        ))

        target_angle = torch.atan2(z, y)
        leg_plane_angle = torch.atan2(sagittal_z, shoulder_y)
        shoulder_physical = self._wrap_pi(target_angle - leg_plane_angle)
        shoulder_physical = shoulder_physical * self.shoulder_y_gain
        shoulder_physical = torch.clamp(
            shoulder_physical,
            -self.shoulder_limit,
            self.shoulder_limit,
        )
        shoulder = shoulder_physical / shoulder_axis

        cos_s = torch.cos(shoulder_physical)
        sin_s = torch.sin(shoulder_physical)
        z_in_shoulder = -y * sin_s + z * cos_s
        thigh, knee = self._solve_sagittal_ik(x, z_in_shoulder)

        target = torch.stack((shoulder, thigh, knee), dim=2).reshape(commands.shape[0], 12)
        if self.joint_min is not None:
            target = torch.max(torch.min(target, self.joint_max), self.joint_min)
        return target

    def _limit_stride(self, stride, limit):
        if limit <= 0.0:
            return torch.zeros_like(stride)
        if self.soft_stride_limit:
            return limit * torch.tanh(stride / limit)
        return torch.clamp(stride, -limit, limit)

    def _solve_sagittal_ik(self, x, z):
        upper_eff = math.sqrt(
            self.upper_link_x * self.upper_link_x
            + self.upper_link_z * self.upper_link_z
        )
        upper_alpha = math.atan2(self.upper_link_x, self.upper_link_z)

        r2 = x * x + z * z
        cos_knee = (
            r2 - upper_eff * upper_eff - self.lower_link * self.lower_link
        ) / (2.0 * upper_eff * self.lower_link)
        cos_knee = torch.clamp(cos_knee, -1.0, 1.0)
        sin_knee = torch.sqrt(torch.clamp(1.0 - cos_knee * cos_knee, min=0.0))
        knee_raw = torch.atan2(sin_knee, cos_knee)

        a = upper_eff + self.lower_link * cos_knee
        b = self.lower_link * sin_knee
        det = a * a + b * b
        z_neg = -z

        sin_thigh = (a * x - b * z_neg) / det
        cos_thigh = (b * x + a * z_neg) / det
        thigh = torch.atan2(sin_thigh, cos_thigh) - upper_alpha
        knee = knee_raw + upper_alpha
        return thigh, knee

    @staticmethod
    def _wrap_pi(angle):
        return torch.remainder(angle + math.pi, 2.0 * math.pi) - math.pi


class SpotmicroTest(LeggedRobot):
    def _init_buffers(self):
        super()._init_buffers()
        rigid_body_state = self.gym.acquire_rigid_body_state_tensor(self.sim)
        self.gym.refresh_rigid_body_state_tensor(self.sim)
        self.rigid_body_states = gymtorch.wrap_tensor(rigid_body_state).view(
            self.num_envs, self.num_bodies, 13)
        self._init_ik_reference()
        self._init_domain_randomization_buffers()
        self._init_actuator_response_model()

        self.gait_phase = torch.zeros(self.num_envs, 1, dtype=torch.float, device=self.device)
        self.commands_scale = torch.tensor(
            [self.obs_scales.lin_vel, self.obs_scales.lin_vel, self.obs_scales.ang_vel],
            device=self.device)
        # ==== Step 5: 서보 응답 지연 (substep 단위, dt=5ms 해상도) ====
        if self.cfg.domain_rand.action_delay:
            delay_range = self.cfg.domain_rand.action_delay_range
            decimation = self.cfg.control.decimation
            assert delay_range[1] < decimation, (
                f"action_delay_range max({delay_range[1]})은 "
                f"decimation({decimation})보다 작아야 합니다. "
                f"그래야 한 policy step 안에서 처리됩니다.")
            
            # 환경별 지연 substep 수 (정수)
            self.action_delay_substeps = torch.randint(
                delay_range[0], delay_range[1] + 1,
                (self.num_envs,), device=self.device)
            
            dt_ms = self.sim_params.dt * 1000
            print(f"[Action Delay] 활성화: "
                  f"substep 단위, dt={dt_ms:.1f}ms, "
                  f"range={delay_range[0]*dt_ms:.0f}~{delay_range[1]*dt_ms:.0f}ms")
                  
        # ==== Recovery assist randomization ====
        # Stage 1: 너무 세게 시작하지 말 것
        self.recovery_roll_pitch_range = math.radians(
            getattr(self.cfg.domain_rand, "recovery_roll_pitch_range_deg", 10.0)
        )
        self.recovery_yaw_range = math.radians(
            getattr(self.cfg.domain_rand, "recovery_yaw_range_deg", 180.0)
        )
        self.recovery_lin_vel_xy_range = getattr(self.cfg.domain_rand, "recovery_lin_vel_xy_range", 0.10)
        self.recovery_lin_vel_z_range = getattr(self.cfg.domain_rand, "recovery_lin_vel_z_range", 0.03)
        self.recovery_ang_vel_xy_range = getattr(self.cfg.domain_rand, "recovery_ang_vel_xy_range", 0.60)
        self.recovery_ang_vel_z_range = getattr(self.cfg.domain_rand, "recovery_ang_vel_z_range", 0.30)
        
        # ==== Recovery diagnostic용 reset 상태 기록 ====
        self.last_reset_roll = torch.zeros(
            self.num_envs, device=self.device, dtype=torch.float
        )
        self.last_reset_pitch = torch.zeros(
            self.num_envs, device=self.device, dtype=torch.float
        )
        self.last_reset_tilt = torch.zeros(
            self.num_envs, device=self.device, dtype=torch.float
        )
        self.last_tilt_metric = torch.zeros(
            self.num_envs, device=self.device, dtype=torch.float
        )
        self.last_ang_vel_xy_metric = torch.zeros(
            self.num_envs, device=self.device, dtype=torch.float
        )

    def _init_domain_randomization_buffers(self):
        cfg = self.cfg.domain_rand
        self.motor_strength_scales = torch.ones(
            self.num_envs, self.num_actions, dtype=torch.float, device=self.device)
        self.stiffness_scales = torch.ones(
            self.num_envs, self.num_actions, dtype=torch.float, device=self.device)
        self.damping_scales = torch.ones(
            self.num_envs, self.num_actions, dtype=torch.float, device=self.device)
        self.joint_obs_offsets = torch.zeros(
            self.num_envs, self.num_dof, dtype=torch.float, device=self.device)

        if getattr(cfg, "randomize_motor_strength", False):
            rng = cfg.motor_strength_range
            self.motor_strength_scales = torch_rand_float(
                rng[0], rng[1],
                (self.num_envs, self.num_actions),
                device=self.device,
            )
            print(
                f"[DR] Motor strength scale: "
                f"{self.motor_strength_scales.min().item():.3f} ~ "
                f"{self.motor_strength_scales.max().item():.3f}")

        if getattr(cfg, "randomize_pd_gains", False):
            k_rng = cfg.stiffness_scale_range
            d_rng = cfg.damping_scale_range
            self.stiffness_scales = torch_rand_float(
                k_rng[0], k_rng[1],
                (self.num_envs, self.num_actions),
                device=self.device,
            )
            self.damping_scales = torch_rand_float(
                d_rng[0], d_rng[1],
                (self.num_envs, self.num_actions),
                device=self.device,
            )
            print(
                f"[DR] Stiffness scale: "
                f"{self.stiffness_scales.min().item():.3f} ~ "
                f"{self.stiffness_scales.max().item():.3f}, "
                f"damping scale: {self.damping_scales.min().item():.3f} ~ "
                f"{self.damping_scales.max().item():.3f}")

        if getattr(cfg, "randomize_joint_obs_offset", False):
            self._resample_joint_obs_offsets(
                torch.arange(self.num_envs, device=self.device))

    def _init_actuator_response_model(self):
        cfg = self.cfg.domain_rand
        self.use_actuator_lag = bool(getattr(cfg, "actuator_lag", False))
        self.actuated_dof_target = self.default_dof_pos.repeat(self.num_envs, 1).clone()
        self.actuator_lag_alpha = torch.ones(
            self.num_envs, self.num_actions, dtype=torch.float, device=self.device)

        if not self.use_actuator_lag:
            return

        tau_range = getattr(cfg, "actuator_lag_tau_range", [0.16, 0.26])
        self._resample_actuator_lag(
            torch.arange(self.num_envs, device=self.device),
            tau_range,
        )
        print(
            f"[Actuator Lag] enabled: tau={tau_range[0]:.3f}~{tau_range[1]:.3f}s "
            f"(measured CLASSIC response was about 0.18~0.24s)")

    def _resample_actuator_lag(self, env_ids, tau_range=None):
        if not self.use_actuator_lag:
            return
        if tau_range is None:
            tau_range = getattr(self.cfg.domain_rand, "actuator_lag_tau_range", [0.16, 0.26])
        tau = torch_rand_float(
            tau_range[0],
            tau_range[1],
            (len(env_ids), self.num_actions),
            device=self.device,
        )
        tau = torch.clamp(tau, min=self.sim_params.dt)
        self.actuator_lag_alpha[env_ids] = 1.0 - torch.exp(-self.sim_params.dt / tau)

    def _init_ik_reference(self):
        ik_cfg = self.cfg.ik
        self.gait_period = float(ik_cfg.gait_period)
        self.duty_factor = float(ik_cfg.duty_factor)
        self.phase_cmd_norm = float(ik_cfg.phase_cmd_norm)
        self.blend_cmd_norm = float(ik_cfg.blend_cmd_norm)
        self.phase_offsets = torch.tensor(
            ik_cfg.phase_offsets,
            device=self.device,
            dtype=torch.float,
        )
        self.ik_reference = TorchSharedTrotReference(
            device=self.device,
            dtype=torch.float,
            gait_period=ik_cfg.gait_period,
            duty_factor=ik_cfg.duty_factor,
            body_height=ik_cfg.body_height,
            step_height=ik_cfg.step_height,
            default_foot_x=ik_cfg.default_foot_x,
            default_foot_y=ik_cfg.default_foot_y,
            leg_origin_x=ik_cfg.leg_origin_x,
            leg_origin_y=ik_cfg.leg_origin_y,
            shoulder_sign=ik_cfg.shoulder_sign,
            shoulder_offset_y=ik_cfg.shoulder_offset_y,
            phase_offsets=ik_cfg.phase_offsets,
            max_stride_x=ik_cfg.max_stride_x,
            max_stride_y=ik_cfg.max_stride_y,
            soft_stride_limit=ik_cfg.soft_stride_limit,
            upper_link_x=ik_cfg.upper_link_x,
            upper_link_z=ik_cfg.upper_link_z,
            lower_link=ik_cfg.lower_link,
            toe_radius=ik_cfg.toe_radius,
            shoulder_y_gain=ik_cfg.shoulder_y_gain,
            shoulder_limit=ik_cfg.shoulder_limit,
            joint_min=ik_cfg.joint_min,
            joint_max=ik_cfg.joint_max,
        )


    def step(self, actions):
        """서보 응답 지연을 substep 단위로 적용
        
        decimation 루프의 각 substep에서:
          - i < delay인 환경 → 이전 액션(last_actions) 사용
          - i >= delay인 환경 → 현재 액션 사용
        
        이렇게 하면 5ms 해상도로 지연을 시뮬레이션할 수 있음.
        self.actions는 현재 액션 그대로 유지 (observation, reward용).
        """
        clip_actions = self.cfg.normalization.clip_actions
        self.actions = torch.clip(actions, -clip_actions, clip_actions).to(self.device)
        
        self.render()
        for i in range(self.cfg.control.decimation):
            if self.cfg.domain_rand.action_delay:
                # i < delay인 환경: 서보에 아직 새 명령 안 도착 → 이전 액션
                # i >= delay인 환경: 새 명령 도착 → 현재 액션
                use_old = (i < self.action_delay_substeps).unsqueeze(1)  # [num_envs, 1]
                torque_actions = torch.where(use_old, self.last_actions, self.actions)
            else:
                torque_actions = self.actions

            self.torques = self._compute_torques(torque_actions).view(self.torques.shape)
            self.gym.set_dof_actuation_force_tensor(
                self.sim, gymtorch.unwrap_tensor(self.torques))
            self.gym.simulate(self.sim)
            if self.device == 'cpu':
                self.gym.fetch_results(self.sim, True)
            self.gym.refresh_dof_state_tensor(self.sim)
        self.post_physics_step()

        clip_obs = self.cfg.normalization.clip_observations
        self.obs_buf = torch.clip(self.obs_buf, -clip_obs, clip_obs)
        if self.privileged_obs_buf is not None:
            self.privileged_obs_buf = torch.clip(self.privileged_obs_buf, -clip_obs, clip_obs)
        return self.obs_buf, self.privileged_obs_buf, self.rew_buf, self.reset_buf, self.extras

    def post_physics_step(self):
        self.gym.refresh_rigid_body_state_tensor(self.sim)
        super().post_physics_step()
        self.last_tilt_metric = torch.norm(self.projected_gravity[:, :2], dim=1)
        self.last_ang_vel_xy_metric = torch.norm(self.base_ang_vel[:, :2], dim=1)

    def _post_physics_step_callback(self):
        super()._post_physics_step_callback()

        cmd_norm = torch.norm(
            self._get_effective_commands(), dim=1, keepdim=True)
        phase_scale = torch.clamp(cmd_norm / self.phase_cmd_norm, 0.0, 1.0)

        recovery_cfg = getattr(self.cfg, "recovery", None)
        if getattr(recovery_cfg, "phase_enabled", False):
            blend = self._recovery_blend().unsqueeze(1)
            if getattr(recovery_cfg, "phase_freeze", False):
                phase_scale = torch.where(
                    blend > 0.0, torch.zeros_like(phase_scale), phase_scale)
            else:
                recovery_phase_scale = float(
                    getattr(recovery_cfg, "phase_scale", 0.2))
                phase_scale = phase_scale * (
                    1.0 - blend * (1.0 - recovery_phase_scale))

        dt_phase = self.dt / self.gait_period
        self.gait_phase = (self.gait_phase + dt_phase * phase_scale) % 1.0
              
    def check_termination(self):
        super().check_termination()

        base_height = self.root_states[:, 2]
        min_base_height = getattr(self.cfg.rewards, "min_base_height", 0.155)
        max_base_tilt_deg = getattr(self.cfg.rewards, "max_base_tilt_deg", 75.0)
        max_tilt_gravity_z = -math.cos(math.radians(max_base_tilt_deg))
        self.reset_buf |= (base_height < min_base_height)
        self.reset_buf |= (self.projected_gravity[:, 2] > max_tilt_gravity_z)
        
    def _reset_dofs(self, env_ids):
        # Start each episode near the classic IK stand pose.
        joint_noise = torch_rand_float(
            -0.05, 0.05,
            (len(env_ids), self.num_dof),
            device=self.device,
        )
        self.dof_pos[env_ids] = self.default_dof_pos + joint_noise
        self.dof_vel[env_ids] = 0.

        env_ids_int32 = env_ids.to(dtype=torch.int32)
        self.gym.set_dof_state_tensor_indexed(
            self.sim,
            gymtorch.unwrap_tensor(self.dof_state),
            gymtorch.unwrap_tensor(env_ids_int32),
            len(env_ids_int32),
        )

        if hasattr(self, 'pair_air_time'):
            self.pair_air_time[env_ids] = 0.
        if hasattr(self, 'max_feet_height'):
            self.max_feet_height[env_ids] = 0.
        if hasattr(self, 'feet_swing_contact_time'):
            self.feet_swing_contact_time[env_ids] = 0.

        self.gait_phase[env_ids] = torch_rand_float(
            0.0, 1.0,
            (len(env_ids), 1),
            device=self.device,
        )

        # Action Delay: 리셋 환경의 지연 재랜덤화
        if self.cfg.domain_rand.action_delay:
            delay_range = self.cfg.domain_rand.action_delay_range
            self.action_delay_substeps[env_ids] = torch.randint(
                delay_range[0],
                delay_range[1] + 1,
                (len(env_ids),),
                device=self.device,
            )

        if getattr(self, "use_actuator_lag", False):
            self.actuated_dof_target[env_ids] = self.default_dof_pos.repeat(len(env_ids), 1)
            self._resample_actuator_lag(env_ids)

        if getattr(self.cfg.domain_rand, "randomize_joint_obs_offset", False):
            self._resample_joint_obs_offsets(env_ids)

    
    def _reset_root_states(self, env_ids):
        """Recovery assist용 reset:
        - base를 살짝 기울어진 상태로 시작
        - roll/pitch angular velocity 부여
        - 아직 완전히 넘어진 상태는 만들지 않음
        """
        num = len(env_ids)

        self.root_states[env_ids] = self.base_init_state
        self.root_states[env_ids, :3] += self.env_origins[env_ids]

        # plane이면 필요 없지만, trimesh 확장 고려해서 z는 초기 높이 유지
        self.root_states[env_ids, 2] = self.base_init_state[2] + self.env_origins[env_ids, 2]

        # roll/pitch/yaw randomization
        roll = torch_rand_float(
            -self.recovery_roll_pitch_range,
            self.recovery_roll_pitch_range,
            (num, 1),
            device=self.device,
        ).squeeze(1)

        pitch = torch_rand_float(
            -self.recovery_roll_pitch_range,
            self.recovery_roll_pitch_range,
            (num, 1),
            device=self.device,
        ).squeeze(1)

        yaw = torch_rand_float(
            -self.recovery_yaw_range,
            self.recovery_yaw_range,
            (num, 1),
            device=self.device,
        ).squeeze(1)

        self.root_states[env_ids, 3:7] = quat_from_euler_xyz(roll, pitch, yaw)
        
        # diagnostic에서 reset 직후의 실제 perturbation을 initial tilt로 쓰기 위해 저장
        self.last_reset_roll[env_ids] = roll
        self.last_reset_pitch[env_ids] = pitch
        self.last_reset_tilt[env_ids] = torch.maximum(torch.abs(roll), torch.abs(pitch))

        # base linear velocity
        self.root_states[env_ids, 7:9] = torch_rand_float(
            -self.recovery_lin_vel_xy_range,
            self.recovery_lin_vel_xy_range,
            (num, 2),
            device=self.device,
        )
        self.root_states[env_ids, 9] = torch_rand_float(
            -self.recovery_lin_vel_z_range,
            self.recovery_lin_vel_z_range,
            (num, 1),
            device=self.device,
        ).squeeze(1)

        # base angular velocity: roll/pitch 방향을 중점적으로 교란
        self.root_states[env_ids, 10:12] = torch_rand_float(
            -self.recovery_ang_vel_xy_range,
            self.recovery_ang_vel_xy_range,
            (num, 2),
            device=self.device,
        )
        self.root_states[env_ids, 12] = torch_rand_float(
            -self.recovery_ang_vel_z_range,
            self.recovery_ang_vel_z_range,
            (num, 1),
            device=self.device,
        ).squeeze(1)

        env_ids_int32 = env_ids.to(dtype=torch.int32)
        self.gym.set_actor_root_state_tensor_indexed(
            self.sim,
            gymtorch.unwrap_tensor(self.root_states),
            gymtorch.unwrap_tensor(env_ids_int32),
            len(env_ids_int32),
        )

    def _push_robots(self):
        """주행 중 전환 복구 상황을 만들기 위해 선속도와 roll/pitch 각속도 impulse를 더한다."""
        max_lin = self.cfg.domain_rand.max_push_vel_xy
        lin_clip = getattr(self.cfg.domain_rand, "push_lin_vel_clip", max_lin)
        lin_delta = torch_rand_float(
            -max_lin, max_lin, (self.num_envs, 2), device=self.device)
        self.root_states[:, 7:9] = torch.clamp(
            self.root_states[:, 7:9] + lin_delta,
            min=-lin_clip,
            max=lin_clip,
        )

        max_ang_xy = getattr(self.cfg.domain_rand, "max_push_ang_vel_xy", 0.0)
        if max_ang_xy > 0.0:
            ang_xy_clip = getattr(self.cfg.domain_rand, "push_ang_vel_xy_clip", max_ang_xy)
            ang_xy_delta = torch_rand_float(
                -max_ang_xy, max_ang_xy, (self.num_envs, 2), device=self.device)
            self.root_states[:, 10:12] = torch.clamp(
                self.root_states[:, 10:12] + ang_xy_delta,
                min=-ang_xy_clip,
                max=ang_xy_clip,
            )

        max_ang_z = getattr(self.cfg.domain_rand, "max_push_ang_vel_z", 0.0)
        if max_ang_z > 0.0:
            ang_z_clip = getattr(self.cfg.domain_rand, "push_ang_vel_z_clip", max_ang_z)
            ang_z_delta = torch_rand_float(
                -max_ang_z, max_ang_z, (self.num_envs, 1), device=self.device).squeeze(1)
            self.root_states[:, 12] = torch.clamp(
                self.root_states[:, 12] + ang_z_delta,
                min=-ang_z_clip,
                max=ang_z_clip,
            )

        transition_tilt_count = self._apply_transition_tilt_push()

        self.last_transition_push_step = int(self.common_step_counter)
        self.last_transition_push_lin = float(max_lin)
        self.last_transition_push_ang_xy = float(max_ang_xy)
        self.last_transition_push_ang_z = float(max_ang_z)

        self.gym.set_actor_root_state_tensor(self.sim, gymtorch.unwrap_tensor(self.root_states))
        if not hasattr(self, '_push_count'):
            self._push_count = 0
        self._push_count += 1
        if self._push_count <= 3:
            print(
                f"[DR] Push #{self._push_count} at step {self.common_step_counter}, "
                f"lin={max_lin}, ang_xy={max_ang_xy}, ang_z={max_ang_z}, "
                f"transition_tilt_envs={transition_tilt_count}")

    def _apply_transition_tilt_push(self):
        """일부 주행 환경을 pre-fall tilt 상태로 직접 보내 transition recovery 샘플을 만든다."""
        cfg = self.cfg.domain_rand
        if not getattr(cfg, "transition_tilt_push", False):
            return 0

        prob = float(getattr(cfg, "transition_tilt_push_prob", 0.0))
        if prob <= 0.0:
            return 0

        mask = torch.rand(self.num_envs, device=self.device) < prob
        env_ids = torch.nonzero(mask, as_tuple=False).flatten()
        num = len(env_ids)
        if num == 0:
            return 0

        min_deg = float(getattr(cfg, "transition_tilt_push_min_deg", 18.0))
        max_deg = float(getattr(cfg, "transition_tilt_push_max_deg", 28.0))
        min_rad = math.radians(min_deg)
        max_rad = math.radians(max_deg)

        tilt = torch_rand_float(min_rad, max_rad, (num, 1), device=self.device).squeeze(1)
        sign = torch.where(
            torch.rand(num, device=self.device) < 0.5,
            -torch.ones(num, device=self.device),
            torch.ones(num, device=self.device),
        )
        use_roll = torch.rand(num, device=self.device) < 0.5
        roll = torch.zeros(num, device=self.device)
        pitch = torch.zeros(num, device=self.device)
        roll[use_roll] = tilt[use_roll] * sign[use_roll]
        pitch[~use_roll] = tilt[~use_roll] * sign[~use_roll]
        yaw = torch.zeros(num, device=self.device)

        self.root_states[env_ids, 3:7] = quat_from_euler_xyz(roll, pitch, yaw)

        ang_vel_xy = float(getattr(cfg, "transition_tilt_push_ang_vel_xy", 0.0))
        if ang_vel_xy > 0.0:
            ang = torch_rand_float(0.0, ang_vel_xy, (num, 1), device=self.device).squeeze(1)
            self.root_states[env_ids, 10:12] = 0.0
            self.root_states[env_ids, 10] = torch.sign(roll) * ang
            self.root_states[env_ids, 11] = torch.sign(pitch) * ang

        cmd_range = getattr(cfg, "transition_tilt_cmd_x_range", None)
        if cmd_range is not None:
            self.commands[env_ids, 0] = torch_rand_float(
                float(cmd_range[0]),
                float(cmd_range[1]),
                (num, 1),
                device=self.device,
            ).squeeze(1)
            self.commands[env_ids, 1] = 0.0
            if getattr(cfg, "transition_tilt_zero_yaw_cmd", True):
                self.commands[env_ids, 2] = 0.0

        return int(num)

    def _process_rigid_shape_props(self, props, env_id):
        props = super()._process_rigid_shape_props(props, env_id)
        if env_id == 0 and self.cfg.domain_rand.randomize_friction:
            print(
                f"[DR] Plane friction static/dynamic="
                f"{self.cfg.terrain.static_friction:.3f}/"
                f"{self.cfg.terrain.dynamic_friction:.3f}")
        return props

    def _process_rigid_body_props(self, props, env_id):
        props = super()._process_rigid_body_props(props, env_id)
        cfg = self.cfg.domain_rand

        if getattr(cfg, "randomize_base_com", False):
            dx = np.random.uniform(
                cfg.base_com_offset_x_range[0],
                cfg.base_com_offset_x_range[1],
            )
            dy = np.random.uniform(
                cfg.base_com_offset_y_range[0],
                cfg.base_com_offset_y_range[1],
            )
            dz = np.random.uniform(
                cfg.base_com_offset_z_range[0],
                cfg.base_com_offset_z_range[1],
            )
            props[0].com.x += dx
            props[0].com.y += dy
            props[0].com.z += dz
            if env_id < 5:
                print(
                    f"[DR] Env {env_id}: base COM offset "
                    f"dx={dx:+.3f}, dy={dy:+.3f}, dz={dz:+.3f}")

        return props

    def _resample_commands(self, env_ids):
        if len(env_ids) == 0:
            return

        self.commands[env_ids, 0] = torch_rand_float(
            self.command_ranges["lin_vel_x"][0],
            self.command_ranges["lin_vel_x"][1],
            (len(env_ids), 1),
            device=self.device,
        ).squeeze(1)
        self.commands[env_ids, 1] = torch_rand_float(
            self.command_ranges["lin_vel_y"][0],
            self.command_ranges["lin_vel_y"][1],
            (len(env_ids), 1),
            device=self.device,
        ).squeeze(1)
        if self.cfg.commands.heading_command:
            self.commands[env_ids, 3] = torch_rand_float(
                self.command_ranges["heading"][0],
                self.command_ranges["heading"][1],
                (len(env_ids), 1),
                device=self.device,
            ).squeeze(1)
        else:
            self.commands[env_ids, 2] = torch_rand_float(
                self.command_ranges["ang_vel_yaw"][0],
                self.command_ranges["ang_vel_yaw"][1],
                (len(env_ids), 1),
                device=self.device,
            ).squeeze(1)

        deadband = getattr(self.cfg.commands, "command_deadband", 0.02)
        if deadband <= 0.0:
            return

        lin_norm = torch.norm(self.commands[env_ids, :2], dim=1)
        self.commands[env_ids, :2] *= (lin_norm > deadband).unsqueeze(1)
        
    def compute_observations(self):
        effective_commands = self._get_effective_commands()
        ref_dof_pos = self._get_ik_target(effective_commands)
        dof_pos_obs = self.dof_pos + self.joint_obs_offsets
        phase_sin = torch.sin(2 * torch.pi * self.gait_phase)
        phase_cos = torch.cos(2 * torch.pi * self.gait_phase)
        self.obs_buf = torch.cat([
            self.base_ang_vel * self.obs_scales.ang_vel,           # 3
            self.projected_gravity,                                 # 3
            effective_commands * self.commands_scale,               # 3
            (dof_pos_obs - ref_dof_pos) * self.obs_scales.dof_pos,  # 12
            self.dof_vel * self.obs_scales.dof_vel,                # 12
            self.actions,                                           # 12 (현재 액션)
            phase_sin, phase_cos,                                   # 2
        ], dim=-1)
        if self.add_noise:
            self.obs_buf += (2 * torch.rand_like(self.obs_buf) - 1) * self.noise_scale_vec
            
    def _get_noise_scale_vec(self, cfg):
        noise_vec = torch.zeros_like(self.obs_buf[0])
        self.add_noise = self.cfg.noise.add_noise
        noise_scales = self.cfg.noise.noise_scales
        noise_level = self.cfg.noise.noise_level

        # obs layout:
        # 0:3   base_ang_vel
        # 3:6   projected_gravity
        # 6:9   commands
        # 9:21  dof_pos - ref_dof_pos
        # 21:33 dof_vel
        # 33:45 actions
        # 45:47 phase_sin, phase_cos

        noise_vec[:3] = noise_scales.ang_vel * noise_level * self.obs_scales.ang_vel
        noise_vec[3:6] = noise_scales.gravity * noise_level
        noise_vec[6:9] = 0.0
        noise_vec[9:21] = noise_scales.dof_pos * noise_level * self.obs_scales.dof_pos
        noise_vec[21:33] = noise_scales.dof_vel * noise_level * self.obs_scales.dof_vel
        noise_vec[33:45] = 0.0
        noise_vec[45:47] = 0.0
        if self.cfg.terrain.measure_heights:
            noise_vec[48:235] = noise_scales.height_measurements* noise_level * self.obs_scales.height_measurements

        return noise_vec

    def _resample_joint_obs_offsets(self, env_ids):
        if len(env_ids) == 0:
            return
        rng = self.cfg.domain_rand.joint_obs_offset_range
        self.joint_obs_offsets[env_ids] = torch_rand_float(
            rng[0], rng[1],
            (len(env_ids), self.num_dof),
            device=self.device,
        )

    def _recovery_tilt_metric(self):
        return torch.norm(self.projected_gravity[:, :2], dim=1)

    def _recovery_blend(self, threshold_deg=None, full_tilt_deg=None):
        recovery_cfg = getattr(self.cfg, "recovery", None)
        if not getattr(recovery_cfg, "enabled", False):
            return torch.zeros(self.num_envs, device=self.device)

        if threshold_deg is None:
            threshold_deg = getattr(recovery_cfg, "tilt_threshold_deg", 14.0)
        if full_tilt_deg is None:
            full_tilt_deg = getattr(recovery_cfg, "full_tilt_deg", 25.0)

        threshold = math.sin(math.radians(float(threshold_deg)))
        full_tilt = math.sin(math.radians(float(full_tilt_deg)))
        tilt = self._recovery_tilt_metric()
        if full_tilt <= threshold:
            return (tilt > threshold).float()
        return torch.clamp((tilt - threshold) / (full_tilt - threshold), 0.0, 1.0)

    def _get_effective_commands(self):
        commands = self.commands[:, :3]
        recovery_cfg = getattr(self.cfg, "recovery", None)
        if not getattr(recovery_cfg, "command_scale_enabled", False):
            return commands

        blend = self._recovery_blend().unsqueeze(1)
        recovery_cmd_scale = float(
            getattr(recovery_cfg, "command_scale", 0.25))
        scale = 1.0 - blend * (1.0 - recovery_cmd_scale)
        return commands * scale

    def _get_effective_action_scale(self):
        normal_scale = float(self.cfg.control.action_scale)
        recovery_cfg = getattr(self.cfg, "recovery", None)
        if not getattr(recovery_cfg, "action_scale_enabled", False):
            return normal_scale

        recovery_scale = float(
            getattr(self.cfg.control, "recovery_action_scale", normal_scale))
        blend = self._recovery_blend().unsqueeze(1)
        return normal_scale + blend * (recovery_scale - normal_scale)

    def _recovery_relief_scale(self, scale_attr):
        threshold_deg = getattr(
            self.cfg.rewards, "recovery_relief_tilt_threshold_deg", 14.0)
        full_tilt_deg = getattr(
            self.cfg.rewards, "recovery_relief_full_tilt_deg", 25.0)
        relief = getattr(self.cfg.rewards, scale_attr, 0.25)
        blend = self._recovery_blend(threshold_deg, full_tilt_deg)
        return 1.0 - blend * (1.0 - float(relief))
        
    def _reward_feet_air_time(self):
        contact = self.contact_forces[:, self.feet_indices, 2] > 1.
        contact_filt = torch.logical_or(contact, self.last_contacts)
        self.last_contacts = contact
        first_contact = (self.feet_air_time > 0.) * contact_filt
        self.feet_air_time += self.dt
        rew_airTime = torch.sum((self.feet_air_time - 0.15) * first_contact, dim=1)
        rew_airTime *= torch.norm(
            self._get_effective_commands(), dim=1) > self.blend_cmd_norm
        self.feet_air_time *= ~contact_filt
        return rew_airTime
        
    def _reward_trot_symmetry(self):
        contact = self.contact_forces[:, self.feet_indices, 2] > 1.
        sync_0 = (contact[:, 0] == contact[:, 3]).float()
        sync_1 = (contact[:, 1] == contact[:, 2]).float()  
        anti_phase = (contact[:, 0] != contact[:, 1]).float()
        reward = (sync_0 + sync_1 + anti_phase) / 3.0
        reward *= (torch.norm(
            self._get_effective_commands(), dim=1) > self.blend_cmd_norm).float()
        return reward
        
    def _reward_no_stuck_feet(self):
        contact = self.contact_forces[:, self.feet_indices, 2] > 1.
        contact_filt = torch.logical_or(contact, self.last_contacts)

        phases = (self.gait_phase + self.phase_offsets) % 1.0
        desired_air = phases >= self.duty_factor
        swing_contact = desired_air & contact_filt

        if not hasattr(self, 'feet_swing_contact_time'):
            self.feet_swing_contact_time = torch.zeros(
                self.num_envs, len(self.feet_indices), device=self.device
            )

        self.feet_swing_contact_time = (
            self.feet_swing_contact_time + self.dt
        ) * swing_contact.float()

        grace_time = getattr(self.cfg.rewards, "swing_contact_grace_time", 0.02)

        weights = torch.tensor(
            getattr(self.cfg.rewards, "swing_contact_weights", [1.3, 1.3, 1.0, 1.0]),
            device=self.device,
            dtype=torch.float,
        ).view(1, 4)

        over_time = torch.clamp(
            self.feet_swing_contact_time - grace_time,
            min=0.0,
            max=0.20,
        )

        denom = torch.clamp(
            (desired_air.float() * weights).sum(dim=1),
            min=1.0,
        )

        penalty = torch.sum(over_time * weights, dim=1) / denom

        penalty *= (
            torch.norm(self._get_effective_commands(), dim=1)
            > self.blend_cmd_norm
        ).float()

        penalty *= self._recovery_relief_scale("recovery_gait_relief_scale")
        return penalty

    def _reward_symmetric_gait(self):
        contact = self.contact_forces[:, self.feet_indices, 2] > 1.
        contact_filt = torch.logical_or(contact, self.last_contacts)
        if not hasattr(self, 'pair_air_ema'):
            self.pair_air_ema = torch.zeros(self.num_envs, 2, device=self.device)
        pair0_air = (~contact_filt[:, 0] & ~contact_filt[:, 3]).float()
        pair1_air = (~contact_filt[:, 1] & ~contact_filt[:, 2]).float()
        alpha = 0.02
        self.pair_air_ema[:, 0] = (1 - alpha) * self.pair_air_ema[:, 0] + alpha * pair0_air
        self.pair_air_ema[:, 1] = (1 - alpha) * self.pair_air_ema[:, 1] + alpha * pair1_air
        diff = torch.abs(self.pair_air_ema[:, 0] - self.pair_air_ema[:, 1])
        total = self.pair_air_ema[:, 0] + self.pair_air_ema[:, 1]
        balance = torch.where(
            total > 0.02,
            1.0 - diff / (total + 1e-6),
            torch.zeros_like(diff)
        )
        reward = balance
        reward *= (torch.norm(
            self._get_effective_commands(), dim=1) > self.blend_cmd_norm).float()
        return reward

    def _reward_survival(self):
        return (~self.reset_buf.bool()).float()

    def _reward_feet_clearance(self):
        contact = self.contact_forces[:, self.feet_indices, 2] > 1.
        contact_filt = torch.logical_or(contact, self.last_contacts)
        if not hasattr(self, 'max_feet_height'):
            self.max_feet_height = torch.zeros(
                self.num_envs, len(self.feet_indices), device=self.device)
        feet_z = self.rigid_body_states[:, self.feet_indices, 2]
        is_air = (~contact_filt).float()
        self.max_feet_height = torch.max(self.max_feet_height, feet_z * is_air)
        first_contact = (self.max_feet_height > 0.) * contact_filt
        clearance_min = getattr(self.cfg.rewards, "feet_clearance_min", 0.02)
        clearance_cap = getattr(self.cfg.rewards, "feet_clearance_cap", 0.03)
        height_reward = torch.clamp(
            self.max_feet_height - clearance_min,
            min=0.,
            max=clearance_cap,
        )
        reward = torch.sum(height_reward * first_contact.float(), dim=1)
        self.max_feet_height *= is_air
        reward *= (torch.norm(
            self._get_effective_commands(), dim=1) > self.blend_cmd_norm).float()
        return reward

    def _reward_swing_contact(self):
        phases = (self.gait_phase + self.phase_offsets) % 1.0
        desired_air = phases >= self.duty_factor
        actual_contact = self.contact_forces[:, self.feet_indices, 2] > 1.0

        cmd_norm = torch.norm(
            self._get_effective_commands(), dim=1, keepdim=True
        )
        is_moving = (cmd_norm > self.blend_cmd_norm).float()

        # 앞발 dragging이 핵심 문제라면 앞다리에 약간 더 가중
        weights = torch.tensor(
            getattr(self.cfg.rewards, "swing_contact_weights", [1.3, 1.3, 1.0, 1.0]),
            device=self.device,
            dtype=torch.float,
        ).view(1, 4)

        bad = (desired_air & actual_contact).float() * weights

        # /4 고정이 아니라, 실제 swing이어야 하는 발 기준으로 정규화
        denom = torch.clamp(
            (desired_air.float() * weights).sum(dim=1),
            min=1.0,
        )

        penalty = bad.sum(dim=1) / denom
        penalty = penalty * is_moving.squeeze(1)

        return penalty * self._recovery_relief_scale("recovery_gait_relief_scale")
        
    
    def _reward_stand_still(self):
        cmd_norm = torch.norm(self._get_effective_commands(), dim=1)
        return torch.sum(torch.abs(self.dof_pos - self.default_dof_pos), dim=1) * (cmd_norm < self.blend_cmd_norm)
    '''
    def _reward_stand_still(self):
        cmd_norm = torch.norm(self.commands[:, :3], dim=1)
        is_stand = (cmd_norm < 0.1).float()

        lin_penalty = torch.sum(torch.square(self.base_lin_vel[:, :2]), dim=1)
        yaw_penalty = torch.square(self.base_ang_vel[:, 2])
        pose_penalty = 0.2 * torch.sum(
            torch.square(self.dof_pos - self.default_dof_pos), dim=1
        )

        return (lin_penalty + 0.5 * yaw_penalty + pose_penalty) * is_stand
    '''
    def _get_ik_target(self, commands=None):
        if commands is None:
            commands = self._get_effective_commands()
        ref_dof_pos = self.ik_reference.get_reference(
            self.gait_phase,
            commands,
        )

        # 정지 시 default pose로 블렌딩
        cmd_norm = torch.norm(commands, dim=1, keepdim=True)
        blend = torch.clamp(cmd_norm / self.blend_cmd_norm, 0.0, 1.0)

        ref_dof_pos = blend * ref_dof_pos + (1.0 - blend) * self.default_dof_pos

        return ref_dof_pos
        
    def _compute_torques(self, actions):
        actions_scaled = actions * self._get_effective_action_scale()
        ref_dof_pos = self._get_ik_target()
        desired_dof_pos = actions_scaled + ref_dof_pos
        if getattr(self, "use_actuator_lag", False):
            self.actuated_dof_target += (
                self.actuator_lag_alpha * (desired_dof_pos - self.actuated_dof_target))
            desired_dof_pos = self.actuated_dof_target
        p_gains = self.p_gains.unsqueeze(0) * self.stiffness_scales
        d_gains = self.d_gains.unsqueeze(0) * self.damping_scales
        torques = p_gains * (desired_dof_pos - self.dof_pos) - d_gains * self.dof_vel
        torques = torques * self.motor_strength_scales
        return torch.clip(torques, -self.torque_limits, self.torque_limits)
        
    def _reward_tracking_ik(self):
        scale = self._get_effective_action_scale()
        residual_rad = self.actions * scale

        weights = torch.tensor(
            [1.0, 1.0, 1.0] * 4,
            device=self.device,
            dtype=torch.float,
        ).view(1, 12)

        error = torch.sum(torch.square(residual_rad * weights), dim=1)

        # rad 단위 기준. 0.15~0.25 사이에서 튜닝
        sigma = getattr(self.cfg.rewards, "tracking_ik_sigma", 0.20)
        reward = torch.exp(-error / sigma)

        return reward * self._recovery_relief_scale("recovery_ik_relief_scale")

    def _reward_tracking_lin_vel(self):
        commands = self._get_effective_commands()
        lin_vel_error = torch.sum(
            torch.square(commands[:, :2] - self.base_lin_vel[:, :2]), dim=1)
        return torch.exp(-lin_vel_error / self.cfg.rewards.tracking_sigma)
        
    def _reward_tracking_ang_vel(self):
        commands = self._get_effective_commands()
        ang_vel_error = torch.square(
            commands[:, 2] - self.base_ang_vel[:, 2])
        return torch.exp(
            -ang_vel_error / self.cfg.rewards.tracking_sigma_ang_vel)

    def _reward_trot_contact(self):
        phases = (self.gait_phase + self.phase_offsets) % 1.0
        desired_contact = phases < self.duty_factor
        actual_contact = self.contact_forces[:, self.feet_indices, 2] > 1.0
        match = (actual_contact == desired_contact).float()
        cmd_norm = torch.norm(
            self._get_effective_commands(), dim=1, keepdim=True)
        is_moving = (cmd_norm > self.blend_cmd_norm).float()
        reward = torch.sum(match * is_moving, dim=1) / 4.0
        return reward * self._recovery_relief_scale("recovery_gait_relief_scale")

    def _recovery_tilt_mask(self):
        threshold_deg = getattr(self.cfg.rewards, "recovery_reward_tilt_threshold_deg", 4.0)
        threshold = math.sin(math.radians(threshold_deg))
        tilt = self._recovery_tilt_metric()
        return tilt, (tilt > threshold).float()

    def _reward_tilt_recovery(self):
        tilt, mask = self._recovery_tilt_mask()
        improvement = torch.clamp(self.last_tilt_metric - tilt, min=0.0, max=0.05)
        return improvement * mask

    def _reward_ang_vel_xy_recovery(self):
        _, mask = self._recovery_tilt_mask()
        ang_vel_xy = torch.norm(self.base_ang_vel[:, :2], dim=1)
        damping = torch.clamp(self.last_ang_vel_xy_metric - ang_vel_xy, min=0.0, max=0.5)
        return damping * mask

    def _reward_recovery_stance_contact(self):
        contact = (self.contact_forces[:, self.feet_indices, 2] > 1.0).float()
        contact_ratio = torch.mean(contact, dim=1)
        return contact_ratio * self._recovery_blend()
