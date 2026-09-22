#!/usr/bin/env python3
"""
sim_executor_node.py
====================
프로젝트 : SPOT Get IT
작성일   : 2026-05-18
위치     : robot_ws/src/simulation/sim_executor/sim_executor/sim_executor_node.py

단일 노드에서 spot_02 / spot_03 을 각각 독립 스레드로 처리.
노드가 하나이므로 stdin 키보드 입력이 정상 동작.

기능
  1. /planning/global_path/{robot_id} 수신 → 경로 추종
  2. Pose   → PKT_TYPE_ODOM  (0x03)  UDP 직송  (pose_send_hz)
  3. MJPEG  → PKT_TYPE_IMAGE (0x01)  UDP 직송  (video_fps, 루프 재생)
  4. person_detected → PKT_TYPE_EVENT (0x07) UDP 직송  (키보드)

UDP 프로토콜 (proto.h, little-endian)
  PktHeader 24B
    type(1)  robot_id(1)  frag_idx(2)  frag_total(2)
    payload_len(2)  frame_id(4)  payload_offset(4)  timestamp_us(8)
  OdomPayload  24B  — x y theta vx vy omega (모두 float)
  EventPayload 104B — severity(B) reserved(B) event_type(H) code(I) message[96s]
  IMAGE: 1400B 단위 프래그먼트

robot_id 체계 (0-based)
  spot_01 = 0  (Jetson 담당)
  spot_02 = 1
  spot_03 = 2

키보드 (stdin, 줄 단위)
  s2   spot_02 시작 (경로 추종 + 영상)
  s3   spot_03 시작
  v2   spot_02 person_detected ON
  f2   spot_02 person_detected OFF
  v3   spot_03 person_detected ON
  f3   spot_03 person_detected OFF
  q    종료
"""

import math
import socket
import struct
import sys
import threading
import time

import cv2
import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, QoSDurabilityPolicy, QoSReliabilityPolicy

from robot_interfaces.msg import GlobalPathWaypoints

# ─── proto.h 상수 ─────────────────────────────────────────────────────────────
PKT_TYPE_IMAGE = 0x01
PKT_TYPE_ODOM  = 0x03
PKT_TYPE_EVENT = 0x07

BRIDGE_PORT = 9000
PROTO_MTU   = 1400

EVENT_SEVERITY_CRITICAL    = 4
EVENT_TYPE_VICTIM_DETECTED = 8

_HDR_FMT   = '<BBHHHIIQ'
_ODOM_FMT  = '<ffffff'
_EVENT_FMT = '<BBHi96s'

_HDR_SIZE   = struct.calcsize(_HDR_FMT)   # 24
_ODOM_SIZE  = struct.calcsize(_ODOM_FMT)  # 24
_EVENT_SIZE = struct.calcsize(_EVENT_FMT) # 104


def _now_us() -> int:
    return int(time.monotonic() * 1_000_000)


def _hdr(pkt_type: int, robot_id: int, payload_len: int, frame_id: int,
         frag_idx: int = 0, frag_total: int = 1, offset: int = 0) -> bytes:
    return struct.pack(_HDR_FMT,
                       pkt_type, robot_id,
                       frag_idx, frag_total, payload_len,
                       frame_id, offset, _now_us())


def _build_odom(robot_id: int, seq: int,
                x: float, y: float, theta: float) -> bytes:
    return (_hdr(PKT_TYPE_ODOM, robot_id, _ODOM_SIZE, seq)
            + struct.pack(_ODOM_FMT, x, y, theta, 0.0, 0.0, 0.0))


def _build_event(robot_id: int, seq: int, detected: bool) -> bytes:
    msg_b = b'person detected' if detected else b'person cleared'
    return (_hdr(PKT_TYPE_EVENT, robot_id, _EVENT_SIZE, seq)
            + struct.pack(_EVENT_FMT,
                          EVENT_SEVERITY_CRITICAL, 0,
                          EVENT_TYPE_VICTIM_DETECTED,
                          1 if detected else 0,
                          msg_b))


def _build_image_frags(robot_id: int, seq: int, jpeg: bytes) -> list[bytes]:
    frag_total = math.ceil(len(jpeg) / PROTO_MTU)
    pkts = []
    for idx in range(frag_total):
        offset = idx * PROTO_MTU
        chunk  = jpeg[offset: offset + PROTO_MTU]
        pkts.append(_hdr(PKT_TYPE_IMAGE, robot_id, len(chunk),
                         seq, idx, frag_total, offset) + chunk)
    return pkts


# ─── 단일 로봇 워커 ───────────────────────────────────────────────────────────
class RobotWorker:
    """pose 추종 + MJPEG 송출을 독립 스레드로 수행"""

    def __init__(self, ros_id: str, udp_id: int,
                 video_path: str, speed: float,
                 quality: int, max_size: int,
                 pose_hz: float, video_fps: int,
                 sock: socket.socket, dst: tuple,
                 logger):
        self.ros_id  = ros_id
        self.udp_id  = udp_id
        self._sock   = sock
        self._dst    = dst
        self._log    = logger
        self._max_sz = max_size
        self._quality = quality

        # 경로 추종
        self._speed     = speed
        self._wps: list[tuple[float, float]] = []
        self._seg    = 0
        self._prog   = 0.0
        self._cur_x  = 0.0
        self._cur_y  = 0.0
        self._cur_yaw = 0.0
        self._wps_lock = threading.Lock()

        # 시퀀스
        self._odom_seq  = 0
        self._event_seq = 0
        self._img_seq   = 0

        # 영상
        self._cap = cv2.VideoCapture(video_path)
        if not self._cap.isOpened():
            self._log.warn(f'[{ros_id}] 영상 열기 실패: {video_path}')

        # 스레드 제어
        self._running  = False
        self._stop_evt = threading.Event()

        # 주기
        self._pose_dt  = 1.0 / pose_hz
        self._video_dt = 1.0 / video_fps
        self._odom_min_us = int(1_000_000 / pose_hz)
        self._last_odom   = 0

        # FPS 모니터링
        self._fps_cnt = 0
        self._fps_t   = time.monotonic()

    # ── 외부 제어 ─────────────────────────────────────────────────────────────
    def set_path(self, waypoints_msg: list) -> None:
        with self._wps_lock:
            self._wps  = [(wp.x_m, wp.y_m) for wp in waypoints_msg]
            self._seg  = 0
            self._prog = 0.0
            if self._wps:
                self._cur_x, self._cur_y = self._wps[0]
        self._log.info(f'[{self.ros_id}] 경로 수신  waypoints={len(self._wps)}')

    def start(self) -> None:
        if self._running:
            return
        self._running = True
        self._t_last  = time.monotonic()
        threading.Thread(target=self._pose_loop,  daemon=True,
                         name=f'{self.ros_id}_pose').start()
        threading.Thread(target=self._video_loop, daemon=True,
                         name=f'{self.ros_id}_video').start()
        self._log.info(f'[{self.ros_id}] ▶ 시작')
        print(f'[{self.ros_id}] ▶ 시작')

    def send_event(self, detected: bool) -> None:
        pkt = _build_event(self.udp_id, self._event_seq, detected)
        self._event_seq += 1
        self._send(pkt)
        label = 'ON  ✓' if detected else 'OFF ✗'
        self._log.info(f'[EVENT/{self.ros_id}] {label}')
        print(f'[EVENT/{self.ros_id}] {label}')

    def stop(self) -> None:
        self._stop_evt.set()

    def release(self) -> None:
        self._cap.release()

    # ── Pose 스레드 ───────────────────────────────────────────────────────────
    def _pose_loop(self) -> None:
        while not self._stop_evt.is_set():
            t0 = time.monotonic()

            now  = t0
            dt   = now - self._t_last
            self._t_last = now

            with self._wps_lock:
                x, y, yaw = self._advance(dt)

            now_us = _now_us()
            if now_us - self._last_odom >= self._odom_min_us:
                self._last_odom = now_us
                self._send(_build_odom(self.udp_id, self._odom_seq, x, y, yaw))
                self._odom_seq += 1

            elapsed = time.monotonic() - t0
            time.sleep(max(0.0, self._pose_dt - elapsed))

    def _advance(self, dt: float) -> tuple[float, float, float]:
        wps = self._wps
        n   = len(wps)
        if n < 2:
            return self._cur_x, self._cur_y, self._cur_yaw
        if self._seg >= n - 1:
            self._cur_x, self._cur_y = wps[-1]
            return self._cur_x, self._cur_y, self._cur_yaw

        p0 = wps[self._seg]
        p1 = wps[self._seg + 1]
        dx      = p1[0] - p0[0]
        dy      = p1[1] - p0[1]
        seg_len = math.hypot(dx, dy) or 1e-6

        self._prog += self._speed * dt
        while self._prog >= seg_len and self._seg < n - 2:
            self._prog -= seg_len
            self._seg  += 1
            p0 = wps[self._seg]
            p1 = wps[self._seg + 1]
            dx      = p1[0] - p0[0]
            dy      = p1[1] - p0[1]
            seg_len = math.hypot(dx, dy) or 1e-6

        ratio         = min(self._prog / seg_len, 1.0)
        self._cur_x   = p0[0] + dx * ratio
        self._cur_y   = p0[1] + dy * ratio
        self._cur_yaw = math.atan2(dy, dx)
        return self._cur_x, self._cur_y, self._cur_yaw

    # ── MJPEG 스레드 ──────────────────────────────────────────────────────────
    def _video_loop(self) -> None:
        while not self._stop_evt.is_set():
            t0 = time.monotonic()

            if self._cap.isOpened():
                ret, frame = self._cap.read()
                if not ret:
                    self._cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
                    ret, frame = self._cap.read()

                if ret:
                    ok, buf = cv2.imencode(
                        '.jpg', frame,
                        [cv2.IMWRITE_JPEG_QUALITY, self._quality],
                    )
                    if ok:
                        jpeg = buf.tobytes()
                        if len(jpeg) <= self._max_sz:
                            for pkt in _build_image_frags(
                                self.udp_id, self._img_seq, jpeg
                            ):
                                self._send(pkt)
                            self._img_seq = (self._img_seq + 1) & 0xFFFF_FFFF
                            self._fps_cnt += 1
                        else:
                            self._log.warn(
                                f'[{self.ros_id}] 프레임 너무 큼: {len(jpeg)}B — skip'
                            )

            # FPS 로그 (1초마다)
            now = time.monotonic()
            if now - self._fps_t >= 1.0:
                self._log.info(f'[MJPEG/{self.ros_id}] {self._fps_cnt}fps')
                self._fps_cnt = 0
                self._fps_t   = now

            elapsed = time.monotonic() - t0
            time.sleep(max(0.0, self._video_dt - elapsed))

    # ── 공용 송신 ─────────────────────────────────────────────────────────────
    def _send(self, data: bytes) -> None:
        try:
            self._sock.sendto(data, self._dst)
        except OSError as e:
            self._log.error(f'[{self.ros_id}] UDP 오류: {e}')


# ─── 메인 노드 ────────────────────────────────────────────────────────────────
class SimExecutorNode(Node):

    _KEY_HELP = (
        '  s2   spot_02 시작\n'
        '  s3   spot_03 시작\n'
        '  v2   spot_02 탐지 ON\n'
        '  f2   spot_02 탐지 OFF\n'
        '  v3   spot_03 탐지 ON\n'
        '  f3   spot_03 탐지 OFF\n'
        '  q    종료\n'
    )

    def __init__(self):
        super().__init__('sim_executor_node')

        # ── 파라미터 ──────────────────────────────────────────────────────────
        self.declare_parameter('rpi5_ip',          'REQUIRED')
        self.declare_parameter('bridge_port',      BRIDGE_PORT)
        self.declare_parameter('video_path_02',    'REQUIRED')
        self.declare_parameter('video_path_03',    'REQUIRED')
        self.declare_parameter('udp_robot_id_02',  1)
        self.declare_parameter('udp_robot_id_03',  2)
        self.declare_parameter('robot_speed_mps',  0.4)
        self.declare_parameter('pose_send_hz',     10.0)
        self.declare_parameter('video_fps',        15)
        self.declare_parameter('jpeg_quality',     80)
        self.declare_parameter('max_size_bytes',   200_000)

        rpi5_ip   = self.get_parameter('rpi5_ip').value
        port      = int(self.get_parameter('bridge_port').value)
        video_02  = self.get_parameter('video_path_02').value
        video_03  = self.get_parameter('video_path_03').value
        udp_id_02 = int(self.get_parameter('udp_robot_id_02').value)
        udp_id_03 = int(self.get_parameter('udp_robot_id_03').value)
        speed     = float(self.get_parameter('robot_speed_mps').value)
        pose_hz   = float(self.get_parameter('pose_send_hz').value)
        video_fps = int(self.get_parameter('video_fps').value)
        quality   = int(self.get_parameter('jpeg_quality').value)
        max_size  = int(self.get_parameter('max_size_bytes').value)

        for label, val in [
            ('rpi5_ip',       rpi5_ip),
            ('video_path_02', video_02),
            ('video_path_03', video_03),
        ]:
            if val == 'REQUIRED':
                self.get_logger().fatal(f'파라미터 {label} 미설정')
                raise RuntimeError(f'{label} 파라미터 필수')

        # ── UDP 소켓 (두 워커가 공유) ─────────────────────────────────────────
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        dst        = (rpi5_ip, port)

        # ── RobotWorker ───────────────────────────────────────────────────────
        common = dict(speed=speed, quality=quality, max_size=max_size,
                      pose_hz=pose_hz, video_fps=video_fps,
                      sock=self._sock, dst=dst, logger=self.get_logger())

        self._workers: dict[str, RobotWorker] = {
            'spot_02': RobotWorker('spot_02', udp_id_02, video_02, **common),
            'spot_03': RobotWorker('spot_03', udp_id_03, video_03, **common),
        }

        # ── QoS ───────────────────────────────────────────────────────────────
        transient_qos = QoSProfile(
            depth=1,
            reliability=QoSReliabilityPolicy.RELIABLE,
            durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
        )

        # ── Subscriber ────────────────────────────────────────────────────────
        for rid in self._workers:
            self.create_subscription(
                GlobalPathWaypoints,
                f'/planning/global_path/{rid}',
                lambda msg, r=rid: self._path_cb(msg, r),
                transient_qos,
            )

        self.get_logger().info(
            f'sim_executor_node 시작\n'
            f'  UDP    : {rpi5_ip}:{port}\n'
            f'  spot_02: udp_id={udp_id_02}  {video_02}\n'
            f'  spot_03: udp_id={udp_id_03}  {video_03}\n'
            f'  속도={speed}m/s  pose={pose_hz}Hz  video={video_fps}fps\n'
            f'키보드:\n{self._KEY_HELP}'
        )

    # ── 콜백: global_path ─────────────────────────────────────────────────────
    def _path_cb(self, msg: GlobalPathWaypoints, robot_id: str) -> None:
        w = self._workers.get(robot_id)
        if w:
            w.set_path(msg.waypoints)

    # ── 키보드 ───────────────────────────────────────────────────────────────
    def handle_key(self, key: str) -> bool:
        """키 처리. q 입력 시 True 반환 (종료 신호)."""
        if   key == 's2': self._workers['spot_02'].start()
        elif key == 's3': self._workers['spot_03'].start()
        elif key == 'v2': self._workers['spot_02'].send_event(True)
        elif key == 'f2': self._workers['spot_02'].send_event(False)
        elif key == 'v3': self._workers['spot_03'].send_event(True)
        elif key == 'f3': self._workers['spot_03'].send_event(False)
        elif key == 'q':  return True
        else: print(f'알 수 없는 입력: "{key}"\n{self._KEY_HELP}')
        return False

    # ── 종료 ─────────────────────────────────────────────────────────────────
    def destroy_node(self):
        for w in self._workers.values():
            w.stop()
            w.release()
        self._sock.close()
        super().destroy_node()


# ─── 진입점 ───────────────────────────────────────────────────────────────────
def main(args=None):
    import select

    rclpy.init(args=args)
    try:
        node = SimExecutorNode()
    except RuntimeError:
        rclpy.shutdown()
        return

    executor = rclpy.executors.SingleThreadedExecutor()
    executor.add_node(node)

    print(f'키보드:\n{SimExecutorNode._KEY_HELP}')

    try:
        while rclpy.ok():
            # ROS2 콜백 처리 (0.05초 타임아웃)
            executor.spin_once(timeout_sec=0.05)

            # stdin 입력 확인 (블로킹 없이)
            r, _, _ = select.select([sys.stdin], [], [], 0)
            if r:
                line = sys.stdin.readline()
                if not line:          # EOF
                    break
                if node.handle_key(line.strip().lower()):
                    break

    except KeyboardInterrupt:
        pass
    finally:
        if rclpy.ok():
            node.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    main()
