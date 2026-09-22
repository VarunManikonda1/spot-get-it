#!/usr/bin/env python3

from enum import Enum
from typing import Optional

import rclpy
from geometry_msgs.msg import Twist
from rclpy.node import Node
from rclpy.qos import HistoryPolicy, QoSProfile, ReliabilityPolicy
from rclpy.time import Time
from std_msgs.msg import String

from robot_interfaces.msg import JointTarget


class TestState(Enum):
    COMMANDING = "COMMANDING"
    STOPPING = "STOPPING"
    DONE = "DONE"


class ClassicOneCycleTestNode(Node):
    """Publish a bounded CLASSIC walk command and stop after a phase budget."""

    def __init__(self):
        super().__init__("classic_one_cycle_test_node")

        self.declare_parameter("cmd_vel_topic", "/control/cmd_vel/spot_01")
        self.declare_parameter("mode_topic", "/control/behavior/mode")
        self.declare_parameter(
            "target_topic",
            "/control/classic_control/joint_target",
        )
        self.declare_parameter("vx_mps", 0.05)
        self.declare_parameter("vy_mps", 0.0)
        self.declare_parameter("wz_radps", 0.0)
        self.declare_parameter("cycles", 1.0)
        self.declare_parameter("publish_rate_hz", 20.0)
        self.declare_parameter("walk_start_timeout_sec", 8.0)
        self.declare_parameter("stop_publish_sec", 3.5)
        self.declare_parameter("switch_to_stand", True)

        self.cmd_vel_topic = str(self.get_parameter("cmd_vel_topic").value)
        self.mode_topic = str(self.get_parameter("mode_topic").value)
        self.target_topic = str(self.get_parameter("target_topic").value)
        self.vx_mps = float(self.get_parameter("vx_mps").value)
        self.vy_mps = float(self.get_parameter("vy_mps").value)
        self.wz_radps = float(self.get_parameter("wz_radps").value)
        self.cycles = float(self.get_parameter("cycles").value)
        self.publish_rate_hz = float(
            self.get_parameter("publish_rate_hz").value
        )
        self.walk_start_timeout_sec = float(
            self.get_parameter("walk_start_timeout_sec").value
        )
        self.stop_publish_sec = float(
            self.get_parameter("stop_publish_sec").value
        )
        self.switch_to_stand = bool(
            self.get_parameter("switch_to_stand").value
        )

        if self.publish_rate_hz <= 0.0:
            raise RuntimeError("publish_rate_hz must be positive")
        if self.cycles <= 0.0:
            raise RuntimeError("cycles must be positive")
        if self.walk_start_timeout_sec <= 0.0:
            raise RuntimeError("walk_start_timeout_sec must be positive")
        if self.stop_publish_sec < 0.0:
            raise RuntimeError("stop_publish_sec must be non-negative")
        command_is_zero = (
            abs(self.vx_mps) < 1.0e-6
            and abs(self.vy_mps) < 1.0e-6
            and abs(self.wz_radps) < 1.0e-6
        )
        if command_is_zero:
            raise RuntimeError(
                "at least one of vx_mps, vy_mps, wz_radps must be non-zero"
            )

        self.state = TestState.COMMANDING
        self.finished = False
        self.walk_started = False
        self.start_total_phase: Optional[float] = None
        self.goal_total_phase: Optional[float] = None
        self.stop_start_time: Optional[Time] = None
        self.last_total_phase: Optional[float] = None
        self.start_time = self.get_clock().now()

        self.command_qos = QoSProfile(
            history=HistoryPolicy.KEEP_LAST,
            depth=1,
            reliability=ReliabilityPolicy.RELIABLE,
        )
        self.target_qos = QoSProfile(
            history=HistoryPolicy.KEEP_LAST,
            depth=1,
            reliability=ReliabilityPolicy.BEST_EFFORT,
        )

        self.cmd_pub = self.create_publisher(
            Twist,
            self.cmd_vel_topic,
            self.command_qos,
        )
        self.mode_pub = self.create_publisher(String, self.mode_topic, 10)
        self.target_sub = self.create_subscription(
            JointTarget,
            self.target_topic,
            self.target_callback,
            self.target_qos,
        )
        self.timer = self.create_timer(
            1.0 / self.publish_rate_hz,
            self.timer_callback,
        )

        self.get_logger().info(
            "classic one-cycle test started: "
            f"cmd={self.cmd_vel_topic}, mode={self.mode_topic}, "
            f"target={self.target_topic}, "
            f"vx={self.vx_mps:.3f}, vy={self.vy_mps:.3f}, "
            f"wz={self.wz_radps:.3f}, "
            f"cycles={self.cycles:.2f}"
        )

    def target_callback(self, msg: JointTarget):
        total_phase = float(msg.gait_cycle_count) + float(msg.gait_phase)
        previous_total_phase = self.last_total_phase
        self.last_total_phase = total_phase

        if self.state != TestState.COMMANDING:
            return

        if not self.walk_started:
            if self.start_total_phase is None:
                self.start_total_phase = total_phase
                return

            if (
                previous_total_phase is not None
                and total_phase + 1.0e-3 < previous_total_phase
            ):
                self.start_total_phase = total_phase
                self.get_logger().info(
                    "walk phase reset detected: "
                    f"start_total_phase={self.start_total_phase:.3f}"
                )
                return

            elapsed_phase = total_phase - self.start_total_phase
            if elapsed_phase <= 1.0e-3:
                return

            self.walk_started = True
            self.goal_total_phase = self.start_total_phase + self.cycles
            self.get_logger().info(
                "walk phase detected: "
                f"start_total_phase={self.start_total_phase:.3f}, "
                f"current_total_phase={total_phase:.3f}, "
                f"goal_total_phase={self.goal_total_phase:.3f}"
            )
            return

        if (
            self.goal_total_phase is not None
            and total_phase >= self.goal_total_phase
        ):
            self.get_logger().info(
                "requested phase cycles reached: "
                f"current_total_phase={total_phase:.3f}, "
                f"goal_total_phase={self.goal_total_phase:.3f}"
            )
            self.begin_stop()

    def timer_callback(self):
        if self.state == TestState.COMMANDING:
            self.publish_mode("CLASSIC")
            self.publish_twist(self.vx_mps, self.vy_mps, self.wz_radps)
            self.check_start_timeout()
            return

        if self.state == TestState.STOPPING:
            self.publish_twist(0.0, 0.0, 0.0)
            if self.stop_start_time is None:
                return
            elapsed = (
                self.get_clock().now() - self.stop_start_time
            ).nanoseconds * 1.0e-9
            if elapsed >= self.stop_publish_sec:
                if self.switch_to_stand:
                    self.publish_mode("STAND")
                self.state = TestState.DONE
                self.finished = True
                self.get_logger().info("classic one-cycle test finished")

    def check_start_timeout(self):
        if self.walk_started:
            return

        elapsed = (
            self.get_clock().now() - self.start_time
        ).nanoseconds * 1.0e-9
        if elapsed <= self.walk_start_timeout_sec:
            return

        last_phase = (
            "none"
            if self.last_total_phase is None
            else f"{self.last_total_phase:.3f}"
        )
        self.get_logger().error(
            "timed out waiting for classic walk phase to advance: "
            f"timeout={self.walk_start_timeout_sec:.1f}s, "
            f"last_total_phase={last_phase}"
        )
        self.begin_stop()

    def begin_stop(self):
        if self.state != TestState.COMMANDING:
            return
        self.state = TestState.STOPPING
        self.stop_start_time = self.get_clock().now()
        self.publish_twist(0.0, 0.0, 0.0)

    def publish_mode(self, mode: str):
        msg = String()
        msg.data = mode
        self.mode_pub.publish(msg)

    def publish_twist(self, vx: float, vy: float, wz: float):
        msg = Twist()
        msg.linear.x = float(vx)
        msg.linear.y = float(vy)
        msg.angular.z = float(wz)
        self.cmd_pub.publish(msg)


def main(args=None):
    rclpy.init(args=args)
    node = ClassicOneCycleTestNode()

    try:
        while rclpy.ok() and not node.finished:
            rclpy.spin_once(node, timeout_sec=0.1)
    except KeyboardInterrupt:
        node.publish_twist(0.0, 0.0, 0.0)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
