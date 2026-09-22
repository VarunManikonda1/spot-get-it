# disaster_control_qt 공유메모리 데이터 사용 보고서

## 1. 개요

`disaster_control_qt`는 로봇 관제 Qt GUI이며, 로봇 데이터 수신과 명령 송신 모두 POSIX 공유메모리(SHM)를 통해 `BridgeDaemon`과 통신한다. 공유메모리 레이아웃은 Qt 프로젝트 내부에서 직접 정의하지 않고, `bride_app_codex/shm_def.h`를 공통 ABI 헤더로 include해서 사용한다.

전체 구조는 다음과 같다.

```text
Jetson/Robot
  -> UDP 수신/재조립
  -> BridgeDaemon
  -> /robot_bridge_N SharedData
  -> disaster_control_qt ShmMonitor
  -> 대시보드 영상/상태/지도/이벤트 표시

disaster_control_qt 명령 버튼
  -> BridgeCommandClient
  -> /robot_bridge_N SharedData.cmd_queue
  -> BridgeDaemon jetson_tx
  -> UDP 명령 송신
  -> Jetson/Robot
```

핵심은 로봇마다 독립된 공유메모리 객체를 하나씩 사용한다는 점이다. 이름은 `/robot_bridge_0`, `/robot_bridge_1` 형태이며 최대 로봇 수는 `MAX_ROBOTS = 10`이다.

## 2. 공유메모리 객체와 레이아웃

공유메모리 구조체는 `SharedData` 하나가 로봇 1대분 데이터를 담는 방식이다. 정의 위치는 `bride_app_codex/shm_def.h`이다.

주요 상수와 구조:

| 항목 | 내용 |
|---|---|
| `SHM_NAME_FMT` | `/robot_bridge_%d` |
| `SHM_MAGIC` | `0x42524745`, 매핑 유효성 확인용 |
| `IMG_SLOTS` | 3개, 이미지 Triple Buffer |
| `LIDAR_SLOTS` | 3개, LiDAR Triple Buffer |
| `EVENT_LOG_SIZE` | 1024개 이벤트 ring buffer |
| `SHM_CMD_QUEUE_SIZE` | 64개 명령 queue |

`SharedData` 내부 주요 필드:

| 필드 | 역할 | Qt 사용 방식 |
|---|---|---|
| `img_slots[3]`, `img_ready_idx` | JPEG 이미지 최신 프레임 | `img_ready_idx`를 atomic load 후 해당 슬롯 JPEG decode |
| `lidar_slots[3]`, `lidar_ready_idx` | LiDAR point cloud 최신 프레임 | `lidar_ready_idx`를 atomic load 후 x/y/z/intensity 배열 읽기 |
| `meta_lock`, `meta` | 연결 상태, drop count 등 | read lock 후 snapshot에 반영 |
| `state_lock`, `state` | 로봇 상태 통합 snapshot | read lock 후 위치, 배터리, fps, 임무, fault 정보 복사 |
| `odom_lock`, odom 필드 | odometry fallback | `state_lock` 실패 시 odom만 읽는 fallback |
| `event_log` | 이벤트 ring buffer | `write_seq` 기준 신규 이벤트만 읽음 |
| `metrics` | rx/tx/ack 통계 | atomic load |
| `cmd_queue` | Qt -> BridgeDaemon 명령 queue | Qt가 mutex lock 후 push |

공유메모리 생성과 초기화는 `BridgeDaemon` 쪽에서 수행한다. `bridge_main.c`의 `shm_init_one()`이 기존 SHM을 unlink하고, `shm_open(O_CREAT | O_RDWR)`, `ftruncate(sizeof(SharedData))`, `mmap(MAP_SHARED)` 순서로 로봇별 공유메모리를 만든다. 이후 ready index를 `-1`로 초기화하고, semaphore, process-shared rwlock, process-shared mutex를 초기화한다.

## 3. Qt에서 공유메모리 연결

Qt 쪽 공유메모리 읽기 담당 클래스는 `ShmMonitor`이다.

`MainWindow` 생성 시 다음 순서로 동작한다.

1. `ShmMonitor::snapshotsUpdated`를 `MainWindow::updateSnapshots`에 연결한다.
2. `ShmMonitor::eventReceived`를 `MainWindow::appendEvent`에 연결한다.
3. `/dev/shm/robot_bridge_0`이 없으면 `BridgeDaemon` 자동 실행을 시도한다.
4. `m_monitor.start(4)`로 기본 4대 로봇 감시를 시작한다.

`ShmMonitor::start()`는 감시할 로봇 수를 `1..MAX_ROBOTS` 범위로 제한하고, 즉시 `poll()`을 한 번 수행한 뒤 100ms 주기 타이머를 시작한다. 따라서 GUI는 약 10Hz로 공유메모리를 스캔한다.

로봇별 연결은 `ShmMonitor::openMapping(id)`에서 한다.

```text
id
  -> "/robot_bridge_%id" 이름 생성
  -> shm_open(name, O_RDWR, 0666)
  -> mmap(sizeof(SharedData), MAP_SHARED)
  -> shm_magic == SHM_MAGIC 확인
  -> event_log.write_seq를 lastEventSeq로 저장
```

`shm_magic` 검사를 통과하지 못하면 해당 공유메모리는 잘못된 ABI 또는 초기화되지 않은 메모리로 보고 닫는다.

## 4. 데이터 읽기 흐름

`ShmMonitor::poll()`은 감시 대상 로봇 수만큼 반복하면서 다음 데이터를 읽는다.

```text
for each robot id:
  openMapping(id)
  readState()
  readImage()
  readLidar()
  readEvents()
  m_snapshots[id] = snapshot

emit snapshotsUpdated(m_snapshots)
```

Qt 내부에서는 공유메모리 구조체를 UI에 직접 넘기지 않는다. 대신 `RobotSnapshot`이라는 Qt용 구조체로 복사해 전달한다. 이 구조체에는 연결 상태, 위치, 속도, 배터리, fps, drop count, 이미지, LiDAR point vector, 이벤트 관련 값 등이 들어간다.

### 4.1 상태 데이터

`readState()`는 먼저 `meta_lock` read lock을 잡고 `meta.jetson_connected`, `img_drop_count`, `lidar_drop_count`를 읽는다. 그 다음 `state_lock` read lock을 잡아 `RobotState` 전체를 지역 변수로 복사한다.

`RobotState`에서 Qt snapshot으로 복사되는 값:

| RobotState | RobotSnapshot/UI 의미 |
|---|---|
| `connected` | 온라인/대기/미연결 표시 |
| `mode`, `fault_level` | 상태/장애 수준 |
| `x`, `y`, `theta` | 지도상 로봇 위치와 방향 |
| `vx`, `vy`, `omega` | 속도 |
| `battery_percent` | 배터리 |
| `link_rtt_ms` | 통신 RTT |
| `image_fps`, `lidar_fps` | 센서 수신 fps |
| `drop_rate` | 드롭률 |
| `mission_id`, `waypoint_idx`, `mission_progress` | 임무 진행 정보 |
| `fault_code`, `fault_text` | 장애 정보 |
| `last_rx_us` | 마지막 수신 시각 |

`state_lock`을 잡지 못하면 fallback으로 `odom_lock`을 사용해 위치와 속도만 읽는다. 마지막으로 `metrics.rx_packets`, `metrics.tx_commands`, `metrics.ack_packets`를 atomic load해서 통계에 반영한다.

### 4.2 이미지 데이터

이미지는 lock을 사용하지 않고 Triple Buffer + atomic ready index 방식으로 읽는다.

1. `img_ready_idx.load(memory_order_acquire)`로 최신 슬롯 번호를 읽는다.
2. 슬롯 번호가 `0..IMG_SLOTS-1` 범위인지 확인한다.
3. `ImgSlot.size`가 0이 아니고 `IMG_SLOT_SIZE` 이하인지 확인한다.
4. `frame_id`가 이전 프레임과 다르면 `QImage::fromData(slot.data, slot.size, "JPEG")`로 JPEG를 디코딩한다.
5. 성공하면 `RobotSnapshot.image`에 저장하고 `lastImageFrame`을 갱신한다.

BridgeDaemon 쪽은 완성된 JPEG를 빈 슬롯에 복사한 뒤 마지막에 `img_ready_idx`를 갱신한다. 따라서 Qt는 최신 슬롯 index만 보고 완성된 프레임을 가져오는 구조다.

주의할 점은 `shm_def.h`에는 `img_sem`이 정의되어 있지만, 현재 Qt 구현은 semaphore wait 방식이 아니라 100ms polling 방식으로 이미지를 읽는다는 점이다.

### 4.3 LiDAR 데이터

LiDAR도 이미지와 같은 Triple Buffer + atomic ready index 방식이다.

1. `lidar_ready_idx.load(memory_order_acquire)`로 최신 슬롯 번호를 읽는다.
2. 슬롯 번호가 `0..LIDAR_SLOTS-1` 범위인지 확인한다.
3. `slot.count`를 `LIDAR_MAX_PTS`로 clamp한다.
4. UI 부하를 줄이기 위해 최대 약 500개 수준으로 down-sampling한다.
5. `x[i]`, `y[i]`, `z[i]`, `intensity[i]`를 `QVector<LidarPoint>`에 넣는다.

LiDAR 원본 슬롯은 최대 8192점을 담을 수 있지만, Qt snapshot에는 표시용으로 샘플링된 점만 들어간다.

### 4.4 이벤트 로그

이벤트는 `EventLog` ring buffer로 전달된다. Qt는 `event_log.write_seq`를 atomic load한 뒤, 이전에 읽은 `lastEventSeq`부터 현재 `writeSeq` 직전까지 이벤트를 순서대로 읽는다.

읽지 못한 이벤트가 `EVENT_LOG_SIZE`보다 많으면 오래된 이벤트는 이미 덮였다고 판단하고 `writeSeq - EVENT_LOG_SIZE`부터 읽는다. 이벤트 메시지가 비어 있으면 `event type X code Y` 형태의 fallback 문구를 만든다.

읽은 이벤트는 `eventReceived(UiEvent)` signal로 `MainWindow::appendEvent()`에 전달되어 이벤트 목록과 로그 페이지에 표시된다.

## 5. UI에서 데이터 사용 방식

`MainWindow::updateSnapshots()`는 `ShmMonitor`가 만든 `QVector<RobotSnapshot>`을 받아 전체 대시보드를 갱신한다.

주요 사용처:

| UI 영역 | 사용하는 snapshot 데이터 |
|---|---|
| 상단 메트릭 | 연결 로봇 수, drop count, rx packet count |
| 영상 타일 | `snapshot.image`, `connected`, `shmOpen` |
| 로봇 상태 row | 연결 상태, 배터리, rx packet |
| 지도 | 로봇 위치 `x/y/theta`, `lidarPoints`, `lidarFrameId` |
| 운용 제어 정보 | 선택 로봇의 배터리, RTT, 임무 진행률, waypoint, 좌표, fps |
| 시스템 상태 문구 | 연결 로봇 수가 0보다 크면 정상, 아니면 브릿지 대기 |

영상은 `VideoTile::setSnapshot()`에서 `QImage`를 `QPixmap`으로 바꿔 tile 크기에 맞게 표시한다. 이미지가 없으면 SHM 상태에 따라 `VIDEO WAITING` 또는 `SHM NOT OPEN`을 표시한다.

지도는 `MapWidget`이 2D와 3D view를 동시에 관리한다. 2D view는 선택된 로봇의 LiDAR point를 로봇 pose 기준으로 월드 좌표에 누적해 그린다. 3D view는 선택된 로봇의 최신 LiDAR frame을 OpenGL VBO에 올려 point cloud와 간단한 wireframe으로 표시한다.

## 6. 명령 송신 흐름

Qt에서 로봇 명령은 `BridgeCommandClient`가 공유메모리의 `cmd_queue`에 넣는다. `MainWindow`의 버튼은 다음 command type을 사용한다.

| 버튼 | command type | 추가 값 |
|---|---|---|
| 이동 | `CMD_TYPE_MOVE` | `vx=0.25`, `vy=0`, `omega=0` |
| 탐색 | `CMD_TYPE_START_MISSION` | 없음 |
| 대기 | `CMD_TYPE_PAUSE_MISSION` | 없음 |
| 중계/정지 | `CMD_TYPE_STOP` | 없음 |
| 긴급 정지 | `CMD_TYPE_ESTOP` | critical priority |

`BridgeCommandClient::sendCommand()`의 처리 순서:

```text
robotId 검증
  -> /robot_bridge_N shm_open
  -> mmap SharedData
  -> shm_magic 확인
  -> ShmCmdEntry 작성
  -> cmd_queue.mu mutex lock
  -> ring queue tail에 entry 저장
  -> tail/count/write_seq atomic 갱신
  -> mutex unlock
  -> munmap
```

명령 우선순위는 기본 normal이며, `CMD_TYPE_ESTOP`은 critical로 설정된다. `priority >= CMD_PRIORITY_HIGH`이면 `CMD_FLAG_REQUIRES_ACK` flag도 붙는다.

큐가 꽉 찬 경우 현재 큐에서 가장 낮은 priority 항목을 찾아 제거할 수 있다. 새 명령의 priority가 기존 최저 priority보다 낮으면 새 명령은 넣지 않고 실패한다. 즉 긴급 정지 같은 중요 명령이 일반 명령 때문에 밀리지 않도록 설계되어 있다.

BridgeDaemon 쪽 `jetson_tx_thread()`는 각 로봇의 `SharedData.cmd_queue`를 round-robin으로 확인하고, queue 안에서는 가장 높은 priority 명령을 골라 pop한 뒤 UDP로 Jetson에 보낸다.

## 7. 동기화 전략

이 코드의 동기화 방식은 데이터 종류별로 다르다.

| 데이터 | 동기화 방식 | 이유 |
|---|---|---|
| 이미지 | Triple Buffer + atomic ready index | 큰 JPEG 데이터를 lock 없이 최신 프레임 중심으로 읽기 위함 |
| LiDAR | Triple Buffer + atomic ready index | 큰 point cloud 배열을 lock 없이 최신 프레임 중심으로 읽기 위함 |
| 메타 | process-shared `pthread_rwlock_t` | 여러 필드의 일관된 읽기 |
| 상태 | process-shared `pthread_rwlock_t` | `RobotState` 전체 snapshot 일관성 |
| odom | process-shared `pthread_rwlock_t` | 상태 lock 실패 시 fallback |
| metrics | atomic | 단일 counter 값이라 lock 불필요 |
| event log | atomic write sequence + ring buffer | append-only 이벤트 흐름 |
| 명령 queue | process-shared mutex + atomic head/tail/count | Qt producer와 BridgeDaemon consumer 간 queue 보호 |

현재 Qt는 `img_sem`, `lidar_sem`을 기다리지 않는다. 공유메모리 안에 semaphore는 준비되어 있지만, `ShmMonitor`는 `QTimer` 기반 polling으로 최신 index를 확인한다.

## 8. 장애/대기 상태 처리

Qt는 공유메모리 연결 상태와 로봇 연결 상태를 구분한다.

| 상태 | 의미 | UI 표시 |
|---|---|---|
| `shmOpen = false` | `/robot_bridge_N`을 열지 못함 | 미연결, `SHM NOT OPEN` |
| `shmOpen = true`, `connected = false` | SHM은 있지만 Jetson/Robot 연결 전 또는 수신 대기 | 대기, `VIDEO WAITING` |
| `connected = true` | RobotState 또는 meta 기준 연결됨 | 온라인, `LIVE` |

또한 Qt 시작 시 `/dev/shm/robot_bridge_0`이 없으면 `BridgeDaemon` 실행 파일을 찾아 자동 실행한다. 이미 공유메모리가 있으면 기존 daemon이 실행 중이라고 보고 감시만 시작한다.

## 9. 코드 근거

주요 코드 위치:

| 파일 | 역할 |
|---|---|
| `bride_app_codex/shm_def.h` | 공유메모리 ABI, `SharedData` 정의 |
| `bride_app_codex/bridge_main.c` | 로봇별 SHM 생성/초기화 |
| `bride_app_codex/reassembly_shm.c` | 이미지/LiDAR frame을 SHM 슬롯에 commit |
| `bride_app_codex/jetson_tx.c` | Qt가 넣은 `cmd_queue` 명령을 pop해 Jetson으로 송신 |
| `disaster_control_qt/src/shmmonitor.h/.cpp` | Qt 쪽 SHM mapping, polling, snapshot 생성 |
| `disaster_control_qt/src/bridgecommandclient.cpp` | Qt 쪽 명령 queue push |
| `disaster_control_qt/src/mainwindow.cpp` | snapshot/event signal 연결, UI 갱신, 명령 버튼 |
| `disaster_control_qt/src/dashboardwidgets.cpp` | 영상 타일, 상태 row, 2D/3D LiDAR 지도 표시 |

## 10. 결론

`disaster_control_qt`는 공유메모리를 단순한 데이터 저장소가 아니라 로봇 관제용 IPC 버스로 사용한다. BridgeDaemon은 로봇별 `/robot_bridge_N`에 센서, 상태, 이벤트, 통계를 publish하고, Qt는 100ms 주기로 이를 읽어 `RobotSnapshot`으로 변환해 UI를 갱신한다. 반대로 Qt에서 발생한 운용 명령은 같은 공유메모리의 `cmd_queue`에 push되고, BridgeDaemon이 이를 읽어 Jetson으로 UDP 전송한다.

이 구조의 장점은 이미지와 LiDAR 같은 큰 데이터를 socket 재전송 없이 같은 장비 내부에서 바로 공유할 수 있고, 명령도 같은 ABI 안에서 처리해 IPC 경로를 통일한다는 점이다. 단, 현재 Qt는 semaphore 기반 즉시 알림을 쓰지 않고 polling을 사용하므로, UI 갱신 지연은 기본적으로 최대 약 100ms 주기에 묶인다.
