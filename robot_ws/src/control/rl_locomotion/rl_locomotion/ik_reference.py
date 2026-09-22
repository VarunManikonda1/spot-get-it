# robot_ws/src/control/rl_locomotion/rl_locomotion/ik_reference.py

import math
from typing import List, Optional, Sequence

from locomotion_common import SharedTrotReference


def clamp(x: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, x))


class Exp043TrotIkReference:
    """
    기존 exp043 계열 IK.

    특징:
    - yaw를 좌/우 stride 차이로만 반영
    - cmd_vy는 blend에는 쓰지만 foot lateral motion에는 쓰지 않음
    - shoulder_ref = 0
    """

    def __init__(
        self,
        default_joint_angles: List[float],
        gait_period: float = 0.6,
        duty_factor: float = 0.5,
        step_height: float = 0.03,
        body_height: float = 0.190,
        robot_width: float = 0.15,
        blend_cmd_norm: float = 0.1,
    ):
        if len(default_joint_angles) != 12:
            raise ValueError("default_joint_angles must have 12 elements")

        self.default_joint_angles = list(default_joint_angles)

        self.L1_X = 0.01
        self.L1_Z = 0.12
        self.L2 = 0.115
        self.L1_EFF = math.sqrt(self.L1_X ** 2 + self.L1_Z ** 2)
        self.ALPHA = math.atan2(self.L1_X, self.L1_Z)

        self.robot_width = robot_width
        self.gait_period = gait_period
        self.duty_factor = duty_factor
        self.step_height = step_height
        self.body_height = body_height
        self.blend_cmd_norm = blend_cmd_norm

        # FL, FR, RL, RR
        self.phase_offsets = [0.0, 0.5, 0.5, 0.0]

    def get_reference(
        self,
        phase: float,
        cmd_vx: float,
        cmd_vy: float,
        cmd_wz: float,
    ) -> List[float]:
        vx = cmd_vx
        wz = cmd_wz

        v_left = vx - (wz * self.robot_width / 2.0)
        v_right = vx + (wz * self.robot_width / 2.0)

        stance_time = self.gait_period * self.duty_factor
        stride_l = v_left * stance_time
        stride_r = v_right * stance_time

        # FL, FR, RL, RR
        strides = [stride_l, stride_r, stride_l, stride_r]

        ref = [0.0] * 12

        for leg_idx in range(4):
            local_phase = (phase + self.phase_offsets[leg_idx]) % 1.0
            stride = strides[leg_idx]

            z = -self.body_height

            if local_phase < self.duty_factor:
                t_stance = local_phase / self.duty_factor
                x = stride * (0.5 - t_stance)
            else:
                t_swing = (local_phase - self.duty_factor) / (1.0 - self.duty_factor)
                x = stride * (-0.5 + t_swing)
                z = -self.body_height + self.step_height * math.sin(math.pi * t_swing)

            theta_leg, theta_foot = self._solve_leg_ik(x, z)

            base = leg_idx * 3
            ref[base + 0] = 0.0
            ref[base + 1] = theta_leg
            ref[base + 2] = theta_foot

        cmd_norm = math.sqrt(cmd_vx * cmd_vx + cmd_vy * cmd_vy + cmd_wz * cmd_wz)
        blend = clamp(cmd_norm / self.blend_cmd_norm, 0.0, 1.0)

        return [
            blend * ref[i] + (1.0 - blend) * self.default_joint_angles[i]
            for i in range(12)
        ]

    def _solve_leg_ik(self, x: float, z: float):
        d = math.sqrt(x * x + z * z)

        cos_q2 = (
            d * d - self.L1_EFF * self.L1_EFF - self.L2 * self.L2
        ) / (2.0 * self.L1_EFF * self.L2)

        cos_q2 = clamp(cos_q2, -0.999, 0.999)
        q2 = math.acos(cos_q2)

        beta = math.atan2(x, -z)
        alpha_k = math.atan2(
            self.L2 * math.sin(q2),
            self.L1_EFF + self.L2 * math.cos(q2),
        )

        q1 = beta - alpha_k

        theta_leg = q1 - self.ALPHA
        theta_foot = q2 + self.ALPHA

        return theta_leg, theta_foot


class LateralTrotIkReference(Exp043TrotIkReference):
    """
    새 spotmicro_test.py 계열 IK.

    특징:
    - 각 다리의 leg_origin_x/y를 사용해 yaw에 따른 foot_vx/foot_vy 계산
    - cmd_vy 반영
    - lateral y displacement를 shoulder_ref로 변환
    - leg/foot IK는 z_eff = -sqrt(z^2 + y^2) 기반
    """

    def __init__(
        self,
        default_joint_angles: List[float],
        gait_period: float = 1.0,
        duty_factor: float = 0.55,
        step_height: float = 0.025,
        body_height: float = 0.190,
        robot_width: float = 0.15,
        blend_cmd_norm: float = 0.1,
        leg_origin_x: Optional[Sequence[float]] = None,
        leg_origin_y: Optional[Sequence[float]] = None,
        shoulder_sign: Optional[Sequence[float]] = None,
        max_stride_x: float = 0.12,
        max_stride_y: float = 0.03,
        shoulder_y_gain: float = 1.0,
        shoulder_ref_limit: float = 0.1,
    ):
        super().__init__(
            default_joint_angles=default_joint_angles,
            gait_period=gait_period,
            duty_factor=duty_factor,
            step_height=step_height,
            body_height=body_height,
            robot_width=robot_width,
            blend_cmd_norm=blend_cmd_norm,
        )

        self.leg_origin_x = list(leg_origin_x or [0.093, 0.093, -0.093, -0.093])
        self.leg_origin_y = list(leg_origin_y or [0.036, -0.036, 0.036, -0.036])
        self.shoulder_sign = list(shoulder_sign or [1.0, -1.0, 1.0, -1.0])

        if len(self.leg_origin_x) != 4:
            raise ValueError("leg_origin_x must have 4 elements")
        if len(self.leg_origin_y) != 4:
            raise ValueError("leg_origin_y must have 4 elements")
        if len(self.shoulder_sign) != 4:
            raise ValueError("shoulder_sign must have 4 elements")

        self.max_stride_x = float(max_stride_x)
        self.max_stride_y = float(max_stride_y)
        self.shoulder_y_gain = float(shoulder_y_gain)
        self.shoulder_ref_limit = float(shoulder_ref_limit)

    def get_reference(
        self,
        phase: float,
        cmd_vx: float,
        cmd_vy: float,
        cmd_wz: float,
    ) -> List[float]:
        stance_time = self.gait_period * self.duty_factor

        ref = [0.0] * 12

        for leg_idx in range(4):
            leg_x = self.leg_origin_x[leg_idx]
            leg_y = self.leg_origin_y[leg_idx]

            # 첨부 spotmicro_test.py와 동일한 의미
            foot_vx = cmd_vx - cmd_wz * leg_y
            foot_vy = cmd_vy + cmd_wz * leg_x

            stride_x = clamp(
                foot_vx * stance_time,
                -self.max_stride_x,
                self.max_stride_x,
            )
            stride_y = clamp(
                foot_vy * stance_time,
                -self.max_stride_y,
                self.max_stride_y,
            )

            local_phase = (phase + self.phase_offsets[leg_idx]) % 1.0

            x = 0.0
            y = 0.0
            z = -self.body_height

            if local_phase < self.duty_factor:
                t_stance = local_phase / self.duty_factor
                x = stride_x * (0.5 - t_stance)
                y = stride_y * (0.5 - t_stance)
            else:
                t_swing = (local_phase - self.duty_factor) / (1.0 - self.duty_factor)
                x = stride_x * (-0.5 + t_swing)
                y = stride_y * (-0.5 + t_swing)
                z = -self.body_height + self.step_height * math.sin(math.pi * t_swing)

            shoulder_raw = self.shoulder_y_gain * math.atan2(y, -z)
            shoulder_ref = clamp(
                shoulder_raw,
                -self.shoulder_ref_limit,
                self.shoulder_ref_limit,
            )
            shoulder_ref *= self.shoulder_sign[leg_idx]

            z_eff = -math.sqrt(max(z * z + y * y, 1.0e-6))
            theta_leg, theta_foot = self._solve_leg_ik(x, z_eff)

            base = leg_idx * 3
            ref[base + 0] = shoulder_ref
            ref[base + 1] = theta_leg
            ref[base + 2] = theta_foot

        cmd_norm = math.sqrt(cmd_vx * cmd_vx + cmd_vy * cmd_vy + cmd_wz * cmd_wz)
        blend = clamp(cmd_norm / self.blend_cmd_norm, 0.0, 1.0)

        return [
            blend * ref[i] + (1.0 - blend) * self.default_joint_angles[i]
            for i in range(12)
        ]


class SharedV1TrotIkReference:
    """
    Shared reference profile for future RL retraining.

    The same generator is used by classic_control directly. RL uses it as
    ik_ref and adds policy residuals.
    """

    def __init__(
        self,
        default_joint_angles: List[float],
        gait_period: float = 1.0,
        duty_factor: float = 0.55,
        body_height: float = 0.190,
        blend_cmd_norm: float = 0.1,
        leg_origin_x: Optional[Sequence[float]] = None,
        leg_origin_y: Optional[Sequence[float]] = None,
        shoulder_sign: Optional[Sequence[float]] = None,
        shoulder_offset_y: Optional[Sequence[float]] = None,
        max_stride_x: float = 0.085,
        max_stride_y: float = 0.024,
        soft_stride_limit: bool = True,
        shoulder_y_gain: float = 1.0,
        shoulder_ref_limit: float = 0.548,
        shared_step_height: Optional[Sequence[float]] = None,
        shared_default_foot_x: Optional[Sequence[float]] = None,
        shared_default_foot_y: Optional[Sequence[float]] = None,
        shared_phase_offsets: Optional[Sequence[float]] = None,
        upper_link_x: float = 0.010,
        upper_link_z: float = 0.120,
        lower_link: float = 0.115,
        toe_radius: float = 0.015,
    ):
        if len(default_joint_angles) != 12:
            raise ValueError("default_joint_angles must have 12 elements")

        self.default_joint_angles = list(default_joint_angles)
        self.blend_cmd_norm = float(blend_cmd_norm)
        self.reference = SharedTrotReference(
            gait_period=gait_period,
            duty_factor=duty_factor,
            body_height=body_height,
            step_height=shared_step_height or [0.025, 0.025, 0.025, 0.025],
            default_foot_x=shared_default_foot_x or [-0.040, -0.040, -0.040, -0.040],
            default_foot_y=shared_default_foot_y or [0.052, -0.052, 0.052, -0.052],
            leg_origin_x=leg_origin_x or [0.093, 0.093, -0.093, -0.093],
            leg_origin_y=leg_origin_y or [0.036, -0.036, 0.036, -0.036],
            shoulder_sign=shoulder_sign or [1.0, -1.0, 1.0, -1.0],
            shoulder_offset_y=shoulder_offset_y or [0.052, -0.052, 0.052, -0.052],
            phase_offsets=shared_phase_offsets or [0.0, 0.5, 0.5, 0.0],
            max_stride_x=max_stride_x,
            max_stride_y=max_stride_y,
            soft_stride_limit=soft_stride_limit,
            upper_link_x=upper_link_x,
            upper_link_z=upper_link_z,
            lower_link=lower_link,
            toe_radius=toe_radius,
            shoulder_y_gain=shoulder_y_gain,
            shoulder_limit=shoulder_ref_limit,
        )

    def get_reference(
        self,
        phase: float,
        cmd_vx: float,
        cmd_vy: float,
        cmd_wz: float,
    ) -> List[float]:
        ref = self.reference.get_reference(phase, cmd_vx, cmd_vy, cmd_wz)
        cmd_norm = math.sqrt(cmd_vx * cmd_vx + cmd_vy * cmd_vy + cmd_wz * cmd_wz)
        blend = clamp(cmd_norm / self.blend_cmd_norm, 0.0, 1.0)

        return [
            blend * ref[i] + (1.0 - blend) * self.default_joint_angles[i]
            for i in range(12)
        ]


# 기존 import 호환성 유지
TrotIkReference = Exp043TrotIkReference


def make_ik_reference(
    profile: str,
    default_joint_angles: List[float],
    gait_period: float,
    duty_factor: float,
    step_height: float,
    body_height: float,
    robot_width: float,
    blend_cmd_norm: float,
    leg_origin_x: Optional[Sequence[float]] = None,
    leg_origin_y: Optional[Sequence[float]] = None,
    shoulder_sign: Optional[Sequence[float]] = None,
    shoulder_offset_y: Optional[Sequence[float]] = None,
    max_stride_x: float = 0.085,
    max_stride_y: float = 0.024,
    soft_stride_limit: bool = True,
    shoulder_y_gain: float = 1.0,
    shoulder_ref_limit: float = 0.548,
    shared_step_height: Optional[Sequence[float]] = None,
    shared_default_foot_x: Optional[Sequence[float]] = None,
    shared_default_foot_y: Optional[Sequence[float]] = None,
    shared_phase_offsets: Optional[Sequence[float]] = None,
    upper_link_x: float = 0.010,
    upper_link_z: float = 0.120,
    lower_link: float = 0.115,
    toe_radius: float = 0.015,
):
    p = profile.strip().lower()

    if p in ("exp043", "legacy", "old"):
        return Exp043TrotIkReference(
            default_joint_angles=default_joint_angles,
            gait_period=gait_period,
            duty_factor=duty_factor,
            step_height=step_height,
            body_height=body_height,
            robot_width=robot_width,
            blend_cmd_norm=blend_cmd_norm,
        )

    if p in ("lateral", "lateral_ik", "spotmicro_test"):
        return LateralTrotIkReference(
            default_joint_angles=default_joint_angles,
            gait_period=gait_period,
            duty_factor=duty_factor,
            step_height=step_height,
            body_height=body_height,
            robot_width=robot_width,
            blend_cmd_norm=blend_cmd_norm,
            leg_origin_x=leg_origin_x,
            leg_origin_y=leg_origin_y,
            shoulder_sign=shoulder_sign,
            max_stride_x=max_stride_x,
            max_stride_y=max_stride_y,
            shoulder_y_gain=shoulder_y_gain,
            shoulder_ref_limit=shoulder_ref_limit,
        )

    if p in ("shared", "shared_v1", "common"):
        return SharedV1TrotIkReference(
            default_joint_angles=default_joint_angles,
            gait_period=gait_period,
            duty_factor=duty_factor,
            body_height=body_height,
            blend_cmd_norm=blend_cmd_norm,
            leg_origin_x=leg_origin_x,
            leg_origin_y=leg_origin_y,
            shoulder_sign=shoulder_sign,
            shoulder_offset_y=shoulder_offset_y,
            max_stride_x=max_stride_x,
            max_stride_y=max_stride_y,
            soft_stride_limit=soft_stride_limit,
            shoulder_y_gain=shoulder_y_gain,
            shoulder_ref_limit=shoulder_ref_limit,
            shared_step_height=shared_step_height,
            shared_default_foot_x=shared_default_foot_x,
            shared_default_foot_y=shared_default_foot_y,
            shared_phase_offsets=shared_phase_offsets,
            upper_link_x=upper_link_x,
            upper_link_z=upper_link_z,
            lower_link=lower_link,
            toe_radius=toe_radius,
        )

    raise ValueError(f"unknown ik_profile: {profile}")
