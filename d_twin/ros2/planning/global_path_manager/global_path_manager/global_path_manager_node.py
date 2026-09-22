#!/usr/bin/env python3
"""
global_path_manager_node.py

시작 시:
  맵 로드 → 로봇별 path set 생성 → 유효성 판정 → 선정 → 즉시 발행
이후 spin() 유지 (TRANSIENT_LOCAL 캐시 보존)

Publish:
  /planning/global_path/spot_01  (robot_interfaces/GlobalPathWaypoints)
  /planning/global_path/spot_02
  /planning/global_path/spot_03
  /planning/global_path/spot_04
  /planning/global_path/spot_05  (spot_01 경로를 robot_id=spot_05로 복사)
  ※ spot_01~04 토픽은 map.yaml의 starts 키 기준으로 자동 생성
"""

import math
import os
from typing import Dict, List, Tuple

import rclpy
from ament_index_python.packages import get_package_share_directory
from geometry_msgs.msg import Point, Quaternion
from rclpy.node import Node
from rclpy.qos import QoSDurabilityPolicy, QoSProfile, QoSReliabilityPolicy

from robot_interfaces.msg import GlobalPathWaypoints, LocalizedRobotPose
from .path_set import GlobalPath, MapConfig, build_path_set, select_paths, validate_path_set

INTERP_MAX_DIST = 0.5   # 보간 최대 구간 거리 (m)

Waypoint = Tuple[float, float]


def _yaw_between(p1, p2) -> float:
    return math.atan2(p2[1] - p1[1], p2[0] - p1[0])


def _interpolate(wps: List[Waypoint]) -> List[Tuple[Waypoint, float]]:
    """
    waypoints 리스트를 받아 각 선분을 INTERP_MAX_DIST 이하로 보간한다.
    반환: [(point, yaw), ...]
    마지막 점의 yaw는 직전 선분 방향을 그대로 사용한다.
    """
    result: List[Tuple[Waypoint, float]] = []

    for i in range(len(wps) - 1):
        p1, p2 = wps[i], wps[i + 1]
        yaw = _yaw_between(p1, p2)
        length = math.hypot(p2[0] - p1[0], p2[1] - p1[1])
        n = math.ceil(length / INTERP_MAX_DIST)  # 구간 수

        for k in range(n):
            t = k / n
            x = round(p1[0] + t * (p2[0] - p1[0]), 2)
            y = round(p1[1] + t * (p2[1] - p1[1]), 2)
            result.append(((x, y), yaw))

    # 마지막 점 추가 (직전 선분 yaw 유지)
    last_yaw = _yaw_between(wps[-2], wps[-1]) if len(wps) >= 2 else 0.0
    result.append((wps[-1], last_yaw))

    return result


def _to_waypoints_msg(
    path: GlobalPath,
    robot_id: str,
    stamp,
    frame_id: str,
) -> GlobalPathWaypoints:
    msg = GlobalPathWaypoints()
    msg.header.stamp    = stamp
    msg.header.frame_id = frame_id
    msg.robot_id        = robot_id

    for (x, y), yaw in _interpolate(path.waypoints):
        half = yaw / 2.0
        q = Quaternion(x=0.0, y=0.0, z=math.sin(half), w=math.cos(half))

        wp = LocalizedRobotPose()
        wp.header.stamp     = stamp
        wp.header.frame_id  = frame_id
        wp.robot_id         = robot_id
        wp.base_frame       = 'base_link'
        wp.x_m              = float(x)
        wp.y_m              = float(y)
        wp.z_m              = 0.05
        wp.yaw_rad          = float(yaw)
        wp.pose.position    = Point(x=float(x), y=float(y), z=0.05)
        wp.pose.orientation = q

        msg.waypoints.append(wp)

    return msg


class GlobalPathManagerNode(Node):

    def __init__(self):
        super().__init__("global_path_manager_node")

        self.declare_parameter("frame_id",        "map")
        self.declare_parameter("map_config_path", "")

        self.frame_id   = self.get_parameter("frame_id").value
        map_config_path = self.get_parameter("map_config_path").value

        if not map_config_path:
            map_config_path = os.path.join(
                get_package_share_directory("global_path_manager"),
                "config", "map.yaml",
            )

        # ── Publisher: TRANSIENT_LOCAL ──
        qos = QoSProfile(
            depth=1,
            reliability=QoSReliabilityPolicy.RELIABLE,
            durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
        )

        # ── 맵 로드 ──
        self.get_logger().info(f"맵 로드: {map_config_path}")
        cfg = MapConfig(map_config_path)
        self.get_logger().info(
            f"로봇별 출발지: { {k: v for k, v in cfg.starts.items()} }"
        )
        self.get_logger().info(
            f"장애물 {len(cfg.obstacles)}개: {[o['id'] for o in cfg.obstacles]}"
        )

        # 로봇별 publisher 생성 (starts 기준)
        pubs: Dict[str, rclpy.publisher.Publisher] = {}
        for robot_key in cfg.starts:
            topic = f"/planning/global_path/{robot_key}"
            pubs[robot_key] = self.create_publisher(GlobalPathWaypoints, topic, qos)
            self.get_logger().info(f"Publisher 등록: {topic}")

        # spot_05 별도 등록 (starts에 없음)
        pubs['spot_05'] = self.create_publisher(
            GlobalPathWaypoints, '/planning/global_path/spot_05', qos
        )
        self.get_logger().info("Publisher 등록: /planning/global_path/spot_05")

        # ── path set 생성 ──
        path_sets = build_path_set(cfg)
        for robot_key, paths in path_sets.items():
            self.get_logger().info(f"  [{robot_key}] path set: {len(paths)}개")

        # ── 유효성 + 커버리지 판정 ──
        validate_path_set(path_sets, cfg)
        for robot_key, paths in path_sets.items():
            valid = sum(1 for p in paths if p.valid)
            self.get_logger().info(f"  [{robot_key}] 유효: {valid}/{len(paths)}개")

        # ── 선정 및 즉시 발행 ──
        selected = select_paths(path_sets, starts=cfg.starts, cfg=cfg)
        stamp = self.get_clock().now().to_msg()
        for robot_key, path in selected.items():
            wp_msg = _to_waypoints_msg(path, robot_key, stamp, self.frame_id)
            pubs[robot_key].publish(wp_msg)
            self.get_logger().info(
                f"  [{robot_key}] {path.path_id} "
                f"원본 {len(path.waypoints)}wp → 보간 후 {len(wp_msg.waypoints)}wp"
            )

        # spot_05: spot_01 경로를 robot_id=spot_05로 복사 발행
        src = selected.get('spot_01')
        if src:
            wp_msg = _to_waypoints_msg(src, 'spot_05', stamp, self.frame_id)
            pubs['spot_05'].publish(wp_msg)
            self.get_logger().info(
                f"  [spot_05] spot_01 경로 복사 발행 ({len(wp_msg.waypoints)}wp)"
            )

        self.get_logger().info("발행 완료.")


def main(args=None):
    rclpy.init(args=args)
    node = GlobalPathManagerNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
