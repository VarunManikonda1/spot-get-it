#!/usr/bin/env python3
"""
lidar_stream_sender_node.py

역할:
  /scan_3D (sensor_msgs/PointCloud2) 를 구독하여
  proto.h LiDAR 페이로드 포맷으로 변환 후 UDP로 RPi BridgeDaemon에 송신한다.

Subscribe:
  /scan_3D  (sensor_msgs/msg/PointCloud2)

Publish:
  없음

Params:
  config/lidar_stream_sender.param.yaml 참조
"""

import math
import socket
import struct
import time

import numpy as np
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import PointCloud2

# ─── proto.h 상수 ──────────────────────────────────────────────
PKT_TYPE_LIDAR = 0x02
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
        PKT_TYPE_LIDAR,
        robot_id,
        frag_idx,
        frag_total,
        payload_len,
        frame_id,
        offset,
        _now_us(),
    )


class LidarStreamSenderNode(Node):
    def __init__(self):
        super().__init__('lidar_stream_sender_node')

        # ─── 파라미터 선언 ─────────────────────────────────────
        self.declare_parameter('target_ip',   'REQUIRED')
        self.declare_parameter('robot_id', 0)
        self.declare_parameter('max_pts',  600)
        self.declare_parameter('default_intensity', 1.0)

        target_ip = self.get_parameter('target_ip').value
        if target_ip == 'REQUIRED':
            self.get_logger().fatal('파라미터 target_ip 가 설정되지 않았습니다.')
            raise RuntimeError('target_ip 파라미터 필수')

        self._robot_id          = int(self.get_parameter('robot_id').value)
        self._max_pts           = int(self.get_parameter('max_pts').value)
        self._default_intensity = float(self.get_parameter('default_intensity').value)

        # ─── UDP 소켓 ──────────────────────────────────────────
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._dst  = (target_ip, BRIDGE_PORT)

        # ─── 상태 ──────────────────────────────────────────────
        self._frame_id  = 0
        self._fps_count = 0
        self._t_last    = time.monotonic()

        # ─── 구독 ──────────────────────────────────────────────
        self._sub = self.create_subscription(
            PointCloud2,
            '/perception/lidar/clustered_points_colored',
            self._on_scan_3d,
            10,
        )

        self.get_logger().info(
            f'lidar_stream_sender 시작 | '
            f'TARGET={target_ip}:{BRIDGE_PORT} | robot_id={self._robot_id}'
        )

    # ──────────────────────────────────────────────────────────
    def _on_scan_3d(self, msg: PointCloud2) -> None:
        # 1. raw 버퍼에서 직접 필드 추출 (structured dtype 캐스팅 문제 완전 회피)
        field_map      = {f.name: f.offset for f in msg.fields}
        point_step     = msg.point_step
        n_pts          = msg.width * msg.height
        has_intensity  = 'intensity' in field_map

        if n_pts == 0:
            return

        try:
            buf = np.frombuffer(bytes(msg.data), dtype=np.uint8).reshape(n_pts, point_step)
            x = buf[:, field_map['x']:field_map['x']+4].copy().view(np.float32).reshape(-1)
            y = buf[:, field_map['y']:field_map['y']+4].copy().view(np.float32).reshape(-1)
            z = buf[:, field_map['z']:field_map['z']+4].copy().view(np.float32).reshape(-1)
            if has_intensity:
                intensity = buf[:, field_map['intensity']:field_map['intensity']+4].copy().view(np.float32).reshape(-1)
            else:
                intensity = np.full(n_pts, self._default_intensity, dtype=np.float32)
        except Exception as e:
            self.get_logger().error(f'PointCloud2 파싱 오류: {e}')
            return

        # NaN 필터
        valid = np.isfinite(x) & np.isfinite(y) & np.isfinite(z)
        x, y, z, intensity = x[valid], y[valid], z[valid], intensity[valid]

        if len(x) == 0:
            return

        # 3. 포인트 수 상한
        if len(x) > self._max_pts:
            x         = x[:self._max_pts]
            y         = y[:self._max_pts]
            z         = z[:self._max_pts]
            intensity = intensity[:self._max_pts]

        count = len(x)

        # 4. proto.h 페이로드 패킹
        #    [ uint32_t count ][ float x, y, z, intensity × count ]
        xyzi = np.zeros((count, 4), dtype=np.float32)
        xyzi[:, 0] = x
        xyzi[:, 1] = y
        xyzi[:, 2] = z
        xyzi[:, 3] = intensity

        payload = struct.pack('<I', count) + xyzi.tobytes()

        # 5. MTU 분할 → UDP 송신
        self._send_fragments(payload)

        # 6. FPS 로그 (1초마다)
        self._frame_id  = (self._frame_id + 1) & 0xFFFF_FFFF
        self._fps_count += 1
        now = time.monotonic()
        if now - self._t_last >= 1.0:
            frags = math.ceil(len(payload) / PROTO_MTU)
            self.get_logger().info(
                f'FPS={self._fps_count}  pts={count}  '
                f'payload={len(payload)}B  frags={frags}'
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
        node = LidarStreamSenderNode()
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
