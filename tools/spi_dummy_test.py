#!/usr/bin/env python3
"""
SPI dummy test — Jetson에서 STM32로 더미 MOSI 전송, MISO 수신 확인.
ROS2 없이 spidev로 직접 통신.

사용법 (Jetson):
  sudo python3 spi_dummy_test.py

필요 패키지:
  pip install spidev
"""

import struct
import time
import sys

try:
    import spidev
except ImportError:
    print("spidev 패키지 필요: pip install spidev")
    sys.exit(1)

# === 프로토콜 상수 ===
COMMAND_MAGIC  = 0xA55A
FEEDBACK_MAGIC = 0x5AA5
NUM_JOINTS     = 12
SPI_FRAME_SIZE = 261  # MISO 크기에 맞춤

# MOSI payload: 116B
MOSI_PAYLOAD_SIZE = 116

# 모드
MODE_IDLE     = 0
MODE_POSITION = 1
MODE_HOLD     = 3

# flags
FLAG_TORQUE_EN = 0x01


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc = crc << 1
            crc &= 0xFFFF
    return crc


def build_mosi(seq, mode, flags, target_rad, max_delta_rad,
               gait_phase=0.0, gait_cycle_count=0):
    """116B MOSI payload 생성 → 261B 패딩."""
    buf = bytearray()

    # magic(2) + seq(2) + timestamp_us(4) + mode(1) + flags(1)
    buf += struct.pack('<HHI', COMMAND_MAGIC, seq, int(time.time() * 1e6) & 0xFFFFFFFF)
    buf += struct.pack('<BB', mode, flags)

    # target_rad[12] + max_delta_rad[12]
    for v in target_rad:
        buf += struct.pack('<f', v)
    for v in max_delta_rad:
        buf += struct.pack('<f', v)

    # gait_phase(4) + gait_cycle_count(4)
    buf += struct.pack('<fI', gait_phase, gait_cycle_count)

    # CRC over 114 bytes (before crc field)
    crc = crc16_ccitt_false(bytes(buf))
    buf += struct.pack('<H', crc)

    assert len(buf) == MOSI_PAYLOAD_SIZE, f"MOSI payload={len(buf)}, expected {MOSI_PAYLOAD_SIZE}"

    # 261B로 패딩
    buf += b'\x00' * (SPI_FRAME_SIZE - MOSI_PAYLOAD_SIZE)
    return bytes(buf)


def parse_miso(data: bytes):
    """261B MISO 파싱."""
    if len(data) < SPI_FRAME_SIZE:
        return None

    magic = struct.unpack_from('<H', data, 0)[0]
    if magic != FEEDBACK_MAGIC:
        return {'error': f'BAD_MAGIC: 0x{magic:04X}'}

    # CRC 검증 (259B payload + 2B crc)
    calc_crc = crc16_ccitt_false(data[:259])
    recv_crc = struct.unpack_from('<H', data, 259)[0]
    if calc_crc != recv_crc:
        return {'error': f'BAD_CRC: calc=0x{calc_crc:04X}, recv=0x{recv_crc:04X}'}

    off = 2
    seq_echo, ts_us = struct.unpack_from('<HI', data, off); off += 6
    status, fault, motion_state = struct.unpack_from('<BBB', data, off); off += 3
    gait_phase, gait_cycle = struct.unpack_from('<fI', data, off); off += 8
    imu_yaw_rad = struct.unpack_from('<f', data, off)[0]; off += 4

    pos   = struct.unpack_from('<12f', data, off); off += 48
    vel   = struct.unpack_from('<12f', data, off); off += 48
    load  = struct.unpack_from('<12f', data, off); off += 48
    temp  = struct.unpack_from('<12f', data, off); off += 48
    gyro  = struct.unpack_from('<3f', data, off);  off += 12
    accel = struct.unpack_from('<3f', data, off);  off += 12
    quat  = struct.unpack_from('<4f', data, off);  off += 16
    vbus  = struct.unpack_from('<f', data, off)[0]

    return {
        'seq_echo': seq_echo,
        'status': status,
        'fault': fault,
        'motion_state': motion_state,
        'gait_phase': gait_phase,
        'gait_cycle': gait_cycle,
        'imu_yaw_rad': imu_yaw_rad,
        'pos': list(pos),
        'vel': list(vel),
        'temp': list(temp),
        'gyro': list(gyro),
        'accel': list(accel),
        'quat': list(quat),
        'bus_voltage': vbus,
    }


def main():
    # SPI 설정
    spi = spidev.SpiDev()
    spi.open(0, 0)  # /dev/spidev0.0
    spi.max_speed_hz = 5_000_000
    spi.mode = 0
    spi.bits_per_word = 8

    print(f"=== SPI Dummy Test (frame={SPI_FRAME_SIZE}B, 5MHz) ===\n")

    # default standing pose
    default_target = [
        0.0, -0.6, 1.1,   # FL
        0.0, -0.6, 1.1,   # FR
        0.0, -0.6, 1.1,   # RL
        0.0, -0.6, 1.1,   # RR
    ]
    default_delta = [0.02] * NUM_JOINTS

    seq = 0
    try:
        while True:
            # --- Test 1: IDLE (torque OFF) ---
            if seq < 5:
                mode = MODE_IDLE
                flags = 0
                label = "IDLE (torque OFF)"

            # --- Test 2: POSITION (torque ON) ---
            elif seq < 50:
                mode = MODE_POSITION
                flags = FLAG_TORQUE_EN
                label = "POSITION (torque ON)"

            # --- Test 3: HOLD ---
            else:
                mode = MODE_HOLD
                flags = FLAG_TORQUE_EN
                label = "HOLD"

            tx = build_mosi(seq, mode, flags, default_target, default_delta,
                            gait_phase=0.0, gait_cycle_count=seq // 25)

            rx = bytes(spi.xfer2(list(tx)))

            fb = parse_miso(rx)

            if seq % 10 == 0:
                print(f"[seq={seq:3d}] TX: {label}")
                if fb and 'error' not in fb:
                    print(f"  RX: echo={fb['seq_echo']} mode={fb['motion_state']} "
                          f"fault={fb['fault']} torque_st=0x{fb['status']:02X}")
                    print(f"      pos[0:3]={fb['pos'][:3]}")
                    print(f"      gyro={fb['gyro']}  yaw={fb['imu_yaw_rad']:.3f} rad")
                    print(f"      quat={fb['quat']}  Vbus={fb['bus_voltage']:.1f}V")
                    print()
                elif fb:
                    print(f"  RX ERROR: {fb['error']}\n")

            seq = (seq + 1) & 0xFFFF
            time.sleep(0.02)  # 50Hz

    except KeyboardInterrupt:
        print("\n--- Ctrl+C: 종료 ---")
        # IDLE로 복귀
        tx = build_mosi(seq, MODE_IDLE, 0, [0]*12, [0]*12)
        spi.xfer2(list(tx))
        print("IDLE 전송 완료")

    finally:
        spi.close()


if __name__ == '__main__':
    main()
