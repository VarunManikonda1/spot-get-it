import math

class GaitPhaseGenerator:
    """
    exp043 학습 코드의 post_physics_step() phase update와 맞춘 버전.

    학습 코드:
      cmd_norm = norm(commands[:, :3])
      phase_scale = clamp(cmd_norm / 0.1, 0.0, 1.0)
      gait_phase = (gait_phase + dt / gait_period * phase_scale) % 1.0
    """

    def __init__(self, gait_period: float, phase_cmd_norm: float = 0.1):
        if gait_period <= 0.0:
            raise ValueError("gait_period must be positive")
        if phase_cmd_norm <= 0.0:
            raise ValueError("phase_cmd_norm must be positive")

        self.gait_period = gait_period
        self.phase_cmd_norm = phase_cmd_norm
        self.phase = 0.0

    def reset(self):
        self.phase = 0.0

    def update(
        self,
        dt: float,
        cmd_vx: float,
        cmd_vy: float,
        cmd_wz: float,
        phase_scale_multiplier: float = 1.0,
    ) -> float:
        cmd_norm = math.sqrt(cmd_vx * cmd_vx + cmd_vy * cmd_vy + cmd_wz * cmd_wz)
        phase_scale = max(0.0, min(1.0, cmd_norm / self.phase_cmd_norm))
        phase_scale *= max(0.0, min(1.0, phase_scale_multiplier))

        self.phase = (self.phase + (dt / self.gait_period) * phase_scale) % 1.0
        return self.phase

    def sin_cos(self):
        return (
            math.sin(2.0 * math.pi * self.phase),
            math.cos(2.0 * math.pi * self.phase),
        )
