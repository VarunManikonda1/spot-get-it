#!/usr/bin/env python3
"""
camera_stream_sender_node.py

역할:
  /perception/camera/encoded (CompressedImage) 를 구독하여
  proto.h IMAGE 페이로드 포맷으로 변환 후 UDP로 RPi BridgeDaemon에 송신한다.

  이미지 페이로드: JPEG 바이트 그대로 전송 (재압축 없음).

Subscribe:  /perception/camera/encoded   (sensor_msgs/msg/CompressedImage)
Publish:    없음

Params:
  config/camera_stream_sender.param.yaml 참조
"""

import math
import socket
import struct
import time

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import CompressedImage

# ─── proto.h 상수 ──────────────────────────────────────────────
PKT_TYPE_IMAGE = 0x01
BRIDGE_PORT    = 9000
PROTO_MTU      = 1400

# PktHeader (24B, little-endian):
#   type(1) robot_id(1) frag_idx(2) frag_total(2)
#   payload_len(2) frame_id(4) payload_offset(4) timestamp_us(8)
_HDR_FMT  = '<BBHHHIIQ'
_HDR_SIZE = struct.calcsize(_HDR_FMT)   # 24


def _now_us() -> int:
    return int(time.monotonic() * 1_000_000)


def _build_header(robot_id: int, frag_idx: int, frag_total: int,
                  payload_len: int, frame_id: int, offset: int) -> bytes:
    return struct.pack(
        _HDR_FMT,
        PKT_TYPE_IMAGE,
        robot_id,
        frag_idx,
        frag_total,
        payload_len,
        frame_id,
        offset,
        _now_us(),
    )


class CameraStreamSenderNode(Node):
    def __init__(self):
        super().__init__('camera_stream_sender_node')

        # ─── 파라미터 선언 ─────────────────────────────────────
        self.declare_parameter('rpi_ip',   'REQUIRED')
        self.declare_parameter('robot_id', 0)
        self.declare_parameter('max_size_bytes', 200000)   # 200KB 초과 프레임 skip

        rpi_ip = self.get_parameter('rpi_ip').value
        if rpi_ip == 'REQUIRED':
            self.get_logger().fatal('파라미터 rpi_ip 가 설정되지 않았습니다.')
            raise RuntimeError('rpi_ip 파라미터 필수')

        self._robot_id      = int(self.get_parameter('robot_id').value)
        self._max_size      = int(self.get_parameter('max_size_bytes').value)

        # ─── UDP 소켓 ──────────────────────────────────────────
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._dst  = (rpi_ip, BRIDGE_PORT)

        # ─── 상태 ──────────────────────────────────────────────
        self._frame_id  = 0
        self._fps_count = 0
        self._t_last    = time.monotonic()

        # ─── 구독 ──────────────────────────────────────────────
        self._sub = self.create_subscription(
            CompressedImage,
            '/vendor/camera/encoded',
            self._on_image,
            10,
        )

        self.get_logger().info(
            f'camera_stream_sender 시작 | '
            f'RPi={rpi_ip}:{BRIDGE_PORT} | robot_id={self._robot_id}'
        )

    # ──────────────────────────────────────────────────────────
    def _on_image(self, msg: CompressedImage) -> None:
        # 1. JPEG 바이트 추출 (재압축 없음)
        payload = bytes(msg.data)
        size    = len(payload)

        if size == 0:
            return

        # 2. 크기 초과 프레임 skip (BridgeDaemon REASM_BUF_MAX = 512KB)
        if size > self._max_size:
            self.get_logger().warn(f'프레임 너무 큼: {size}B > {self._max_size}B — skip')
            return

        # 3. MTU 분할 → UDP 송신
        self._send_fragments(payload)

        self._frame_id  = (self._frame_id + 1) & 0xFFFF_FFFF
        self._fps_count += 1

        # 4. FPS 로그 (1초마다)
        now = time.monotonic()
        if now - self._t_last >= 1.0:
            frags = math.ceil(size / PROTO_MTU)
            self.get_logger().info(
                f'FPS={self._fps_count}  size={size}B  frags={frags}'
            )
            self._fps_count = 0
            self._t_last    = now

    def _send_fragments(self, payload: bytes) -> None:
        size       = len(payload)
        frag_total = math.ceil(size / PROTO_MTU)

        for idx in range(frag_total):
            offset = idx * PROTO_MTU
            chunk  = payload[offset: offset + PROTO_MTU]
            hdr    = _build_header(
                robot_id    = self._robot_id,
                frag_idx    = idx,
                frag_total  = frag_total,
                payload_len = len(chunk),
                frame_id    = self._frame_id,
                offset      = offset,
            )
            try:
                self._sock.sendto(hdr + chunk, self._dst)
            except OSError as e:
                self.get_logger().error(f'UDP 송신 오류: {e}')
                return

    # ──────────────────────────────────────────────────────────
    def destroy_node(self) -> None:
        self._sock.close()
        super().destroy_node()


def main(args=None):
    rclpy.init(args=args)
    try:
        node = CameraStreamSenderNode()
    except RuntimeError:
        rclpy.shutdown()
        return

    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
