#!/usr/bin/env python3

import math
import threading
import time
from typing import List, Optional, Sequence, Tuple

import rclpy
from geometry_msgs.msg import Twist
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy
from std_msgs.msg import String

from locomotion_common import SharedTrotReference
from classic_control.constants import MODE_CLASSIC_CONTROL, NUM_JOINTS, NUM_LEGS
from classic_control.math_utils import clamp, finite_list, smoothstep
from robot_interfaces.msg import JointFeedback, JointTarget


class ClassicControlNode(Node):
    """
    Classic body-frame trot controller.

    The ROS node owns command input, state transitions, and JointTarget output.
    Gait trajectory generation and leg IK live in separate modules so hardware
    geometry changes can be reviewed without touching ROS transport code.
    """

    STAND = "STAND"
    STANDUP = "STANDUP"
    DWELL = "DWELL"
    WALK = "WALK"
    SETTLE = "SETTLE"

    def __init__(self):
        super().__init__("classic_control_node")

        self.control_qos = QoSProfile(
            history=HistoryPolicy.KEEP_LAST,
            depth=1,
            reliability=ReliabilityPolicy.BEST_EFFORT,
        )

        self.declare_node_parameters()
        self.load_parameters()
        self.validate_config()
        self.configure_motion_model()
        self.configure_runtime_state()
        self.configure_ros_interfaces()

        self.timer = self.create_timer(self.dt, self.control_loop)

        self.get_logger().info(
            "classic_control_node started: "
            f"cmd={self.cmd_vel_topic}, target={self.target_topic}, "
            f"feedback={self.feedback_topic}, rate={self.rate_hz:.1f}Hz, "
            f"period={self.gait_period_sec:.2f}s, "
            f"upper=({self.upper_link_x_mm:.1f},{self.upper_link_z_mm:.1f})mm, "
            f"lower={self.lower_link_mm:.1f}mm, "
            f"body_height={self.body_height_mm_per_leg}mm, "
            f"foot_x={self.default_foot_x_mm_per_leg}mm"
        )

    def declare_node_parameters(self):
        self.declare_parameter("rate_hz", 50.0)
        self.declare_parameter("robot_id", "spot_01")
        self.declare_parameter("cmd_vel_topic", "")
        self.declare_parameter("target_topic", "/control/classic_control/joint_target")
        self.declare_parameter("feedback_topic", "/control/actuator/joint_feedback")
        self.declare_parameter("active_mode_topic", "/control/behavior/active_mode")

        self.declare_parameter("command_timeout_sec", 0.35)
        self.declare_parameter("linear_deadband_mps", 0.01)
        self.declare_parameter("lateral_deadband_mps", 0.01)
        self.declare_parameter("angular_deadband_radps", 0.03)
        self.declare_parameter("vx_min_mps", -0.05)
        self.declare_parameter("vx_max_mps", 0.20)
        self.declare_parameter("vy_min_mps", -0.05)
        self.declare_parameter("vy_max_mps", 0.05)
        self.declare_parameter("wz_min_radps", -0.35)
        self.declare_parameter("wz_max_radps", 0.35)
        self.declare_parameter("max_linear_accel_mps2", 0.25)
        self.declare_parameter("max_angular_accel_radps2", 0.7)

        self.declare_parameter("upper_link_x_mm", 10.0)
        self.declare_parameter("upper_link_z_mm", 120.0)
        self.declare_parameter("lower_link_mm", 115.0)
        self.declare_parameter("body_height_mm", 210.0)
        self.declare_parameter("body_height_mm_per_leg", [210.0, 210.0, 210.0, 210.0])
        self.declare_parameter("default_foot_x_mm", 0.0)
        self.declare_parameter("default_foot_x_mm_per_leg", [0.0, 0.0, 0.0, 0.0])
        self.declare_parameter("default_foot_y_mm", 52.0)
        self.declare_parameter("default_foot_y_mm_per_leg", [52.0, -52.0, 52.0, -52.0])
        self.declare_parameter("toe_radius_mm", 15.0)

        self.declare_parameter("leg_origin_x_m", [0.093, 0.093, -0.093, -0.093])
        self.declare_parameter("leg_origin_y_m", [0.036, -0.036, 0.036, -0.036])
        self.declare_parameter("shoulder_sign", [1.0, -1.0, 1.0, -1.0])
        self.declare_parameter("shoulder_offset_y_m", [0.052, -0.052, 0.052, -0.052])
        self.declare_parameter("shoulder_limit_rad", 0.548)
        self.declare_parameter("phase_offsets", [0.0, 0.5, 0.5, 0.0])

        self.declare_parameter("gait_period_sec", 1.2)
        self.declare_parameter("duty_factor", 0.58)
        self.declare_parameter("lift_z_mm", 22.0)
        self.declare_parameter("lift_z_mm_per_leg", [22.0, 22.0, 22.0, 22.0])
        self.declare_parameter("max_stride_x_mm", 85.0)
        self.declare_parameter("max_stride_y_mm", 24.0)
        self.declare_parameter("soft_stride_limit", True)

        self.declare_parameter("stand_dwell_sec", 1.0)
        self.declare_parameter("min_transition_sec", 2.0)
        self.declare_parameter("max_transition_sec", 3.0)
        self.declare_parameter("transition_sec_per_rad", 1.0)
        self.declare_parameter("walk_start_stand_tolerance_rad", 0.08)
        self.declare_parameter("max_delta_smooth_rad", 0.02)
        self.declare_parameter("max_delta_walk_rad", 0.06)
        self.declare_parameter("max_delta_walk_rad_per_joint", [0.0] * NUM_JOINTS)
        self.declare_parameter("max_delta_smooth_rad_per_joint", [0.0] * NUM_JOINTS)

        self.declare_parameter(
            "joint_min_rad",
            [
                -0.548, -2.666, -0.100,
                -0.548, -2.666, -0.100,
                -0.548, -2.666, -0.100,
                -0.548, -2.666, -0.100,
            ],
        )
        self.declare_parameter(
            "joint_max_rad",
            [
                0.548, 1.548, 2.590,
                0.548, 1.548, 2.590,
                0.548, 1.548, 2.590,
                0.548, 1.548, 2.590,
            ],
        )

    def load_parameters(self):
        self.rate_hz = float(self.get_parameter("rate_hz").value)
        if self.rate_hz <= 0.0:
            raise RuntimeError("rate_hz must be positive")
        self.dt = 1.0 / self.rate_hz

        self.robot_id = str(self.get_parameter("robot_id").value)
        cmd_topic = str(self.get_parameter("cmd_vel_topic").value).strip()
        self.cmd_vel_topic = cmd_topic or f"/control/cmd_vel/{self.robot_id}"
        self.target_topic = str(self.get_parameter("target_topic").value)
        self.feedback_topic = str(self.get_parameter("feedback_topic").value)
        self.active_mode_topic = str(self.get_parameter("active_mode_topic").value)

        self.command_timeout_sec = float(self.get_parameter("command_timeout_sec").value)
        self.linear_deadband_mps = float(self.get_parameter("linear_deadband_mps").value)
        self.lateral_deadband_mps = float(self.get_parameter("lateral_deadband_mps").value)
        self.angular_deadband_radps = float(self.get_parameter("angular_deadband_radps").value)
        self.vx_min_mps = float(self.get_parameter("vx_min_mps").value)
        self.vx_max_mps = float(self.get_parameter("vx_max_mps").value)
        self.vy_min_mps = float(self.get_parameter("vy_min_mps").value)
        self.vy_max_mps = float(self.get_parameter("vy_max_mps").value)
        self.wz_min_radps = float(self.get_parameter("wz_min_radps").value)
        self.wz_max_radps = float(self.get_parameter("wz_max_radps").value)
        self.max_linear_accel_mps2 = float(self.get_parameter("max_linear_accel_mps2").value)
        self.max_angular_accel_radps2 = float(
            self.get_parameter("max_angular_accel_radps2").value
        )

        self.upper_link_x_mm = float(self.get_parameter("upper_link_x_mm").value)
        self.upper_link_z_mm = float(self.get_parameter("upper_link_z_mm").value)
        self.lower_link_mm = float(self.get_parameter("lower_link_mm").value)
        self.body_height_mm = float(self.get_parameter("body_height_mm").value)
        self.body_height_mm_per_leg = self._load_leg_values_mm(
            "body_height_mm_per_leg",
            self.body_height_mm,
        )
        self.default_foot_x_mm = float(self.get_parameter("default_foot_x_mm").value)
        self.default_foot_x_mm_per_leg = self._load_leg_values_mm(
            "default_foot_x_mm_per_leg",
            self.default_foot_x_mm,
        )
        self.default_foot_y_mm = float(self.get_parameter("default_foot_y_mm").value)
        self.default_foot_y_mm_per_leg = self._load_leg_values_mm(
            "default_foot_y_mm_per_leg",
            self.default_foot_y_mm,
        )
        self.toe_radius_mm = float(self.get_parameter("toe_radius_mm").value)

        self.leg_origin_x_m = [float(x) for x in self.get_parameter("leg_origin_x_m").value]
        self.leg_origin_y_m = [float(y) for y in self.get_parameter("leg_origin_y_m").value]
        self.shoulder_sign = [float(s) for s in self.get_parameter("shoulder_sign").value]
        self.shoulder_offset_y_m = [
            float(y) for y in self.get_parameter("shoulder_offset_y_m").value
        ]
        self.shoulder_limit_rad = float(self.get_parameter("shoulder_limit_rad").value)
        self.phase_offsets = [float(x) for x in self.get_parameter("phase_offsets").value]

        self.gait_period_sec = float(self.get_parameter("gait_period_sec").value)
        self.duty_factor = float(self.get_parameter("duty_factor").value)
        self.lift_z_mm = float(self.get_parameter("lift_z_mm").value)
        self.lift_z_mm_per_leg = self._load_leg_values_mm(
            "lift_z_mm_per_leg",
            self.lift_z_mm,
        )
        self.max_stride_x_mm = float(self.get_parameter("max_stride_x_mm").value)
        self.max_stride_y_mm = float(self.get_parameter("max_stride_y_mm").value)
        self.soft_stride_limit = bool(self.get_parameter("soft_stride_limit").value)

        self.stand_dwell_sec = float(self.get_parameter("stand_dwell_sec").value)
        self.min_transition_sec = float(self.get_parameter("min_transition_sec").value)
        self.max_transition_sec = float(self.get_parameter("max_transition_sec").value)
        self.transition_sec_per_rad = float(self.get_parameter("transition_sec_per_rad").value)
        self.walk_start_stand_tolerance_rad = float(
            self.get_parameter("walk_start_stand_tolerance_rad").value
        )
        self.max_delta_smooth_rad = float(self.get_parameter("max_delta_smooth_rad").value)
        self.max_delta_walk_rad = float(self.get_parameter("max_delta_walk_rad").value)

        self.max_delta_smooth_rad_per_joint = self._load_joint_values_rad(
                "max_delta_smooth_rad_per_joint",
                self.max_delta_smooth_rad,
)

        self.max_delta_walk_rad_per_joint = self._load_joint_values_rad(
                "max_delta_walk_rad_per_joint",
                self.max_delta_walk_rad,
)
        self.joint_min_rad = [float(x) for x in self.get_parameter("joint_min_rad").value]
        self.joint_max_rad = [float(x) for x in self.get_parameter("joint_max_rad").value]

    def _load_leg_values_mm(self, name: str, fallback: float) -> List[float]:
        if name not in getattr(self, "_parameter_overrides", {}):
            return [float(fallback)] * NUM_LEGS
        values = [float(x) for x in self.get_parameter(name).value]
        if not values:
            return [float(fallback)] * NUM_LEGS
        if len(values) != NUM_LEGS:
            raise RuntimeError(f"{name} must have {NUM_LEGS} elements")
        return values

    def _load_joint_values_rad(self, name: str, fallback: float) -> List[float]:
        if name not in getattr(self, "_parameter_overrides", {}):
            return [float(fallback)] * NUM_JOINTS

        values = [float(x) for x in self.get_parameter(name).value]
        if len(values) != NUM_JOINTS:
            raise RuntimeError(f"{name} must have {NUM_JOINTS} elements")

        return values

    def validate_config(self):
        if not 0.0 < self.duty_factor < 1.0:
            raise RuntimeError("duty_factor must be in (0, 1)")
        if self.gait_period_sec <= 0.0:
            raise RuntimeError("gait_period_sec must be positive")
        if self.upper_link_z_mm <= 0.0 or self.lower_link_mm <= 0.0:
            raise RuntimeError("leg link lengths must be positive")
        if self.body_height_mm <= 0.0:
            raise RuntimeError("body_height_mm must be positive")
        if self.toe_radius_mm < 0.0:
            raise RuntimeError("toe_radius_mm must be non-negative")
        if self.body_height_mm <= self.toe_radius_mm:
            raise RuntimeError("body_height_mm must be larger than toe_radius_mm")
        if self.max_stride_x_mm <= 0.0 or self.max_stride_y_mm < 0.0:
            raise RuntimeError("stride limits must be valid")
        if self.max_delta_walk_rad <= 0.0:
            raise RuntimeError("max_delta_walk_rad must be positive")
        if self.walk_start_stand_tolerance_rad < 0.0:
            raise RuntimeError("walk_start_stand_tolerance_rad must be non-negative")

        for name, arr, expected in [
            ("leg_origin_x_m", self.leg_origin_x_m, NUM_LEGS),
            ("leg_origin_y_m", self.leg_origin_y_m, NUM_LEGS),
            ("shoulder_sign", self.shoulder_sign, NUM_LEGS),
            ("shoulder_offset_y_m", self.shoulder_offset_y_m, NUM_LEGS),
            ("phase_offsets", self.phase_offsets, NUM_LEGS),
            ("body_height_mm_per_leg", self.body_height_mm_per_leg, NUM_LEGS),
            ("default_foot_x_mm_per_leg", self.default_foot_x_mm_per_leg, NUM_LEGS),
            ("default_foot_y_mm_per_leg", self.default_foot_y_mm_per_leg, NUM_LEGS),
            ("lift_z_mm_per_leg", self.lift_z_mm_per_leg, NUM_LEGS),
            ("joint_min_rad", self.joint_min_rad, NUM_JOINTS),
            ("joint_max_rad", self.joint_max_rad, NUM_JOINTS),
        ]:
            if len(arr) != expected:
                raise RuntimeError(f"{name} must have {expected} elements")

        for i in range(NUM_JOINTS):
            if self.joint_min_rad[i] >= self.joint_max_rad[i]:
                raise RuntimeError(f"invalid joint limit at index {i}")

        for name, arr in [
                ("max_delta_smooth_rad_per_joint", self.max_delta_smooth_rad_per_joint),
                ("max_delta_walk_rad_per_joint", self.max_delta_walk_rad_per_joint),
]:
            if len(arr) != NUM_JOINTS:
                raise RuntimeError(f"{name} must have {NUM_JOINTS} elements")
        for v in arr:
            if v <= 0.0:
                raise RuntimeError(f"{name} values must be positive")

    def configure_motion_model(self):
        self.gait = SharedTrotReference(
            gait_period=self.gait_period_sec,
            duty_factor=self.duty_factor,
            body_height=[v * 0.001 for v in self.body_height_mm_per_leg],
            step_height=[v * 0.001 for v in self.lift_z_mm_per_leg],
            default_foot_x=[v * 0.001 for v in self.default_foot_x_mm_per_leg],
            default_foot_y=[v * 0.001 for v in self.default_foot_y_mm_per_leg],
            leg_origin_x=self.leg_origin_x_m,
            leg_origin_y=self.leg_origin_y_m,
            shoulder_sign=self.shoulder_sign,
            shoulder_offset_y=self.shoulder_offset_y_m,
            phase_offsets=self.phase_offsets,
            max_stride_x=self.max_stride_x_mm * 0.001,
            max_stride_y=self.max_stride_y_mm * 0.001,
            soft_stride_limit=self.soft_stride_limit,
            upper_link_x=self.upper_link_x_mm * 0.001,
            upper_link_z=self.upper_link_z_mm * 0.001,
            lower_link=self.lower_link_mm * 0.001,
            toe_radius=self.toe_radius_mm * 0.001,
            shoulder_limit=self.shoulder_limit_rad,
            joint_min=self.joint_min_rad,
            joint_max=self.joint_max_rad,
        )

    def configure_runtime_state(self):
        self.seq = 0
        self.gait_phase = 0.0
        self.gait_cycle_count = 0

        self.state = self.STAND
        self.state_start_time = time.perf_counter()
        self.walk_start_time = self.state_start_time
        self.transition_duration = self.min_transition_sec
        self.transition_from = [0.0] * NUM_JOINTS
        self.transition_to = [0.0] * NUM_JOINTS
        self.output_motion_active = False

        self.filtered_vx = 0.0
        self.filtered_vy = 0.0
        self.filtered_wz = 0.0

        self.stand_target = self.compute_targets(0.0, 0.0, 0.0, 0.0)
        self.last_target = list(self.stand_target)
        self.latest_feedback: Optional[List[float]] = None
        self.latest_feedback_time: Optional[float] = None
        self.classic_active = False
        self.activation_pending = False

        self.cmd_lock = threading.Lock()
        self.cmd_vx = 0.0
        self.cmd_vy = 0.0
        self.cmd_wz = 0.0
        self.last_cmd_time: Optional[float] = None

    def configure_ros_interfaces(self):
        self.cmd_sub = self.create_subscription(
            Twist,
            self.cmd_vel_topic,
            self.cmd_vel_callback,
            10,
        )
        self.active_mode_sub = self.create_subscription(
            String,
            self.active_mode_topic,
            self.active_mode_callback,
            QoSProfile(
                history=HistoryPolicy.KEEP_LAST,
                depth=1,
                reliability=ReliabilityPolicy.RELIABLE,
                durability=DurabilityPolicy.TRANSIENT_LOCAL,
            ),
        )
        self.feedback_sub = self.create_subscription(
            JointFeedback,
            self.feedback_topic,
            self.joint_feedback_callback,
            self.control_qos,
        )
        self.target_pub = self.create_publisher(
            JointTarget,
            self.target_topic,
            self.control_qos,
        )

    def active_mode_callback(self, msg: String):
        new_active = msg.data.strip().upper() in ("CLASSIC", "CLASSIC_CONTROL")
        if new_active == self.classic_active:
            return

        self.classic_active = new_active
        self.update_filtered_command(0.0, 0.0, 0.0)

        if new_active:
            self.activation_pending = True
            self.state = self.STAND
            self.gait_phase = 0.0
            self.gait_cycle_count = 0
            self.get_logger().info("CLASSIC activated; standing before walk")
            return

        self.activation_pending = False
        self.state = self.STAND
        self.state_start_time = time.perf_counter()
        self.last_target = list(self.stand_target)

    def cmd_vel_callback(self, msg: Twist):
        vx = clamp(float(msg.linear.x), self.vx_min_mps, self.vx_max_mps)
        vy = clamp(float(msg.linear.y), self.vy_min_mps, self.vy_max_mps)
        wz = clamp(float(msg.angular.z), self.wz_min_radps, self.wz_max_radps)

        if abs(vx) < self.linear_deadband_mps:
            vx = 0.0
        if abs(vy) < self.lateral_deadband_mps:
            vy = 0.0
        if abs(wz) < self.angular_deadband_radps:
            wz = 0.0

        with self.cmd_lock:
            self.cmd_vx = vx
            self.cmd_vy = vy
            self.cmd_wz = wz
            self.last_cmd_time = time.perf_counter()

    def joint_feedback_callback(self, msg: JointFeedback):
        if not finite_list(msg.position_rad, NUM_JOINTS):
            return
        self.latest_feedback = [float(v) for v in msg.position_rad]
        self.latest_feedback_time = time.perf_counter()

    def snapshot_command(self) -> Tuple[float, float, float]:
        now = time.perf_counter()
        with self.cmd_lock:
            if self.last_cmd_time is None:
                return 0.0, 0.0, 0.0
            if now - self.last_cmd_time > self.command_timeout_sec:
                return 0.0, 0.0, 0.0
            return self.cmd_vx, self.cmd_vy, self.cmd_wz

    def command_is_moving(self, vx: float, vy: float, wz: float) -> bool:
        return (
            abs(vx) >= self.linear_deadband_mps
            or abs(vy) >= self.lateral_deadband_mps
            or abs(wz) >= self.angular_deadband_radps
        )

    def update_filtered_command(self, vx: float, vy: float, wz: float):
        dv = self.max_linear_accel_mps2 * self.dt
        dw = self.max_angular_accel_radps2 * self.dt

        self.filtered_vx += clamp(vx - self.filtered_vx, -dv, dv)
        self.filtered_vy += clamp(vy - self.filtered_vy, -dv, dv)
        self.filtered_wz += clamp(wz - self.filtered_wz, -dw, dw)

        if abs(self.filtered_vx) < 1.0e-4:
            self.filtered_vx = 0.0
        if abs(self.filtered_vy) < 1.0e-4:
            self.filtered_vy = 0.0
        if abs(self.filtered_wz) < 1.0e-4:
            self.filtered_wz = 0.0

    def compute_targets(
        self,
        phase: float,
        vx_mps: float,
        vy_mps: float,
        wz_radps: float,
    ) -> List[float]:
        return self.gait.get_reference(phase, vx_mps, vy_mps, wz_radps)

    def clamp_joint_targets(self, target: Sequence[float]) -> List[float]:
        return [
            clamp(float(target[i]), self.joint_min_rad[i], self.joint_max_rad[i])
            for i in range(NUM_JOINTS)
        ]

    def transition_source(self) -> List[float]:
        now = time.perf_counter()
        if (
            self.latest_feedback is not None
            and self.latest_feedback_time is not None
            and now - self.latest_feedback_time <= 0.5
        ):
            return list(self.latest_feedback)
        return list(self.last_target)

    def begin_transition(self, next_state: str, target: Sequence[float]):
        self.transition_from = self.transition_source()
        self.transition_to = list(target)
        max_delta = max(
            abs(self.transition_to[i] - self.transition_from[i])
            for i in range(NUM_JOINTS)
        )
        duration = max_delta * self.transition_sec_per_rad
        self.transition_duration = clamp(
            duration,
            self.min_transition_sec,
            self.max_transition_sec,
        )
        self.state = next_state
        self.state_start_time = time.perf_counter()
        self.get_logger().info(
            f"{next_state} transition started: "
            f"{self.transition_duration:.2f}s, max_delta={max_delta:.3f}rad"
        )

    def stand_error_rad(self) -> float:
        source = self.transition_source()
        return max(
            abs(float(source[i]) - float(self.stand_target[i]))
            for i in range(NUM_JOINTS)
        )

    def start_walking(self, now: float):
        self.state = self.WALK
        self.walk_start_time = now
        self.gait_cycle_count = 0
        self.gait_phase = 0.0
        self.get_logger().info("classic trot walking started")

    def walk_target(self, now: float) -> Tuple[List[float], List[float]]:
        elapsed = now - self.walk_start_time
        total_phase = elapsed / self.gait_period_sec
        self.gait_cycle_count = int(math.floor(total_phase))
        self.gait_phase = total_phase - self.gait_cycle_count
        self.output_motion_active = True
        return (
            self.compute_targets(
                self.gait_phase,
                self.filtered_vx,
                self.filtered_vy,
                self.filtered_wz,
            ),
            self.max_delta_walk_rad_per_joint,
        )

    def transition_target(self) -> Tuple[List[float], bool]:
        elapsed = time.perf_counter() - self.state_start_time
        ratio = clamp(elapsed / self.transition_duration, 0.0, 1.0)
        s = smoothstep(ratio)
        target = [
            self.transition_from[i]
            + (self.transition_to[i] - self.transition_from[i]) * s
            for i in range(NUM_JOINTS)
        ]
        return self.clamp_joint_targets(target), ratio >= 1.0

    def update_state_and_target(
        self,
        vx: float,
        vy: float,
        wz: float,
    ) -> Tuple[List[float], float]:
        self.output_motion_active = False
        moving = self.command_is_moving(vx, vy, wz)
        now = time.perf_counter()

        if not self.classic_active:
            self.activation_pending = False
            self.state = self.STAND
            self.update_filtered_command(0.0, 0.0, 0.0)
            return self.transition_source(), self.max_delta_smooth_rad_per_joint

        if self.activation_pending:
            self.activation_pending = False
            self.begin_transition(self.STANDUP, self.stand_target)
            return self.transition_target()[0], self.max_delta_smooth_rad_per_joint

        if self.state == self.STAND:
            self.update_filtered_command(0.0, 0.0, 0.0)
            if moving:
                if self.stand_error_rad() <= self.walk_start_stand_tolerance_rad:
                    self.start_walking(now)
                    self.update_filtered_command(vx, vy, wz)
                    return self.walk_target(now)
                self.begin_transition(self.STANDUP, self.stand_target)
                return self.transition_target()[0], self.max_delta_smooth_rad_per_joint
            return list(self.stand_target), self.max_delta_smooth_rad_per_joint

        if self.state == self.STANDUP:
            self.update_filtered_command(0.0, 0.0, 0.0)
            target, done = self.transition_target()
            if done:
                self.state = self.DWELL
                self.state_start_time = now
                self.get_logger().info("standing dwell started")
            return target, self.max_delta_smooth_rad_per_joint

        if self.state == self.DWELL:
            self.update_filtered_command(0.0, 0.0, 0.0)
            if not moving:
                self.state = self.STAND
                return list(self.stand_target), self.max_delta_smooth_rad_per_joint
            if now - self.state_start_time >= self.stand_dwell_sec:
                self.start_walking(now)
            return list(self.stand_target), self.max_delta_smooth_rad_per_joint

        if self.state == self.WALK:
            self.update_filtered_command(vx, vy, wz)
            filtered_moving = self.command_is_moving(
                self.filtered_vx,
                self.filtered_vy,
                self.filtered_wz,
            )

            if not moving and not filtered_moving:
                self.begin_transition(self.SETTLE, self.stand_target)
                return self.transition_target()[0], self.max_delta_smooth_rad_per_joint

            return self.walk_target(now)

        if self.state == self.SETTLE:
            self.update_filtered_command(0.0, 0.0, 0.0)
            target, done = self.transition_target()
            if done:
                self.state = self.STAND
                self.gait_phase = 0.0
                self.get_logger().info("settled to standing")
            return target, self.max_delta_smooth_rad_per_joint

        self.state = self.STAND
        return list(self.stand_target), self.max_delta_smooth_rad_per_joint

    def publish_target(
        self,
        target: Sequence[float],
        max_delta_rad: float,
    ):
        msg = JointTarget()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = "base_link"
        msg.seq = self.seq
        msg.mode = (
            MODE_CLASSIC_CONTROL if self.output_motion_active else JointTarget.MODE_STAND
        )
        msg.flags = 0
        msg.target_rad = list(target)
        if isinstance(max_delta_rad, (float, int)):
            msg.max_delta_rad = [float(max_delta_rad)] * NUM_JOINTS
        else:
            msg.max_delta_rad = [float(x) for x in max_delta_rad]
        msg.gait_phase = float(self.gait_phase)
        msg.gait_cycle_count = int(self.gait_cycle_count)
        self.target_pub.publish(msg)
        self.seq = (self.seq + 1) & 0xFFFF

    def control_loop(self):
        vx, vy, wz = self.snapshot_command()
        target, max_delta = self.update_state_and_target(vx, vy, wz)
        self.last_target = list(target)

        self.publish_target(target, max_delta)


def main(args=None):
    rclpy.init(args=args)
    node = ClassicControlNode()

    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
