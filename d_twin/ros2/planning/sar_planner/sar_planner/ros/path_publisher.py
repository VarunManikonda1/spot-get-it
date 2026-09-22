"""
ros/path_publisher.py

ROS2 publisher wrapper for GlobalPathWaypoints.
Imported only when ROS2 is available (checked in main.py).

Adapted from global_path_manager_node.py.
"""

import math
import threading
from typing import Dict, List, Tuple

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSDurabilityPolicy, QoSProfile, QoSReliabilityPolicy

from geometry_msgs.msg import Point, Quaternion
from robot_interfaces.msg import GlobalPathWaypoints, LocalizedRobotPose

INTERP_MAX_DIST = 0.5   # m
Waypoint = Tuple[float, float]


def _yaw_between(p1: Waypoint, p2: Waypoint) -> float:
    return math.atan2(p2[1] - p1[1], p2[0] - p1[0])


def _interpolate(wps: List[Waypoint]) -> List[Tuple[Waypoint, float]]:
    """Interpolate waypoints so no segment exceeds INTERP_MAX_DIST."""
    result = []
    for i in range(len(wps) - 1):
        p1, p2 = wps[i], wps[i + 1]
        yaw    = _yaw_between(p1, p2)
        length = math.hypot(p2[0]-p1[0], p2[1]-p1[1])
        n      = math.ceil(length / INTERP_MAX_DIST)
        for k in range(n):
            t = k / n
            x = round(p1[0] + t * (p2[0]-p1[0]), 2)
            y = round(p1[1] + t * (p2[1]-p1[1]), 2)
            result.append(((x, y), yaw))
    last_yaw = _yaw_between(wps[-2], wps[-1]) if len(wps) >= 2 else 0.0
    result.append((wps[-1], last_yaw))
    return result


def _build_msg(path, robot_id: str, stamp, frame_id: str) -> GlobalPathWaypoints:
    msg             = GlobalPathWaypoints()
    msg.header.stamp    = stamp
    msg.header.frame_id = frame_id
    msg.robot_id        = robot_id

    for (x, y), yaw in _interpolate(path.waypoints):
        half = yaw / 2.0
        q    = Quaternion(x=0.0, y=0.0, z=math.sin(half), w=math.cos(half))
        wp                  = LocalizedRobotPose()
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


class PathPublisherNode(Node):
    """
    Minimal ROS2 node that publishes GlobalPathWaypoints (TRANSIENT_LOCAL).
    Created once; publish() can be called multiple times.
    """

    def __init__(self, frame_id: str = 'mission_map'):
        super().__init__('sar_planner_node')
        self.frame_id = frame_id
        self._qos = QoSProfile(
            depth=1,
            reliability=QoSReliabilityPolicy.RELIABLE,
            durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
        )
        self._pubs: Dict[str, rclpy.publisher.Publisher] = {}

    def publish(self, selected: dict, on_done: callable = None):
        """
        Publish selected paths for all robots.
        Also publishes spot_01 path as spot_05 (legacy behaviour).

        Args:
            selected:  {robot_key: GlobalPath}
            on_done:   optional callback(summary_str) called after publish
        """
        stamp = self.get_clock().now().to_msg()
        lines = []

        for robot_key, path in selected.items():
            pub = self._get_or_create_pub(robot_key)
            msg = _build_msg(path, robot_key, stamp, self.frame_id)
            pub.publish(msg)
            lines.append(f'{robot_key}: {len(msg.waypoints)} wp')
            self.get_logger().info(
                f'Published {robot_key} — {len(msg.waypoints)} waypoints')

        # spot_05: copy of spot_01 (legacy)
        src = selected.get('spot_01')
        if src:
            pub = self._get_or_create_pub('spot_05')
            msg = _build_msg(src, 'spot_05', stamp, self.frame_id)
            pub.publish(msg)
            lines.append(f'spot_05 (copy of spot_01): {len(msg.waypoints)} wp')
            self.get_logger().info(
                f'Published spot_05 (spot_01 copy) — {len(msg.waypoints)} waypoints')

        if on_done:
            on_done('\n'.join(lines))

    def _get_or_create_pub(self, robot_key: str):
        if robot_key not in self._pubs:
            topic = f'/planning/global_path/{robot_key}'
            self._pubs[robot_key] = self.create_publisher(
                GlobalPathWaypoints, topic, self._qos)
            self.get_logger().info(f'Publisher created: {topic}')
        return self._pubs[robot_key]


class ROS2Manager:
    """
    Manages the ROS2 node and spin thread.
    Instantiated once in main.py if ROS2 is available.
    """

    def __init__(self, frame_id: str = 'mission_map'):
        self.node    = PathPublisherNode(frame_id=frame_id)
        self._thread = threading.Thread(
            target=rclpy.spin,
            args=(self.node,),
            daemon=True,
            name='rclpy_spin',
        )
        self._thread.start()

    def publish(self, selected: dict, on_done: callable = None):
        """Thread-safe publish (called from worker or main thread)."""
        self.node.publish(selected, on_done=on_done)

    def shutdown(self):
        rclpy.shutdown()
