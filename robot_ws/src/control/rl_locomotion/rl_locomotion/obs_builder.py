import math
from typing import List, Sequence, Tuple


class ObsBuilder:
    """
    exp043 observation layout 생성기.

    obs layout:
      0:3   base_ang_vel * 0.25
      3:6   projected_gravity
      6:9   commands * [2.0, 2.0, 0.25]
      9:21  dof_pos - ik_ref
      21:33 dof_vel * 0.05
      33:45 prev_actions
      45:47 sin(2*pi*phase), cos(2*pi*phase)
    """

    def __init__(
        self,
        obs_dim: int = 47,
        ang_vel_scale: float = 0.25,
        lin_vel_cmd_scale: float = 2.0,
        yaw_cmd_scale: float = 0.25,
        dof_pos_scale: float = 1.0,
        dof_vel_scale: float = 0.05,
    ):
        self.obs_dim = obs_dim
        self.ang_vel_scale = ang_vel_scale
        self.lin_vel_cmd_scale = lin_vel_cmd_scale
        self.yaw_cmd_scale = yaw_cmd_scale
        self.dof_pos_scale = dof_pos_scale
        self.dof_vel_scale = dof_vel_scale

    def build(
        self,
        base_ang_vel: Sequence[float],
        projected_gravity: Sequence[float],
        cmd_vx: float,
        cmd_vy: float,
        cmd_wz: float,
        dof_pos: Sequence[float],
        dof_vel: Sequence[float],
        ik_ref: Sequence[float],
        prev_actions: Sequence[float],
        gait_phase: float,
    ) -> List[float]:
        self._check_len(base_ang_vel, 3, "base_ang_vel")
        self._check_len(projected_gravity, 3, "projected_gravity")
        self._check_len(dof_pos, 12, "dof_pos")
        self._check_len(dof_vel, 12, "dof_vel")
        self._check_len(ik_ref, 12, "ik_ref")
        self._check_len(prev_actions, 12, "prev_actions")

        base_ang_vel_scaled = [
            float(base_ang_vel[i]) * self.ang_vel_scale
            for i in range(3)
        ]

        projected_gravity_scaled = [
            float(projected_gravity[i])
            for i in range(3)
        ]

        commands_scaled = [
            float(cmd_vx) * self.lin_vel_cmd_scale,
            float(cmd_vy) * self.lin_vel_cmd_scale,
            float(cmd_wz) * self.yaw_cmd_scale,
        ]

        dof_pos_minus_ref = [
            (float(dof_pos[i]) - float(ik_ref[i])) * self.dof_pos_scale
            for i in range(12)
        ]

        dof_vel_scaled = [
            float(dof_vel[i]) * self.dof_vel_scale
            for i in range(12)
        ]

        phase_sin = math.sin(2.0 * math.pi * gait_phase)
        phase_cos = math.cos(2.0 * math.pi * gait_phase)

        obs = (
            base_ang_vel_scaled
            + projected_gravity_scaled
            + commands_scaled
            + dof_pos_minus_ref
            + dof_vel_scaled
            + list(prev_actions)
            + [phase_sin, phase_cos]
        )

        if len(obs) != self.obs_dim:
            raise RuntimeError(f"observation size mismatch: {len(obs)} != {self.obs_dim}")

        return obs

    @staticmethod
    def _check_len(values: Sequence[float], expected: int, name: str):
        if len(values) != expected:
            raise ValueError(f"{name} must have {expected} elements, got {len(values)}")
