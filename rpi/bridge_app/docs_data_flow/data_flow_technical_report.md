# BridgeDaemon 데이터별 송수신 기술 보고서

대상 코드베이스: `bride_app_codex`

이 보고서는 현재 C 코드 기준으로 데이터 종류별 송신/수신 흐름을 파일, 스레드, 주요 함수, 동기화 수단 단위로 정리한다. 전체 구조는 Jetson, RPi5 BridgeDaemon, Qt GUI, 원격 PC 사이의 중계 구조이다.

## 1. 전체 구조 요약

### 실행 진입점

- 파일: `bridge_main.c`
- 함수: `main()`
- 역할:
  - 로봇 수를 인자로 받는다.
  - 로봇별 공유메모리 `/robot_bridge_N`을 생성한다.
  - 로봇별 `FragQueue`를 만든다.
  - 공통 API 컨텍스트 `BridgeApi`를 초기화한다.
  - 스레드를 생성한다.
  - 메인 스레드는 `watchdog_loop()`를 수행한다.

### 생성되는 스레드

| 스레드 | 파일 | 함수 | 역할 |
|---|---|---|---|
| main/watchdog | `bridge_main.c` | `watchdog_loop()` | 수신 패킷 증가 여부로 연결 상태 감시 |
| Jetson 수신 | `jetson_rx.c` | `jetson_rx_thread()` | UDP 9000에서 Jetson 패킷 수신 |
| 이미지/라이다 재조립 | `reassembly_shm.c` | `reassembly_shm_thread()` | fragment를 frame으로 조립 후 SHM에 publish |
| Jetson 명령 송신 | `jetson_tx.c` | `jetson_tx_thread()` | Qt가 SHM `cmd_queue`에 넣은 명령을 Jetson UDP 9001로 송신 |
| 프로토콜 타이머 | `protocol_timer.c` | `protocol_timer_thread()` | 1Hz heartbeat, ACK timeout/retry |
| PC 연동 | `pc_link.c` | `pc_link_thread()` | PC UDP 9002 명령 수신, 상태 송신 |

### 주요 통신 채널

| 구간 | 방식 | 포트/경로 | 정의 위치 |
|---|---|---|---|
| Jetson -> BridgeDaemon | UDP 수신 | `BRIDGE_PORT` = 9000 | `proto.h` |
| BridgeDaemon -> Jetson | UDP 송신 | `JETSON_CMD_PORT` = 9001 | `proto.h` |
| PC <-> BridgeDaemon | UDP | `PC_LINK_PORT` = 9002 | `proto.h` |
| Qt -> BridgeDaemon 명령 | POSIX SHM queue | `/robot_bridge_N`의 `SharedData.cmd_queue` | `shm_def.h` |
| BridgeDaemon -> Qt 데이터 | POSIX SHM | `/robot_bridge_0`, ... | `shm_def.h` |

## 2. 공통 패킷 구조

모든 Jetson UDP 패킷은 다음 형태를 따른다.

```text
[ PktHeader ][ payload ]
```

파일: `proto.h`

핵심 구조체:

- `PktHeader`
- `OdomPayload`
- `CmdPayload`
- `CmdPacket`
- `CmdAckPayload`
- `PcStatusPacketV2`

`PktHeader.type`으로 데이터 종류를 구분한다.

| 타입 | 값 | 현재 코드상 처리 |
|---|---:|---|
| `PKT_TYPE_IMAGE` | `0x01` | 수신 후 fragment queue, 재조립, SHM publish |
| `PKT_TYPE_LIDAR` | `0x02` | 수신 후 fragment queue, 재조립, SHM publish |
| `PKT_TYPE_ODOM` | `0x03` | 수신 즉시 SHM odom/state 갱신 |
| `PKT_TYPE_CMD` | `0x04` | RPi5 -> Jetson 송신용 |
| `PKT_TYPE_CMD_ACK` | `0x05` | 수신 후 pending command 해제 |
| `PKT_TYPE_STATE` | `0x06` | 타입 정의만 있음. 현재 `jetson_rx_thread()`에서 별도 처리 없음 |
| `PKT_TYPE_EVENT` | `0x07` | 타입 정의만 있음. 현재 수신 dispatch 처리 없음 |
| `PKT_TYPE_MISSION` | `0x08` | 타입 정의만 있음. 현재 수신 dispatch 처리 없음 |
| `PKT_TYPE_HEALTH` | `0x09` | 타입 정의만 있음. 현재 수신 dispatch 처리 없음 |
| `PKT_TYPE_CAPABILITY` | `0x0A` | 타입 정의만 있음. 현재 수신 dispatch 처리 없음 |

수신 패킷 검증은 `proto_validate_header()`가 담당한다.

## 3. 공유메모리 구조

파일: `shm_def.h`

로봇 1대당 `SharedData` 1개가 만들어진다.

| 영역 | 데이터 | 동기화 |
|---|---|---|
| `img_slots[3]` | JPEG 이미지 frame | `img_ready_idx` atomic + `img_sem` |
| `lidar_slots[3]` | 3D LiDAR point cloud | `lidar_ready_idx` atomic + `lidar_sem` |
| odom 필드 | 위치/속도 | `odom_lock` rwlock |
| `meta` | 연결 여부, drop count, packet count | atomic + `meta_lock` |
| `state` | GUI/PC용 로봇 상태 snapshot | `state_lock` |
| `event_log` | 연결, 명령, drop 등 이벤트 | atomic write sequence |
| `metrics` | rx/tx/ack/retry/drop 카운터 | atomic 일부 + timestamp |
| `cmd_queue` | Qt -> BridgeDaemon 우선순위 명령 queue | process-shared mutex + atomic head/tail/count |

초기화는 `bridge_main.c`의 `shm_init_one()`과 `bridge_api.c`의 `bridge_api_init_shm()`에서 수행된다.

## 4. 이미지 수신 흐름

### 방향

Jetson -> BridgeDaemon -> Qt GUI

### 흐름

```text
Jetson
  -> UDP 9000
  -> jetson_rx_thread()
  -> frag_queue_push()
  -> reassembly_shm_thread()
  -> commit_image()
  -> SharedData.img_slots[]
  -> img_ready_idx 갱신
  -> sem_post(img_sem)
  -> Qt GUI가 SHM에서 읽음
```

### 사용 파일/함수

| 단계 | 파일 | 함수/코드 |
|---|---|---|
| UDP 소켓 생성 | `jetson_rx.c` | `create_jetson_socket()` |
| UDP 수신 | `jetson_rx.c` | `jetson_rx_thread()`의 `recvfrom()` |
| 헤더 검증 | `proto.h` | `proto_validate_header()` |
| robot_id 검증 | `jetson_rx.c` | `jetson_rx_thread()` |
| Jetson 주소 학습 | `jetson_rx.c` | `learn_addr()` |
| 수신 통계/연결 갱신 | `bridge_api.c` | `bridge_api_note_rx()` |
| 이미지 dispatch | `jetson_rx.c` | `case PKT_TYPE_IMAGE` |
| fragment queue 저장 | `frag_queue.h` | `frag_queue_push()` |
| queue pop | `frag_queue.h` | `frag_queue_pop()` |
| 재조립 | `reassembly_shm.c` | `find_slot()`, `alloc_slot()`, `expire_stale()` |
| SHM publish | `reassembly_shm.c` | `commit_image()` |
| frame ready 통계 | `bridge_api.c` | `bridge_api_note_frame_ready()` |

### 스레드

- 수신: `jetson_rx_thread`
- 재조립/publish: 로봇별 `reassembly_shm_thread`
- 소비: Qt GUI 프로세스가 `/robot_bridge_N`을 attach해서 읽는 외부 reader

### 버퍼링

- 네트워크 fragment 임시 저장: `FragQueue`
- frame 재조립 임시 저장: `ReasmSlot`
- 최종 공유메모리: `SharedData.img_slots[IMG_SLOTS]`
- 최신 frame 인덱스: `img_ready_idx`
- 새 frame 알림: `img_sem`

### 예외 처리

- fragment timeout: `expire_stale()`이 100ms 초과 슬롯 폐기
- 재조립 슬롯 부족: `alloc_slot()` 실패 시 drop 기록
- fragment index/offset 이상: 해당 frame 폐기
- 이미지 크기 초과: `commit_image()`에서 `IMG_SLOT_SIZE` 초과 시 publish하지 않음

## 5. LiDAR 수신 흐름

### 방향

Jetson -> BridgeDaemon -> Qt GUI

### 흐름

```text
Jetson
  -> UDP 9000
  -> jetson_rx_thread()
  -> frag_queue_push()
  -> reassembly_shm_thread()
  -> commit_lidar()
  -> SharedData.lidar_slots[]
  -> lidar_ready_idx 갱신
  -> sem_post(lidar_sem)
  -> Qt GUI가 SHM에서 읽음
```

### 사용 파일/함수

이미지와 수신, queue, 재조립 단계는 동일하다. 최종 publish 함수만 다르다.

| 단계 | 파일 | 함수/코드 |
|---|---|---|
| LiDAR dispatch | `jetson_rx.c` | `case PKT_TYPE_LIDAR` |
| fragment queue 저장 | `frag_queue.h` | `frag_queue_push()` |
| 재조립 | `reassembly_shm.c` | `reassembly_shm_thread()` |
| SHM publish | `reassembly_shm.c` | `commit_lidar()` |
| frame ready 통계 | `bridge_api.c` | `bridge_api_note_frame_ready()` |

### LiDAR payload 형식

파일: `shm_def.h`, `reassembly_shm.c`

```text
[ uint32_t count ]
[ float x ][ float y ][ float z ][ float intensity ] * count
```

`commit_lidar()`는 count를 읽고, `LIDAR_MAX_PTS`를 넘으면 clipping한 뒤 `LidarSlot.x/y/z/intensity` 배열에 분리 저장한다.

## 6. Odom 수신 흐름

### 방향

Jetson -> BridgeDaemon -> Qt GUI/PC 상태

### 흐름

```text
Jetson
  -> UDP 9000
  -> jetson_rx_thread()
  -> handle_odom()
  -> bridge_api_update_odom()
  -> SharedData odom 필드 갱신
  -> SharedData.state 갱신
  -> Qt GUI/pc_link 상태 송신에서 읽음
```

### 사용 파일/함수

| 단계 | 파일 | 함수/코드 |
|---|---|---|
| UDP 수신 | `jetson_rx.c` | `jetson_rx_thread()` |
| Odom dispatch | `jetson_rx.c` | `case PKT_TYPE_ODOM` |
| payload 크기 확인 | `jetson_rx.c` | `handle_odom()` |
| odom SHM 기록 | `bridge_api.c` | `bridge_api_update_odom()` |
| state snapshot 갱신 | `bridge_api.c` | `bridge_api_update_odom()` |

### 스레드

- 수신/기록: `jetson_rx_thread`
- 소비:
  - Qt GUI가 SHM의 odom/state를 읽음
  - `pc_link_thread()`가 `bridge_api_snapshot_status()`로 PC 상태 패킷을 구성할 때 읽음

### 동기화

- odom 필드: `odom_lock` write lock
- state snapshot: `state_lock` write lock

Odom은 이미지/라이다처럼 fragment queue를 거치지 않는다. 작은 단일 패킷이므로 수신 스레드가 바로 공유메모리에 기록한다.

## 7. 명령 송신 흐름: Qt GUI -> Jetson

### 방향

Qt GUI -> BridgeDaemon -> Jetson

### 흐름

```text
Qt GUI
  -> /robot_bridge_N SharedData.cmd_queue
  -> jetson_tx_thread()
  -> shm_cmd_queue_pop()
  -> send_cmd_to_jetson()
  -> bridge_api_send_command()
  -> send_legacy_cmd()
  -> UDP 9001
  -> Jetson
```

### 사용 파일/함수

| 단계 | 파일 | 함수/코드 |
|---|---|---|
| SHM command queue 초기화 | `bridge_main.c` | `shm_cmd_queue_init()` |
| Qt command enqueue | `disaster_control_qt/src/bridgecommandclient.cpp` | `pushCommandToShm()` |
| 최고 priority `CmdPacket` 선택 | `jetson_tx.c` | `shm_cmd_queue_pop()` |
| priority/ACK flag 결정 | Qt enqueue + `jetson_tx.c` fallback | `BridgeCommandClient::sendCommand()`, `effective_priority()` |
| robot 간 공정 스케줄링 | `jetson_tx.c` | round-robin `next_robot` |
| 공통 송신 함수 호출 | `cmd_dispatch.c` | `send_cmd_to_jetson()` |
| 주소 조회 | `bridge_api.c` | `lookup_addr()` |
| UDP packet 생성/송신 | `bridge_api.c` | `send_legacy_cmd()` |
| pending command 등록 | `bridge_api.c` | `track_pending()` |
| event/metrics 기록 | `bridge_api.c` | `bridge_api_send_command()` |

### Jetson 주소 학습

명령을 보내려면 Jetson의 IP 주소가 필요하다. 이 주소는 `jetson_rx_thread()`가 처음 Jetson 패킷을 받을 때 `learn_addr()`로 `JetsonAddrTable`에 저장한다. 따라서 해당 로봇에서 아직 아무 패킷도 수신하지 않았다면 명령 송신은 실패한다.

### ACK 필요 조건

`bridge_api_send_command()`는 다음 명령을 ACK 필요 명령으로 취급한다.

- `CMD_TYPE_ESTOP`
- `CMD_TYPE_STOP`
- `CMD_TYPE_START_MISSION`
- `CMD_TYPE_CANCEL_MISSION`
- 또는 `CMD_FLAG_REQUIRES_ACK` flag가 지정된 명령

ACK가 필요한 명령은 `PendingCommand` 배열에 등록되고 timeout/retry 대상이 된다.

## 8. 명령 송신 흐름: PC -> Jetson

### 방향

PC -> BridgeDaemon -> Jetson

### 흐름

```text
PC
  -> UDP 9002
  -> pc_link_thread()
  -> send_cmd_to_jetson()
  -> bridge_api_send_command()
  -> UDP 9001
  -> Jetson
```

### 사용 파일/함수

| 단계 | 파일 | 함수/코드 |
|---|---|---|
| PC UDP socket 생성 | `pc_link.c` | `create_pc_socket()` |
| PC 명령 수신 | `pc_link.c` | `pc_link_thread()`의 `recvfrom()` |
| PC 주소 학습 | `pc_link.c` | `pc_link_thread()` |
| 공통 명령 송신 | `cmd_dispatch.c` | `send_cmd_to_jetson()` |
| Jetson UDP 송신 | `bridge_api.c` | `bridge_api_send_command()`, `send_legacy_cmd()` |

PC와 Qt의 명령은 입력 통로만 다르고, 최종 Jetson 송신은 같은 `bridge_api_send_command()`를 사용한다. Qt 명령은 로봇별 SHM command queue를 통해 들어온다.

## 9. Heartbeat 송신 흐름

### 방향

BridgeDaemon -> Jetson

### 흐름

```text
protocol_timer_thread()
  -> 1초마다 등록된 robot 순회
  -> send_heartbeat()
  -> bridge_api_send_command()
  -> UDP 9001
  -> Jetson
```

### 사용 파일/함수

| 단계 | 파일 | 함수/코드 |
|---|---|---|
| 타이머 스레드 | `protocol_timer.c` | `protocol_timer_thread()` |
| heartbeat 명령 구성 | `protocol_timer.c` | `send_heartbeat()` |
| 공통 명령 송신 | `bridge_api.c` | `bridge_api_send_command()` |
| timeout/retry 검사 | `bridge_api.c` | `bridge_api_poll_timeouts()` |

heartbeat는 `CMD_TYPE_HEARTBEAT` 명령으로 송신된다.

## 10. 명령 ACK 수신 흐름

### 방향

Jetson -> BridgeDaemon

### 흐름

```text
Jetson
  -> UDP 9000, PKT_TYPE_CMD_ACK
  -> jetson_rx_thread()
  -> bridge_api_handle_ack()
  -> pending command 해제
  -> metrics/state/event 갱신
```

### 사용 파일/함수

| 단계 | 파일 | 함수/코드 |
|---|---|---|
| ACK 수신 | `jetson_rx.c` | `case PKT_TYPE_CMD_ACK` |
| ACK 처리 | `bridge_api.c` | `bridge_api_handle_ack()` |
| pending command 해제 | `bridge_api.c` | `bridge_api_handle_ack()` |
| RTT/state 갱신 | `bridge_api.c` | `bridge_api_handle_ack()` |
| timeout/retry | `bridge_api.c` | `bridge_api_poll_timeouts()` |

`bridge_api_poll_timeouts()`는 `jetson_tx_thread()`, `pc_link_thread()`, `protocol_timer_thread()`에서 주기적으로 호출된다.

## 11. PC 상태 송신 흐름

### 방향

BridgeDaemon -> PC

### 흐름

```text
pc_link_thread()
  -> PC 주소가 학습된 뒤 1초마다
  -> send_status_all()
  -> bridge_api_snapshot_status()
  -> PcStatusPacketV2 구성
  -> UDP sendto()
  -> PC
```

### 사용 파일/함수

| 단계 | 파일 | 함수/코드 |
|---|---|---|
| PC 주소 학습 | `pc_link.c` | `pc_link_thread()` |
| 주기 체크 | `pc_link.c` | `pc_link_thread()` |
| 전체 로봇 상태 송신 | `pc_link.c` | `send_status_all()` |
| 상태 snapshot 생성 | `bridge_api.c` | `bridge_api_snapshot_status()` |

PC 상태 송신은 PC가 먼저 UDP 9002로 한 번이라도 패킷을 보내 주소가 등록된 뒤에 가능하다.

## 12. 연결 상태와 이벤트 흐름

### 연결 감지

`jetson_rx_thread()`는 모든 정상 수신 패킷마다 `bridge_api_note_rx()`를 호출한다.

`bridge_api_note_rx()`는 다음을 수행한다.

- `meta.jetson_connected = 1`
- `meta.pkt_count++`
- `metrics.rx_packets++`
- `state.connected = 1`
- 처음 연결된 경우 event log에 connected 이벤트 기록

### 연결 끊김 감지

`bridge_main.c`의 `watchdog_loop()`는 1초마다 `meta.pkt_count` 변화 여부를 본다. 3초 동안 packet count가 증가하지 않으면 해당 로봇을 disconnected로 판단한다.

### 이벤트 기록

파일: `bridge_api.c`

함수: `bridge_api_publish_event()`

기록되는 대표 이벤트:

- 연결됨
- 연결 끊김
- 명령 송신
- 명령 ACK
- 명령 timeout
- packet drop

이벤트는 `SharedData.event_log` ring buffer에 저장된다.

## 13. 미구현 또는 제한 사항

현재 코드 기준으로 다음 타입은 `proto.h`에 정의되어 있지만 `jetson_rx_thread()`의 switch에서 별도 처리되지 않는다.

- `PKT_TYPE_STATE`
- `PKT_TYPE_EVENT`
- `PKT_TYPE_MISSION`
- `PKT_TYPE_HEALTH`
- `PKT_TYPE_CAPABILITY`

따라서 Jetson이 이 타입을 보내면 `proto_validate_header()`의 타입 검증은 통과할 수 있지만, 현재 dispatch에서는 `default`로 떨어져 알 수 없는 타입 로그만 남긴다.

또한 이미지/라이다 `FragQueue` 포화 시 가장 오래된 fragment를 덮어쓰지만, 이 순간 별도 drop metric을 직접 증가시키지는 않는다. 이후 재조립 timeout 또는 metadata mismatch 단계에서 drop으로 관측될 수 있다.

## 14. 데이터별 최종 요약표

| 데이터 | 송신자 | 수신 스레드 | 중간 경로 | 최종 저장/송신 | 소비자 |
|---|---|---|---|---|---|
| 이미지 | Jetson | `jetson_rx_thread` | `FragQueue` -> `reassembly_shm_thread` | `SharedData.img_slots[]` | Qt GUI |
| LiDAR | Jetson | `jetson_rx_thread` | `FragQueue` -> `reassembly_shm_thread` | `SharedData.lidar_slots[]` | Qt GUI |
| Odom | Jetson | `jetson_rx_thread` | 직접 API 호출 | `SharedData` odom/state | Qt GUI, PC status |
| Qt 명령 | Qt GUI | `jetson_tx_thread` | `SharedData.cmd_queue` -> `send_cmd_to_jetson()` | UDP 9001 | Jetson |
| PC 명령 | PC | `pc_link_thread` | `send_cmd_to_jetson()` | UDP 9001 | Jetson |
| Heartbeat | `protocol_timer_thread` | 없음 | `bridge_api_send_command()` | UDP 9001 | Jetson |
| Command ACK | Jetson | `jetson_rx_thread` | `bridge_api_handle_ack()` | pending 해제, state/event 갱신 | BridgeDaemon/Qt |
| PC 상태 | BridgeDaemon | 없음 | `bridge_api_snapshot_status()` | UDP 9002 응답 방향 | PC |
| Event log | BridgeDaemon 내부 | 없음 | `bridge_api_publish_event()` | `SharedData.event_log` | Qt GUI |

## 15. 코드 위치 빠른 참조

아래 라인 번호는 현재 파일 기준이다.

| 목적 | 파일:라인 | 함수/구조 |
|---|---|---|
| 스레드 함수 선언 | `bridge_main.c:37` | `jetson_rx_thread`, `jetson_tx_thread`, `reassembly_shm_thread`, `protocol_timer_thread`, `pc_link_thread` |
| SHM 생성 | `bridge_main.c:68` | `shm_init_one()` |
| 스레드 생성 helper | `bridge_main.c:179` | `create_thread()` |
| watchdog | `bridge_main.c:228` | `watchdog_loop()` |
| main 진입점 | `bridge_main.c:273` | `main()` |
| Jetson UDP 수신 socket | `jetson_rx.c:31` | `create_jetson_socket()` |
| Jetson 주소 학습 | `jetson_rx.c:68` | `learn_addr()` |
| Odom 직접 처리 | `jetson_rx.c:83` | `handle_odom()` |
| Jetson 수신 스레드 | `jetson_rx.c:91` | `jetson_rx_thread()` |
| Qt 명령 SHM queue | `shm_def.h` | `ShmCmdQueue` |
| Qt 명령 packet 처리 | `jetson_tx.c` | `shm_cmd_queue_pop()` |
| Jetson 명령 송신 스레드 | `jetson_tx.c:80` | `jetson_tx_thread()` |
| 재조립 timeout | `reassembly_shm.c:51` | `expire_stale()` |
| 이미지 SHM publish | `reassembly_shm.c:88` | `commit_image()` |
| LiDAR SHM publish | `reassembly_shm.c:119` | `commit_lidar()` |
| 재조립 스레드 | `reassembly_shm.c:168` | `reassembly_shm_thread()` |
| PC 상태 송신 | `pc_link.c:35` | `send_status_all()` |
| PC UDP socket | `pc_link.c:47` | `create_pc_socket()` |
| PC 연동 스레드 | `pc_link.c:71` | `pc_link_thread()` |
| heartbeat 생성 | `protocol_timer.c:30` | `send_heartbeat()` |
| heartbeat 스레드 | `protocol_timer.c:45` | `protocol_timer_thread()` |
| SHM 기본값 초기화 | `bridge_api.c:47` | `bridge_api_init_shm()` |
| 이벤트 기록 | `bridge_api.c:62` | `bridge_api_publish_event()` |
| 수신 통계/연결 표시 | `bridge_api.c:78` | `bridge_api_note_rx()` |
| Odom/state 갱신 | `bridge_api.c:103` | `bridge_api_update_odom()` |
| frame ready 통계 | `bridge_api.c:132` | `bridge_api_note_frame_ready()` |
| drop 기록 | `bridge_api.c:155` | `bridge_api_note_drop()` |
| 명령 UDP packet 생성 | `bridge_api.c:176` | `send_legacy_cmd()` |
| pending command 등록 | `bridge_api.c:197` | `track_pending()` |
| 명령 송신 공통 API | `bridge_api.c:217` | `bridge_api_send_command()` |
| ACK 처리 | `bridge_api.c:264` | `bridge_api_handle_ack()` |
| timeout/retry 처리 | `bridge_api.c:293` | `bridge_api_poll_timeouts()` |
| PC 상태 snapshot | `bridge_api.c:332` | `bridge_api_snapshot_status()` |
| 명령 dispatch wrapper | `cmd_dispatch.c:20` | `send_cmd_to_jetson()` |
| 패킷 타입/포트 정의 | `proto.h:16` | `PKT_TYPE_*`, `BRIDGE_PORT`, `JETSON_CMD_PORT`, `PC_LINK_PORT` |
| 공유메모리 전체 구조 | `shm_def.h:147` | `SharedData` |
| 스레드 컨텍스트 구조 | `bridge_ctx.h:34` | `JetsonRxCtx`, `JetsonTxCtx`, `ProtoTimerCtx`, `PcLinkCtx`, `ReasmCtx` |
| fragment queue | `frag_queue.h:23` | `FragQueue` |
