# 재난 대응 로봇 브릿지 및 Qt 대시보드

RPi5에서 여러 대의 Jetson 기반 로봇 데이터를 수신하고, Qt 대시보드에서 로봇 상태를 모니터링/제어하기 위한 프로젝트입니다.

이 레포는 크게 두 부분으로 구성됩니다.

- `bride_app_codex_indexed/`: Jetson 로봇과 UDP로 통신하고 POSIX 공유메모리에 데이터를 쓰는 브릿지 데몬
- `disaster_control_qt/`: 브릿지 데몬이 만든 공유메모리를 읽어 로봇 상태, 이미지, LiDAR, 경로 정보를 표시하는 Qt6 대시보드

## 전체 구조

```text
Jetson 로봇
  |  UDP 9000: 이미지 / LiDAR / 오도메트리 / 상태 / 이벤트 / 경로
  v
RPi5 브릿지 데몬
  |  POSIX 공유메모리: /robot_bridge_0, /robot_bridge_1, ...
  v
Qt 대시보드
  |  공유메모리 cmd_queue
  v
RPi5 브릿지 데몬
  |  UDP 9001: 명령 / 하트비트
  v
Jetson 로봇
```

외부 PC 연동이 필요한 경우 브릿지 데몬은 `UDP 9002`를 통해 PC 명령 수신과 상태 송신도 처리합니다.

## 폴더 구조

```text
.
├── bride_app_codex_indexed/
│   ├── bridge_main.c          # 브릿지 데몬 진입점
│   ├── jetson_rx.c            # Jetson -> RPi5 UDP 수신
│   ├── jetson_tx.c            # RPi5 -> Jetson 명령 송신
│   ├── reassembly_shm.c       # 분할 이미지/LiDAR 재조립
│   ├── protocol_timer.c       # 하트비트 및 타이머 스레드
│   ├── pc_link.c              # 외부 PC UDP 연동
│   ├── shm_def.h              # 공유메모리 구조 정의
│   ├── proto.h                # UDP 패킷 프로토콜 정의
│   └── Makefile
│
└── disaster_control_qt/
    ├── CMakeLists.txt
    ├── src/
    │   ├── main.cpp
    │   ├── mainwindow.cpp
    │   ├── robotshmconnection.cpp
    │   ├── robotworkerthread.cpp
    │   └── dashboardwidgets.cpp
    └── assets/
        ├── logo.png
        └── spot_robot.png
```

## 필요 환경

### 브릿지 데몬

- RPi5 또는 호환되는 ARM64 Linux 환경
- `gcc`
- POSIX thread
- POSIX shared memory 및 semaphore
- realtime scheduling 권한은 선택 사항입니다. 권한이 없으면 일반 스레드 생성 방식으로 자동 재시도합니다.

### Qt 대시보드

- Qt 6.5 이상
- CMake 3.19 이상
- C++ 컴파일러
- Qt 컴포넌트:
  - `Core`
  - `Widgets`
  - `OpenGL`
  - `OpenGLWidgets`

## 빌드 방법

### 1. 브릿지 데몬 빌드

```bash
cd bride_app_codex_indexed
make
```

빌드가 끝나면 다음 실행 파일이 생성됩니다.

```text
bride_app_codex_indexed/bridge_daemon
```

디버깅용 sanitizer 빌드도 사용할 수 있습니다.

```bash
make asan
make tsan
```

빌드 산출물을 지우려면 다음 명령을 사용합니다.

```bash
make clean
```

### 2. Qt 대시보드 빌드

Qt 앱은 `../bride_app_codex_indexed`에 있는 브릿지 헤더를 직접 참조합니다. 따라서 두 폴더는 같은 상위 폴더 아래에 있어야 합니다.

```bash
cd disaster_control_qt
cmake -S . -B build
cmake --build build
```

빌드가 끝나면 다음 실행 파일이 생성됩니다.

```text
disaster_control_qt/build/DisasterControlQt
```

## 실행 방법

먼저 브릿지 데몬을 실행해서 공유메모리를 생성합니다.

```bash
cd bride_app_codex_indexed
./bridge_daemon 4
```

인자는 관리할 로봇 수입니다. 생략하면 기본값은 `1`입니다. 최대 로봇 수는 `shm_def.h`의 `MAX_ROBOTS`에 정의되어 있습니다.

그 다음 Qt 대시보드를 실행합니다.

```bash
cd disaster_control_qt
./build/DisasterControlQt
```

Qt 앱은 다음 형식의 공유메모리를 읽습니다.

```text
/robot_bridge_0
/robot_bridge_1
/robot_bridge_2
...
```

## 통신 구조

| 방향 | 포트 / IPC | 용도 |
| --- | --- | --- |
| Jetson -> 브릿지 | UDP `9000` | 이미지, LiDAR, 오도메트리, 상태, 이벤트, 미션/경로 데이터 |
| 브릿지 -> Jetson | UDP `9001` | 이동, 정지, 비상 정지, 미션 명령, 하트비트 |
| PC <-> 브릿지 | UDP `9002` | 외부 PC 명령 및 상태 연동 |
| 브릿지 -> Qt | POSIX 공유메모리 `/robot_bridge_N` | 로봇 상태, 이미지, LiDAR, 오도메트리, 경로, 이벤트 로그 |
| Qt -> 브릿지 | 공유메모리 `cmd_queue` | 대시보드 제어 명령 큐 |

## 공유메모리 구조

로봇마다 독립된 POSIX 공유메모리 객체를 하나씩 사용합니다.

```text
/robot_bridge_0
/robot_bridge_1
...
/robot_bridge_(MAX_ROBOTS - 1)
```

공유메모리 레이아웃은 `bride_app_codex_indexed/shm_def.h`에 정의되어 있습니다.

- 이미지 데이터는 triple buffering 방식으로 처리합니다.
- LiDAR 데이터도 triple buffering 방식으로 처리합니다.
- 오도메트리, 메타데이터, 상태, 경로 데이터는 process-shared lock 또는 atomic 필드를 사용합니다.
- Qt에서 보내는 제어 명령은 공유메모리 내부의 `cmd_queue`에 기록됩니다.

Qt 앱과 브릿지 데몬은 같은 `shm_def.h`, `proto.h`를 공유합니다. 따라서 공유메모리 구조나 UDP 프로토콜이 바뀌면 두 프로그램을 모두 다시 빌드해야 합니다.

## UDP 프로토콜

UDP 패킷 형식은 `bride_app_codex_indexed/proto.h`에 정의되어 있습니다.

주요 패킷 타입은 다음과 같습니다.

- `PKT_TYPE_IMAGE`
- `PKT_TYPE_LIDAR`
- `PKT_TYPE_ODOM`
- `PKT_TYPE_CMD`
- `PKT_TYPE_CMD_ACK`
- `PKT_TYPE_STATE`
- `PKT_TYPE_EVENT`
- `PKT_TYPE_GLOBAL_PATH`
- `PKT_TYPE_PATH_PROGRESS`

이미지와 LiDAR 패킷은 여러 UDP 패킷으로 분할되어 들어올 수 있습니다. 브릿지 데몬은 분할 패킷을 재조립한 뒤 공유메모리에 기록합니다.
```

## 개발 참고 사항

- Qt 대시보드보다 브릿지 데몬을 먼저 실행해야 합니다.
- Qt 대시보드가 공유메모리에 연결하지 못하면 `/dev/shm/robot_bridge_N` 파일이 생성되어 있는지 확인합니다.
- 명령 송신이 되지 않으면 브릿지 데몬이 Jetson의 UDP 송신 주소를 학습했는지 확인합니다.
- 공유메모리 구조체 필드가 변경되면 브릿지 데몬과 Qt 대시보드를 모두 다시 빌드해야 합니다.
