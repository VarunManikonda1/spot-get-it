# firmware

STM32F446RE 펌웨어. Spot Micro 4족 보행 로봇의 실시간 관절 제어.

## 개요

- **MCU**: STM32F446RE (Nucleo-64)
- **빌드 환경**: STM32CubeIDE
- **실행 모델**: Bare-metal (super-loop)
  - FreeRTOS 도입은 향후 검토 (현재는 미사용)

## 역할

Jetson Orin Nano로부터 RL 정책 출력(12관절 목표 라디안)을 50Hz로 수신.
STM32는 단순 passthrough가 아니라 **자체 폐루프 제어 + 안전 계층**을 수행.

```
Jetson (50Hz)         STM32 (500Hz)              Hardware
─────────────         ──────────────             ─────────
RL inference  ──UART─→ ① 명령 검증
                       ② PID 제어 (관절각 → 서보 명령)
                       ③ 안전 체크 (limit, watchdog, temp)
                       ④ SYNC_WRITE  ──HDSEL──→ 12 STS3215
                       ⑤ Bulk read   ←─────────────┘
                       ⑥ IMU read    ←──I2C──── BNO055
       ←──obs──        ⑦ State 패킹
```

50Hz Jetson 명령 사이의 9 사이클 동안 STM은 마지막 명령을 유지하면서 PID로 추종 — last-command-wins.

## 통신 인터페이스

| 링크 | 인터페이스 | 속도 | 주기 |
|---|---|---|---|
| STM ↔ 서보 (4 버스 병렬) | HDSEL UART | 1 Mbps | 500 Hz |
| STM ↔ IMU (BNO055) | I2C Fast-Mode | 400 kHz | 500 Hz |
| STM ↔ Jetson | UART | 921600 | 50 Hz |
| 디버그 콘솔 | USART2 (ST-Link VCP) | 115200 | - |

상세 핀 매핑 / 회로는 [`hardware_design.md`](../docs/hardware/hardware_design.md) 참고.

## 폴더 구조

```
firmware/
├── Core/             # CubeMX 생성물 (main.c, stm32f4xx_it.c 등)
├── Drivers/          # STM32 HAL, CMSIS
├── App/              # 팀 작업 영역
│   ├── servo/        # STS3215 Feetech serial bus protocol (PING, READ, WRITE, SYNC_WRITE)
│   ├── imu/          # BNO055 드라이버
│   ├── control/      # PID, 관절 제어, 운동학
│   ├── comm/         # Jetson UART 통신, 패킷 정의
│   └── safety/       # watchdog, limit / temperature / current 체크
└── README.md
```

## 진행 상황

### 완료

- [x] **Phase 0** — 하드웨어 설계, 핀 할당, 만능기판 회로
- [x] **Phase 1** — 4 UART HDSEL PING (12/12 alive)
- [x] **Phase 2** — 5Hz 12 서보 read state monitoring (bulk read 8B)
- [x] **Phase 3** — Torque enable + Goal Position write (단일 서보 ±5°)
- [x] **Phase 4** — SYNC_WRITE 다리 단위 동시 명령 (한 다리 3 서보, 17B/~150μs)
- [x] **Phase 5** — 4 다리 순차 SYNC_WRITE 검증 (FL/FR/RL/RR 모두 ±5°)
- [x] **Phase 6** — Sign table 정의 + 검증 (좌우 mirror, 앞뒤 동일)
- [x] **Phase 7** — 12관절 영점 캘리브레이션
  - URT-2 SW로 영점 설정 후 EEPROM Lock(0x37) 해제 → Position Offset(0x1F) 영구 저장
  - 검증: 모든 서보 영점 ±50 unit (±4°) 이내
- [x] **Phase 8** — 단일 다리 자세 사이클 (default ↔ 영점, smoothstep 80step × 25ms)
- [x] **Phase 9** — 안전 메커니즘 (ESC 긴급정지, jump 방지, ESC abort 카운트다운)

### 진행 예정

- [ ] **Phase 10** — Default pose 확정 + 4 다리 동시 진입 (각도 결정 → 4 다리 검증)
- [ ] **Phase 11** — Blocking HAL → DMA + IT 전환, 500Hz 제어 루프
- [ ] **Phase 12** — IMU (BNO055) 통합, projected gravity 계산
- [ ] **Phase 13** — UART 통신 검증 (더미 데이터로 패킷 송수신 self-test)
- [ ] **Phase 14** — Jetson UART 실제 연동 + watchdog
- [ ] **Phase 15** — 조이스틱 + 고전 제어 시연 트랙
  - Jetson에서 조이스틱 입력 읽고 IK + gait pattern 계산 → SPI로 STM에 target_rad 전송
  - STM 펌웨어는 RL과 동일 경로 사용 (입력 소스만 다를 뿐 SPI 인터페이스 공통)
- [ ] **Phase 16** — RL policy deploy + sim-to-real 검증

## 빌드 / Flash

STM32CubeIDE에서:
1. `firmware/` 폴더 import (`File > Import > Existing Projects`)
2. Build: `Project > Build All` (Ctrl+B)
3. Flash: `Run > Run` (Ctrl+F11) — ST-Link로 자동 플래싱

## 디버그 콘솔

USART2(ST-Link VCP)로 `printf` 출력. PC에서 시리얼 터미널을 `115200 8N1` 로 열어 접속:
- Linux/Mac: `screen /dev/ttyACM0 115200`
- Windows: PuTTY → Serial → COMx, 115200

ESC 키로 즉시 비상정지 (12 서보 토크 OFF + halt).

## 트러블슈팅 메모

### 영점이 전원 cycle 후 풀림 (EEPROM Lock)

**증상**: URT-2 SW로 영점(`Position Offset` 0x1F) 설정 후 동작은 정상이지만, PDB 전원 OFF/ON 시 default 값으로 돌아감.

**원인**: STS3215 `EEPROM Lock`(0x37)이 1(잠금) 상태면 EEPROM 영역(0x00–0x2F) 쓰기가 RAM에만 일시 적용되고 EEPROM에는 저장 안 됨. `Position Offset`(0x1F)도 EEPROM 영역.

**해결**:
1. Lock 해제: `WRITE 0x37 ← 0`
2. Offset 쓰기: `WRITE 0x1F ← <offset>`
3. (선택) Lock 복원: `WRITE 0x37 ← 1`
4. PDB 전원 cycle 후 검증

---

담당: 제영호 (jeayoungho97)
