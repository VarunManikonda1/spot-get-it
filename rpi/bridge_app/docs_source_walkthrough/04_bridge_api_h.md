# 04. `bridge_api.h` 수업 자료

## 이 파일의 역할

`bridge_api.h`는 middleware 내부 plane들이 공통으로 사용하는 API 선언부다. RX, TX, PC, timer, reassembly 코드가 SHM이나 UDP command 구조를 각자 따로 만지지 않고, 이 API를 통해 같은 방식으로 상태와 이벤트와 command를 처리한다.

## 개념 설명: 통합 API Layer

사용자가 말한 “각 plane에서 작동은 다를 수 있어도 API가 동일하게 동작한다”는 요구를 코드로 만든 부분이다.

각 plane의 내부 역할은 다르다.

- Sensor plane: image/lidar frame publish
- State plane: odom/state update
- Command plane: command send/ACK/retry
- Event plane: warning/info/error 기록
- Ops plane: metrics/status snapshot

하지만 외부에서 쓰는 방식은 다음처럼 통일된다.

```c
bridge_api_note_rx(...);
bridge_api_update_odom(...);
bridge_api_send_command(...);
bridge_api_publish_event(...);
bridge_api_snapshot_status(...);
```

## include 설명

| 라인 | 헤더 | 출처 | 쓰는 기능 |
|---:|---|---|---|
| 10 | `<stdint.h>` | C 표준 | fixed-width integer |
| 11 | `<netinet/in.h>` | POSIX | network address type |
| 13 | `"bridge_ctx.h"` | project | `BridgeApi`, context 구조체 |
| 14 | `"proto.h"` | project | `CmdPacket`, `PktHeader`, `PcStatusPacketV2` |

## event severity/type

라인 16-27.

severity는 event의 심각도다.

| 값 | 의미 |
|---|---|
| `EVENT_SEVERITY_INFO` | 일반 정보 |
| `EVENT_SEVERITY_WARN` | 경고 |
| `EVENT_SEVERITY_ERROR` | 오류 |
| `EVENT_SEVERITY_CRITICAL` | 치명적 상황 |

event type은 어떤 일이 발생했는지 나타낸다.

| 값 | 의미 |
|---|---|
| `EVENT_TYPE_CONNECTED` | robot 연결됨 |
| `EVENT_TYPE_DISCONNECTED` | robot 연결 끊김 |
| `EVENT_TYPE_CMD_SENT` | command 송신 |
| `EVENT_TYPE_CMD_ACK` | command ACK 수신 |
| `EVENT_TYPE_CMD_TIMEOUT` | ACK timeout |
| `EVENT_TYPE_PACKET_DROP` | packet/frame drop |
| `EVENT_TYPE_FRAME_READY` | frame publish 완료 |

## 함수 선언과 파라미터

### `bridge_api_init`

```c
void bridge_api_init(BridgeApi *api, JetsonAddrTable *addr_table,
                     SharedData **shm_arr, int num_robots);
```

기능: `BridgeApi` runtime을 초기화한다.

파라미터:
- `api`: 초기화할 API 객체.
- `addr_table`: Jetson 주소 table.
- `shm_arr`: robot별 SHM pointer 배열.
- `num_robots`: 운용 robot 수.

### `bridge_api_destroy`

API 내부 mutex를 정리한다.

### `bridge_api_init_shm`

SHM v2 header, robot id, event/metric counter 초기화.

### `bridge_api_publish_event`

EventLog ring buffer에 event를 추가한다.

파라미터:
- `robot_id`: event 대상 robot.
- `severity`: info/warn/error/critical.
- `event_type`: 연결, command, drop 등 event 종류.
- `code`: type별 부가 code.
- `message`: event message.

### `bridge_api_note_rx`

packet 수신 사실을 기록한다. 연결 상태와 RX metrics가 갱신된다.

### `bridge_api_update_odom`

odom payload를 SHM odom 영역과 `RobotState`에 동시에 반영한다.

### `bridge_api_note_frame_ready`

image/lidar frame이 SHM slot에 publish된 후 FPS와 timestamp를 갱신한다.

### `bridge_api_note_drop`

drop counter를 올리고 drop event를 남긴다.

### `bridge_api_send_command`

command 송신의 중심 API다. Jetson 주소 lookup, UDP 송신, ACK pending 등록, event 기록을 수행한다.

### `bridge_api_handle_ack`

Jetson ACK를 처리한다. pending command 완료, ACK metric, RTT 계산, ACK event 기록을 한다.

### `bridge_api_poll_timeouts`

pending command table을 검사해 timeout이면 retry하거나 실패 event를 남긴다.

### `bridge_api_snapshot_status`

PC로 보낼 `PcStatusPacketV2` snapshot을 만든다.

## 수업 포인트

- API layer를 두면 plane별 구현이 달라도 사용하는 인터페이스는 통일된다.
- event/state/metrics를 여기서 모으면 GUI와 PC가 같은 상태 모델을 볼 수 있다.
- command ACK/retry는 여러 송신 경로(Qt, PC, timer)가 공유해야 하므로 공통 API에 있어야 한다.

## 실습 질문

1. `jetson_tx.c`와 `pc_link.c`가 command packet을 직접 만들면 어떤 중복이 생길까?
2. event severity와 event type을 분리한 이유는 무엇일까?
3. `bridge_api_snapshot_status()`가 lock을 잡고 복사하는 이유는 무엇일까?
