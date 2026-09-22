# 05. `bridge_api.c` 수업 자료

## 이 파일의 역할

`bridge_api.c`는 `bridge_api.h`에 선언된 공통 API의 실제 구현이다. 이 파일이 하는 일은 크게 네 가지다.

1. SHM 상태와 metrics 갱신
2. EventLog ring buffer 기록
3. command UDP 송신
4. ACK pending table 관리와 timeout/retry 처리

## 개념 설명: Middleware Core

미들웨어가 단순 중계 프로그램과 다른 점은 “공통 상태 모델”을 가진다는 것이다. `bridge_api.c`는 각 plane에서 발생한 일을 하나의 상태 모델로 합친다.

예:

- RX plane이 packet을 받으면 `bridge_api_note_rx()` 호출
- Sensor plane이 frame을 완성하면 `bridge_api_note_frame_ready()` 호출
- Command plane이 명령을 보내면 `bridge_api_send_command()` 호출
- Jetson이 ACK를 보내면 `bridge_api_handle_ack()` 호출

이렇게 하면 GUI/PC는 어디서 발생한 일이든 같은 `RobotState`, `EventLog`, `BridgeMetrics`에서 읽을 수 있다.

## include 설명

| 라인 | 헤더 | 쓰는 기능 |
|---:|---|---|
| 1 | `"bridge_api.h"` | public API와 type 선언 |
| 3 | `<arpa/inet.h>` | network address 관련 type |
| 4 | `<pthread.h>` | pending mutex |
| 5 | `<stdatomic.h>` | atomic counter |
| 6 | `<stddef.h>` | `offsetof` |
| 7 | `<stdio.h>` | log, `snprintf`, `perror` |
| 8 | `<string.h>` | `memset` |
| 9 | `<sys/socket.h>` | `sendto` |
| 11 | `"utils.h"` | `now_us()` |

## 주요 상수

`COMMAND_ACK_TIMEOUT_US`는 250000us, 즉 250ms다. command를 보낸 뒤 이 시간 안에 ACK가 없으면 retry 대상으로 본다.

## 함수별 설명

### `cmd_name(uint8_t type)`

기능: command type 숫자를 사람이 읽기 쉬운 문자열로 바꾼다.

파라미터:
- `type`: `CMD_TYPE_*`.

반환:
- `"estop"`, `"move"` 같은 문자열.
- 모르면 `"unknown"`.

수업 포인트:
- protocol 값은 숫자지만 log에는 문자열이 있어야 현장 디버깅이 쉽다.

### `bridge_api_init(...)`

기능:
- `BridgeApi` 구조체를 0으로 초기화.
- address table, SHM 배열, robot 수 저장.
- pending table mutex 초기화.
- command id counter를 1로 초기화.

파라미터:
- `api`: 초기화 대상.
- `addr_table`: Jetson 주소 table.
- `shm_arr`: robot별 SHM pointer.
- `num_robots`: robot 수.

### `bridge_api_destroy(BridgeApi *api)`

기능:
- pending table mutex를 destroy한다.

주의:
- SHM 자체 cleanup은 `bridge_main.c`에서 한다.

### `bridge_api_init_shm(SharedData *shm, int robot_id)`

기능:
- `SHM_MAGIC`, `SHM_VERSION`, `shared_data_size` 기록.
- `battery_percent`를 `-1.0f`로 설정해 아직 모르는 값임을 표현.
- robot id 저장.
- event/metrics atomic counter 초기화.

개념:
- GUI가 attach할 때 magic/version/size를 확인하면 daemon과 GUI의 ABI 불일치를 빨리 잡을 수 있다.

### `bridge_api_publish_event(...)`

기능:
- `EventLog` ring buffer에 event를 하나 추가한다.

파라미터:
- `api`: 공통 API runtime.
- `robot_id`: event 대상 robot.
- `severity`: info/warn/error/critical.
- `event_type`: connected, timeout, drop 등.
- `code`: type별 code.
- `message`: event message.

동작:
1. robot id 범위 검사.
2. `event_log.write_seq`를 atomic 증가.
3. `seq % EVENT_LOG_SIZE` 위치에 event 저장.
4. timestamp와 message 기록.

수업 포인트:
- ring buffer는 오래된 event를 덮어쓴다. 실시간 GUI에는 적합하지만 영구 로그는 별도 recorder가 필요하다.

### `bridge_api_note_rx(...)`

기능:
- packet 수신 시 연결 상태, packet count, RX metrics, `RobotState.last_rx_us` 갱신.
- 이전에 disconnected였다면 connected event 발행.

파라미터:
- `pkt_type`, `timestamp_us`는 현재 일부만 쓰며 향후 type별 통계 확장용으로 남겨져 있다.

### `bridge_api_update_odom(...)`

기능:
- odom rwlock으로 기존 odom 영역 갱신.
- state rwlock으로 `RobotState` pose/twist 갱신.

파라미터:
- `hdr`: frame id, timestamp를 제공.
- `odom`: x/y/theta/vx/vy/omega payload.

개념:
- 기존 GUI 호환을 위해 odom 전용 필드를 유지하고, 새 GUI용 통합 상태에도 같은 값을 복사한다.

### `bridge_api_note_frame_ready(...)`

기능:
- image/lidar frame publish 완료 후 FPS를 추정하고 `RobotState`에 기록.

주의:
- 현재 image는 `metrics.last_rx_us`, lidar는 `metrics.last_ack_us`를 임시 last frame timestamp로 사용한다. 장기적으로는 image/lidar 전용 last frame timestamp 필드를 분리하는 것이 더 좋다.

### `bridge_api_note_drop(...)`

기능:
- drop counter 증가.
- image/lidar drop count 증가.
- packet drop event 발행.

파라미터:
- `reason`: 사람이 읽을 수 있는 drop 이유.

### `lookup_addr(...)`

기능:
- `JetsonAddrTable`에서 robot id에 해당하는 주소를 mutex로 보호해 복사한다.

파라미터:
- `dst`: 주소가 있으면 여기에 복사된다.

반환:
- 주소가 있으면 1, 없으면 0.

개념:
- `sendto()`는 lock 밖에서 해야 한다. 네트워크 호출이 지연되면 address table 전체가 막힐 수 있기 때문이다.

### `send_legacy_cmd(...)`

기능:
- 현재 Jetson과 호환되는 `PktHeader + CmdPayload` packet을 만들어 UDP로 송신한다.

파라미터:
- `udp_fd`: 송신 socket.
- `dst`: Jetson address.
- `c`: command.
- `now`: timestamp.

개념:
- `CommandEnvelope`가 준비되어 있지만 Jetson 측 호환을 위해 실제 wire는 legacy payload를 유지한다.

### `track_pending(...)`

기능:
- ACK가 필요한 command를 pending table에 등록한다.

파라미터:
- `command_id`: BridgeDaemon이 부여한 id.
- `now`: 송신 시각.
- `retries`: 남은 retry 횟수.

### `bridge_api_send_command(...)`

기능:
- command plane의 중심 함수.

동작 순서:
1. robot id 검증.
2. Jetson 주소 lookup.
3. UDP command 송신.
4. TX metrics 갱신.
5. ACK 필요 여부 판단.
6. pending table 등록.
7. command sent event 발행.

파라미터:
- `priority`: critical이면 retry 횟수를 더 준다.
- `flags`: `CMD_FLAG_REQUIRES_ACK` 등.
- `tag`: log prefix.

### `bridge_api_handle_ack(...)`

기능:
- Jetson에서 온 ACK를 처리한다.

동작:
1. ACK counter 증가.
2. pending table에서 matching command 제거.
3. `RobotState.last_cmd_ack_us` 갱신.
4. timestamp 기반 RTT 계산.
5. ACK event 발행.

### `bridge_api_poll_timeouts(...)`

기능:
- pending command table을 검사해 deadline이 지난 command를 처리한다.

동작:
- retry가 남아 있으면 다시 송신.
- retry가 없으면 timeout event 발행.

주의:
- retry 시 현재는 velocity 값이 0으로 재구성된다. STOP/ESTOP/mission 계열에는 괜찮지만 MOVE reliable retry까지 하려면 payload 전체를 pending에 저장해야 한다.

### `bridge_api_snapshot_status(...)`

기능:
- PC로 보낼 `PcStatusPacketV2`를 만든다.

동작:
- `state_lock`으로 RobotState 복사.
- `meta_lock`으로 drop count 복사.
- event sequence와 timestamp 추가.

## 수업 포인트

- 공통 API는 각 plane의 중복을 줄이고 정책을 한 곳에 모은다.
- ACK/retry 같은 reliability 정책은 command 송신 경로 모두에서 일관되어야 한다.
- event는 UI뿐 아니라 디버깅/운영 관측성에도 중요하다.

## 실습 질문

1. `sendto()`를 address table mutex 안에서 호출하면 어떤 문제가 생길 수 있을까?
2. pending command table이 꽉 찼을 때 현재 코드는 어떻게 동작하며, 개선하려면 어떻게 해야 할까?
3. MOVE command에 ACK/retry를 적용할 때 왜 payload 전체 저장이 필요할까?
