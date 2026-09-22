#!/usr/bin/env python3
"""
SPI trot walking test — Jetson에서 STM32로 trot 보행 명령 전송.
firmware/Src/gait.c + leg_ik.c 와 동일한 궤적을 Python으로 포팅.

사용법 (Jetson):
  sudo python3 spi_trot_test.py
  sudo python3 spi_trot_test.py --cycles 5 --stride 50 --lift 10

주의:
  - 첫 테스트는 로봇을 손으로 잡고 (IN_HAND_MODE=1 권장)
  - Ctrl+C 로 언제든 IDLE (torque OFF) 복귀
"""

import struct
import time
import sys
import math
import argparse

try:
    import spidev
except ImportError:
    print("spidev 패키지 필요: pip install spidev")
    sys.exit(1)

# ─── 프로토콜 상수 (spi_dummy_test.py 와 동일) ───
COMMAND_MAGIC  = 0xA55A
FEEDBACK_MAGIC = 0x5AA5
NUM_JOINTS     = 12
SPI_FRAME_SIZE = 261
MOSI_PAYLOAD_SIZE = 116

MODE_IDLE     = 0
MODE_POSITION = 1
MODE_HOLD     = 3

FLAG_TORQUE_EN = 0x01

# ─── 로봇 기구학 파라미터 (firmware/Inc/config.h) ───
L1_MM = 105.0
L2_MM = 130.0
BODY_HEIGHT_MM = 170.0
DEFAULT_FOOT_X = -10.0
DEFAULT_FOOT_Z = -BODY_HEIGHT_MM

# Trot phase offset: [FL, FR, RL, RR]
# Pair A (FR+RL) = 0.0,  Pair B (FL+RR) = 0.5
TROT_PHASE_OFFSET = [0.5, 0.0, 0.0, 0.5]
LEG_NAMES = ["FL", "FR", "RL", "RR"]


# ═══════════════════════════════════════════
#  CRC / packet  (spi_dummy_test.py 와 동일)
# ═══════════════════════════════════════════

def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if (crc & 0x8000) else (crc << 1)
            crc &= 0xFFFF
    return crc


def build_mosi(seq, mode, flags, target_rad, max_delta_rad,
               gait_phase=0.0, gait_cycle_count=0):
    buf = bytearray()
    buf += struct.pack('<HHI', COMMAND_MAGIC, seq,
                       int(time.time() * 1e6) & 0xFFFFFFFF)
    buf += struct.pack('<BB', mode, flags)
    for v in target_rad:
        buf += struct.pack('<f', v)
    for v in max_delta_rad:
        buf += struct.pack('<f', v)
    buf += struct.pack('<fI', gait_phase, gait_cycle_count)
    crc = crc16_ccitt_false(bytes(buf))
    buf += struct.pack('<H', crc)
    assert len(buf) == MOSI_PAYLOAD_SIZE
    buf += b'\x00' * (SPI_FRAME_SIZE - MOSI_PAYLOAD_SIZE)
    return bytes(buf)


def parse_miso(data: bytes):
    if len(data) < SPI_FRAME_SIZE:
        return None
    magic = struct.unpack_from('<H', data, 0)[0]
    if magic != FEEDBACK_MAGIC:
        return {'error': f'BAD_MAGIC: 0x{magic:04X}'}
    calc_crc = crc16_ccitt_false(data[:259])
    recv_crc = struct.unpack_from('<H', data, 259)[0]
    if calc_crc != recv_crc:
        return {'error': f'BAD_CRC'}
    off = 2
    seq_echo, ts_us = struct.unpack_from('<HI', data, off); off += 6
    status, fault, motion_state = struct.unpack_from('<BBB', data, off); off += 3
    gait_phase, gait_cycle = struct.unpack_from('<fI', data, off); off += 8
    imu_yaw_rad = struct.unpack_from('<f', data, off)[0]; off += 4
    pos = struct.unpack_from('<12f', data, off); off += 48
    vel = struct.unpack_from('<12f', data, off); off += 48
    off += 48  # load
    off += 48  # temp
    gyro = struct.unpack_from('<3f', data, off); off += 12
    accel = struct.unpack_from('<3f', data, off); off += 12
    quat = struct.unpack_from('<4f', data, off); off += 16
    vbus = struct.unpack_from('<f', data, off)[0]
    return {
        'seq_echo': seq_echo, 'status': status, 'fault': fault,
        'motion_state': motion_state, 'pos': list(pos),
        'gyro': list(gyro), 'quat': list(quat), 'bus_voltage': vbus,
        'gait_phase': gait_phase, 'gait_cycle': gait_cycle,
    }


# ═══════════════════════════════════════════
#  IK — firmware/Src/leg_ik.c 포팅
# ═══════════════════════════════════════════

def ik_2link(foot_x_mm, foot_z_mm):
    """
    2-link planar IK.
    foot_z_mm 은 음수 (발이 hip 아래).
    Returns (theta_thigh_rad, theta_knee_rad).
    """
    r2 = foot_x_mm ** 2 + foot_z_mm ** 2
    cos_k = (r2 - L1_MM ** 2 - L2_MM ** 2) / (2.0 * L1_MM * L2_MM)
    cos_k = max(-1.0, min(1.0, cos_k))
    sin_k = math.sqrt(1.0 - cos_k * cos_k)
    theta_k = math.atan2(sin_k, cos_k)

    A = L1_MM + L2_MM * cos_k
    B = L2_MM * sin_k
    det = A * A + B * B
    foot_z_neg = -foot_z_mm
    sin_t = (A * foot_x_mm - B * foot_z_neg) / det
    cos_t = (B * foot_x_mm + A * foot_z_neg) / det
    theta_t = math.atan2(sin_t, cos_t)

    return theta_t, theta_k


# ═══════════════════════════════════════════
#  Gait — firmware/Src/gait.c 포팅
# ═══════════════════════════════════════════

def smoothstep(t):
    return t * t * (3.0 - 2.0 * t)


def compute_trot_offset(leg_phase, stride_x, lift_z, duty_factor):
    """Stance: 선형 sweep, Swing: Bezier arch + smoothstep."""
    if leg_phase < duty_factor:
        s = leg_phase / duty_factor
        fx_off = stride_x * 0.5 - s * stride_x
        fz_off = 0.0
    else:
        s = (leg_phase - duty_factor) / (1.0 - duty_factor)
        s = smoothstep(s)
        oms = 1.0 - s
        B0 = oms ** 3
        B1 = 3.0 * oms * oms * s
        B2 = 3.0 * oms * s * s
        B3 = s ** 3

        xP0 = -stride_x * 0.5
        xP3 = stride_x * 0.5
        z_ctrl = (4.0 / 3.0) * lift_z

        fx_off = B0 * xP0 + B3 * xP3       # B1*0 + B2*0
        fz_off = B1 * z_ctrl + B2 * z_ctrl  # B0*0 + B3*0

    return fx_off, fz_off


def compute_trot_targets(phase, stride_x, lift_z, duty_factor):
    """
    phase [0, 1) → target_rad[12] 계산.
    Joint order: [FL_s, FL_t, FL_k, FR_s, FR_t, FR_k, RL_s, RL_t, RL_k, RR_s, RR_t, RR_k]
    """
    target = [0.0] * NUM_JOINTS

    for leg in range(4):
        lp = phase + TROT_PHASE_OFFSET[leg]
        if lp >= 1.0:
            lp -= 1.0

        fx_off, fz_off = compute_trot_offset(lp, stride_x, lift_z, duty_factor)
        foot_x = DEFAULT_FOOT_X + fx_off
        foot_z = DEFAULT_FOOT_Z + fz_off   # fz_off > 0 = 발 올라감

        theta_t, theta_k = ik_2link(foot_x, foot_z)

        base = leg * 3
        target[base + 0] = 0.0       # shoulder (hip abduction) = 0
        target[base + 1] = theta_t   # thigh
        target[base + 2] = theta_k   # knee

    return target


def compute_stand_targets():
    """정자세(standing) target_rad[12]."""
    return compute_trot_targets(0.0, 0.0, 0.0, 0.55)


# ═══════════════════════════════════════════
#  메인 루프
# ═══════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(description='SPI Trot Walking Test')
    parser.add_argument('--cycles', type=int, default=3,
                        help='trot 사이클 수 (default: 3)')
    parser.add_argument('--stride', type=float, default=70.0,
                        help='stride X (mm, default: 70)')
    parser.add_argument('--lift', type=float, default=13.0,
                        help='swing lift Z (mm, default: 13)')
    parser.add_argument('--period', type=float, default=1.5,
                        help='gait period (sec, default: 1.5)')
    parser.add_argument('--duty', type=float, default=0.55,
                        help='duty factor (default: 0.55)')
    parser.add_argument('--bus', type=int, default=0,
                        help='SPI bus (default: 0)')
    parser.add_argument('--dev', type=int, default=0,
                        help='SPI device (default: 0)')
    args = parser.parse_args()

    # SPI 설정
    spi = spidev.SpiDev()
    spi.open(args.bus, args.dev)
    spi.max_speed_hz = 5_000_000
    spi.mode = 0
    spi.bits_per_word = 8

    stride_x = args.stride
    lift_z = args.lift
    gait_period = args.period
    duty_factor = args.duty
    n_cycles = args.cycles

    stand_target = compute_stand_targets()

    print(f"=== SPI Trot Walking Test ===")
    print(f"  Cycles: {n_cycles}  Period: {gait_period}s  Duty: {duty_factor}")
    print(f"  Stride: {stride_x}mm  Lift: {lift_z}mm")
    print(f"  Standing pose (thigh, knee): "
          f"({stand_target[1]:.3f}, {stand_target[2]:.3f}) rad")
    print(f"  SPI: spidev{args.bus}.{args.dev} @ 5MHz")
    print()

    # max_delta_rad: 부드러운 움직임 위해 phase별로 다르게
    delta_standup = [0.02] * NUM_JOINTS    # stand-up: 느리게 (1 rad/s)
    delta_walk    = [0.08] * NUM_JOINTS    # trot: 빠르게 (4 rad/s)
    delta_settle  = [0.03] * NUM_JOINTS    # settle: 중간

    TICK = 0.02   # 50Hz = 20ms
    seq = 0

    def send(mode, flags, target, delta, gait_ph=0.0, gait_cyc=0):
        nonlocal seq
        tx = build_mosi(seq, mode, flags, target, delta, gait_ph, gait_cyc)
        rx = bytes(spi.xfer2(list(tx)))
        seq = (seq + 1) & 0xFFFF
        return parse_miso(rx)

    def send_idle():
        return send(MODE_IDLE, 0, [0.0]*12, [0.0]*12)

    try:
        # ── Phase 0: 카운트다운 ──
        print(">>> 3초 후 시작 — 로봇을 잡을 준비! <<<")
        for i in range(3, 0, -1):
            print(f"  {i}...")
            # 카운트다운 중에도 IDLE 전송 (SPI 동기 유지)
            for _ in range(int(1.0 / TICK)):
                send_idle()
                time.sleep(TICK)
        print()

        # ── Phase 1: Stand-up (2초) ──
        print("[Phase 1] Standing up... (2s)")
        t_start = time.time()
        while time.time() - t_start < 2.0:
            fb = send(MODE_POSITION, FLAG_TORQUE_EN,
                      stand_target, delta_standup)
            if fb and 'error' not in fb:
                pass  # 조용히
            time.sleep(TICK)

        # 현재 피드백 출력
        fb = send(MODE_POSITION, FLAG_TORQUE_EN,
                  stand_target, delta_standup)
        if fb and 'error' not in fb:
            print(f"  pos[FL]: [{fb['pos'][0]:.3f}, {fb['pos'][1]:.3f}, {fb['pos'][2]:.3f}]")
            print(f"  Vbus: {fb['bus_voltage']:.1f}V  fault: {fb['fault']}")
        print()

        # ── Phase 2: Trot walking ──
        total_walk_time = n_cycles * gait_period
        print(f"[Phase 2] Trot walking — {n_cycles} cycles ({total_walk_time:.1f}s)")

        t_walk_start = time.time()
        tick_count = 0

        while True:
            t_now = time.time()
            elapsed = t_now - t_walk_start
            if elapsed >= total_walk_time:
                break

            # gait phase 계산
            phase_in_cycle = (elapsed % gait_period) / gait_period
            cycle_count = int(elapsed / gait_period)

            target = compute_trot_targets(phase_in_cycle, stride_x, lift_z,
                                          duty_factor)

            fb = send(MODE_POSITION, FLAG_TORQUE_EN,
                      target, delta_walk,
                      gait_ph=phase_in_cycle,
                      gait_cyc=cycle_count)

            # 0.5초마다 상태 출력
            if tick_count % 25 == 0 and fb and 'error' not in fb:
                print(f"  [cycle {cycle_count+1}/{n_cycles} ph={phase_in_cycle:.2f}] "
                      f"mode={fb['motion_state']} fault={fb['fault']} "
                      f"Vbus={fb['bus_voltage']:.1f}V")

            tick_count += 1
            # 정확한 50Hz 유지
            t_next = t_walk_start + (tick_count * TICK)
            sleep_time = t_next - time.time()
            if sleep_time > 0:
                time.sleep(sleep_time)

        print()

        # ── Phase 3: Settle back to standing (2초) ──
        print("[Phase 3] Settling to stand... (2s)")
        t_start = time.time()
        while time.time() - t_start < 2.0:
            send(MODE_POSITION, FLAG_TORQUE_EN,
                 stand_target, delta_settle)
            time.sleep(TICK)
        print()

        # ── Phase 4: HOLD → IDLE ──
        print("[Phase 4] HOLD (1s) → IDLE")
        t_start = time.time()
        while time.time() - t_start < 1.0:
            send(MODE_HOLD, FLAG_TORQUE_EN, stand_target, delta_settle)
            time.sleep(TICK)

        # IDLE (torque OFF)
        for _ in range(25):
            send_idle()
            time.sleep(TICK)

        print("\n=== Trot test complete! ===")

    except KeyboardInterrupt:
        print("\n\n--- Ctrl+C: 긴급 정지 ---")
        # IDLE 전송 (torque OFF)
        for _ in range(10):
            send_idle()
            time.sleep(TICK)
        print("IDLE 전송 완료 (torque OFF)")

    finally:
        spi.close()


if __name__ == '__main__':
    main()
