import math
from typing import List, Optional, Sequence


NUM_LEGS = 4
NUM_JOINTS = 12


def clamp(x: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, x))


def expand_leg_values(value, default: float, name: str) -> List[float]:
    if value is None:
        return [float(default)] * NUM_LEGS
    if isinstance(value, (float, int)):
        return [float(value)] * NUM_LEGS
    values = [float(v) for v in value]
    if len(values) != NUM_LEGS:
        raise ValueError(f"{name} must have {NUM_LEGS} elements")
    return values


class SharedTrotReference:
    """
    Shared diagonal-trot foot reference and URDF-based IK.

    The reference target is the toe contact point in the shoulder frame.
    The IK solves the URDF chain:
    shoulder -> leg joint, leg -> foot joint, foot -> toe sphere center.
    """

    def __init__(
        self,
        gait_period: float = 1.0,
        duty_factor: float = 0.55,
        body_height: float = 0.210,
        step_height: Sequence[float] = (0.022, 0.022, 0.022, 0.022),
        default_foot_x: Sequence[float] = (0.0, 0.0, 0.0, 0.0),
        default_foot_y: Sequence[float] = (0.052, -0.052, 0.052, -0.052),
        leg_origin_x: Sequence[float] = (0.093, 0.093, -0.093, -0.093),
        leg_origin_y: Sequence[float] = (0.036, -0.036, 0.036, -0.036),
        shoulder_sign: Sequence[float] = (1.0, -1.0, 1.0, -1.0),
        shoulder_offset_y: Sequence[float] = (0.052, -0.052, 0.052, -0.052),
        phase_offsets: Sequence[float] = (0.0, 0.5, 0.5, 0.0),
        max_stride_x: float = 0.085,
        max_stride_y: float = 0.024,
        soft_stride_limit: bool = True,
        upper_link_x: float = 0.010,
        upper_link_z: float = 0.120,
        lower_link: float = 0.115,
        toe_radius: float = 0.015,
        shoulder_y_gain: float = 1.0,
        shoulder_limit: float = 0.548,
        joint_min: Optional[Sequence[float]] = None,
        joint_max: Optional[Sequence[float]] = None,
    ):
        if gait_period <= 0.0:
            raise ValueError("gait_period must be positive")
        if not 0.0 < duty_factor < 1.0:
            raise ValueError("duty_factor must be in (0, 1)")
        if upper_link_z <= 0.0 or lower_link <= 0.0:
            raise ValueError("link lengths must be positive")
        if toe_radius < 0.0:
            raise ValueError("toe_radius must be non-negative")

        self.gait_period = float(gait_period)
        self.duty_factor = float(duty_factor)
        self.body_height = expand_leg_values(body_height, body_height, "body_height")
        self.step_height = expand_leg_values(step_height, 0.03, "step_height")
        self.default_foot_x = expand_leg_values(default_foot_x, 0.0, "default_foot_x")
        self.default_foot_y = expand_leg_values(default_foot_y, 0.0, "default_foot_y")
        self.leg_origin_x = expand_leg_values(leg_origin_x, 0.0, "leg_origin_x")
        self.leg_origin_y = expand_leg_values(leg_origin_y, 0.0, "leg_origin_y")
        self.shoulder_sign = expand_leg_values(shoulder_sign, 1.0, "shoulder_sign")
        self.shoulder_offset_y = expand_leg_values(
            shoulder_offset_y,
            0.0,
            "shoulder_offset_y",
        )
        self.phase_offsets = expand_leg_values(phase_offsets, 0.0, "phase_offsets")

        self.max_stride_x = float(max_stride_x)
        self.max_stride_y = float(max_stride_y)
        self.soft_stride_limit = bool(soft_stride_limit)
        self.upper_link_x = float(upper_link_x)
        self.upper_link_z = float(upper_link_z)
        self.lower_link = float(lower_link)
        self.toe_radius = float(toe_radius)
        self.shoulder_y_gain = float(shoulder_y_gain)
        self.shoulder_limit = float(shoulder_limit)

        self.joint_min = list(joint_min) if joint_min is not None else None
        self.joint_max = list(joint_max) if joint_max is not None else None
        if (self.joint_min is None) != (self.joint_max is None):
            raise ValueError("joint_min and joint_max must be provided together")
        if self.joint_min is not None:
            if len(self.joint_min) != NUM_JOINTS or len(self.joint_max) != NUM_JOINTS:
                raise ValueError("joint limits must have 12 elements")

    def get_reference(
        self,
        phase: float,
        cmd_vx: float,
        cmd_vy: float,
        cmd_wz: float,
    ) -> List[float]:
        stance_time = self.gait_period * self.duty_factor
        target = [0.0] * NUM_JOINTS

        for leg in range(NUM_LEGS):
            foot_vx = cmd_vx - cmd_wz * self.leg_origin_y[leg]
            foot_vy = cmd_vy + cmd_wz * self.leg_origin_x[leg]

            stride_x = self._limit_stride(foot_vx * stance_time, self.max_stride_x)
            stride_y = self._limit_stride(foot_vy * stance_time, self.max_stride_y)

            local_phase = (phase + self.phase_offsets[leg]) % 1.0
            x_off, y_off, z_off = self._foot_offset(
                local_phase,
                stride_x,
                stride_y,
                self.step_height[leg],
            )

            x = self.default_foot_x[leg] + x_off
            y = self.default_foot_y[leg] + y_off
            z_contact = -self.body_height[leg] + z_off
            z = z_contact + self.toe_radius

            shoulder, thigh, knee = self._leg_ik(leg, x, y, z)
            base = leg * 3
            target[base + 0] = shoulder
            target[base + 1] = thigh
            target[base + 2] = knee

        return self._clamp_joints(target)

    def _foot_offset(
        self,
        leg_phase: float,
        stride_x: float,
        stride_y: float,
        step_height: float,
    ):
        if leg_phase < self.duty_factor:
            s = leg_phase / self.duty_factor
            x = stride_x * (0.5 - s)
            y = stride_y * (0.5 - s)
            return x, y, 0.0

        s = (leg_phase - self.duty_factor) / (1.0 - self.duty_factor)
        # Smooth x/y endpoints. Use sin^2 lift so liftoff/touchdown vertical
        # velocity starts and ends at zero, which is easier for small servos.
        ss = s * s * (3.0 - 2.0 * s)
        x = stride_x * (-0.5 + ss)
        y = stride_y * (-0.5 + ss)
        lift = math.sin(math.pi * s)
        z = step_height * lift * lift
        return x, y, z

    def _limit_stride(self, stride: float, limit: float) -> float:
        if limit <= 0.0:
            return 0.0
        if self.soft_stride_limit:
            return limit * math.tanh(stride / limit)
        return clamp(stride, -limit, limit)

    def _leg_ik(self, leg: int, x: float, y: float, z: float):
        shoulder_axis = self.shoulder_sign[leg]
        if abs(shoulder_axis) < 1.0e-9:
            raise ValueError("shoulder axis sign must be non-zero")

        leg_y = self.shoulder_offset_y[leg]
        yz_radius2 = y * y + z * z
        sagittal_z = -math.sqrt(max(yz_radius2 - leg_y * leg_y, 1.0e-9))

        target_angle = math.atan2(z, y)
        leg_plane_angle = math.atan2(sagittal_z, leg_y)
        shoulder_physical = self._wrap_pi(target_angle - leg_plane_angle)
        shoulder_physical *= self.shoulder_y_gain
        shoulder_physical = clamp(
            shoulder_physical,
            -self.shoulder_limit,
            self.shoulder_limit,
        )
        shoulder = shoulder_physical / shoulder_axis

        cos_s = math.cos(shoulder_physical)
        sin_s = math.sin(shoulder_physical)
        z_in_shoulder = -y * sin_s + z * cos_s

        thigh, knee = self._solve_sagittal_ik(x, z_in_shoulder)
        return shoulder, thigh, knee

    def _solve_sagittal_ik(self, x: float, z: float):
        # URDF geometry:
        # leg -> foot joint: (upper_link_x, 0, -upper_link_z)
        # foot -> toe fixed joint: (0, 0, -lower_link)
        upper_eff = math.sqrt(
            self.upper_link_x * self.upper_link_x
            + self.upper_link_z * self.upper_link_z
        )
        upper_alpha = math.atan2(self.upper_link_x, self.upper_link_z)

        r2 = x * x + z * z
        cos_knee = (
            r2 - upper_eff * upper_eff - self.lower_link * self.lower_link
        ) / (2.0 * upper_eff * self.lower_link)
        cos_knee = clamp(cos_knee, -1.0, 1.0)
        sin_knee = math.sqrt(max(0.0, 1.0 - cos_knee * cos_knee))
        knee_raw = math.atan2(sin_knee, cos_knee)

        a = upper_eff + self.lower_link * cos_knee
        b = self.lower_link * sin_knee
        det = a * a + b * b
        z_neg = -z

        sin_thigh = (a * x - b * z_neg) / det
        cos_thigh = (b * x + a * z_neg) / det
        thigh = math.atan2(sin_thigh, cos_thigh) - upper_alpha
        knee = knee_raw + upper_alpha

        return thigh, knee

    @staticmethod
    def _wrap_pi(angle: float) -> float:
        return (angle + math.pi) % (2.0 * math.pi) - math.pi

    def _clamp_joints(self, target: Sequence[float]) -> List[float]:
        if self.joint_min is None:
            return [float(v) for v in target]
        return [
            clamp(float(target[i]), self.joint_min[i], self.joint_max[i])
            for i in range(NUM_JOINTS)
        ]
