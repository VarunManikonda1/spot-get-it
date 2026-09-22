#!/usr/bin/env python3
import queue
import socket
import struct
import threading
import time

import rclpy
from geometry_msgs.msg import Twist
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy
from std_msgs.msg import String

BRIDGE_IP       = "192.168.0.13"
ROBOT_IDS       = {0}
ROBOT_NAMES     = {0: "spot_01"}
BRIDGE_PORT     = 9000
JETSON_CMD_PORT = 9001

PKT_TYPE_CMD        = 0x04
CMD_TYPE_ESTOP      = 0x01
CMD_TYPE_STOP       = 0x02
CMD_TYPE_MOVE       = 0x03
CMD_TYPE_HEARTBEAT  = 0x04
CMD_TYPE_SET_MODE   = 0x05
CMD_TYPE_MANUAL_MOVE = 0x0D
CMD_TYPE_SET_AUTO    = 0x0E
CMD_TYPE_BODY_ACTION = 0x0F

MANUAL_ACTION_STAND = 1
MANUAL_ACTION_SIT   = 2
MANUAL_ACTION_GREET = 1

PKT_HEADER_STRUCT  = struct.Struct("<BBHHHIIQ")
CMD_PAYLOAD_STRUCT = struct.Struct("<BfffI")
CMD_ACK_STRUCT     = struct.Struct("<IIBBHQ")
PROTO_PKT_MAX      = 24 + 1400


def _now_us() -> int:
    return time.monotonic_ns() // 1000


class CommandReceiverNode(Node):

    def __init__(self):
        super().__init__('command_receiver_node')

        self._mode_pub = self.create_publisher(String, '/control/behavior/mode', 10)

        drive_mode_qos = QoSProfile(
            history=HistoryPolicy.KEEP_LAST,
            depth=1,
            reliability=ReliabilityPolicy.RELIABLE,
            durability=DurabilityPolicy.TRANSIENT_LOCAL,
        )
        self._drive_mode_qos = drive_mode_qos
        self._drive_mode_pubs = {}
        self._mode_pubs = {}
        self._cmd_vel_pubs = {}

        self._queue: queue.Queue = queue.Queue()
        for robot_id in ROBOT_IDS:
            self._queue.put({"robot_id": robot_id, "drive_mode": "AUTO"})

        # ── UDP 소켓 ──
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind(("0.0.0.0", JETSON_CMD_PORT))
        self._sock.settimeout(1.0)
        self.get_logger().info(f"UDP {JETSON_CMD_PORT} 수신 대기 중")

        # ── announce: bridge가 Jetson IP:9001을 기억하도록 ──
        self._announce()

        # ── timer: queue → publish (executor 스레드에서 실행) ──
        self.create_timer(0.05, self._publish_cb)

        # ── UDP 수신 스레드 ──
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._udp_loop, daemon=True)
        self._thread.start()

    def _announce(self) -> None:
        """bind된 소켓으로 CMD_ACK 송신 → bridge가 src=Jetson_IP:9001 기억."""
        for robot_id in ROBOT_IDS:
            ts  = _now_us()
            hdr = PKT_HEADER_STRUCT.pack(
                0x05, robot_id, 0, 1,
                CMD_ACK_STRUCT.size, 0, 0, ts,
            )
            ack = CMD_ACK_STRUCT.pack(0, 0, CMD_TYPE_MOVE, 0, 0, ts)
            try:
                self._sock.sendto(hdr + ack, (BRIDGE_IP, BRIDGE_PORT))
                self.get_logger().info(f"announce 송신: robot_id={robot_id}")
            except OSError as e:
                self.get_logger().error(f"announce 실패: {e}")

    def _udp_loop(self) -> None:
        while not self._stop.is_set():
            try:
                data, _ = self._sock.recvfrom(PROTO_PKT_MAX)
            except socket.timeout:
                continue
            except Exception as e:
                if not self._stop.is_set():
                    self.get_logger().error(f"UDP recv error: {e}")
                continue

            if len(data) < PKT_HEADER_STRUCT.size:
                continue

            header  = self._unpack_header(data[:PKT_HEADER_STRUCT.size])
            payload = data[PKT_HEADER_STRUCT.size:]

            if header["type"] != PKT_TYPE_CMD:
                continue
            if header["robot_id"] not in ROBOT_IDS:
                continue
            if header["payload_len"] != CMD_PAYLOAD_STRUCT.size:
                self.get_logger().warn(
                    "invalid CMD payload_len="
                    f"{header['payload_len']} expected={CMD_PAYLOAD_STRUCT.size}")
                continue
            if len(payload) < CMD_PAYLOAD_STRUCT.size:
                continue

            cmd = self._unpack_cmd(payload[:CMD_PAYLOAD_STRUCT.size])
            cmd_type = cmd["cmd_type"]

            if cmd_type == CMD_TYPE_MOVE:
                self._queue.put({
                    "robot_id": header["robot_id"],
                    "drive_mode": "AUTO",
                    "behavior_mode": "CLASSIC",
                })
            elif cmd_type == CMD_TYPE_MANUAL_MOVE:
                self._queue.put({
                    "robot_id": header["robot_id"],
                    "drive_mode": "MANUAL",
                    "behavior_mode": "CLASSIC",
                    "vx": cmd["vx"],
                    "vy": cmd["vy"],
                    "omega": cmd["omega"],
                })
            elif cmd_type == CMD_TYPE_SET_MODE:
                action = int(round(cmd["vx"]))
                if action == MANUAL_ACTION_STAND:
                    self._queue.put({
                        "robot_id": header["robot_id"],
                        "drive_mode": "MANUAL",
                        "behavior_mode": "CLASSIC",
                        "vx": 0.0,
                        "vy": 0.0,
                        "omega": 0.0,
                    })
                elif action == MANUAL_ACTION_SIT:
                    self._queue.put({
                        "robot_id": header["robot_id"],
                        "drive_mode": "MANUAL",
                        "behavior_mode": "SIT",
                        "vx": 0.0,
                        "vy": 0.0,
                        "omega": 0.0,
                    })
                else:
                    self.get_logger().info(
                        f"미등록 SET_MODE action={action} seq={cmd['seq']}")
            elif cmd_type == CMD_TYPE_BODY_ACTION:
                action = int(round(cmd["vx"]))
                if action == MANUAL_ACTION_GREET:
                    self._queue.put({
                        "robot_id": header["robot_id"],
                        "drive_mode": "MANUAL",
                        "behavior_mode": "DETECT",
                        "vx": 0.0,
                        "vy": 0.0,
                        "omega": 0.0,
                    })
                else:
                    self.get_logger().info(
                        f"미등록 BODY_ACTION action={action} seq={cmd['seq']}")
            elif cmd_type in (CMD_TYPE_STOP, CMD_TYPE_ESTOP):
                self._queue.put({
                    "robot_id": header["robot_id"],
                    "vx": 0.0,
                    "vy": 0.0,
                    "omega": 0.0,
                })
            elif cmd_type == CMD_TYPE_HEARTBEAT:
                pass
            elif cmd_type == CMD_TYPE_SET_AUTO:
                self._queue.put({
                    "robot_id": header["robot_id"],
                    "drive_mode": "AUTO",
                })
            else:
                self.get_logger().info(f"미등록 cmd_type=0x{cmd_type:02x}")

    def _publish_cb(self) -> None:
        """timer callback — executor 스레드에서 queue 소비 후 publish."""
        while not self._queue.empty():
            event = self._queue.get_nowait()
            robot_id = int(event.get("robot_id", 0))
            robot_name = self._robot_name(robot_id)

            drive_mode = event.get("drive_mode")
            if drive_mode:
                msg = String()
                msg.data = drive_mode
                drive_mode_pub = self._drive_mode_pub(robot_name)
                drive_mode_pub.publish(msg)
                self.get_logger().info(
                    f"publish: {drive_mode} → /control/drive_mode/{robot_name}")

            behavior_mode = event.get("behavior_mode")
            if behavior_mode:
                msg = String()
                msg.data = behavior_mode
                self._mode_pub.publish(msg)
                mode_pub = self._mode_pub_for_robot(robot_name)
                mode_pub.publish(msg)
                self.get_logger().info(
                    "publish: "
                    f"{behavior_mode} → /control/behavior/mode, "
                    f"/control/behavior/mode/{robot_name}")

            if {"vx", "vy", "omega"}.issubset(event):
                msg = Twist()
                msg.linear.x = float(event["vx"])
                msg.linear.y = float(event["vy"])
                msg.angular.z = float(event["omega"])
                cmd_vel_pub = self._cmd_vel_pub(robot_name)
                cmd_vel_pub.publish(msg)
                self.get_logger().info(
                    "publish: "
                    f"vx={msg.linear.x:.3f}, "
                    f"vy={msg.linear.y:.3f}, "
                    f"omega={msg.angular.z:.3f} "
                    f"→ /control/cmd_vel/{robot_name}")

    @staticmethod
    def _robot_name(robot_id: int) -> str:
        return ROBOT_NAMES.get(robot_id, f"spot_{robot_id + 1:02d}")

    def _drive_mode_pub(self, robot_name: str):
        if robot_name not in self._drive_mode_pubs:
            self._drive_mode_pubs[robot_name] = self.create_publisher(
                String,
                f'/control/drive_mode/{robot_name}',
                self._drive_mode_qos,
            )
        return self._drive_mode_pubs[robot_name]

    def _mode_pub_for_robot(self, robot_name: str):
        if robot_name not in self._mode_pubs:
            self._mode_pubs[robot_name] = self.create_publisher(
                String,
                f'/control/behavior/mode/{robot_name}',
                10,
            )
        return self._mode_pubs[robot_name]

    def _cmd_vel_pub(self, robot_name: str):
        if robot_name not in self._cmd_vel_pubs:
            self._cmd_vel_pubs[robot_name] = self.create_publisher(
                Twist,
                f'/control/cmd_vel/{robot_name}',
                10,
            )
        return self._cmd_vel_pubs[robot_name]

    @staticmethod
    def _unpack_header(data: bytes) -> dict:
        (pkt_type, robot_id, frag_idx, frag_total,
         payload_len, frame_id, payload_offset, timestamp_us) = \
            PKT_HEADER_STRUCT.unpack(data)
        return {
            "type": pkt_type, "robot_id": robot_id,
            "frag_idx": frag_idx, "frag_total": frag_total,
            "payload_len": payload_len, "frame_id": frame_id,
            "payload_offset": payload_offset, "timestamp_us": timestamp_us,
        }

    @staticmethod
    def _unpack_cmd(data: bytes) -> dict:
        cmd_type, vx, vy, omega, seq = CMD_PAYLOAD_STRUCT.unpack(data)
        return {"cmd_type": cmd_type, "vx": vx, "vy": vy, "omega": omega, "seq": seq}

    def destroy_node(self):
        self._stop.set()
        try:
            self._sock.close()
        except Exception:
            pass
        try:
            super().destroy_node()
        except Exception:
            pass


def main():
    rclpy.init()
    node = CommandReceiverNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        try:
            rclpy.shutdown()
        except Exception:
            pass


if __name__ == '__main__':
    main()
