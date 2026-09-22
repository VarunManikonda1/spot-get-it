#!/usr/bin/env python3

import csv
import os
import time
from typing import Optional

import rclpy
from rclpy.node import Node
from rclpy.qos import HistoryPolicy, QoSProfile, ReliabilityPolicy

from robot_interfaces.msg import JointFeedback, JointTarget


NUM_JOINTS = 12


class KpKdResponseRecorder(Node):
    def __init__(self):
        super().__init__("kp_kd_response_recorder")

        self.declare_parameter("target_topic", "/control/selected/joint_target")
        self.declare_parameter("feedback_topic", "/control/actuator/joint_feedback")
        self.declare_parameter("output_csv", "/tmp/spot_kp_kd_response.csv")
        self.declare_parameter("flush_every_rows", 50)

        self.target_topic = str(self.get_parameter("target_topic").value)
        self.feedback_topic = str(self.get_parameter("feedback_topic").value)
        self.output_csv = os.path.expanduser(str(self.get_parameter("output_csv").value))
        self.flush_every_rows = int(self.get_parameter("flush_every_rows").value)

        os.makedirs(os.path.dirname(os.path.abspath(self.output_csv)), exist_ok=True)
        self.file = open(self.output_csv, "w", newline="", encoding="utf-8")
        self.writer = csv.writer(self.file)
        self.writer.writerow(self._header())

        qos = QoSProfile(
            history=HistoryPolicy.KEEP_LAST,
            depth=1,
            reliability=ReliabilityPolicy.BEST_EFFORT,
        )
        self.target_sub = self.create_subscription(
            JointTarget,
            self.target_topic,
            self.target_callback,
            qos,
        )
        self.feedback_sub = self.create_subscription(
            JointFeedback,
            self.feedback_topic,
            self.feedback_callback,
            qos,
        )

        self.latest_target: Optional[JointTarget] = None
        self.latest_target_time: Optional[float] = None
        self.start_time = time.perf_counter()
        self.rows = 0

        self.get_logger().info(
            "recording target/feedback response: "
            f"target={self.target_topic}, feedback={self.feedback_topic}, "
            f"output={self.output_csv}"
        )

    def _header(self):
        header = [
            "t_sec",
            "target_age_sec",
            "target_seq",
            "target_mode",
            "gait_phase",
            "gait_cycle_count",
        ]
        header += [f"target_{i}" for i in range(NUM_JOINTS)]
        header += [f"position_{i}" for i in range(NUM_JOINTS)]
        header += [f"velocity_{i}" for i in range(NUM_JOINTS)]
        header += [f"error_{i}" for i in range(NUM_JOINTS)]
        header += [f"max_delta_{i}" for i in range(NUM_JOINTS)]
        return header

    def target_callback(self, msg: JointTarget):
        if len(msg.target_rad) != NUM_JOINTS:
            return
        self.latest_target = msg
        self.latest_target_time = time.perf_counter()

    def feedback_callback(self, msg: JointFeedback):
        if self.latest_target is None or self.latest_target_time is None:
            return
        if len(msg.position_rad) != NUM_JOINTS or len(msg.velocity_rad_s) != NUM_JOINTS:
            return

        now = time.perf_counter()
        target = [float(v) for v in self.latest_target.target_rad]
        position = [float(v) for v in msg.position_rad]
        velocity = [float(v) for v in msg.velocity_rad_s]
        max_delta = list(self.latest_target.max_delta_rad)
        if len(max_delta) != NUM_JOINTS:
            max_delta = [0.0] * NUM_JOINTS

        error = [target[i] - position[i] for i in range(NUM_JOINTS)]
        row = [
            now - self.start_time,
            now - self.latest_target_time,
            int(self.latest_target.seq),
            int(self.latest_target.mode),
            float(self.latest_target.gait_phase),
            int(self.latest_target.gait_cycle_count),
        ]
        row += target
        row += position
        row += velocity
        row += error
        row += [float(v) for v in max_delta]
        self.writer.writerow(row)

        self.rows += 1
        if self.flush_every_rows > 0 and self.rows % self.flush_every_rows == 0:
            self.file.flush()

    def destroy_node(self):
        try:
            self.file.flush()
            self.file.close()
        finally:
            super().destroy_node()


def main(args=None):
    rclpy.init(args=args)
    node = KpKdResponseRecorder()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
