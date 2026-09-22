# 02. `shm_def.h` 수업 자료

## 이 파일의 역할

`shm_def.h`는 BridgeDaemon과 Qt GUI가 공유하는 POSIX shared memory layout을 정의한다. sensor/state/event는 주로 daemon이 쓰고 Qt가 읽으며, command queue는 Qt가 쓰고 daemon이 읽는다. 두 process가 같은 구조체를 같은 offset으로 해석해야 하므로 이 파일은 ABI 문서에 가깝다.

## 개념 설명: Shared Memory ABI (Application Binary Interface)

socket은 process 사이 데이터를 복사해서 전달한다. 하지만 image/lidar는 크기가 크고 빈도가 높다. 그래서 이 프로젝트는 대용량 sensor frame을 POSIX shared memory에 놓고, Qt GUI가 직접 읽는다.

중요한 개념은 ABI 안정성이다.

- 구조체 필드를 중간에 삽입하면 기존 GUI가 offset을 잘못 읽는다.
- version/magic/size가 있어야 GUI가 “내가 아는 layout인지” 확인할 수 있다.
- multi-field 값은 lock이나 seqlock 같은 snapshot 보호가 필요하다.

## include 설명

| 라인 | 헤더 | 출처 | 쓰는 기능 |
|---:|---|---|---|
| 22 | `<stdint.h>` | C 표준 | 고정 폭 integer |
| 24 | `<atomic>` | C++ 표준 | Qt/C++ 빌드에서 `std::atomic` 사용 |
| 28 | `<stdatomic.h>` | C11 | C daemon 빌드에서 `_Atomic` 사용 |
| 32 | `<pthread.h>` | POSIX | process-shared rwlock/mutex |
| 33 | `<semaphore.h>` | POSIX | SHM 안에 들어가는 `sem_t` |
| 35 | `"proto.h"` | project | `CmdPacket`, command priority/flag |

## 매크로 설명

| 라인 | 이름 | 의미 |
|---:|---|---|
| 36 | `MAX_ROBOTS` | daemon이 처리할 최대 robot 수 |
| 37 | `SHM_NAME_FMT` | robot별 SHM 이름 format. `/robot_bridge_%d` |
| 38 | `SHM_MAGIC` | GUI attach 시 layout 확인용 magic |
| 39 | `SHM_VERSION` | SHM layout version. 현재 2 |
| 42 | `IMG_SLOT_SIZE` | image 1개 slot 최대 byte |
| 43 | `IMG_SLOTS` | image triple buffer slot 수 |
| 45 | `LIDAR_MAX_PTS` | lidar 최대 point 수 |
| 46 | `LIDAR_SLOTS` | lidar triple buffer slot 수 |
| 47 | `EVENT_LOG_SIZE` | event ring buffer entry 수 |
| 48 | `SHM_CMD_QUEUE_SIZE` | Qt command queue slot 수 |

## 구조체 설명

### `ImgSlot`

라인 50-55.

| 필드 | 의미 |
|---|---|
| `size` | JPEG 실제 byte 크기 |
| `timestamp_us` | 송신 timestamp |
| `frame_id` | image frame sequence |
| `data` | JPEG byte buffer |

### `LidarSlot`

라인 67-75.

LiDAR point cloud를 구조화해서 GUI가 바로 읽을 수 있게 만든다.

| 필드 | 의미 |
|---|---|
| `count` | 유효 point 수 |
| `timestamp_us` | 송신 timestamp |
| `frame_id` | lidar frame sequence |
| `x/y/z/intensity` | point attribute 배열 |

### `BridgeMeta`

라인 78-85. 기존 GUI와 호환되는 작고 빠른 metadata 영역.

| 필드 | 의미 |
|---|---|
| `jetson_connected` | watchdog 판단 연결 상태 |
| `pkt_count` | 수신 packet 수. watchdog이 변화 여부 확인 |
| `img_drop_count` | image drop 누적 |
| `lidar_drop_count` | lidar drop 누적 |
| `odom_seq` | odom sequence |
| `avg_img_latency_us` | image 평균 latency용 필드 |

### `RobotState`

라인 87-119. 새 GUI 상태 모델.

이 구조체는 “GUI가 로봇 카드/상태 패널에서 바로 읽을 수 있는 최신 상태”를 목표로 한다.

| 범주 | 필드 |
|---|---|
| snapshot | `seq`, `updated_us` |
| identity/connection | `robot_id`, `connected` |
| mode/fault | `mode`, `fault_level`, `fault_code`, `fault_text` |
| control | `control_owner` |
| pose/twist | `x`, `y`, `theta`, `vx`, `vy`, `omega` |
| power | `battery_percent`, `voltage`, `current`, `temperature` |
| network/stream | `link_rtt_ms`, `image_fps`, `lidar_fps`, `drop_rate` |
| mission | `mission_id`, `waypoint_idx`, `mission_progress`, `goal_x`, `goal_y` |
| timestamps | `last_rx_us`, `last_cmd_ack_us` |

### `RobotEvent`, `EventLog`

라인 121-133.

상태는 최신값만 의미하지만, event는 시간순 기록이 중요하다. 그래서 ring buffer를 쓴다.

- `write_seq`: producer가 증가시키는 sequence.
- `events[write_seq % EVENT_LOG_SIZE]`: 새 event가 들어갈 위치.

### `BridgeMetrics`

라인 135-144. 운영 진단용 counter와 timestamp.

| 필드 | 의미 |
|---|---|
| `rx_packets` | 수신 packet 수 |
| `tx_commands` | 송신 command 수 |
| `ack_packets` | ACK 수신 수 |
| `retry_commands` | retry 수행 수 |
| `dropped_packets` | drop 수 |
| `last_rx_us/last_tx_us/last_ack_us` | 마지막 작업 timestamp |

### `ShmCmdEntry`, `ShmCmdQueue`

Qt GUI가 BridgeDaemon에 보내는 command를 담는 SHM ring queue다. 저장은 ring 구조지만 소비자는 FIFO가 아니라 priority를 비교해 높은 priority command를 먼저 꺼낸다.

| 필드 | 의미 |
|---|---|
| `ShmCmdEntry.cmd` | Jetson으로 보낼 `CmdPacket` |
| `priority`, `flags` | dequeue 순서와 ACK/retry 정책에 쓰는 command metadata |
| `enqueue_us` | enqueue timestamp용 예약 필드 |
| `ShmCmdQueue.mu` | Qt producer와 bridge consumer 사이를 보호하는 process-shared mutex |
| `head`, `tail`, `count` | ring queue 위치와 개수 |
| `write_seq`, `drop_count` | 진단용 sequence/drop counter |

### `SharedData`

라인 147-185. robot 1대분 전체 SHM layout.

구성 순서:

1. ABI header: `shm_magic`, `shm_version`, `shared_data_size`
2. image triple buffer
3. lidar triple buffer
4. odom rwlock 영역
5. meta rwlock 영역
6. state rwlock 영역
7. event log
8. metrics
9. command queue

## 수업 포인트

- SHM은 빠르지만 구조체 layout이 곧 계약이다.
- `sem_t`를 SHM 안에 넣으려면 `sem_init(..., pshared=1, ...)`로 초기화해야 한다.
- `pthread_mutex_t`를 SHM command queue에 넣으려면 `pthread_mutexattr_setpshared(..., PTHREAD_PROCESS_SHARED)`로 초기화해야 한다.
- image/lidar는 triple buffer로 lock 없이 최신 frame을 읽게 한다.
- odom/state/meta처럼 여러 필드를 함께 읽는 데이터는 rwlock으로 snapshot을 보호한다.
- command queue는 단일 버퍼가 아니라 ring queue라서 여러 command가 짧은 시간에 들어와도 보존할 수 있고, pop할 때는 priority가 높은 command를 먼저 처리한다.

## 실습 질문

1. `SharedData` 중간에 필드를 추가하면 Qt GUI는 어떤 증상을 보일까?
2. event log가 단순 배열이 아니라 ring buffer인 이유는 무엇인가?
3. image/lidar에 rwlock을 쓰지 않고 atomic ready index를 쓰는 이유는 무엇인가?
4. command를 단일 SHM 버퍼가 아니라 ring queue에 넣는 이유는 무엇인가?
