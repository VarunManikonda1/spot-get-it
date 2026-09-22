"""
SpotMicro IK & Trot Gait Generator
URDF 기반 역기구학 + trot 보행 궤적 생성

사용법:
  1. 단독 실행: python spotmicro_gait.py  → FK 검증 + 궤적 시각화
  2. legged_gym 통합: spotmicro_test.py에서 import하여 사용

URDF 관절 구조:
  - shoulder: 좌(1,0,0) / 우(-1,0,0) → roll (abduction)
  - leg:      전부 (0,-1,0)          → pitch (hip flexion)
  - foot:     전부 (0,-1,0)          → pitch (knee flexion)

기구학 파라미터 (URDF에서 추출):
  - shoulder → leg joint offset:  (0, ±0.052, 0)
  - leg → foot joint offset:     (0.010, 0, -0.120)
  - foot → toe offset (fixed):   (0, 0, -0.115)    → L2 = 0.115m
"""

import numpy as np
import math


# ============================================================
# URDF 기구학 상수
# ============================================================
L1_X = 0.010     # upper leg X offset (forward lean)
L1_Z = 0.120     # upper leg Z offset (downward)
L2   = 0.115     # lower leg length (foot → toe)

L1_EFF = math.sqrt(L1_X**2 + L1_Z**2)
ALPHA  = math.atan2(L1_X, L1_Z)         # upper leg offset angle

# 관절 한계 (URDF)
JOINT_LIMITS = {
    'shoulder': (-0.548, 0.548),
    'leg':      (-2.666, 1.548),
    'foot':     (-0.100, 2.590),
}

# default standing angles
DEFAULT_ANGLES = {
    'shoulder': 0.0,
    'leg':     -0.9921237899157832,
    'foot':     1.4907337340120823,
}

# 다리 배치 (base_link 기준)
LEG_ORIGINS = {
    'FL': {'x':  0.093, 'y':  0.036, 'shoulder_axis': 1},
    'FR': {'x':  0.093, 'y': -0.036, 'shoulder_axis': -1},
    'RL': {'x': -0.093, 'y':  0.036, 'shoulder_axis': 1},
    'RR': {'x': -0.093, 'y': -0.036, 'shoulder_axis': -1},
}

# legged_gym 관절 순서 (config의 default_joint_angles 순서)
JOINT_ORDER = [
    'front_left_shoulder', 'front_left_leg', 'front_left_foot',
    'front_right_shoulder', 'front_right_leg', 'front_right_foot',
    'rear_left_shoulder', 'rear_left_leg', 'rear_left_foot',
    'rear_right_shoulder', 'rear_right_leg', 'rear_right_foot',
]

# 다리 이름 → 인덱스 매핑
LEG_INDEX = {'FL': 0, 'FR': 1, 'RL': 2, 'RR': 3}


# ============================================================
# Forward Kinematics (검증용)
# ============================================================
def fk_leg(theta_leg, theta_foot):
    """
    순기구학: 관절 각도 → 발끝(toe) 위치 (hip 기준, XZ 평면)
    
    Args:
        theta_leg: leg joint angle (rad)
        theta_foot: foot joint angle (rad)
    
    Returns:
        (toe_x, toe_z): hip 기준 toe 위치 (m)
    """
    # knee position (foot joint)
    knee_x = L1_X * math.cos(theta_leg) + L1_Z * math.sin(theta_leg)
    knee_z = L1_X * math.sin(theta_leg) - L1_Z * math.cos(theta_leg)
    
    # toe position
    theta_total = theta_leg + theta_foot
    toe_x = knee_x + L2 * math.sin(theta_total)
    toe_z = knee_z - L2 * math.cos(theta_total)
    
    return toe_x, toe_z


# ============================================================
# Inverse Kinematics
# ============================================================
def ik_leg(x_d, z_d):
    """
    역기구학: 발끝 위치 → 관절 각도 (hip 기준, XZ 평면)
    
    Args:
        x_d: 목표 toe X 위치 (hip 기준, +X = forward) (m)
        z_d: 목표 toe Z 위치 (hip 기준, -Z = downward) (m)
    
    Returns:
        (theta_leg, theta_foot): 관절 각도 (rad)
        None: 도달 불가능한 위치
    """
    d = math.sqrt(x_d**2 + z_d**2)
    
    # 도달 가능 범위 체크
    if d > L1_EFF + L2 - 0.001:
        return None  # 너무 멀리
    if d < abs(L1_EFF - L2) + 0.001:
        return None  # 너무 가까이
    
    # 표준 2-link IK
    cos_q2 = (d**2 - L1_EFF**2 - L2**2) / (2 * L1_EFF * L2)
    cos_q2 = np.clip(cos_q2, -1.0, 1.0)
    q2 = math.acos(cos_q2)  # internal knee angle (elbow-up)
    
    beta = math.atan2(x_d, -z_d)  # angle to target from -Z
    alpha_k = math.atan2(L2 * math.sin(q2), L1_EFF + L2 * math.cos(q2))
    q1 = beta - alpha_k
    
    # URDF 관절 각도로 변환
    theta_leg = q1 - ALPHA
    theta_foot = q2 + ALPHA
    
    # 관절 한계 체크
    if not (JOINT_LIMITS['leg'][0] <= theta_leg <= JOINT_LIMITS['leg'][1]):
        return None
    if not (JOINT_LIMITS['foot'][0] <= theta_foot <= JOINT_LIMITS['foot'][1]):
        return None
    
    return theta_leg, theta_foot


# ============================================================
# Trot Gait Trajectory Generator
# ============================================================
class TrotGaitGenerator:
    """
    Trot 보행 궤적 생성기
    
    Trot: FL-RR이 한 쌍, FR-RL이 한 쌍
          두 쌍이 반위상(0.5 phase offset)으로 번갈아 swing
    """
    
    def __init__(
        self,
        gait_period=1.2,       # 한 주기 (초)
        duty_factor=0.58,      # stance 비율
        stride_length=0.04,    # 전후 보폭 (m) — 한 방향 기준
        step_height=0.025,     # swing 시 발 높이 (m)
        body_height=0.190,     # 기본 서 있는 높이 (hip→toe contact, m)
        x_offset=-0.040,       # 발끝 전후 오프셋 (m)
    ):
        self.gait_period = gait_period
        self.duty_factor = duty_factor
        self.stride_length = stride_length
        self.step_height = step_height
        self.body_height = body_height
        self.x_offset = x_offset
        
        # trot phase offset: FL-RR = 0, FR-RL = 0.5
        self.phase_offsets = {
            'FL': 0.0,
            'FR': 0.5,
            'RL': 0.5,
            'RR': 0.0,
        }
    
    def get_foot_position(self, leg_name, t):
        """
        시간 t에서의 발끝 위치 (hip 기준)
        
        Args:
            leg_name: 'FL', 'FR', 'RL', 'RR'
            t: 시간 (초)
        
        Returns:
            (x, z): hip 기준 발끝 위치
        """
        # 현재 phase (0~1)
        raw_phase = (t / self.gait_period) + self.phase_offsets[leg_name]
        phase = raw_phase % 1.0
        
        x0 = self.x_offset
        z0 = -self.body_height
        
        if phase < self.duty_factor:
            # === STANCE phase: 발이 바닥에서 뒤로 밀기 ===
            t_stance = phase / self.duty_factor  # 0→1
            x = x0 + self.stride_length * (0.5 - t_stance)
            z = z0
        else:
            # === SWING phase: 발을 들어서 앞으로 ===
            t_swing = (phase - self.duty_factor) / (1.0 - self.duty_factor)  # 0→1
            x = x0 + self.stride_length * (-0.5 + t_swing)
            z = z0 + self.step_height * math.sin(math.pi * t_swing)
        
        return x, z
    
    def get_joint_angles(self, leg_name, t):
        """
        시간 t에서의 관절 각도
        
        Args:
            leg_name: 'FL', 'FR', 'RL', 'RR'
            t: 시간 (초)
        
        Returns:
            (shoulder, leg, foot): 관절 각도 (rad)
            None: IK 실패 시
        """
        x, z = self.get_foot_position(leg_name, t)
        result = ik_leg(x, z)
        
        if result is None:
            # IK 실패 시 default 반환
            return (DEFAULT_ANGLES['shoulder'],
                    DEFAULT_ANGLES['leg'],
                    DEFAULT_ANGLES['foot'])
        
        theta_leg, theta_foot = result
        return (0.0, theta_leg, theta_foot)  # shoulder = 0
    
    def get_all_joint_angles(self, t):
        """
        시간 t에서의 전체 12 관절 각도 (legged_gym 순서)
        
        Returns:
            list[12]: [FL_s, FL_l, FL_f, FR_s, FR_l, FR_f, 
                       RL_s, RL_l, RL_f, RR_s, RR_l, RR_f]
        """
        angles = []
        for leg_name in ['FL', 'FR', 'RL', 'RR']:
            s, l, f = self.get_joint_angles(leg_name, t)
            angles.extend([s, l, f])
        return angles
    
    def generate_trajectory(self, dt=0.02, num_cycles=1):
        """
        전체 궤적 생성
        
        Args:
            dt: 시간 간격 (0.02 = 50Hz, legged_gym 정책 주기)
            num_cycles: 생성할 주기 수
        
        Returns:
            np.array: shape (N, 12) — 시간 × 관절 각도
        """
        total_time = self.gait_period * num_cycles
        steps = int(total_time / dt)
        trajectory = np.zeros((steps, 12))
        
        for i in range(steps):
            t = i * dt
            trajectory[i] = self.get_all_joint_angles(t)
        
        return trajectory


# ============================================================
# 검증 & 시각화
# ============================================================
def verify_ik():
    """FK-IK 라운드트립 검증"""
    print("=" * 60)
    print("  FK-IK 라운드트립 검증")
    print("=" * 60)
    
    # 1. default 각도로 검증
    theta_leg_def = DEFAULT_ANGLES['leg']
    theta_foot_def = DEFAULT_ANGLES['foot']
    
    toe_x, toe_z = fk_leg(theta_leg_def, theta_foot_def)
    print(f"\n  Default angles: leg={theta_leg_def}, foot={theta_foot_def}")
    print(f"  FK → toe position: x={toe_x:.4f}m, z={toe_z:.4f}m")
    
    result = ik_leg(toe_x, toe_z)
    if result:
        ik_leg_angle, ik_foot_angle = result
        print(f"  IK → leg={ik_leg_angle:.4f}, foot={ik_foot_angle:.4f}")
        print(f"  오차: leg={abs(ik_leg_angle - theta_leg_def):.6f}, "
              f"foot={abs(ik_foot_angle - theta_foot_def):.6f}")
    
    # 2. 여러 위치에서 검증
    print(f"\n  다양한 위치 검증:")
    test_positions = [
        (0.0, -0.20),    # 중립 아래
        (0.03, -0.19),   # 앞으로 + 약간 위
        (-0.03, -0.19),  # 뒤로 + 약간 위
        (0.0, -0.17),    # 높이 들기
        (0.0, -0.22),    # 낮게
    ]
    
    for x_d, z_d in test_positions:
        result = ik_leg(x_d, z_d)
        if result:
            theta_l, theta_f = result
            # FK로 역검증
            fx, fz = fk_leg(theta_l, theta_f)
            err = math.sqrt((fx - x_d)**2 + (fz - z_d)**2)
            status = "✓" if err < 0.001 else "✗"
            print(f"  {status} target=({x_d:+.3f}, {z_d:+.3f}) → "
                  f"leg={theta_l:+.3f}, foot={theta_f:.3f} → "
                  f"FK=({fx:+.4f}, {fz:+.4f}), err={err:.6f}m")
        else:
            print(f"  ✗ target=({x_d:+.3f}, {z_d:+.3f}) → IK 실패 (도달 불가)")


def verify_gait():
    """Trot 궤적 검증"""
    print("\n" + "=" * 60)
    print("  Trot Gait 궤적 검증")
    print("=" * 60)
    
    gait = TrotGaitGenerator(
        gait_period=0.6,
        step_height=0.03,
        stride_length=0.04,
    )
    
    # 한 주기 동안 발 위치 출력
    print(f"\n  파라미터: period={gait.gait_period}s, "
          f"height={gait.step_height}m, stride={gait.stride_length}m")
    print(f"\n  {'Time':>6s}  {'FL_x':>7s} {'FL_z':>7s}  "
          f"{'FR_x':>7s} {'FR_z':>7s}  Phase")
    print(f"  {'-'*55}")
    
    dt = 0.05
    steps = int(gait.gait_period / dt)
    for i in range(steps + 1):
        t = i * dt
        phase_fl = (t / gait.gait_period) % 1.0
        phase_fr = (t / gait.gait_period + 0.5) % 1.0
        
        fl_x, fl_z = gait.get_foot_position('FL', t)
        fr_x, fr_z = gait.get_foot_position('FR', t)
        
        fl_state = "swing" if phase_fl >= gait.duty_factor else "stance"
        fr_state = "swing" if phase_fr >= gait.duty_factor else "stance"
        
        print(f"  {t:6.2f}s  {fl_x:+7.4f} {fl_z:+7.4f}  "
              f"{fr_x:+7.4f} {fr_z:+7.4f}  FL:{fl_state} FR:{fr_state}")
    
    # 관절 한계 체크
    print(f"\n  관절 한계 체크:")
    trajectory = gait.generate_trajectory(dt=0.002, num_cycles=1)
    joint_names = ['shoulder', 'leg', 'foot']
    
    for leg_idx, leg_name in enumerate(['FL', 'FR', 'RL', 'RR']):
        for j_idx, j_name in enumerate(joint_names):
            col = trajectory[:, leg_idx * 3 + j_idx]
            lo, hi = JOINT_LIMITS[j_name]
            min_val, max_val = col.min(), col.max()
            in_range = lo <= min_val and max_val <= hi
            status = "✓" if in_range else "✗ LIMIT!"
            print(f"  {status} {leg_name}_{j_name}: "
                  f"[{min_val:+.3f}, {max_val:+.3f}] "
                  f"(limit: [{lo:+.3f}, {hi:+.3f}])")
    
    # 대칭성 체크
    print(f"\n  대칭성 체크:")
    t_test = np.arange(0, gait.gait_period, 0.01)
    fl_air = sum(1 for t in t_test 
                 if gait.get_foot_position('FL', t)[1] > -gait.body_height + 0.001)
    fr_air = sum(1 for t in t_test 
                 if gait.get_foot_position('FR', t)[1] > -gait.body_height + 0.001)
    rl_air = sum(1 for t in t_test 
                 if gait.get_foot_position('RL', t)[1] > -gait.body_height + 0.001)
    rr_air = sum(1 for t in t_test 
                 if gait.get_foot_position('RR', t)[1] > -gait.body_height + 0.001)
    total = len(t_test)
    print(f"  FL 공중: {fl_air/total*100:.1f}%  FR 공중: {fr_air/total*100:.1f}%")
    print(f"  RL 공중: {rl_air/total*100:.1f}%  RR 공중: {rr_air/total*100:.1f}%")
    print(f"  FL-RR 대칭: {'✓' if fl_air == rr_air else '✗'}")
    print(f"  FR-RL 대칭: {'✓' if fr_air == rl_air else '✗'}")


def plot_trajectory():
    """궤적 시각화 (matplotlib 있을 때만)"""
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("\n  matplotlib 없음 → 시각화 건너뜀")
        return
    
    gait = TrotGaitGenerator(
        gait_period=0.6,
        step_height=0.03,
        stride_length=0.04,
    )
    
    dt = 0.002
    times = np.arange(0, gait.gait_period * 2, dt)
    
    fig, axes = plt.subplots(3, 1, figsize=(14, 10))
    fig.suptitle('SpotMicro Trot Gait Trajectory', fontsize=14)
    
    # 1. 발끝 XZ 궤적
    ax = axes[0]
    for leg_name, color in [('FL', 'blue'), ('FR', 'red'), ('RL', 'green'), ('RR', 'orange')]:
        xs = [gait.get_foot_position(leg_name, t)[0] for t in times]
        zs = [gait.get_foot_position(leg_name, t)[1] for t in times]
        ax.plot(xs, zs, color=color, label=leg_name, alpha=0.7)
    ax.set_xlabel('X (m)')
    ax.set_ylabel('Z (m)')
    ax.set_title('Foot Trajectory (hip frame)')
    ax.legend()
    ax.set_aspect('equal')
    ax.grid(True)
    
    # 2. 관절 각도 over time
    trajectory = gait.generate_trajectory(dt=dt, num_cycles=2)
    ax = axes[1]
    t_axis = np.arange(len(trajectory)) * dt
    for leg_idx, (leg_name, color) in enumerate(
            [('FL', 'blue'), ('FR', 'red'), ('RL', 'green'), ('RR', 'orange')]):
        ax.plot(t_axis, np.degrees(trajectory[:, leg_idx*3+1]), 
                color=color, linestyle='-', label=f'{leg_name}_leg', alpha=0.7)
        ax.plot(t_axis, np.degrees(trajectory[:, leg_idx*3+2]), 
                color=color, linestyle='--', label=f'{leg_name}_foot', alpha=0.5)
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Angle (deg)')
    ax.set_title('Joint Angles over Time')
    ax.legend(ncol=4, fontsize=7)
    ax.grid(True)
    
    # 3. 접촉 패턴
    ax = axes[2]
    for leg_idx, (leg_name, color) in enumerate(
            [('FL', 'blue'), ('FR', 'red'), ('RL', 'green'), ('RR', 'orange')]):
        contacts = []
        for t in times:
            _, z = gait.get_foot_position(leg_name, t)
            contacts.append(0 if z > -gait.body_height + 0.001 else 1)
        ax.fill_between(times, leg_idx, [leg_idx + c * 0.8 for c in contacts], 
                        color=color, alpha=0.5, label=leg_name)
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Leg')
    ax.set_yticks([0.4, 1.4, 2.4, 3.4])
    ax.set_yticklabels(['FL', 'FR', 'RL', 'RR'])
    ax.set_title('Contact Pattern (filled = stance)')
    ax.grid(True, axis='x')
    
    plt.tight_layout()
    plt.savefig('trot_trajectory.png', dpi=150)
    print(f"\n  궤적 그래프 저장: trot_trajectory.png")


if __name__ == '__main__':
    verify_ik()
    verify_gait()
    plot_trajectory()
    
    print("\n" + "=" * 60)
    print("  사용 예시 (legged_gym 통합)")
    print("=" * 60)
    print("""
  from spotmicro_gait import TrotGaitGenerator, ik_leg
  
  gait = TrotGaitGenerator(
      gait_period=0.6,
      step_height=0.03,     # 3cm 들기
      stride_length=0.04,   # 4cm 보폭
  )
  
  # 시간 t에서 전체 12관절 각도
  angles = gait.get_all_joint_angles(t=0.3)
  
  # legged_gym reward에서 사용:
  # ref_angles = gait.get_all_joint_angles(current_time)
  # error = sum((dof_pos - ref_angles)^2)
  # reward = exp(-error / sigma)
    """)
