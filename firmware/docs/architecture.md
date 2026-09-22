# SPOT_GET_IT — STM32 펌웨어 아키텍처

> 4족 보행 로봇의 STM32F446RE 펌웨어. Jetson Orin Nano (RL/Vision) 의 50Hz 명령을 받아 12개 STS3215 서보를 구동하고, BNO055 IMU로 자세를 모니터링한다.

---

## 1. 시스템 전체 구성

```
┌────────────────────┐       UART 921600, 50Hz    ┌──────────────────┐
│  Jetson Orin Nano  │ ─── command (117B) ──────▶ │  STM32F446RE     │
│  (High-level)      │ ◀── feedback (261B) ────── │  (Low-level)     │
│  - RL policy       │                            │  - control loop  │
│  - Vision          │                            │  - safety        │
│  - ROS2            │                            │  - servo I/O     │
└────────────────────┘                            └────┬─────────────┘
                                                       │
                                  ┌────────────────────┼─────────────────┐
                                  │                    │                 │
                              ┌───▼───┐          ┌─────▼──────┐    ┌─────▼─────┐
                              │ BNO055│          │ STS3215 ×12│    │ ADC (Vbus)│
                              │ IMU   │          │ (4 UART)   │    │           │
                              │ I2C1  │          │ huart3/4/5/6│   │           │
                              └───────┘          └────────────┘    └───────────┘
```

**역할 분리**:
- **Jetson (high-level)**: RL 정책 추론, vision SLAM, 경로 계획. 결정론적 50Hz 명령 송신.
- **STM32 (low-level)**: 결정론적 50Hz 제어 루프. 서보 직접 구동, IMU 직접 read, 안전 fallback.

이렇게 분리하는 이유:
- Jetson은 비-RT Linux라 실시간 보장 X → 고수준 추론에 적합
- STM32는 bare metal에서 50Hz 정확히 유지 → 안전한 fallback 가능
- Jetson 죽어도 STM32가 stale 검출 → HOLD 모드로 자세 유지

---

## 2. 펌웨어 모듈 의존성

```
                   ┌────────────────────────────────┐
                   │  main.c (boot, mode select)   │
                   └─────────┬──────────────────────┘
                             │
              ┌──────────────┼──────────────┐
              ▼              ▼              ▼
     ┌──────────────┐  ┌──────────┐  ┌──────────────┐
     │ control_loop │  │  gait    │  │  calibration │
     │ (50Hz, RL)   │  │ (trot)   │  │  (raw 출력)  │
     └──────┬───────┘  └────┬─────┘  └──────┬───────┘
            │               │               │
            ├───────────────┼───────────────┤
            ▼               ▼               ▼
     ┌─────────────────────────────────────────────┐
     │  joint_control (rad → raw, slew limit, NaN) │
     │  telemetry    (servo + IMU 50Hz read)       │
     │  spi_protocol (encode/decode + CRC16)       │
     └─────────┬─────────────────────┬─────────────┘
               │                     │
               ▼                     ▼
     ┌────────────────┐    ┌─────────────────┐
     │ robot.c        │    │ robot_state.c   │
     │ (legs[], pose) │    │ (g_robot_state) │
     └────────┬───────┘    └─────────────────┘
              │
              ▼
     ┌─────────────────────────────────────────────┐
     │  HAL drivers                                │
     │  - servo_sts3215 (UART half-duplex)         │
     │  - imu_bno055    (I2C IMUPLUS mode)         │
     │  - leg_ik        (2-link planar IK)         │
     │  - spi.c         (SPI1 slave + DMA)         │
     │  - system_hal    (clock, GPIO, UART init)   │
     └─────────────────────────────────────────────┘
```

**상위가 하위 참조 OK, 역참조 금지** (단방향 의존성).

---

## 3. 50Hz 제어 루프 (control_loop.c)

```c
while (1) {
    t_start = HAL_GetTick();

    if (check_esc()) emergency_stop();         // ESC → torque OFF + halt

    // ① UART RX 처리 (DMA circular buffer)
    uart_jetson_process_rx();                   // CRC 검증 + g_robot_state 갱신

    // ② 명령 stale 검사 (Jetson 죽음 감지)
    check_stale(t_start);                       // age > 200ms → 강제 HOLD

    // ③ Telemetry (서보 12ch + IMU read)
    telemetry_update_all();                     // ~5ms 소요

    // ④ Safety (자세/온도/전압)
    check_safety();                             // pitch/roll 초과 → torque OFF

    // ⑤ Mode dispatch
    dispatch_mode();                            // IDLE/POSITION/HOLD/CALIB

    // ⑥ Feedback encode + UART TX DMA
    spi_encode_feedback(tx_buffer);
    uart_jetson_transmit_dma(tx_buffer, MISO_PAYLOAD_SIZE);

    // ⑦ 디버그 print (1초마다)
    if (t_start - last_print >= 1000) printf(...);

    // ⑧ 주기 유지
    next_tick += 20; HAL_Delay(next_tick - HAL_GetTick());
}
```

**핵심 설계 결정**:
- DMA 더블버퍼: SPI 전송이 CPU 차단 안 함 → 제어 루프와 동시 실행
- DATA_READY GPIO: master(Jetson)가 폴링하지 않고 인터럽트로 동기화 가능
- Period 유지는 `HAL_Delay`로 단순화 (RTOS 없음)

---

## 4. SPI 프로토콜

### 4.1 프레임 포맷

**MOSI (Jetson → STM32, UART 117 B payload, padding 없음)**:

| Offset | Size | Field | 비고 |
|--------|------|-------|------|
| 0  | 2 | magic = 0xA55A | LE |
| 2  | 2 | seq | rolling counter |
| 4  | 4 | timestamp_us | |
| 8  | 1 | mode | IDLE/POSITION/HOLD/CALIB |
| 9  | 1 | flags | bit0 = TORQUE_EN, bit1 = E_STOP |
| 10 | 48 | target_rad[12] | f32 LE × 12 |
| 58 | 48 | max_delta_rad[12] | slew rate limit |
| 106| 4  | gait_phase | f32 (echo용) |
| 110| 4  | gait_cycle_count | u32 (echo용) |
| 114| 1  | motion_state | u8 (echo용) |
| 115| 2  | crc16 | CCITT-FALSE, payload 115B |

**MISO (STM32 → Jetson, 261 B)**:

| Offset | Size | Field |
|--------|------|-------|
| 0   | 2  | magic = 0x5AA5 |
| 2   | 2  | seq_echo |
| 4   | 4  | timestamp_us |
| 8   | 1  | status (bitmask) |
| 9   | 1  | fault_code |
| 10  | 1  | motion_state |
| 11  | 4  | gait_phase echo |
| 15  | 4  | gait_cycle echo |
| 19  | 4  | imu_yaw_rad |
| 23  | 48 | position_rad[12] |
| 71  | 48 | velocity_rad_s[12] |
| 119 | 48 | load[12] |
| 167 | 48 | temperature[12] |
| 215 | 12 | gyro_rad_s[3] |
| 227 | 12 | accel_m_s2[3] |
| 239 | 16 | quat_wxyz[4] |
| 255 | 4  | bus_voltage |
| 259 | 2  | crc16 |

### 4.2 CRC16-CCITT-FALSE
- 다항식 0x1021, 초깃값 0xFFFF, no XOR-out, MSB-first
- Polling 방식 비트 단위 계산 (테이블 안 씀 — 메모리 절약)

### 4.3 SPI HW 설정

| 항목 | 값 |
|------|-----|
| Mode | 0 (CPOL=0, CPHA=0) |
| Speed | 5 MHz |
| Bit order | MSB first |
| DataSize | 8-bit |
| NSS | hard input (master driven) |
| DMA RX | DMA2 Stream0, Channel 3 |
| DMA TX | DMA2 Stream3, Channel 3 |

### 4.4 DMA 더블버퍼 흐름

```
시각: 0ms                  20ms                 40ms
      │                    │                    │
TX ─► [feedback_n-1]────►  [feedback_n]────►   [feedback_n+1]
RX ◄─ [command_n-1] ◄────  [command_n] ◄────   [command_n+1]
              │                    │
              ▼                    ▼
        decode + dispatch    decode + dispatch
```

DMA TxRx 완료 콜백에서 `volatile spi_transfer_done = true` 세팅 →
다음 루프 시작 시 처리 후 다음 transfer 즉시 시작.

---

## 5. 모드 머신

```
                      Jetson 명령 → mode 변경
                               │
              ┌────────────────┼────────────────┐
              ▼                ▼                ▼
        ┌─────────┐     ┌──────────────┐  ┌──────────┐
        │ IDLE    │     │ POSITION     │  │ HOLD     │
        │ torque  │     │ slew + apply │  │ prev 유지│
        │ OFF     │     │ target_rad   │  │ torque ON│
        └─────────┘     └──────────────┘  └──────────┘
                               │
                  Safety 위반 / Stale ▼
              ┌─────────────────────────────────┐
              │ 강제 IDLE 또는 HOLD (fallback)  │
              └─────────────────────────────────┘
```

| 모드 | 동작 |
|------|------|
| **IDLE** (0) | 토크 OFF. 다리 free. |
| **POSITION** (1) | `target_rad` → slew limit → joint limit → raw → 서보 |
| **HOLD** (3) | `target = prev_target` (현재 자세 유지) |
| **CALIBRATION** (4) | 토크 OFF + raw position 출력 (스테이션 zero 측정용) |

**Torque 전환 시점**:
- OFF → ON: `joint_control_capture_current_as_prev()` 호출 → 점프 방지
- ON → OFF: `robot_torque_off_all()` 만 호출

---

## 6. 안전 메커니즘 (check_safety + check_stale)

### 6.1 Fault code 일람

| Fault Code | 트리거 | 동작 | 분류 |
|---|---|---|---|
| 0 OK | — | — | — |
| 1 IMU_FAIL | BNO055 read 실패 | warning만 | transient |
| 2 SERVO_TIMEOUT | UART read 실패 | warning만 | transient |
| 3 CRC_ERROR | SPI 디코드 CRC 불일치 | 명령 무시 | **sticky** |
| 4 STALE_COMMAND | last_cmd_time 200ms 초과 | 강제 HOLD | transient |
| 5 TEMP_HIGH | 서보 온도 > 70°C | warning만 | transient |
| 6 VOLTAGE_LOW | bus_voltage < 10V | warning만 | transient |
| 7 SAFETY_LIMIT | pitch > MAX_PITCH 또는 roll > MAX_ROLL | **torque OFF + 강제 IDLE** | transient |
| 8 NAN_IN_TARGET | target_rad 에 NaN | hold fallback | **sticky** |

**IN_HAND_MODE = 1**: 손에 들고 테스트 시 MAX_PITCH/ROLL = 180° (사실상 비활성).
바닥 운용 시 IN_HAND_MODE = 0 으로 빌드 → MAX_PITCH = 35°, MAX_ROLL = 40°.

### 6.2 Fault lifecycle (transient vs sticky)

- **Transient fault**: 매 `check_safety()` cycle 재평가. 조건 해소 시 자동 `FAULT_OK` 복귀.
  - 예: 일시적 UART noise 로 인한 garbage temperature byte → 다음 cycle 에 정상 read → fault clear.
- **Sticky fault**: 한 번 set 되면 유지. 데이터 무결성이 영구 손상된 경우 (CRC 깨짐, NaN 입력) — 다음 transient fault 가 발생하면 그게 우선.

이 분리가 없으면 노이즈 한 번에 fault 가 영구 latch 돼 운영 불가. 사용자가 "서보 안 뜨거운데 왜 TEMP_HIGH 가 떠있냐" 같은 진단을 막아야 함.

### 6.3 우선순위

`SAFETY > NAN > TEMP > VOLTAGE > IMU > STALE > SERVO_TMO > CRC` 순서로 `check_safety()` 가 평가. 가장 위험한 조건만 fault_code 에 반영 (현재는 single fault code; 향후 bitmask 로 다중 fault 표현 가능).

### 6.4 디버그 출력 정책

`control_loop.c` 의 1초 주기 print 에 fault_code 뿐 아니라 raw 값도 같이 출력:
```c
[12345] mode=1 st=0x0F fault=0 torque=1 seq=617 maxT=42C(j7) Vbus=12.1V
```
→ fault 의 진위를 즉시 판별 가능 (특히 TEMP_HIGH, VOLTAGE_LOW 같이 raw 값으로 검증 가능한 fault).

---

## 7. 다리 제어 변환 체계

### 7.1 좌표계
- **Body frame**: hip 중심. X 전후, Y 좌우, Z 상하 (Z up).
- **Foot in hip frame**: `foot_x` (전후 mm), `foot_z` (상하, 음수가 아래)
- 기본 standing: `foot_x = -10mm` (COM 균형), `foot_z = -BODY_HEIGHT_MM`

### 7.2 2-link IK (leg_ik.c)

```
                 hip
                  ●
                  │
          L1=105  │ ─── thigh
                  │
                  ●─── knee joint
                  │
          L2=130  │ ─── shin
                  │
                  ●─── foot (foot_x, foot_z)
```

**Knee 각도** (cosine rule):
```
r²    = foot_x² + foot_z²
cos_k = (r² - L1² - L2²) / (2·L1·L2)
θ_k   = atan2(sqrt(1-cos_k²), cos_k)
```

**Thigh 각도** (회전된 좌표계):
```
A = L1 + L2·cos_k,   B = L2·sin_k
sin_t = (A·foot_x - B·(-foot_z)) / (A²+B²)
cos_t = (B·foot_x + A·(-foot_z)) / (A²+B²)
θ_t   = atan2(sin_t, cos_t)
```

### 7.3 좌우 미러링 (JOINT_SIGN)

**물리 구성**: 좌측(FL/RL)과 우측(FR/RR) 다리는 mirrored 장착. 같은 IK 각도라도 서보의 회전 방향은 반대.

**해결책**: `JOINT_SIGN[12] = {+1,+1,+1, -1,-1,-1, +1,+1,+1, -1,-1,-1}`

| 단계 | 변환 |
|------|------|
| Jetson → STM | `target_rad` (canonical: 모든 다리 같은 부호) |
| STM 내부 (rad→raw) | `raw = ZERO + SIGN · (rad · RAW_PER_RAD)` |
| 서보 → STM (raw→rad) | `rad = SIGN · (raw - ZERO) · (2π/4096)` |

⚠️ **주의**: telemetry에서 raw → rad 변환 시 반드시 `JOINT_SIGN` 적용해야 한다.
안 그러면 `prev_target_rad`가 잘못된 부호로 캡처돼서 첫 명령이 반대 방향으로 튀고 다리가 "기지개" 켜는 것처럼 보임.

### 7.4 Slew rate + 관절 한계 (joint_control.c)

```c
delta = target - prev
delta = clamp(delta, -max_delta, +max_delta)   // ① slew rate
applied = prev + delta
applied = clamp(applied, JOINT_MIN_RAD, JOINT_MAX_RAD)  // ② 관절 가동범위
raw = ZERO + SIGN · (applied · RAW_PER_RAD)
```

`max_delta_rad`는 SPI 명령으로 매 프레임 갱신 가능 → standup 천천히, walking 빠르게 등 유연하게 조절.

---

## 8. Trot 보행 알고리즘 (gait.c)

### 8.1 Phase 정의

```
phase = (elapsed_ms % GAIT_PERIOD_MS) / GAIT_PERIOD_MS    // [0, 1)
```

각 다리는 자기만의 `leg_phase = phase + offset`:

| 다리 | offset | Pair |
|------|--------|------|
| FL | 0.5 | B |
| FR | 0.0 | A |
| RL | 0.0 | A |
| RR | 0.5 | B |

→ FR+RL 동시에 stance/swing, FL+RR 동시에 stance/swing (대각선 페어)

### 8.2 Stance phase (`leg_phase < DUTY_FACTOR`)
선형 sweep — 발이 땅 위에서 뒤로 미는 동작:
```
s     = leg_phase / DUTY_FACTOR
fx_off = stride/2 - s · stride       // +stride/2 → -stride/2
fz_off = 0                           // 땅에 붙음
```

### 8.3 Swing phase (`leg_phase ≥ DUTY_FACTOR`)
**Cubic Bezier arch + smoothstep easing**:
```
s = smoothstep((leg_phase - DUTY_FACTOR) / (1 - DUTY_FACTOR))
B0,B1,B2,B3 = Bernstein coefficients

P0 = (-stride/2, 0)     // touch-up
P1 = (0, 4/3 · LIFT_Z)  // ← 4/3 보정으로 peak 정확히 LIFT_Z
P2 = (0, 4/3 · LIFT_Z)
P3 = (+stride/2, 0)     // touch-down

fx_off = B0·xP0 + B3·xP3
fz_off = B1·zP1 + B2·zP2
```

**왜 4/3?** Cubic Bezier에서 control point가 endpoint 사이에 있을 때, 곡선의 최대 height는 control height의 0.75배. 따라서 control = (4/3)·LIFT_Z 로 두면 actual peak = LIFT_Z 정확.

**왜 smoothstep?** 양 끝(liftoff/touchdown) 속도를 0으로 만들어 부드럽게. 안 그러면 발이 땅을 차고 출발하거나 부딪히듯 착지.

### 8.4 IK 통합

```c
foot_pos[l][0] = DEFAULT_FOOT_X + fx_off    // [-10 + (-35..+35)] mm
foot_pos[l][1] = DEFAULT_FOOT_Z + fz_off    // [-170 + (0..13)] mm
ik_2link_to_raw(L1, L2, foot_pos[l][0], foot_pos[l][1], &t, &k)
pose[l][1] = ZERO + sign·t
pose[l][2] = ZERO + sign·k
```

---

## 9. Jetson 측 SPI 트로트 테스트 (tools/spi_trot_test.cpp)

### 9.1 단계별 흐름 (STM의 `run_trot` 미러링)

```
Phase 0  카운트다운 3초 + 초기 자세 read (IDLE 모드 feedback 캡처)
   ↓
Phase 1  Smooth standup
         · adaptive duration (max delta · 1초/rad, 1~3초 클램프)
         · smoothstep 보간 (start/end 속도 0 → 부드러움)
         · max_delta_rad = 10 (slew 사실상 무력화 → 보간이 부드러움 책임)
   ↓
Phase 2  Trot N cycles
         · 50Hz tight loop, printf 0개
         · diag/fault 데이터는 메모리 버퍼에만 저장
   ↓
Phase 3  Smooth settle (마지막 trot 자세 → standing, smoothstep)
   ↓
[진단 데이터 전체 한 번에 출력]
   ↓
Phase 4  HOLD 무한 유지 (Ctrl+C 대기)
   ↓
Ctrl+C → IDLE 25프레임 (안전한 토크 OFF)
```

### 9.2 핵심 설계 원칙

1. **제어 루프 안 printf 절대 금지**
   - printf to USB serial = 5~50ms 소요 → 50Hz tick (20ms) 깨짐
   - STM32가 stale (200ms) 검출 → HOLD 모드 진입 → 다리 굳음

2. **smoothstep 보간 vs slew rate**
   - slew = constant 속도, abrupt start/stop
   - smoothstep = ease-in-out, 자연스러움
   - standup/settle엔 보간, walking엔 slew (안전 limit으로)

3. **HOLD 무한 유지로 끝**
   - 자동 IDLE 안 함 → 마지막에 다리 안 풀림
   - 사용자가 안전한 자세에서 Ctrl+C 결정

---

## 10. 데모 모드 (config.h)

`DEMO_MODE` 매크로로 컴파일 타임 선택 (향후 runtime state machine으로 승격 예정):

| Mode | 용도 |
|------|------|
| MODE_STAND_ONLY | standing 자세만 유지 |
| MODE_TROT | STM 자체 trot (Jetson 없이) |
| MODE_TELEMETRY_TEST | 50Hz read 검증 |
| MODE_JOINT_TEST | rad → raw 변환 검증 |
| MODE_CAL_MEASURE | 캘리브레이션 (zero 측정) |
| MODE_SPI_TEST | 레거시 SPI slave DMA echo 검증 |
| **MODE_RL_CONTROL** | **Jetson UART 제어 (현재 사용)** |

---

## 11. 캘리브레이션 절차 (calibration.c)

1. `DEMO_MODE = MODE_CAL_MEASURE`로 빌드/플래시
2. 토크 OFF 상태에서 다리를 standing pose로 손으로 배치
3. 시리얼 출력에서 각 서보의 raw position 측정
4. `firmware/Src/config.c`의 `JOINT_ZERO_POS[]`에 측정값 입력
5. 다시 정상 모드로 빌드/플래시

→ 서보 장착 시 0~4095 zero 위치 오차 보정.

---

## 12. 향후 개선 포인트

- [ ] Telemetry round-robin: 매 cycle 12 서보 read 대신 1~2 서보씩 분산 (제어 루프 budget 확보)
- [ ] Runtime mode 전환: SPI 명령으로 DEMO_MODE 변경 가능하게
- [ ] FreeRTOS 도입: 제어 루프와 통신 루프 분리, priority 기반 스케줄링
- [ ] Watchdog timer: 제어 루프 deadlock 검출
- [ ] CAN bus: SPI 대신 CAN으로 노이즈 강건성 향상 검토
