# Qt BridgeClient Encapsulation Plan

작성일: 2026-05-04

대상:
- BridgeDaemon: `/home/pi/robot_project/bride_app_codex`
- Qt App: `/home/pi/robot_project/disaster_control_qt`

## 1. 문제 정의

현재 `bridge_daemon`은 시스템 구조상 미들웨어 역할을 한다.

```text
Jetson <-> bridge_daemon <-> Qt GUI / PC
```

하지만 Qt 앱 관점에서는 아직 미들웨어 API가 충분히 캡슐화되어 있지 않다. Qt 코드가 다음 세부사항을 직접 알고 있다.

- POSIX SHM 이름 규칙: `/robot_bridge_N`
- `SharedData` ABI layout
- `pthread_rwlock_t`
- `std::atomic` ready index
- image/lidar slot 구조
- event ring buffer 구조
- SHM command queue 구조
- `shm_open`, `mmap`, `munmap`

즉, 현재 구조는 “데몬이 미들웨어 역할을 한다”는 말은 맞지만, “Qt 개발자가 get/post/fetch API만 호출한다”는 형태는 아니다.

## 2. 목표

Qt UI 계층은 IPC와 SHM ABI를 몰라야 한다.

UI가 알아야 하는 것은 다음 수준의 API뿐이어야 한다.

```cpp
bridge.start(4);

QVector<RobotSnapshot> robots = bridge.snapshots();
bridge.postCommand(robotId, CMD_TYPE_STOP);
bridge.postVelocity(robotId, vx, vy, omega);
bridge.emergencyStop(robotId);
```

UI가 몰라야 하는 것:

```cpp
shm_open(...)
mmap(...)
SharedData *
pthread_rwlock_rdlock(...)
img_ready_idx.load(...)
cmd_queue.head/tail/count
```

## 3. 목표 아키텍처

### 3.1 전체 구조

```text
┌────────────────────────────────────────────────────────────┐
│ Qt UI Layer                                                │
│                                                            │
│ MainWindow / widgets / dashboard                          │
│ - 화면 표시                                                │
│ - 버튼, 슬라이더, 지도, 로그 UI                            │
│ - BridgeClient public API만 호출                           │
└──────────────────────────────┬─────────────────────────────┘
                               │ Qt signal/slot + simple API
                               ▼
┌────────────────────────────────────────────────────────────┐
│ BridgeClient                                               │
│                                                            │
│ Qt 앱 내부 미들웨어 클라이언트 계층                         │
│ - start/stop                                               │
│ - snapshots                                                │
│ - postCommand/postVelocity/emergencyStop                   │
│ - event signal                                             │
│ - image/lidar/state를 Qt 친화 타입으로 변환                 │
└──────────────────────────────┬─────────────────────────────┘
                               │ private backend calls
                               ▼
┌────────────────────────────────────────────────────────────┐
│ ShmBridgeBackend                                           │
│                                                            │
│ IPC/ABI 전용 구현 계층                                      │
│ - shm_open/mmap/munmap                                     │
│ - SharedData 접근                                          │
│ - pthread rwlock                                           │
│ - atomic ready index                                       │
│ - SHM command queue enqueue                                │
└──────────────────────────────┬─────────────────────────────┘
                               │ POSIX SHM
                               ▼
┌────────────────────────────────────────────────────────────┐
│ bridge_daemon                                              │
│                                                            │
│ /robot_bridge_N SharedData                                 │
│ - image/lidar/state/event/metrics                          │
│ - cmd_queue                                                │
└────────────────────────────────────────────────────────────┘
```

### 3.2 의존성 방향

허용:

```text
MainWindow -> BridgeClient -> ShmBridgeBackend -> SharedData
```

금지:

```text
MainWindow -> SharedData
MainWindow -> shm_open/mmap
MainWindow -> pthread_rwlock
widgets    -> cmd_queue
```

`SharedData`는 backend 구현 파일 밖으로 노출하지 않는 것이 목표다.

## 4. 권장 파일 구조

Qt 앱 내부에 다음 파일을 둔다.

```text
disaster_control_qt/src/
  bridgeclient.h
  bridgeclient.cpp
  bridgetypes.h
  shmbridgebackend.h
  shmbridgebackend.cpp
```

작게 시작하려면 `BridgeClient`와 `ShmBridgeBackend`를 한 번에 만들지 않고, 기존 `ShmMonitor`와 `BridgeCommandClient`를 단계적으로 합쳐도 된다.

최종적으로는 UI가 다음 둘을 직접 쓰지 않게 한다.

```text
ShmMonitor
BridgeCommandClient
```

대신 UI는 하나만 의존한다.

```text
BridgeClient
```

## 5. Public API 설계

### 5.1 `bridgetypes.h`

UI에 노출되는 타입만 둔다. 이 파일은 `SharedData`를 include하지 않는다.

```cpp
struct LidarPoint {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float intensity = 0.0f;
};

struct UiEvent {
    int robotId = -1;
    int severity = 0;
    int type = 0;
    quint64 timestampUs = 0;
    QString message;
};

struct RobotSnapshot {
    int id = -1;
    bool shmOpen = false;
    bool connected = false;

    float x = 0.0f;
    float y = 0.0f;
    float theta = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float omega = 0.0f;

    float battery = 0.0f;
    float linkRttMs = 0.0f;
    float imageFps = 0.0f;
    float lidarFps = 0.0f;

    uint32_t imgDropCount = 0;
    uint32_t lidarDropCount = 0;
    int rxPackets = 0;
    int txCommands = 0;
    int ackPackets = 0;

    QImage image;
    QVector<LidarPoint> lidarPoints;
};
```

### 5.2 `BridgeClient`

UI가 사용하는 단일 진입점이다.

```cpp
class BridgeClient : public QObject {
    Q_OBJECT

public:
    explicit BridgeClient(QObject *parent = nullptr);
    ~BridgeClient() override;

    void start(int maxRobots);
    void stop();

    QVector<RobotSnapshot> snapshots() const;
    RobotSnapshot snapshot(int robotId) const;

    bool postCommand(uint8_t robotId, uint8_t commandType);
    bool postVelocity(uint8_t robotId, float vx, float vy, float omega);
    bool emergencyStop(uint8_t robotId);

    QString lastError() const;

signals:
    void snapshotsUpdated(const QVector<RobotSnapshot> &snapshots);
    void eventReceived(const UiEvent &event);
    void commandFailed(int robotId, QString error);

private:
    QScopedPointer<ShmBridgeBackend> m_backend;
};
```

### 5.3 `ShmBridgeBackend`

IPC 구현 상세를 숨기는 private backend다. 이 계층만 `shm_def.h`, `proto.h`, POSIX header를 include한다.

```cpp
class ShmBridgeBackend {
public:
    bool start(int maxRobots);
    void stop();

    QVector<RobotSnapshot> pollSnapshots();
    QVector<UiEvent> pollEvents();

    bool postCommand(uint8_t robotId, uint8_t commandType,
                     float vx, float vy, float omega);

    QString lastError() const;

private:
    bool openMapping(int robotId);
    void closeMappings();

    RobotSnapshot readSnapshot(int robotId);
    void readState(int robotId, RobotSnapshot &out);
    void readImage(int robotId, RobotSnapshot &out);
    void readLidar(int robotId, RobotSnapshot &out);
    QVector<UiEvent> readEvents(int robotId);

    bool pushCommand(uint8_t robotId, const CmdPacket &cmd,
                     uint8_t priority, uint8_t flags);
};
```

## 6. 데이터 흐름

### 6.1 Sensor/State fetch 흐름

```text
MainWindow
  -> BridgeClient::start(maxRobots)
  -> BridgeClient internal timer
  -> ShmBridgeBackend::pollSnapshots()
  -> read SharedData state/image/lidar/event
  -> convert to RobotSnapshot/UiEvent
  -> BridgeClient emits snapshotsUpdated/eventReceived
  -> MainWindow/widgets update UI
```

UI는 SHM lock을 잡지 않는다. lock은 backend 안에서만 잡는다.

### 6.2 Command post 흐름

```text
MainWindow button/joystick
  -> BridgeClient::postVelocity(robotId, vx, vy, omega)
  -> ShmBridgeBackend::postCommand(...)
  -> openMapping(robotId)
  -> lock SharedData.cmd_queue.mu
  -> enqueue ShmCmdEntry
  -> unlock
  -> bridge_daemon jetson_tx_thread
  -> UDP 9001
  -> Jetson
```

UI는 command queue의 `head/tail/count`를 모른다.

### 6.3 Event 흐름

```text
bridge_daemon
  -> bridge_api_publish_event()
  -> SharedData.event_log
  -> ShmBridgeBackend::readEvents()
  -> UiEvent
  -> BridgeClient::eventReceived()
  -> MainWindow log panel
```

## 7. 캡슐화 규칙

### 7.1 UI Layer 규칙

UI layer에서 금지:

- `#include "shm_def.h"`
- `#include "proto.h"` 직접 의존
- `shm_open`
- `mmap`
- `pthread_*`
- `SharedData *`
- `ShmCmdQueue`

UI layer에서 허용:

- `BridgeClient`
- `RobotSnapshot`
- `UiEvent`
- `LidarPoint`
- command enum 또는 wrapper 함수

### 7.2 Backend 규칙

Backend에서 허용:

- `shm_def.h`
- `proto.h`
- POSIX SHM API
- pthread lock
- atomic load/store

Backend 책임:

- mapping lifecycle 관리
- ABI magic/version/size 검증
- lock 범위 최소화
- SHM data를 Qt 친화 타입으로 복사
- command queue overflow/priority 정책 적용

## 8. 현재 코드에서의 대응 관계

현재:

```text
ShmMonitor
  - SHM mapping
  - state/image/lidar/event polling
  - Qt signal emit

BridgeCommandClient
  - command SHM mapping
  - cmd_queue push

MainWindow
  - ShmMonitor와 BridgeCommandClient를 각각 사용
```

목표:

```text
BridgeClient
  - UI public API
  - timer/signal 관리
  - command API

ShmBridgeBackend
  - 기존 ShmMonitor 내부 SHM 접근 코드 이동
  - 기존 BridgeCommandClient command enqueue 코드 이동

MainWindow
  - BridgeClient 하나만 사용
```

## 9. 단계적 리팩터링 계획

### Phase 1: 타입 분리

- `bridgetypes.h` 생성
- `UiEvent`, `LidarPoint`, `RobotSnapshot`을 `shmmonitor.h`에서 분리
- UI 파일들은 `bridgetypes.h`만 include

목표:

```text
UI data type과 SHM monitor 구현 분리
```

### Phase 2: Backend 분리

- `shmbridgebackend.h/cpp` 생성
- `ShmMonitor`의 mapping/read 로직을 backend로 이동
- `BridgeCommandClient`의 command enqueue 로직도 backend로 이동

목표:

```text
shm_open/mmap/pthread/SharedData 접근은 backend 안에만 존재
```

### Phase 3: BridgeClient 도입

- `bridgeclient.h/cpp` 생성
- 내부에 `ShmBridgeBackend`와 `QTimer`를 둠
- `snapshotsUpdated`, `eventReceived` signal 제공
- `postCommand`, `postVelocity`, `emergencyStop` 제공

목표:

```text
MainWindow가 BridgeClient 하나만 사용
```

### Phase 4: 기존 클래스 제거 또는 축소

- `ShmMonitor` 제거 또는 `BridgeClient` 내부 구현으로 흡수
- `BridgeCommandClient` 제거 또는 `BridgeClient::postCommand()` wrapper로 축소

목표:

```text
UI가 IPC 구현 클래스를 직접 보지 않음
```

### Phase 5: include boundary 점검

다음 명령으로 UI 계층 누수를 확인한다.

```bash
grep -R "shm_def.h\\|shm_open\\|mmap\\|pthread_\\|SharedData\\|ShmCmdQueue" disaster_control_qt/src
```

허용되는 파일:

```text
shmbridgebackend.cpp
shmbridgebackend.h
```

가능하면 header가 아니라 `.cpp`에만 POSIX/SHM include를 둔다.

## 10. 장점

### 10.1 UI 안정성

UI는 IPC 구현을 몰라서 화면 코드가 단순해진다.

### 10.2 교체 가능성

나중에 IPC가 바뀌어도 UI는 유지된다.

예:

```text
SHM backend -> UDS backend
SHM backend -> TCP backend
SHM backend -> ROS2/DDS backend
```

`BridgeClient` public API가 유지되면 `MainWindow` 변경은 작다.

### 10.3 테스트 용이성

`BridgeClient`에 fake backend를 붙이면 Qt UI를 데몬 없이 테스트할 수 있다.

```text
FakeBridgeBackend
  -> 가짜 RobotSnapshot 생성
  -> 버튼/화면 동작 테스트
```

### 10.4 ABI 사고 범위 축소

`SharedData` layout 변경으로 인한 영향을 backend에 가둘 수 있다.

## 11. 주의점

### 11.1 SHM mapping 자체는 문제 아님

Qt 프로세스가 SHM을 mapping하는 것은 성능상 자연스럽다. 문제는 UI가 mapping과 lock을 직접 다루는 것이다.

좋은 구조:

```text
Qt process
  -> BridgeClient/ShmBridgeBackend 내부에서 SHM mapping
```

나쁜 구조:

```text
MainWindow/widgets
  -> 직접 SHM mapping
```

### 11.2 데이터 복사 비용

Backend가 `SharedData`를 `RobotSnapshot`으로 복사하면 약간의 복사 비용이 생긴다. 하지만 UI 안정성과 캡슐화 이점이 더 크다.

이미지는 `QImage::fromData()`가 필요하므로 기존에도 변환 비용이 있다. LiDAR는 UI 표시용 downsample을 backend에서 수행하면 widget 코드가 단순해진다.

### 11.3 Threading

처음에는 현재처럼 UI thread `QTimer` polling으로 충분하다. 이후 필요하면 backend를 worker thread로 옮길 수 있다.

초기 권장:

```text
BridgeClient QTimer 100ms polling
```

향후 확장:

```text
BridgeClient worker thread
  -> polling
  -> queued signal to UI
```

## 12. 최종 목표 그림

```text
Jetson
  -> UDP 9000
  -> bridge_daemon
  -> /robot_bridge_N SharedData
  -> ShmBridgeBackend
  -> BridgeClient
  -> MainWindow/widgets

MainWindow/widgets
  -> BridgeClient::postCommand()
  -> ShmBridgeBackend
  -> /robot_bridge_N SharedData.cmd_queue
  -> bridge_daemon jetson_tx_thread
  -> UDP 9001
  -> Jetson
```

핵심은 다음 한 문장이다.

```text
Qt UI는 bridge_daemon의 IPC/ABI를 직접 사용하지 않고, BridgeClient API만 사용한다.
```
