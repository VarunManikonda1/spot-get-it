#!/usr/bin/env python3
"""
lidar_sparse_node.py

역할:
  /scan_3D (PointCloud2, ~9600pts) 를 구독하여
  2단계 다운샘플링 후 /network/lidar/points_sparse 로 publish.

  1단계 — Voxel Grid (numpy 벡터화, O(N)):
    9600pts → ~300pts. Jetson 부담 최소화.

  2단계 — Poisson Disk (Python for, O(M), M<<N):
    ~300pts → ~150pts. 공간 균일 분포 보장.

Subscribe:  /scan_3D                 (sensor_msgs/msg/PointCloud2)
Publish:    /network/lidar/points_sparse (sensor_msgs/msg/PointCloud2)

Params:
  config/lidar_sparse.param.yaml 참조
"""

import numpy as np
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import PointCloud2, PointField


class LidarSparseNode(Node):
    def __init__(self):
        super().__init__('lidar_sparse_node')

        # ─── 파라미터 ──────────────────────────────────────────
        self.declare_parameter('min_dist_m',  0.05)   # 거리 필터 하한
        self.declare_parameter('max_dist_m',  2.0)    # 거리 필터 상한
        self.declare_parameter('voxel_pre_r', 0.08)   # 1단계: voxel 셀 크기 (m)
        self.declare_parameter('poisson_r',   0.30)   # 2단계: Poisson 최소 간격 (m)
        self.declare_parameter('max_pts',     400)    # 출력 포인트 상한

        self._min_d      = float(self.get_parameter('min_dist_m').value)
        self._max_d      = float(self.get_parameter('max_dist_m').value)
        self._voxel_r    = float(self.get_parameter('voxel_pre_r').value)
        self._poisson_r  = float(self.get_parameter('poisson_r').value)
        self._max_pts    = int(self.get_parameter('max_pts').value)

        # ─── 구독 / 퍼블리시 ───────────────────────────────────
        self._sub = self.create_subscription(
            PointCloud2, '/scan_3D', self._on_scan, 10)
        self._pub = self.create_publisher(
            PointCloud2, '/network/lidar/points_sparse', 10)

        self.get_logger().info(
            f'lidar_sparse 시작 | '
            f'voxel={self._voxel_r}m → poisson={self._poisson_r}m | '
            f'dist={self._min_d}~{self._max_d}m | max={self._max_pts}pts'
        )

    # ──────────────────────────────────────────────────────────
    def _on_scan(self, msg: PointCloud2) -> None:
        # 1. raw 버퍼 → xyz 추출
        field_map  = {f.name: f.offset for f in msg.fields}
        point_step = msg.point_step
        n_raw      = msg.width * msg.height
        if n_raw == 0:
            return

        try:
            buf = np.frombuffer(bytes(msg.data), dtype=np.uint8).reshape(n_raw, point_step)
            x = buf[:, field_map['x']:field_map['x']+4].copy().view(np.float32).reshape(-1)
            y = buf[:, field_map['y']:field_map['y']+4].copy().view(np.float32).reshape(-1)
            z = buf[:, field_map['z']:field_map['z']+4].copy().view(np.float32).reshape(-1)
        except Exception as e:
            self.get_logger().error(f'파싱 오류: {e}')
            return

        # 2. NaN 및 거리 필터
        d = np.sqrt(x**2 + y**2 + z**2)
        mask = np.isfinite(x) & np.isfinite(y) & np.isfinite(z) \
               & (d >= self._min_d) & (d <= self._max_d)
        x, y, z = x[mask], y[mask], z[mask]
        if len(x) == 0:
            return

        # 3. 1단계: Voxel Grid (numpy 벡터화 — O(N), 빠름)
        #    9600pts → ~300pts
        x, y, z = self._voxel_filter(x, y, z, self._voxel_r)
        if len(x) == 0:
            return

        # 4. 2단계: Poisson Disk (Python for, O(M), M은 이미 작음)
        #    ~300pts → ~150pts, 공간 균일 분포 보장
        pts = self._poisson_disk(x, y, z, self._poisson_r)
        if len(pts) == 0:
            return

        # 5. 포인트 수 상한
        if len(pts) > self._max_pts:
            idx = np.random.choice(len(pts), self._max_pts, replace=False)
            pts = pts[idx]

        # 6. PointCloud2 퍼블리시
        out              = PointCloud2()
        out.header       = msg.header
        out.height       = 1
        out.width        = len(pts)
        out.is_bigendian = False
        out.point_step   = 12
        out.row_step     = 12 * len(pts)
        out.is_dense     = True
        out.fields       = [
            PointField(name='x', offset=0,  datatype=PointField.FLOAT32, count=1),
            PointField(name='y', offset=4,  datatype=PointField.FLOAT32, count=1),
            PointField(name='z', offset=8,  datatype=PointField.FLOAT32, count=1),
        ]
        out.data = pts.astype(np.float32).tobytes()
        self._pub.publish(out)

        self.get_logger().debug(
            f'{n_raw} → voxel:{len(x)} → poisson:{len(pts)}pts'
        )

    # ──────────────────────────────────────────────────────────
    @staticmethod
    def _voxel_filter(x: np.ndarray, y: np.ndarray,
                      z: np.ndarray, cell: float):
        """
        Voxel Grid 다운샘플링 — numpy 완전 벡터화 O(N).
        셀당 centroid 1개 유지.
        """
        inv = 1.0 / cell
        vx  = np.floor(x * inv).astype(np.int32)
        vy  = np.floor(y * inv).astype(np.int32)
        vz  = np.floor(z * inv).astype(np.int32)

        keys            = np.stack([vx, vy, vz], axis=1)
        _, inverse      = np.unique(keys, axis=0, return_inverse=True)
        n_voxels        = int(inverse.max()) + 1
        xyz             = np.stack([x, y, z], axis=1).astype(np.float64)
        sum_xyz         = np.zeros((n_voxels, 3), dtype=np.float64)
        np.add.at(sum_xyz, inverse, xyz)
        counts          = np.bincount(inverse).astype(np.float64)
        centroids       = (sum_xyz / counts[:, None]).astype(np.float32)

        return centroids[:, 0], centroids[:, 1], centroids[:, 2]

    # ──────────────────────────────────────────────────────────
    @staticmethod
    def _poisson_disk(x: np.ndarray, y: np.ndarray,
                      z: np.ndarray, r: float) -> np.ndarray:
        """
        Poisson Disk Sampling — 3D grid 가속 O(M).
        M이 voxel 이후 ~300pts로 줄어 Python 루프 부담 최소.
        """
        r2  = r * r
        inv = 1.0 / r

        n     = len(x)
        order = np.random.permutation(n)
        x, y, z = x[order], y[order], z[order]

        gx = np.floor(x * inv).astype(np.int32)
        gy = np.floor(y * inv).astype(np.int32)
        gz = np.floor(z * inv).astype(np.int32)

        grid: dict  = {}
        sel_x: list = []
        sel_y: list = []
        sel_z: list = []

        for i in range(n):
            cx, cy, cz = int(gx[i]), int(gy[i]), int(gz[i])
            px, py, pz = float(x[i]), float(y[i]), float(z[i])

            conflict = False
            for dx in (-1, 0, 1):
                if conflict: break
                for dy in (-1, 0, 1):
                    if conflict: break
                    for dz2 in (-1, 0, 1):
                        nb = grid.get((cx+dx, cy+dy, cz+dz2))
                        if nb is None:
                            continue
                        ddx = px - nb[0]
                        ddy = py - nb[1]
                        ddz = pz - nb[2]
                        if ddx*ddx + ddy*ddy + ddz*ddz < r2:
                            conflict = True
                            break

            if not conflict:
                grid[(cx, cy, cz)] = (px, py, pz)
                sel_x.append(px)
                sel_y.append(py)
                sel_z.append(pz)

        if not sel_x:
            return np.zeros((0, 3), dtype=np.float32)

        return np.column_stack([sel_x, sel_y, sel_z]).astype(np.float32)


def main(args=None):
    rclpy.init(args=args)
    node = LidarSparseNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
