# 2026-05-11 — Jetson ↔ STM32 SPI 통합 디버그 세션

> Jetson Orin Nano (master) 가 STM32F446RE (slave) 에 50Hz로 trot 보행 명령을 SPI로 보내는 작업. 통신 자체부터 안 되던 것을, 5개 버그를 차례로 잡아 trot 보행이 정상 작동할 때까지의 기록.

---

## 작업 결과 요약

| 영역 | Before | After |
|------|--------|-------|
| SPI 통신 | MISO 전부 0 (DMA 무동작) | 261B 정상 송수신 |
| SPI feedback loss rate | 27.5% (62/225 invalid) | **1%** (3/225 invalid) |
| Trot 동작 | 다리 무작위 진동 | 부드러운 3-cycle trot |
| FR/RR 다리 | 시작 시 "기지개" (앞으로 뻗음) | 정상 standing 자세 |
| Loop 안정성 | 0.5~1.5초 stutter 빈발 | 매끄러운 50Hz 유지 |
| 종료 | 자동 IDLE → 토크 풀림 | HOLD 유지, Ctrl+C 시에만 IDLE |
| Fault 상태 | TEMP_HIGH 영구 latch (가짜) | 1 tick noise 만 잠깐 뜨고 OK 복귀 |
| 전체 흐름 | standup → trot → 종료 (어수선) | initial → stand → dwell → trot → settle → return → hold (자연스러움) |

---

## 발견한 5개 버그 + 수정

### 버그 1: SPI DMA 클럭이 DMA Init 후에 활성화됨

**증상**: Jetson loopback 테스트는 통과 (`[0xAA, 0x55, 0x12, 0x34]`), 하지만 STM32와 통신하면 MISO에 전부 0만 옴.

**원인** (`firmware/Src/spi.c`):
```c
/* USER CODE BEGIN SPI1_MspInit 0 */     ← 여기에 클럭 enable 있어야 했음
/* USER CODE END SPI1_MspInit 0 */
__HAL_RCC_SPI1_CLK_ENABLE();

// ... HAL_DMA_Init(&hdma_spi1_rx) ...    ← DMA 레지스터 쓰기, 그러나 클럭 없으니 무시됨
// ... HAL_DMA_Init(&hdma_spi1_tx) ...

/* USER CODE BEGIN SPI1_MspInit 1 */
__HAL_RCC_DMA2_CLK_ENABLE();             ← 여기서 클럭 켬, 너무 늦음
/* USER CODE END SPI1_MspInit 1 */
```

DMA peripheral의 control register는 클럭이 없으면 write가 silently 무시된다. 채널 선택, direction, memory increment 등 모든 DMA 설정이 날아갔고, DMA가 동작 안 함 → SPI slave가 master에게 응답 못 함.

**수정**:
```c
/* USER CODE BEGIN SPI1_MspInit 0 */
__HAL_RCC_DMA2_CLK_ENABLE();   /* DMA Init 이전으로 이동! */
/* USER CODE END SPI1_MspInit 0 */
```

**교훈**:
- HAL/CMSIS는 RCC 클럭 자동 관리 안 해줌 (peripheral enable은 사용자 책임)
- STM32CubeMX 자동 생성 코드도 DMA 클럭 enable 위치를 잘못 둘 수 있음
- "코드는 맞는 것 같은데 동작 안 함" → 클럭 도메인부터 의심

---

### 버그 2: 손에 들고 테스트 중 SAFETY_LIMIT 진동

**증상**: trot 시작하면 다리가 trot 패턴이 아닌 "이상하게 도는" 모터 동작. SPI 통신은 정상.

**원인** (`firmware/Src/control_loop.c`):
```c
static void check_safety(void) {
    if (!safety_check(g_robot_state.imu.pitch, g_robot_state.imu.roll)) {
        g_robot_state.fault_code = FAULT_SAFETY_LIMIT;
        robot_torque_off_all();              // ← 토크 OFF
        g_robot_state.torque_enabled = false;
        g_robot_state.mode = MODE_IDLE;      // ← 강제 IDLE
        return;
    }
    ...
}
```

`IN_HAND_MODE = 0` 으로 빌드된 펌웨어에서 MAX_PITCH = 35°, MAX_ROLL = 40°. 손에 든 상태에서 trot 동작하면 IMU 기울기 35° 쉽게 초과 → SAFETY_LIMIT 발동.

**진동 메커니즘**:
1. trot 명령 → 다리 움직임 → IMU 기울기 ↑
2. SAFETY_LIMIT → 토크 OFF → 다리 free → 가만히 늘어짐
3. 다리 늘어지며 기울기 ↓ → safety 통과
4. 다음 SPI 프레임 → 토크 ON → `joint_control_capture_current_as_prev()` 호출
5. **늘어진 자세를 새 prev_target으로 캡처** (랜덤 시작점)
6. 랜덤 위치에서 trot 타겟으로 슬루잉 시작
7. 다시 기울어짐 → SAFETY_LIMIT → 무한 반복

토크 ON/OFF 진동 + 매번 랜덤 시작점 = 전혀 trot 아닌 불규칙 움직임.

**수정**:
```c
// firmware/Inc/config.h
#define IN_HAND_MODE  1   // 0 → 1, 손에 들고 테스트 시 safety 비활성화
```

**교훈**:
- 안전 로직이 발동하면 "그냥 정지" 가 아니라 "주기적 진동"이 될 수 있음
- 안전 fallback 설계 시 hysteresis 또는 fault latching 고려 필요
- "걷는 모양이 이상하다"는 증상이 항상 모션 알고리즘 버그는 아님 — fault handling 동작도 의심

---

### 버그 3: telemetry가 JOINT_SIGN을 적용 안 함

**증상**: SAFETY 비활성화 후에도 FR/RR 다리만 시작 시 "앞으로 쭉 기지개" 켜듯 뻗음. FL/RL은 정상.

**원인** (`firmware/Src/telemetry.c`):
```c
static inline float position_raw_to_rad(uint16_t raw) {
    return ((float)raw - 2048.0f) * (2.0f * M_PI / 4096.0f);
}
```

`JOINT_SIGN`/`JOINT_ZERO_POS` 적용 없이 raw → rad 변환만 함.

**핵심 문제**: 같은 `g_robot_state` 안에서 두 변수가 서로 다른 부호 체계!
- `target_rad` (Jetson → STM): canonical convention (모든 다리 같은 부호로 표현)
- `position_rad` (STM → Jetson): raw 그대로 (FR/RR은 부호 반대)

`joint_control_capture_current_as_prev()`가 `prev_target_rad = position_rad`로 직접 대입:
```
FR 다리가 카운트다운 동안 raw 3000 위치에 매달려 있다고 가정
→ telemetry: position_rad = (3000-2048)*2π/4096 = +1.46  (sign 미적용)
→ capture: prev_target_rad[FR thigh] = +1.46
→ Jetson 명령: target_rad = -0.926
→ delta = -0.926 - 1.46 = -2.386  (큰 음수)
→ clamped = -0.05
→ applied = +1.41
→ joint_rad_to_raw: raw = 2048 + (-1)·(1.41·651.9) = 1129
```

**첫 명령이 raw 1129!** 서보가 3000에서 1129로 급발진 = "기지개" 동작.

FL/RL은 sign=+1이라 부호 mismatch가 없어서 정상.

**수정**:
```c
static inline float position_raw_to_rad(uint16_t raw, int joint_idx) {
    return (float)JOINT_SIGN[joint_idx]
         * ((float)raw - (float)JOINT_ZERO_POS[joint_idx])
         * (2.0f * (float)M_PI / 4096.0f);
}
```
velocity_rad_s 도 동일하게 JOINT_SIGN 적용.

**교훈**:
- 변환 함수는 항상 "어느 frame의 어떤 단위" 인지 명시적으로 다뤄야 함
- 같은 자료구조 안에 서로 다른 convention의 데이터가 섞이면 버그의 온상
- 단위 테스트로 round-trip (canonical → raw → canonical) 검증했어야 함

---

### 버그 4: STALE_TIMEOUT 60ms가 너무 빡빡

**증상**: trot 도중 1초 정도 멈칫 (다리 동작 정지) 후 다시 진행. Jetson 측 SPI feedback이 27% invalid.

**원인** (`firmware/Src/control_loop.c`):
```c
#define STALE_TIMEOUT_MS  60   /* 3 cycle 이상 패킷 없으면 stale */
```

Linux 비-RT 커널에서 50Hz tight loop은 가끔 100ms+ 지연이 발생 (스케줄링 jitter). STM32는 60ms 만 안 와도 stale → HOLD 모드 진입 → 다리 굳음.

**근본적 분석**:
- 60ms = 3 SPI frame
- Linux Ubuntu 22.04 일반 커널은 latency spike 50~100ms 흔함
- non-RT 시스템에서 50Hz 보장하려면 timeout을 충분히 여유 있게

**수정**:
```c
#define STALE_TIMEOUT_MS  200   /* 10 cycle, Linux non-RT jitter 흡수 */
```

200ms = 10 frame은 일시적 jitter는 흡수, 진짜 통신 끊김 (Jetson 죽음 등)은 여전히 검출.

**교훈**:
- 분산 시스템에서 timeout 설정은 "정상적인 jitter의 95th percentile + safety margin"
- 너무 짧은 timeout은 false positive 양산 → 안 켜진 것만도 못함
- RT 보장 필요하면 RT 커널 (PREEMPT_RT) 도입 고려

---

### 버그 5: printf가 50Hz 제어 루프를 방해

**증상**: 버그 4 수정 후에도 Phase 1 → Phase 2 전환 시점, cycle 1 → 2 사이에 stutter.

**원인**: Jetson 측 `spi_trot_test.cpp`의 50Hz loop 안에서 매 10 tick마다 printf:
```cpp
if (tick_count % 10 == 0) {
    printf("  [c%d/%d ph=%.2f] FL(lp=%.2f foot=%+.0f,%+.0f) ...");
}
```

USB serial로 printf = 5~50ms 소요. 한 tick (20ms) 예산을 쉽게 초과 → 다음 SPI frame 지연 → STM32 stale 감지 → HOLD 모드 → 다리 굳음.

게다가 Phase 전환 시점에 `printf` 7번 연속 호출 (자세 출력 등) → 100~200ms 갭.

**수정 (전면 재설계)**:
1. 제어 루프 안에서 printf 절대 금지
2. 진단 데이터는 메모리 버퍼에 저장
3. 모든 phase 끝나고 한 번에 출력
4. Phase 1/3 standup/settle 도 slew rate 대신 smoothstep 보간
5. 마지막 phase는 자동 IDLE 안 하고 HOLD 유지 (Ctrl+C 시에만 IDLE)

```cpp
// loop 안에는 데이터만 저장
if (tick_count % 10 == 0 && snap_count < 64) {
    snaps[snap_count++] = {tick_count, cycle, phase, ...};
}

// loop 끝나고 한꺼번에 출력
for (int i = 0; i < snap_count; i++) {
    printf("  [c%d ph=%.2f] ...", snaps[i].cycle, snaps[i].phase, ...);
}
```

**교훈**:
- 실시간 제어 루프와 I/O는 절대 같은 thread에서 섞으면 안 됨
- 디버깅 코드가 제어를 방해하면 본말전도 (디버그하려다 더 큰 버그 발생)
- 진단 출력은 항상 buffered + deferred 패턴
- "제어가 가장 우선순위" 라는 사용자의 지적이 정확했음

---

### 버그 6: Fault code latching — 가짜 영구 TEMP_HIGH

**증상**: 서보가 분명 차가운데 (touch test로 확인) STM32 가 부팅 직후부터 `fault=TEMP_HIGH(5)` 를 계속 보고. 테스트 끝날 때까지 fault 가 안 사라짐.

**원인** (`firmware/Src/control_loop.c` 의 기존 `check_safety()`):
```c
for (int i = 0; i < NUM_JOINTS; i++) {
    if (g_robot_state.temperature[i] > TEMP_LIMIT_C) {
        g_robot_state.fault_code = FAULT_TEMP_HIGH;
        break;
    }
}
// ← 온도 정상이어도 fault_code clear 코드가 없음
```

`check_safety()` 가 fault 를 **set 만 하고 clear 는 안 함**. 한 번 SET 되면 영원히 latch.
이전 테스트 중 잠깐 70°C 넘었거나, 한 서보가 한 cycle 만 garbage byte 읽혀도 영원히 fault=5.

진단을 위해 디버그 출력에 max temp 추가 후 확인하니 max=40~50°C 인데 fault=5 → latch 가 확실.

**수정**:
```c
static void check_safety(void) {
    fault_code_t new_fault = FAULT_OK;

    /* 매 cycle 재평가 — transient 조건 해소되면 OK 로 복귀 */
    if (!safety_check(pitch, roll))            new_fault = FAULT_SAFETY_LIMIT;
    else if (temp > TEMP_LIMIT_C)              new_fault = FAULT_TEMP_HIGH;
    else if (voltage < VOLTAGE_LOW_V)          new_fault = FAULT_VOLTAGE_LOW;

    /* Sticky fault (CRC, NAN) 는 보존 — 새 transient 발생 시에만 덮어쓰기 */
    bool sticky = (cur == FAULT_CRC_ERROR || cur == FAULT_NAN_IN_TARGET);
    if (sticky && new_fault == FAULT_OK) {
        /* 유지 */
    } else {
        g_robot_state.fault_code = new_fault;
    }
}
```

**Fault 정책 명확화**:
- **Transient**: SAFETY / TEMP / VOLTAGE / IMU / STALE / SERVO_TMO → 매 cycle 재평가, 자동 clear
- **Sticky**: CRC_ERROR / NAN_IN_TARGET → 명시적 reset 까지 유지 (데이터 무결성 영구 손상)

**검증 결과** (수정 후):
```
[tick=4]   OK → TEMP_HIGH
[tick=5]   TEMP_HIGH → OK    ← 1 tick (20ms) 만에 자동 복귀
[tick=109] OK → TEMP_HIGH
[tick=110] TEMP_HIGH → OK    ← 또 1 tick 후 복귀
```
4.5초 동안 TEMP_HIGH 가 딱 2번, 각 1 tick (20ms) 만 떴음 = telemetry UART 한 바이트 noise. 실제 과열 X. Latch 였다면 영원히 fault=5 였을 것.

**교훈**:
- Fault 는 "set 후 잊음" 으론 부족 — 조건의 lifecycle (transient vs persistent) 을 분리 설계해야
- "왜 알람이 안 꺼지지" 같은 사용자 직감이 latching 버그의 첫 신호
- 디버그 출력에 raw 값 (max temp 등) 도 함께 표시 → 알람의 진위 즉시 판별 가능

---

## 추가 개선 (sw 관점)

### 7. Standup 동작을 STM의 `robot_transition` 처럼 smoothstep 보간

**Before**: slew rate (constant 0.05 rad/tick) — 기계적, abrupt start/stop

**After**:
```cpp
for (int t = 0; t <= n_steps; t++) {
    float r = (float)t / n_steps;
    float s = r * r * (3.0f - 2.0f * r);   // smoothstep
    for (int i = 0; i < 12; i++) {
        interp[i] = initial[i] + (target[i] - initial[i]) * s;
    }
    send(MODE_POSITION, FLAG_TORQUE_EN, interp, BIG_DELTA);
    sleep_until(now_sec() + TICK);
}
```

`max_delta_rad = 10` (사실상 무력화) → 보간 자체가 부드러움 책임. Adaptive duration: `max_delta · 1초/rad`, 1~3초 클램프.

### 8. 전체 phase 흐름을 STM 자체 trot 처럼 재설계

```
Phase 0  카운트다운 + 초기 자세 read (IDLE feedback)
Phase 1  Smooth standup (initial → standing, smoothstep)
Phase 1.5 Dwell at standing (1.5초)   ← "자세 잡고 → 걷기 시작" pause
Phase 2  Trot N cycles
Phase 3  Smooth settle (last_trot → standing)
Phase 4  Smooth return (standing → initial)   ← 시작 자세로 복귀
Phase 5  HOLD at initial pose (Ctrl+C 대기)
Ctrl+C → IDLE (토크 OFF)
```

Phase 1.5 와 Phase 4 가 STM 자체 trot의 자연스러운 흐름 (서기 → 잠시 멈추고 → 걷기 → 멈추고 → 앉기) 을 구현.

### 9. 종료 시 토크 유지 (HOLD 무한)

**Before**: Phase 4에서 1초 HOLD 후 자동 IDLE → standing 자세에서 다리 풀려 body 170mm 추락 위험

**After**: Phase 4에서 initial pose 로 내려와서 HOLD 무한 유지, Ctrl+C 받을 때만 IDLE 25 frame 전송.

이러면 사용자가 안전한 위치 (이미 낮음) 에서 직접 결정 가능.

---

## 변경된 파일 (commit)

```
firmware/Src/spi.c              ← DMA 클럭 순서 수정
firmware/Inc/config.h           ← IN_HAND_MODE=1, trot 파라미터 조정
firmware/Src/telemetry.c        ← JOINT_SIGN 적용
firmware/Src/control_loop.c     ← STALE_TIMEOUT 60→200ms, fault auto-clear, max temp 디버그
tools/spi_dummy_test.py         ← (신규) SPI echo 검증
tools/spi_trot_test.py          ← (신규) Python trot
tools/spi_trot_test.cpp         ← (신규) C++ trot, smoothstep + buffered diag + dwell + return-to-initial
firmware/docs/architecture.md   ← (신규) 아키텍처 문서
firmware/docs/2026-05-11_*.md   ← (신규) 본 디버그 세션 기록
```

---

## 최종 검증 결과

```
══ Phase 2 진단 결과 ══
  총 tick: 225 (valid: 222, 1% loss)            ← 27.5% → 1%
  SAFETY(7) tick: 0 / 222 (0%)
  IDLE mst tick: 0 / 222 (0%)                   ← 0초 stutter
  fault transitions:
    [tick=0   ph=0.00] ???(255) -> OK(0)         (초기, 정상)
    [tick=4   ph=0.05] OK(0) -> TEMP_HIGH(5)
    [tick=5   ph=0.07] TEMP_HIGH(5) -> OK(0)    ← 1 tick (20ms) 만에 auto-clear
    [tick=109 ph=0.45] OK(0) -> TEMP_HIGH(5)
    [tick=110 ph=0.47] TEMP_HIGH(5) -> OK(0)    ← 또 auto-clear
```

3 cycle trot 동안 (4.5초) invalid feedback 3 frame, fault 진입 2번 × 각 1 tick. 모두 정상 운영 범위 내. 다리는 시각적으로도 부드러운 trot.

전체 흐름이 STM 자체 trot (autonomous DEMO_MODE_TROT) 의 동작과 거의 동등한 품질로 SPI 경유 운영 가능해짐.
