#!/usr/bin/env python3
"""
sim_executor_script.py
======================
프로젝트 : SPOT Get IT
작성일   : 2026-05-18

ros2 launch 없이 python3로 직접 실행하는 독립 스크립트.
stdin이 정상 동작하므로 키보드 입력 가능.

실행
  source ~/robot_ws/install/setup.bash
  python3 sim_executor_script.py

흐름
  1. /planning/global_path/spot_02|03|04|05 수신 대기
  2. UDP 9001 수신 대기 (CMD_TYPE_START_MISSION → start, PAUSE/STOP → pause)
     robot_id 기준으로 해당 워커에 디스패치
  3. spot_05 중단 및 종료는 stdin으로 제어

키보드 (Enter 후 처리) — stdin 보조 제어
  s0   spot_02/03/04/05 동시 시작
  p0   전체 중단
  s2~s5 / p2~p5   개별 시작 / 중단
  v2~v5 / f2~f5   탐지 ON / OFF
  q    종료
"""

import math
import select
import socket
import struct
import sys
import argparse
import threading
import time

import cv2
import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, QoSDurabilityPolicy, QoSReliabilityPolicy

from robot_interfaces.msg import GlobalPathWaypoints

# ─── 설정 (필요 시 수정) ──────────────────────────────────────────────────────
RPI5_IP        = '192.168.0.13'
BRIDGE_PORT    = 9000
# Fleet configuration: one real robot and dummy senders
REAL_ROBOT = 'spot_01'                 # real robot (not simulated here)
DUMMY_ROBOTS = ['spot_02', 'spot_03', 'spot_04', 'spot_05']

# Video paths per dummy robot (fallback)
VIDEO_PATH_DEFAULT = '/home/ubuntu/media/spot_02.mp4'
VIDEO_PATHS = {
    'spot_02': '/home/ubuntu/media/spot_02.mp4',
    'spot_03': '/home/ubuntu/media/spot_03.mp4',
    'spot_04': '/home/ubuntu/media/spot_04.mp4',
    'spot_05': '/home/ubuntu/media/spot_05.mp4',
}
ROBOT_SPEED    = 0.1    # m/s
POSE_HZ        = 10.0
VIDEO_FPS      = 15
JPEG_QUALITY   = 80
MAX_FRAME_SIZE = 200_000

# ─── proto.h 상수 ─────────────────────────────────────────────────────────────
PKT_TYPE_IMAGE = 0x01
PKT_TYPE_ODOM  = 0x03
PKT_TYPE_CMD   = 0x04
PKT_TYPE_EVENT = 0x07
PKT_TYPE_PATH_PROGRESS = 0x0C
PROTO_MTU      = 1400

JETSON_CMD_PORT         = 9001
CMD_TYPE_STOP           = 0x02
CMD_TYPE_MOVE           = 0x03
CMD_TYPE_HEARTBEAT      = 0x04
CMD_TYPE_START_MISSION  = 0x09
CMD_TYPE_PAUSE_MISSION  = 0x0A

EVENT_SEVERITY_CRITICAL    = 4
EVENT_TYPE_VICTIM_DETECTED = 8

_HDR_FMT   = '<BBHHHIIQ'
_ODOM_FMT  = '<fffffff'
_EVENT_FMT = '<BBHi96s'
_CMD_FMT   = '<BfffI'      # CmdPayload: cmd_type, vx, vy, omega, seq
_HDR_SIZE   = struct.calcsize(_HDR_FMT)
_ODOM_SIZE  = struct.calcsize(_ODOM_FMT)
_EVENT_SIZE = struct.calcsize(_EVENT_FMT)
_CMD_SIZE   = struct.calcsize(_CMD_FMT)

# PathProgress payload format (matches PathProgressPayload in proto.h)
_PATHPROG_FMT  = '<16sQBBBBIIfIfffffffff'
_PATHPROG_SIZE = struct.calcsize(_PATHPROG_FMT)


# robot_id (0-based) -> UDP id mapping
# Reserve 0 for the real robot, simulated dummies start from 1
UDP_ROBOT_ID = {REAL_ROBOT: 0}
for i, rid in enumerate(DUMMY_ROBOTS):
    UDP_ROBOT_ID[rid] = i + 1


def _now_us() -> int:
    return int(time.monotonic() * 1_000_000)


def _hdr(pkt_type, robot_id, payload_len, frame_id,
         frag_idx=0, frag_total=1, offset=0) -> bytes:
    return struct.pack(_HDR_FMT, pkt_type, robot_id,
                       frag_idx, frag_total, payload_len,
                       frame_id, offset, _now_us())


def _build_odom(robot_id, seq, x, y, theta) -> bytes:
    return (_hdr(PKT_TYPE_ODOM, robot_id, _ODOM_SIZE, seq)
            + struct.pack(_ODOM_FMT, x, y, theta, 0.0, 0.0, 0.0, 0.0))


def _build_event(robot_id, seq, detected) -> bytes:
    msg_b = b'person detected' if detected else b'person cleared'
    return (_hdr(PKT_TYPE_EVENT, robot_id, _EVENT_SIZE, seq)
            + struct.pack(_EVENT_FMT,
                          EVENT_SEVERITY_CRITICAL, 0,
                          EVENT_TYPE_VICTIM_DETECTED,
                          1 if detected else 0,
                          msg_b))


def _build_image_frags(robot_id, seq, jpeg) -> list:
    frag_total = math.ceil(len(jpeg) / PROTO_MTU)
    pkts = []
    for idx in range(frag_total):
        offset = idx * PROTO_MTU
        chunk  = jpeg[offset: offset + PROTO_MTU]
        pkts.append(_hdr(PKT_TYPE_IMAGE, robot_id, len(chunk),
                         seq, idx, frag_total, offset) + chunk)
    return pkts


# ─── 로봇 워커 ────────────────────────────────────────────────────────────────
class RobotWorker:

    def __init__(self, ros_id: str, sock: socket.socket, dst: tuple):
        self.ros_id  = ros_id
        self.udp_id  = UDP_ROBOT_ID[ros_id]
        self._sock   = sock
        self._dst    = dst

        # 경로
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
        self._last_odom = 0
        self._progress_seq = 0

        # 영상
        video_path = VIDEO_PATHS.get(ros_id, VIDEO_PATH_DEFAULT)
        self._cap = cv2.VideoCapture(video_path)
        if not self._cap.isOpened():
            print(f'[{ros_id}] ⚠ 영상 열기 실패')

        self._move_stop_evt  = threading.Event()
        self._odom_stop_evt  = threading.Event()
        self._stop_evt       = threading.Event()
        self._running        = False
        self._odom_running   = False
        self._fps_cnt        = 0
        self._fps_t          = time.monotonic()
        self._move_thread    = None
        self._video_thread   = None
        self._odom_thread    = None
        self._hb_stop        = threading.Event()
        self._hb_thread      = None

    def set_path(self, waypoints_msg: list) -> None:
        with self._wps_lock:
            self._wps  = [(wp.x_m, wp.y_m) for wp in waypoints_msg]
            self._seg  = 0
            self._prog = 0.0
            if self._wps:
                self._cur_x, self._cur_y = self._wps[0]
                if len(self._wps) > 1:
                    next_x, next_y = self._wps[1]
                    self._cur_yaw = math.atan2(next_y - self._cur_y, next_x - self._cur_x)
        print(f'[{self.ros_id}] 경로 수신 waypoints={len(self._wps)}')
        if not self._running:
            self._start_heartbeat()

    def _start_heartbeat(self) -> None:
        """정지 상태에서 현재 위치 odom 주기 송신 (연결 유지용)."""
        if self._hb_thread and self._hb_thread.is_alive():
            return
        self._hb_stop.clear()
        self._hb_thread = threading.Thread(
            target=self._heartbeat_loop, name=f'hb-{self.ros_id}', daemon=True
        )
        self._hb_thread.start()

    def _heartbeat_loop(self) -> None:
        interval = 1.0 / POSE_HZ
        while not self._hb_stop.is_set():
            try:
                self._send(_build_odom(self.udp_id, self._odom_seq,
                                       self._cur_x, self._cur_y, self._cur_yaw))
                self._odom_seq += 1
            except Exception:
                pass
            self._hb_stop.wait(interval)

    def send_path_progress(self) -> None:
        with self._wps_lock:
            wps = list(self._wps)
            seg = self._seg
            prog = self._prog
            cur_x = self._cur_x
            cur_y = self._cur_y

        n = len(wps)
        path_ok = 1 if n > 0 else 0
        pose_ok = 1
        goal_reached = 0
        waypoint_idx = 0
        total_waypoints = n
        mission_progress = 0.0
        nearest_index = 0
        distance_to_nearest_m = 0.0
        nearest_x = 0.0
        nearest_y = 0.0
        target_x = 0.0
        target_y = 0.0
        target_heading = 0.0
        heading_error = 0.0
        distance_to_target = 0.0
        distance_to_goal = 0.0

        if n > 0:
            best_d = float('inf')
            for i, (wx, wy) in enumerate(wps):
                d = math.hypot(cur_x - wx, cur_y - wy)
                if d < best_d:
                    best_d = d
                    nearest_index = i
                    nearest_x = wx
                    nearest_y = wy
            distance_to_nearest_m = float(best_d)

            tgt = min(seg + 1, n - 1)
            target_x, target_y = wps[tgt]
            waypoint_idx = tgt

            if n > 1:
                total_path_len = 0.0
                for i in range(n - 1):
                    a = wps[i]; b = wps[i + 1]
                    total_path_len += math.hypot(b[0] - a[0], b[1] - a[1])
                traveled = 0.0
                for i in range(seg):
                    a = wps[i]; b = wps[i + 1]
                    traveled += math.hypot(b[0] - a[0], b[1] - a[1])
                if seg < n - 1:
                    a = wps[seg]; b = wps[seg + 1]
                    seg_len = math.hypot(b[0] - a[0], b[1] - a[1]) or 1e-6
                    traveled += min(prog, seg_len)
                mission_progress = float(min(max(traveled / (total_path_len or 1.0), 0.0), 1.0))

            distance_to_target = float(math.hypot(cur_x - target_x, cur_y - target_y))
            distance_to_goal   = float(math.hypot(cur_x - wps[-1][0], cur_y - wps[-1][1]))
            goal_reached = 1 if (seg >= n - 1 and distance_to_goal < 0.5) else 0

        ts = _now_us()
        payload = struct.pack(
            _PATHPROG_FMT,
            self.ros_id.encode('utf-8')[:16].ljust(16, b'\x00'),
            ts,
            path_ok, pose_ok, goal_reached, 0,
            waypoint_idx, total_waypoints, mission_progress,
            nearest_index, distance_to_nearest_m,
            nearest_x, nearest_y,
            target_x, target_y, target_heading, heading_error,
            distance_to_target, distance_to_goal,
        )
        hdr = _hdr(PKT_TYPE_PATH_PROGRESS, self.udp_id, len(payload), self._progress_seq)
        self._send(hdr + payload)
        self._progress_seq = (self._progress_seq + 1) & 0xFFFFFFFF

    def send_event(self, detected: bool) -> None:
        self._send(_build_event(self.udp_id, self._event_seq, detected))
        self._event_seq += 1
        label = 'ON  ✓' if detected else 'OFF ✗'
        print(f'[EVENT/{self.ros_id}] {label}')

    def reset(self) -> None:
        """경로 인덱스와 위치를 출발지로 초기화하고 즉시 odom 송신."""
        with self._wps_lock:
            self._seg  = 0
            self._prog = 0.0
            if self._wps:
                self._cur_x, self._cur_y = self._wps[0]
                if len(self._wps) > 1:
                    nx, ny = self._wps[1]
                    self._cur_yaw = math.atan2(ny - self._cur_y, nx - self._cur_x)

        try:
            self._send(_build_odom(self.udp_id, self._odom_seq,
                                   self._cur_x, self._cur_y, self._cur_yaw))
            self._odom_seq += 1
        except Exception:
            pass

    def pause(self) -> None:
        """송신 스레드 전체 정지 (고장 표현). cap 유지 → start()로 재개 가능."""
        # heartbeat도 중단
        self._hb_stop.set()
        if self._hb_thread and self._hb_thread.is_alive():
            self._hb_thread.join(timeout=0.5)
        if not self._running:
            return
        self._stop_evt.set()
        if self._pose_thread and self._pose_thread.is_alive():
            self._pose_thread.join(timeout=1.0)
        if self._video_thread and self._video_thread.is_alive():
            self._video_thread.join(timeout=1.0)
        self._running = False
        print(f'[{self.ros_id}] 중단')

    def stop(self) -> None:
        """완전 종료. pause() 후 cap 해제."""
        self.pause()
        self._cap.release()

    def start(self) -> None:
        if self._running:
            return
        # heartbeat 중단 (pose_loop가 odom 담당)
        self._hb_stop.set()
        if self._hb_thread and self._hb_thread.is_alive():
            self._hb_thread.join(timeout=0.5)
        self._stop_evt.clear()
        self._t_last = time.monotonic()
        self._pose_thread = threading.Thread(target=self._pose_loop, name=f'pose-{self.ros_id}', daemon=True)
        self._video_thread = threading.Thread(target=self._video_loop, name=f'video-{self.ros_id}', daemon=True)
        self._pose_thread.start()
        self._video_thread.start()
        # ensure immediate odom/path progress is sent when starting
        self._last_odom = 0
        self._running = True
        try:
            self._send(_build_odom(self.udp_id, self._odom_seq, self._cur_x, self._cur_y, self._cur_yaw))
            self._odom_seq += 1
        except Exception:
            pass
        try:
            self.send_path_progress()
        except Exception:
            pass
        print(f'[{self.ros_id}] 자동 송신 시작')

    # ── Pose 스레드 ───────────────────────────────────────────────────────────
    def _pose_loop(self) -> None:
        pose_dt      = 1.0 / POSE_HZ
        odom_min_us  = int(1_000_000 / POSE_HZ)

        while not self._stop_evt.is_set():
            t0  = time.monotonic()
            dt  = t0 - self._t_last
            self._t_last = t0

            with self._wps_lock:
                x, y, yaw = self._advance(dt)

            now_us = _now_us()
            if now_us - self._last_odom >= odom_min_us:
                self._last_odom = now_us
                self._send(_build_odom(self.udp_id, self._odom_seq, x, y, yaw))
                self._odom_seq += 1
                try:
                    self.send_path_progress()
                except Exception:
                    pass

            time.sleep(max(0.0, pose_dt - (time.monotonic() - t0)))

    def _advance(self, dt: float) -> tuple:
        wps = self._wps
        n   = len(wps)
        if n < 2:
            return self._cur_x, self._cur_y, self._cur_yaw
        if self._seg >= n - 1:
            self._cur_x, self._cur_y = wps[-1]
            return self._cur_x, self._cur_y, self._cur_yaw

        p0 = wps[self._seg];  p1 = wps[self._seg + 1]
        dx = p1[0] - p0[0];   dy = p1[1] - p0[1]
        seg_len = math.hypot(dx, dy) or 1e-6

        self._prog += ROBOT_SPEED * dt
        while self._prog >= seg_len and self._seg < n - 2:
            self._prog -= seg_len
            self._seg  += 1
            p0 = wps[self._seg];  p1 = wps[self._seg + 1]
            dx = p1[0] - p0[0];   dy = p1[1] - p0[1]
            seg_len = math.hypot(dx, dy) or 1e-6

        r             = min(self._prog / seg_len, 1.0)
        self._cur_x   = p0[0] + dx * r
        self._cur_y   = p0[1] + dy * r
        self._cur_yaw = math.atan2(dy, dx)
        return self._cur_x, self._cur_y, self._cur_yaw

    # ── MJPEG 스레드 ──────────────────────────────────────────────────────────
    def _video_loop(self) -> None:
        video_dt = 1.0 / VIDEO_FPS
        next_send = time.monotonic()

        while not self._stop_evt.is_set():
            t0 = time.monotonic()
            frame = None

            if self._cap.isOpened():
                now = t0
                if now >= next_send:
                    while now >= next_send:
                        grabbed = self._cap.grab()
                        if not grabbed:
                            self._cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
                            break
                        next_send += video_dt
                        now = time.monotonic()
                    ret, frame = self._cap.retrieve()
                    if not ret:
                        self._cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
                        ret, frame = self._cap.read()
                if frame is not None:
                    ok, buf = cv2.imencode(
                        '.jpg', frame,
                        [cv2.IMWRITE_JPEG_QUALITY, JPEG_QUALITY],
                    )
                    if ok:
                        jpeg = buf.tobytes()
                        if len(jpeg) <= MAX_FRAME_SIZE:
                            for pkt in _build_image_frags(
                                self.udp_id, self._img_seq, jpeg
                            ):
                                self._send(pkt)
                            self._img_seq = (self._img_seq + 1) & 0xFFFF_FFFF
                            self._fps_cnt += 1

            now = time.monotonic()
            if now - self._fps_t >= 1.0:
                print(f'[MJPEG/{self.ros_id}] {self._fps_cnt}fps')
                self._fps_cnt = 0
                self._fps_t   = now

            time.sleep(max(0.0, video_dt - (time.monotonic() - t0)))

    def _send(self, data: bytes) -> None:
        try:
            self._sock.sendto(data, self._dst)
        except OSError as e:
            print(f'[{self.ros_id}] UDP 오류: {e}')


# ─── ROS2 구독 노드 ───────────────────────────────────────────────────────────
class PathSubscriber(Node):

    def __init__(self, workers: dict):
        super().__init__('sim_executor_script')
        self._workers = workers

        # spot_05는 /planning/global_path/spot_05 구독
        topic_map = {rid: f'/planning/global_path/{rid}' for rid in workers}

        transient_qos = QoSProfile(
            depth=1,
            reliability=QoSReliabilityPolicy.RELIABLE,
            durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
        )
        for rid, topic in topic_map.items():
            self.create_subscription(
                GlobalPathWaypoints,
                topic,
                lambda msg, r=rid: self._path_cb(msg, r),
                transient_qos,
            )
            self.get_logger().info(f'[{rid}] 구독: {topic}')

    def _path_cb(self, msg: GlobalPathWaypoints, robot_id: str) -> None:
        w = self._workers.get(robot_id)
        if w:
            w.set_path(msg.waypoints)
            try:
                w.send_path_progress()
            except Exception:
                pass




def _receiver_loop(workers: dict, stop_evt: threading.Event, mode: str = 'demo') -> None:
    """
    JETSON_CMD_PORT(9001) 수신 루프.
    PktHeader.robot_id → 워커 매핑, CmdPayload.cmd_type → start/pause 디스패치.
    """
    id_to_key = {v: k for k, v in UDP_ROBOT_ID.items()}

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind(('', JETSON_CMD_PORT))
    except OSError as e:
        print(f'[receiver] bind 실패 (포트 {JETSON_CMD_PORT}): {e}')
        return
    sock.settimeout(1.0)
    print(f'[receiver] UDP {JETSON_CMD_PORT} 수신 대기 중...')
    print(f'[receiver] id_to_key 매핑: {id_to_key}')

    # ── 최초 odom 송신: bridge가 로봇 존재를 인식하도록 ──
    for rid_key, rid_num in UDP_ROBOT_ID.items():
        if rid_key == 'spot_01':
            continue
        worker = workers.get(rid_key)
        if worker is None:
            continue
        try:
            pkt = _build_odom(rid_num, 0, worker._cur_x, worker._cur_y, worker._cur_yaw)
            worker._sock.sendto(pkt, worker._dst)
            print(f'[receiver] 초기 odom 송신: {rid_key} ({rid_num})')
        except OSError as e:
            print(f'[receiver] 초기 odom 실패 ({rid_key}): {e}')

    # ── announce: bridge가 ip:9001을 기억하도록 각 로봇별 CMD_ACK 송신 ──
    PKT_TYPE_CMD_ACK = 0x05
    _ACK_FMT  = '<BBHHHIIQ' + 'IIBBHQi'   # PktHeader + CmdAckPayload (padding)
    for rid_key, rid_num in UDP_ROBOT_ID.items():
        if rid_key == 'spot_01':   # 실물은 제외
            continue
        ts = _now_us()
        hdr = struct.pack('<BBHHHIIQ',
                          PKT_TYPE_CMD_ACK, rid_num,
                          0, 1,               # frag_idx, frag_total
                          20,                 # payload_len (CmdAckPayload size)
                          0, 0, ts)
        ack = struct.pack('<IIBBHQ', 0, 0, 0x09, 0, 0, ts)
        try:
            sock.sendto(hdr + ack, (RPI5_IP, BRIDGE_PORT))
            print(f'[receiver] announce 송신: robot_id={rid_num} ({rid_key})')
        except OSError as e:
            print(f'[receiver] announce 실패 ({rid_key}): {e}')

    while not stop_evt.is_set():
        try:
            data, addr = sock.recvfrom(2048)
        except socket.timeout:
            continue

        # print(f'[receiver] 수신 {len(data)}B from {addr}')

        if len(data) < _HDR_SIZE + _CMD_SIZE:
            print(f'[receiver] 패킷 너무 짧음: {len(data)} < {_HDR_SIZE + _CMD_SIZE}')
            continue

        pkt_type, robot_id = struct.unpack_from('<BB', data, 0)
        # print(f'[receiver] pkt_type=0x{pkt_type:02x} robot_id={robot_id}')

        if pkt_type != PKT_TYPE_CMD:
            # print(f'[receiver] PKT_TYPE_CMD(0x{PKT_TYPE_CMD:02x}) 아님 → 무시')
            continue

        cmd_type = struct.unpack_from('<B', data, _HDR_SIZE)[0]
        # print(f'[receiver] cmd_type=0x{cmd_type:02x}')

        key = id_to_key.get(robot_id)
        if key not in workers:
            print(f'[receiver] robot_id={robot_id} 매핑 없음 → 무시')
            continue

        worker = workers[key]
        if cmd_type in (CMD_TYPE_START_MISSION, CMD_TYPE_MOVE):
            if mode == 'test':
                worker.pause()
                worker.reset()
                print(f'[receiver] [{key}] MOVE → restart (test mode)')
            else:
                print(f'[receiver] [{key}] START → start()')
            worker.start()
        elif cmd_type in (CMD_TYPE_PAUSE_MISSION, CMD_TYPE_STOP):
            print(f'[receiver] [{key}] PAUSE/STOP → pause()')
            worker.pause()
        elif cmd_type == CMD_TYPE_HEARTBEAT:
            pass  # 정상 수신, 출력 생략
        else:
            print(f'[receiver] [{key}] 미등록 cmd_type=0x{cmd_type:02x}')

    sock.close()
    print('[receiver] 종료.')
_KEY_HELP = (
    '  s0        spot_02/03/04/05 동시 시작\n'
    '  p0        전체 중단\n'
    '  s2 / p2   spot_02 시작 / 중단\n'
    '  s3 / p3   spot_03 시작 / 중단\n'
    '  s4 / p4   spot_04 시작 / 중단\n'
    '  s5 / p5   spot_05 시작 / 중단\n'
    '  v2 / f2   spot_02 탐지 ON / OFF\n'
    '  v3 / f3   spot_03 탐지 ON / OFF\n'
    '  v4 / f4   spot_04 탐지 ON / OFF\n'
    '  v5 / f5   spot_05 탐지 ON / OFF\n'
    '  q         종료\n'
)


def main() -> None:
    parser = argparse.ArgumentParser(description='sim_executor_script')
    parser.add_argument('--test', action='store_true',
                        help='MOVE 수신 시 처음부터 재출발 (기본: 이미 실행 중이면 유지)')
    args = parser.parse_args()
    mode = 'test' if args.test else 'demo'
    rclpy.init()

    sock    = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dst     = (RPI5_IP, BRIDGE_PORT)
    workers = {}
    for rid in DUMMY_ROBOTS:
        workers[rid] = RobotWorker(rid, sock, dst)

    node     = PathSubscriber(workers)
    executor = rclpy.executors.SingleThreadedExecutor()
    executor.add_node(node)

    # ── receiver 스레드 (UDP 9001 → start/pause 디스패치) ──
    _recv_stop = threading.Event()
    recv_thread = threading.Thread(
        target=_receiver_loop, args=(workers, _recv_stop, mode), daemon=True
    )
    recv_thread.start()

    print(f'[sim_executor_script] 시작  ({"test" if mode == "test" else "demo"})')
    print(f'  UDP → {RPI5_IP}:{BRIDGE_PORT}')
    print(f'  spot_02/03/04/05: /planning/global_path/spot_XX 수신 대기 중...')
    print(f'  경로 수신 후 키 입력으로 시작하십시오.')
    print(f'키보드:\n{_KEY_HELP}')

    try:
        while rclpy.ok():
            executor.spin_once(timeout_sec=0.05)

            r, _, _ = select.select([sys.stdin], [], [], 0)
            if not r:
                continue

            line = sys.stdin.readline()
            if not line:
                break
            key = line.strip().lower()

            if   key == 's0':
                for rid in ['spot_02', 'spot_03', 'spot_04', 'spot_05']:
                    if rid in workers:
                        workers[rid].start()
            elif key == 'p0':
                for w in workers.values():
                    w.pause()
            elif key == 's2': workers['spot_02'].start()
            elif key == 's3': workers['spot_03'].start()
            elif key == 's4': workers['spot_04'].start()
            elif key == 's5': workers['spot_05'].start()
            elif key == 'p2': workers['spot_02'].pause()
            elif key == 'p3': workers['spot_03'].pause()
            elif key == 'p4': workers['spot_04'].pause()
            elif key == 'p5': workers['spot_05'].pause()
            elif key == 'v2': workers['spot_02'].send_event(True)
            elif key == 'f2': workers['spot_02'].send_event(False)
            elif key == 'v3': workers['spot_03'].send_event(True)
            elif key == 'f3': workers['spot_03'].send_event(False)
            elif key == 'v4': workers['spot_04'].send_event(True)
            elif key == 'f4': workers['spot_04'].send_event(False)
            elif key == 'v5': workers['spot_05'].send_event(True)
            elif key == 'f5': workers['spot_05'].send_event(False)
            elif key == 'q':  break
            else: print(f'알 수 없는 입력: "{key}"\n{_KEY_HELP}')

    except KeyboardInterrupt:
        pass
    finally:
        _recv_stop.set()
        recv_thread.join(timeout=2.0)
        for w in workers.values():
            w.stop()
        sock.close()
        node.destroy_node()
        rclpy.shutdown()
        print('[sim_executor_script] 종료.')


if __name__ == '__main__':
    main()
