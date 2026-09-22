import math
from typing import Tuple


def normalize_quat_xyzw(qx: float, qy: float, qz: float, qw: float) -> Tuple[float, float, float, float]:
    norm = math.sqrt(qx*qx + qy*qy + qz*qz + qw*qw)
    if norm < 1e-9:
        return 0.0, 0.0, 0.0, 1.0
    return qx / norm, qy / norm, qz / norm, qw / norm


def quat_conjugate_xyzw(qx: float, qy: float, qz: float, qw: float) -> Tuple[float, float, float, float]:
    return -qx, -qy, -qz, qw


def quat_multiply_xyzw(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b

    x = aw * bx + ax * bw + ay * bz - az * by
    y = aw * by - ax * bz + ay * bw + az * bx
    z = aw * bz + ax * by - ay * bx + az * bw
    w = aw * bw - ax * bx - ay * by - az * bz

    return x, y, z, w


def rotate_vector_by_quat_xyzw(v, q):
    """
    v_world를 q로 rotate.
    q는 xyzw.
    """
    vx, vy, vz = v
    qv = (vx, vy, vz, 0.0)
    return quat_multiply_xyzw(quat_multiply_xyzw(q, qv), quat_conjugate_xyzw(*q))[:3]


def inverse_rotate_vector_by_quat_xyzw(v, q):
    """
    Isaac Gym의 quat_rotate_inverse(q, v)에 대응.
    q가 base orientation in world라면,
    world vector v를 base/body frame으로 변환한다.
    """
    q_inv = quat_conjugate_xyzw(*q)
    vx, vy, vz = v
    qv = (vx, vy, vz, 0.0)
    return quat_multiply_xyzw(quat_multiply_xyzw(q_inv, qv), q)[:3]


def projected_gravity_from_ros_quat_xyzw(
    qx: float,
    qy: float,
    qz: float,
    qw: float,
) -> Tuple[float, float, float]:
    """
    ROS sensor_msgs/Imu orientation은 xyzw 순서다.

    반환값:
      world gravity [0, 0, -1]을 body/base frame으로 inverse rotate한 값.

    검증:
      identity quaternion (0,0,0,1) -> [0,0,-1]
    """
    q = normalize_quat_xyzw(qx, qy, qz, qw)
    return inverse_rotate_vector_by_quat_xyzw((0.0, 0.0, -1.0), q)


def convert_wxyz_to_xyzw(qw: float, qx: float, qy: float, qz: float):
    return qx, qy, qz, qw
