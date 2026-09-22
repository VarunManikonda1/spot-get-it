import os
import ctypes

LIB_PATH = '/home/ubuntu/robot_ws_311/install/robot_interfaces/lib'

for so in [
    'librobot_interfaces__rosidl_generator_c.so',
    'librobot_interfaces__rosidl_typesupport_introspection_c.so',
    'librobot_interfaces__rosidl_typesupport_fastrtps_c.so',
    'librobot_interfaces__rosidl_typesupport_c.so',
    'librobot_interfaces__rosidl_generator_py.so',
]:
    try:
        ctypes.CDLL(f'{LIB_PATH}/{so}')
        print(f"[robot_localization] 로드 성공: {so}")
    except Exception as e:
        print(f"[robot_localization] 로드 실패: {so} - {e}")

import math
import sys
import rclpy
from pxr import Gf
import omni.usd

sys.path.insert(0, '/home/ubuntu/robot_ws_311/install/robot_interfaces/local/lib/python3.11/dist-packages')

PRIM_MAP = {
    1: '/World/MapRoot/SpawnPoints/Spot_Real',
    2: '/World/MapRoot/SpawnPoints/Spot_Sim_01',
    3: '/World/MapRoot/SpawnPoints/Spot_Sim_02',
}

Z_HEIGHT  = 0.15
# True: 물리 완전 차단 (스크립트 좌표에 고정)
# False: 물리 활성화
KINEMATIC = True

_node             = None
_latest           = {}
_kinematic_set    = False


def _set_kinematic(stage):
    """Play 시작 시 1회 — 모든 Spot prim을 Kinematic으로 설정."""
    for path in PRIM_MAP.values():
        prim = stage.GetPrimAtPath(path)
        if not prim.IsValid():
            continue
        _apply_recursive(prim)
    print(f"[robot_localization] Kinematic {'활성화' if KINEMATIC else '비활성화'} 완료")


def _apply_recursive(prim):
    if prim.HasAPI('PhysxRigidBodyAPI') or prim.HasAPI('PhysicsRigidBodyAPI'):
        for attr_name in ['physics:kinematicEnabled', 'physxRigidBody:kinematicEnabled']:
            attr = prim.GetAttribute(attr_name)
            if attr:
                attr.Set(KINEMATIC)
    for child in prim.GetAllChildren():
        _apply_recursive(child)


def compute(db):
    global _node, _kinematic_set

    if _node is None:
        print("[robot_localization] node 초기화 시작")
        try:
            from robot_interfaces.msg import RobotLocalization

            if not rclpy.ok():
                rclpy.init()
            _node = rclpy.create_node('isaac_localization_sub')

            def callback(msg):
                print(f"[robot_localization] 수신 robot_id:{msg.robot_id} x:{msg.x} y:{msg.y}")
                _latest[msg.robot_id] = msg

            _node.create_subscription(
                RobotLocalization,
                '/localization/robot/state',
                callback,
                10
            )
            print("[robot_localization] subscriber 생성 완료")
        except Exception as e:
            print(f"[robot_localization] 초기화 실패: {e}")
            return True

    rclpy.spin_once(_node, timeout_sec=0)

    stage = omni.usd.get_context().get_stage()

    if not _kinematic_set:
        _set_kinematic(stage)
        _kinematic_set = True

    for robot_id, msg in _latest.items():
        path = PRIM_MAP.get(robot_id)
        if path is None:
            continue

        prim = stage.GetPrimAtPath(path)
        if not prim.IsValid():
            print(f"[robot_localization] prim 없음: {path}")
            continue

        translate_attr = prim.GetAttribute('xformOp:translate')
        if translate_attr:
            translate_attr.Set(Gf.Vec3d(float(msg.x), float(msg.y), Z_HEIGHT))

        orient_attr = prim.GetAttribute('xformOp:orient')
        if orient_attr:
            half = msg.yaw / 2.0
            orient_attr.Set(Gf.Quatd(math.cos(half), 0.0, 0.0, math.sin(half)))

    return True
