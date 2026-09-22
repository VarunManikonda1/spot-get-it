# disaster_control_qt 공유메모리 추상화 리팩터링 보고서

## 1. 목적

이번 변경의 목적은 `disaster_control_qt`에서 공유메모리 접근을 UI 코드와 모니터링 코드에서 분리하고, 로봇별 공유메모리 정보를 가진 전용 객체가 모든 SHM 처리를 맡도록 구조를 바꾸는 것이다.

기존 구조에서는 `ShmMonitor`와 `BridgeCommandClient`가 각각 직접 `shm_open`, `mmap`, `pthread_rwlock`, `SharedData`, `cmd_queue`에 접근했다. 그래서 Qt 쪽 코드 여러 곳에 공유메모리 ABI 지식이 퍼져 있었다.

변경 후 구조에서는 `RobotShmConnection` 객체가 로봇 1대의 공유메모리 연결을 대표한다. 이 객체가 공유메모리 열기, 매핑, 상태 읽기, 이미지 읽기, LiDAR 읽기, 이벤트 읽기, 명령 queue push를 모두 담당한다.

## 2. 변경 전 구조

변경 전 Qt 구조는 다음과 같았다.

```text
MainWindow
  -> ShmMonitor
       -> shm_open / mmap
       -> SharedData 직접 접근
       -> meta/state/image/lidar/event 읽기

MainWindow
  -> BridgeCommandClient
       -> shm_open / mmap
       -> SharedData.cmd_queue 직접 접근
       -> 명령 push
```

문제점:

| 항목 | 문제 |
|---|---|
| 공유메모리 접근 분산 | `ShmMonitor`, `BridgeCommandClient`가 각각 SHM을 직접 열고 닫음 |
| ABI 의존성 노출 | `shmmonitor.h`에 `shm_def.h`, `SharedData *`가 노출됨 |
| 읽기/쓰기 객체 분리 | 같은 로봇의 SHM인데 상태 읽기와 명령 쓰기가 서로 다른 객체에 있음 |
| UI 확장 어려움 | 나중에 robot별 connection 상태, 재연결 정책, 알림 방식 등을 넣기 어려움 |

## 3. 변경 후 전체 구조

변경 후 Qt 구조는 다음과 같다.

```text
MainWindow
  -> ShmMonitor
       -> RobotShmConnection[0]
       -> RobotShmConnection[1]
       -> RobotShmConnection[2]
       -> RobotShmConnection[3]

RobotShmConnection
  -> /robot_bridge_N shm_open
  -> mmap SharedData
  -> 상태/이미지/LiDAR/이벤트 읽기
  -> cmd_queue 명령 쓰기
```

BridgeDaemon 쪽 구조도 명령 queue 처리 일부를 분리했다.

```text
BridgeDaemon
  -> bridge_main.c
       -> shm_cmd_queue_init()
       -> shm_cmd_queue_destroy()

  -> jetson_tx.c
       -> shm_cmd_queue_pop_highest()
       -> send_cmd_to_jetson()

  -> shm_cmd_queue.c/h
       -> cmd_queue 초기화/해제
       -> priority 계산
       -> 최고 priority command pop
```

## 4. 새로 추가된 Qt 파일

### 4.1 `shmtypes.h`

위치: `disaster_control_qt/src/shmtypes.h`

역할:

- Qt UI/모니터 계층에서 사용하는 순수 데이터 타입 정의
- `UiEvent`
- `LidarPoint`
- `RobotSnapshot`
- `kMaxRobots`
- `Q_DECLARE_METATYPE`

중요한 점은 이 파일이 `shm_def.h`를 include하지 않는다는 것이다. 즉 UI는 `SharedData` ABI를 몰라도 `RobotSnapshot`만 보고 화면을 그릴 수 있다.

### 4.2 `robotshmconnection.h`

위치: `disaster_control_qt/src/robotshmconnection.h`

로봇 1대의 공유메모리 연결 객체이다.

주요 public API:

```cpp
explicit RobotShmConnection(int robotId);
int robotId() const;
QString shmName() const;
bool isOpen() const;

RobotSnapshot poll(const std::function<void(const UiEvent &)> &eventSink);

bool sendCommand(uint8_t commandType,
                 float vx,
                 float vy,
                 float omega,
                 uint32_t seq,
                 QString *errorMessage);

void close();
```

여기서 사용자가 말한 “Qt의 스레드? 함수? 객체를 인자로 넣어줘서 공유메모리 관련 객체가 알아서 처리”하는 부분은 현재 `poll()`의 `eventSink` 콜백으로 구현되어 있다. `RobotShmConnection`이 공유메모리 이벤트를 읽고, 새 이벤트가 있으면 외부에서 넘겨준 함수 객체를 호출한다.

현재 `ShmMonitor`는 이 콜백에 Qt signal emit 함수를 넘긴다.

```cpp
snapshot = connection->poll([this](const UiEvent &event) {
    emit eventReceived(event);
});
```

즉 `RobotShmConnection`은 Qt UI를 직접 알 필요 없이, 전달받은 함수로 이벤트를 넘긴다.

### 4.3 `robotshmconnection.cpp`

위치: `disaster_control_qt/src/robotshmconnection.cpp`

현재 Qt에서 실제 공유메모리 저수준 API를 사용하는 핵심 파일이다.

담당 기능:

| 기능 | 처리 내용 |
|---|---|
| SHM open | `/robot_bridge_%N` 이름으로 `shm_open(O_RDWR)` |
| SHM map | `mmap(sizeof(SharedData), MAP_SHARED)` |
| ABI 검증 | `shm_magic == SHM_MAGIC` 확인 |
| 상태 읽기 | `meta_lock`, `state_lock`, `odom_lock` 사용 |
| 이미지 읽기 | `img_ready_idx` atomic load 후 JPEG decode |
| LiDAR 읽기 | `lidar_ready_idx` atomic load 후 point sampling |
| 이벤트 읽기 | `event_log.write_seq` 기준 ring buffer 읽기 |
| 명령 쓰기 | `cmd_queue.mu` lock 후 ring queue push |
| 리소스 정리 | `munmap`, `close` |

중요한 구조적 변화는 `void *m_data`를 헤더에 둔 것이다. 헤더에서는 `SharedData` 타입을 노출하지 않고, `.cpp` 내부에서만 `SharedData *`로 변환한다.

```cpp
void *m_data = nullptr;
```

이 덕분에 `robotshmconnection.h`를 include하는 다른 Qt 파일들은 `shm_def.h`를 몰라도 된다.

## 5. 변경된 Qt 파일

### 5.1 `shmmonitor.h/.cpp`

기존 `ShmMonitor`는 공유메모리 매핑과 데이터 읽기를 직접 수행했다. 변경 후에는 로봇별 `RobotShmConnection`을 보유하고 타이머와 signal 중계를 담당한다.

변경 후 역할:

| 역할 | 설명 |
|---|---|
| connection 관리 | `std::vector<std::unique_ptr<RobotShmConnection>>` |
| polling 주기 관리 | 기존과 동일하게 100ms `QTimer` |
| snapshot emit | `snapshotsUpdated(m_snapshots)` |
| event emit | `RobotShmConnection::poll()`에 event sink 전달 |
| 명령 중계 | `sendCommand()`가 선택 로봇 connection에 위임 |

현재 `ShmMonitor::start()` 흐름:

```text
maxRobots clamp
  -> m_snapshots resize
  -> m_connections clear
  -> robot id별 RobotShmConnection 생성
  -> poll()
  -> 100ms timer start
```

현재 `ShmMonitor::poll()` 흐름:

```text
for each robot id:
  connection = connectionForRobot(id)
  snapshot = connection->poll(eventSink)
  m_snapshots[id] = snapshot

emit snapshotsUpdated(m_snapshots)
```

기존 `ShmMonitor`에서 제거된 것:

- `shm_def.h` include
- `SharedData *`
- `Mapping` 구조체
- `openMapping()`
- `closeMappings()`
- `readState()`
- `readImage()`
- `readLidar()`
- `readEvents()`
- `pthread_*`
- `mmap/munmap`

즉 `ShmMonitor`는 더 이상 공유메모리 ABI를 직접 모른다.

### 5.2 `mainwindow.h/.cpp`

기존에는 `MainWindow`가 `BridgeCommandClient`를 멤버로 가지고 있었다.

변경 전:

```cpp
ShmMonitor m_monitor;
BridgeCommandClient m_commandClient;
```

변경 후:

```cpp
ShmMonitor m_monitor;
```

명령 전송도 `BridgeCommandClient`가 아니라 `ShmMonitor`를 통해 전달한다.

```cpp
QString errorMessage;
if (!m_monitor.sendCommand(m_selectedRobot, commandType, vx, vy, omega, &errorMessage)) {
    QMessageBox::warning(...);
    return;
}
```

따라서 `MainWindow`는 공유메모리 명령 queue의 존재를 직접 다루지 않는다. UI는 “선택 로봇에게 명령을 보낸다”는 수준의 API만 사용한다.

### 5.3 `CMakeLists.txt`

Qt 빌드 대상에서 기존 `bridgecommandclient.cpp/.h`를 제거하고 새 파일을 추가했다.

추가:

- `src/robotshmconnection.cpp`
- `src/robotshmconnection.h`
- `src/shmtypes.h`

제거:

- `src/bridgecommandclient.cpp`
- `src/bridgecommandclient.h`

## 6. 삭제된 Qt 파일

### `bridgecommandclient.cpp/.h`

삭제 이유:

- 명령 송신도 로봇별 공유메모리 객체의 책임으로 통합하기 위해서
- 같은 `/robot_bridge_N`에 대해 읽기 객체와 쓰기 객체가 따로 존재하던 구조를 줄이기 위해서
- `MainWindow`가 명령 IPC 구현체를 직접 들고 있지 않게 하기 위해서

기능은 `RobotShmConnection::sendCommand()`로 이동했다.

## 7. BridgeDaemon 변경 사항

Qt만 바꾸면 추상화 자체는 가능하지만, BridgeDaemon 쪽도 명령 queue 접근이 `bridge_main.c`, `jetson_tx.c` 내부 static 함수로 흩어져 있었다. 그래서 queue 조작 함수를 별도 파일로 분리했다.

### 7.1 `shm_cmd_queue.h`

위치: `bride_app_codex/shm_cmd_queue.h`

제공 API:

```c
void shm_cmd_queue_init(ShmCmdQueue *q, const pthread_mutexattr_t *attr);
void shm_cmd_queue_destroy(ShmCmdQueue *q);
uint8_t shm_cmd_entry_effective_priority(const ShmCmdEntry *entry);
int shm_cmd_queue_pop_highest(ShmCmdQueue *q, ShmCmdEntry *out);
```

### 7.2 `shm_cmd_queue.c`

위치: `bride_app_codex/shm_cmd_queue.c`

담당 기능:

| 함수 | 역할 |
|---|---|
| `shm_cmd_queue_init()` | process-shared mutex 초기화, head/tail/count/write_seq/drop_count 초기화 |
| `shm_cmd_queue_destroy()` | mutex destroy |
| `shm_cmd_entry_effective_priority()` | entry priority 계산, ESTOP은 critical |
| `shm_cmd_queue_pop_highest()` | queue에서 가장 높은 priority command pop |

### 7.3 `bridge_main.c`

기존에는 `shm_cmd_queue_init()`와 `shm_cmd_queue_destroy()`가 `bridge_main.c` 내부 static 함수였다. 이제 `shm_cmd_queue.h`를 include하고 분리된 함수를 호출한다.

효과:

- SHM 생성 코드가 command queue 내부 구현을 덜 알게 됨
- queue 초기화 정책을 한 파일에서 관리 가능

### 7.4 `jetson_tx.c`

기존에는 command queue pop과 priority 계산 함수가 `jetson_tx.c` 내부 static 함수였다. 이제 다음 함수를 사용한다.

```c
shm_cmd_entry_effective_priority(entry)
shm_cmd_queue_pop_highest(q, &entry)
```

효과:

- `jetson_tx.c`는 “큐에서 명령을 꺼내 Jetson으로 보낸다”에 집중
- queue 내부 pop 알고리즘은 `shm_cmd_queue.c`에 격리

### 7.5 `Makefile`

BridgeDaemon 빌드 대상에 `shm_cmd_queue.c`를 추가했다.

```make
SRCS = ... bridge_api.c shm_cmd_queue.c
```

의존성에도 `shm_cmd_queue.h`를 추가했다.

## 8. 데이터 읽기 흐름 상세

변경 후 데이터 흐름은 다음과 같다.

```text
QTimer timeout
  -> ShmMonitor::poll()
  -> RobotShmConnection::poll(eventSink)
       -> open()
       -> readState()
       -> readImage()
       -> readLidar()
       -> readEvents(eventSink)
  -> ShmMonitor::snapshotsUpdated()
  -> MainWindow::updateSnapshots()
  -> Dashboard widgets update
```

### 8.1 상태

`RobotShmConnection::readState()`가 `SharedData`에서 상태를 읽는다.

읽는 순서:

1. `meta_lock` read lock
2. `meta.jetson_connected`, drop count 읽기
3. `state_lock` read lock
4. `RobotState` 전체 복사
5. 실패 시 `odom_lock` fallback
6. `metrics` atomic counter 읽기

Qt UI는 이 결과만 `RobotSnapshot`으로 받는다.

### 8.2 이미지

이미지는 기존과 동일하게 Triple Buffer 구조를 사용한다.

```text
img_ready_idx atomic load
  -> img_slots[idx]
  -> frame_id 확인
  -> QImage::fromData(..., "JPEG")
  -> RobotSnapshot.image
```

변경점은 이 과정이 `ShmMonitor`가 아니라 `RobotShmConnection` 내부로 들어갔다는 것이다.

### 8.3 LiDAR

LiDAR도 기존과 동일하게 ready index 기반으로 최신 슬롯을 읽는다.

```text
lidar_ready_idx atomic load
  -> lidar_slots[idx]
  -> count clamp
  -> 약 500개로 sampling
  -> QVector<LidarPoint>
```

### 8.4 이벤트

이벤트는 `event_log.write_seq`와 `lastEventSeq`를 비교해 새 이벤트만 읽는다. 읽은 이벤트는 `RobotShmConnection`이 직접 UI를 호출하지 않고, 외부에서 받은 `eventSink` 함수로 전달한다.

```text
event_log.write_seq load
  -> lastEventSeq부터 새 이벤트 읽기
  -> UiEvent 생성
  -> eventSink(event)
```

이 구조는 나중에 `eventSink`를 Qt signal이 아니라 queue, worker thread, logger, test double 등으로 바꾸기 쉽다.

## 9. 명령 송신 흐름 상세

변경 후 명령 흐름은 다음과 같다.

```text
MainWindow command button
  -> MainWindow::sendCommand()
  -> ShmMonitor::sendCommand(robotId, commandType, vx, vy, omega)
  -> RobotShmConnection::sendCommand(...)
       -> open()
       -> ShmCmdEntry 생성
       -> cmd_queue.mu lock
       -> queue push
       -> tail/count/write_seq 갱신
       -> cmd_queue.mu unlock
  -> BridgeDaemon jetson_tx_thread()
       -> shm_cmd_queue_pop_highest()
       -> send_cmd_to_jetson()
```

명령 우선순위 정책:

| 명령 | priority |
|---|---|
| `CMD_TYPE_ESTOP` | `CMD_PRIORITY_CRITICAL` |
| 그 외 | `CMD_PRIORITY_NORMAL` |

`priority >= CMD_PRIORITY_HIGH`이면 `CMD_FLAG_REQUIRES_ACK`가 붙는다.

queue가 가득 찼을 때는 Qt 쪽 `RobotShmConnection`이 기존과 동일하게 낮은 priority 항목을 drop하고 새 명령을 넣으려 한다. 새 명령 priority가 기존 최저 priority보다 낮으면 push 실패로 처리한다.

## 10. 추상화 경계

현재 추상화 경계는 다음과 같다.

```text
UI / Widget 계층
  - RobotSnapshot
  - UiEvent
  - ShmMonitor signal
  - sendCommand API

Monitor 계층
  - RobotShmConnection 객체 보유
  - polling 주기 관리
  - Qt signal emit

SHM Adapter 계층
  - RobotShmConnection
  - shm_open/mmap/munmap
  - SharedData 접근
  - pthread lock
  - cmd_queue push

BridgeDaemon SHM utility 계층
  - shm_cmd_queue.c/h
  - queue init/destroy/pop/priority
```

검색 기준으로 Qt에서 저수준 공유메모리 접근은 이제 `robotshmconnection.cpp`에만 남아 있다.

남아 있는 저수준 키워드:

- `shm_def.h`
- `SharedData`
- `ShmCmdQueue`
- `shm_open`
- `mmap`
- `munmap`
- `pthread_rwlock_*`
- `pthread_mutex_*`

이 키워드들은 `disaster_control_qt/src/robotshmconnection.cpp`에 집중되어 있다. `MainWindow`, `dashboardwidgets`, `ShmMonitor`는 직접 접근하지 않는다.

## 11. 현재 구조의 장점

### 11.1 Qt UI 코드 단순화

UI는 공유메모리 구조체를 몰라도 된다.

`MainWindow`는 다음 정도만 알면 된다.

- snapshot이 오면 화면 갱신
- event가 오면 로그에 추가
- 버튼이 눌리면 selected robot에 command 전송

### 11.2 로봇 단위 모델 명확화

`RobotShmConnection` 하나가 로봇 하나의 SHM 상태를 가진다.

객체 내부 상태:

| 필드 | 의미 |
|---|---|
| `m_robotId` | 담당 로봇 id |
| `m_fd` | SHM file descriptor |
| `m_data` | mapped SHM pointer |
| `m_lastImageFrame` | 마지막 이미지 frame id |
| `m_lastLidarFrame` | 마지막 LiDAR frame id |
| `m_lastEventSeq` | 마지막으로 읽은 event sequence |

이전에는 이런 상태가 `ShmMonitor::Mapping`과 `BridgeCommandClient`에 나뉘어 있었다. 이제 한 객체에 모였다.

### 11.3 테스트와 확장 가능성 증가

`RobotShmConnection::poll()`은 event sink를 함수 객체로 받는다. 이 구조는 다음 확장에 유리하다.

- 이벤트를 Qt signal이 아닌 test lambda로 받기
- worker thread에서 이벤트 queue에 넣기
- logging 객체에 전달하기
- 추후 semaphore 기반 대기 방식으로 바꾸기

### 11.4 BridgeDaemon queue 코드 정리

`jetson_tx.c`에서 queue pop 알고리즘이 빠져서 송신 스레드의 책임이 명확해졌다.

```text
jetson_tx.c = queue 확인 + UDP 송신
shm_cmd_queue.c = queue 내부 정책
```

## 12. 현재 한계와 다음 개선 방향

### 12.1 Qt command push와 BridgeDaemon pop 구현이 아직 완전히 공유되지는 않음

BridgeDaemon 쪽 pop/init 로직은 `shm_cmd_queue.c`로 분리되었지만, Qt 쪽 command push는 C++ 파일 `robotshmconnection.cpp` 내부에 있다.

현재 이유:

- Qt는 C++/Qt 타입과 error message를 사용한다.
- 기존 ABI 변경 없이 빠르게 구조를 정리하기 위해 push는 Qt adapter 안에 유지했다.

추가 개선을 한다면 다음 방식이 가능하다.

```text
bride_app_codex/shm_cmd_queue.c
  -> shm_cmd_queue_push()
  -> shm_cmd_queue_pop_highest()

disaster_control_qt
  -> C 함수 shm_cmd_queue_push() 호출
```

이렇게 하면 command queue 알고리즘이 producer/consumer 양쪽에서 완전히 한 곳으로 통일된다.

### 12.2 아직 polling 기반

`SharedData`에는 `img_sem`, `lidar_sem`이 있지만 Qt는 여전히 100ms `QTimer` polling으로 데이터를 읽는다.

다음 단계로는 `RobotShmConnection`을 worker thread에서 실행하고 semaphore/event 기반으로 깨우는 구조를 고려할 수 있다.

예상 구조:

```text
RobotShmWorker(QThread)
  -> RobotShmConnection
  -> sem_timedwait(img_sem/lidar_sem)
  -> poll()
  -> signal snapshotReady
```

단, 현재 UI 갱신 주기가 100ms여도 충분하다면 polling 구조가 더 단순하고 안정적이다.

### 12.3 `RobotShmConnection`은 아직 QObject가 아님

현재 `RobotShmConnection`은 plain C++ 객체다. 그래서 Qt signal/slot을 직접 갖지 않는다. 대신 콜백을 받는다.

장점:

- Qt 의존성이 낮음
- 테스트하기 쉬움
- 수명 관리가 단순함

단점:

- thread affinity, queued signal 같은 Qt 기능을 직접 쓰지는 못함

필요하면 나중에 `QObject` 기반 `RobotShmWorker`를 별도로 두고, 그 안에서 `RobotShmConnection`을 소유하는 방식이 좋다.

## 13. 빌드 검증

변경 후 빌드는 둘 다 성공했다.

Qt:

```text
cmake --build disaster_control_qt/build_codex
[100%] Built target DisasterControlQt
```

BridgeDaemon:

```text
make -C bride_app_codex
빌드 완료: bridge_daemon
```

## 14. 변경 파일 목록

Qt 추가:

- `disaster_control_qt/src/shmtypes.h`
- `disaster_control_qt/src/robotshmconnection.h`
- `disaster_control_qt/src/robotshmconnection.cpp`

Qt 변경:

- `disaster_control_qt/src/shmmonitor.h`
- `disaster_control_qt/src/shmmonitor.cpp`
- `disaster_control_qt/src/mainwindow.h`
- `disaster_control_qt/src/mainwindow.cpp`
- `disaster_control_qt/CMakeLists.txt`

Qt 삭제:

- `disaster_control_qt/src/bridgecommandclient.h`
- `disaster_control_qt/src/bridgecommandclient.cpp`

BridgeDaemon 추가:

- `bride_app_codex/shm_cmd_queue.h`
- `bride_app_codex/shm_cmd_queue.c`

BridgeDaemon 변경:

- `bride_app_codex/bridge_main.c`
- `bride_app_codex/jetson_tx.c`
- `bride_app_codex/Makefile`

## 15. 결론

이번 리팩터링으로 `disaster_control_qt`의 공유메모리 접근은 로봇별 adapter 객체인 `RobotShmConnection`으로 추상화되었다. `ShmMonitor`는 더 이상 `SharedData`를 직접 다루지 않고, 로봇별 connection을 polling하고 Qt signal로 변환하는 역할만 한다. `MainWindow`도 명령 IPC 객체를 직접 들고 있지 않고, `ShmMonitor::sendCommand()`를 통해 선택 로봇에 명령을 보낸다.

BridgeDaemon에서는 command queue 처리 함수를 `shm_cmd_queue.c/h`로 분리해 queue 정책을 독립시켰다. 결과적으로 Qt와 BridgeDaemon 모두 공유메모리 세부 구현이 더 좁은 파일에 모였고, 이후 thread 기반 처리나 semaphore 기반 알림 구조로 확장하기 쉬운 형태가 되었다.
