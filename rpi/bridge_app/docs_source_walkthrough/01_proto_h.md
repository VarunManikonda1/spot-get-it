# 01. `proto.h` 수업 자료

## 이 파일의 역할

`proto.h`는 Jetson, BridgeDaemon, Qt/PC command 경로가 공유하는 “통신 약속”이다. 네트워크에서 오가는 packet type, command type, payload 구조체를 정의한다. 이 파일이 바뀌면 Jetson 송신 코드, BridgeDaemon, PC/GUI receiver가 같은 구조를 이해하도록 함께 맞춰야 한다.

## 개념 설명: Wire Protocol

wire protocol은 실제 네트워크 byte stream 또는 UDP datagram 위에 어떤 순서로 어떤 필드를 올릴지 정한 규칙이다.

이 프로젝트에서는 모든 UDP packet이 다음 구조를 따른다.

```text
[ PktHeader ][ payload ]
```

- `PktHeader`: type, robot id, fragment 정보, timestamp 같은 공통 metadata.
- `payload`: type별 실제 데이터. 예: odom, command, ACK, image fragment.

image/lidar는 크기가 크기 때문에 여러 UDP packet으로 fragment되어 들어온다. odom/command/ACK는 작은 단일 packet이다.

## include 설명

| 라인 | 헤더 | 출처 | 쓰는 기능 |
|---:|---|---|---|
| 13 | `<stdint.h>` | C 표준 | `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t` |

고정 폭 integer를 쓰는 이유는 packet layout이 compiler/platform마다 달라지면 안 되기 때문이다.

## 매크로 설명

| 라인 | 이름 | 의미 |
|---:|---|---|
| 16 | `PKT_TYPE_IMAGE` | image fragment packet |
| 17 | `PKT_TYPE_LIDAR` | lidar fragment packet |
| 18 | `PKT_TYPE_ODOM` | odometry 단일 packet |
| 19 | `PKT_TYPE_CMD` | BridgeDaemon에서 Jetson으로 보내는 command |
| 20 | `PKT_TYPE_CMD_ACK` | Jetson이 command 처리 결과를 돌려주는 ACK |
| 21-25 | `PKT_TYPE_STATE/EVENT/MISSION/HEALTH/CAPABILITY` | 향후 상태, 이벤트, 임무, 헬스, capability 확장 packet |
| 28-39 | `CMD_TYPE_*` | GUI/PC/heartbeat/mission/sensor command 종류 |
| 41 | `CMD_FLAG_REQUIRES_ACK` | 이 command는 ACK가 필요하다는 표시 |
| 42 | `CMD_FLAG_BROADCAST` | broadcast command를 표현하기 위한 flag |
| 44-47 | `CMD_PRIORITY_*` | command 우선순위 |
| 50-52 | port 정의 | Jetson RX 9000, Jetson command 9001, PC link 9002 |
| 55-56 | local command IPC note | Qt GUI command는 `shm_def.h`의 `SharedData.cmd_queue`를 사용한다 |
| 58-59 | `PROTO_MTU`, `PROTO_PKT_MAX` | UDP payload 및 전체 packet buffer 크기 |

## 구조체 설명

### `PktHeader`

라인 62-71.

| 필드 | 의미 |
|---|---|
| `type` | packet 종류. `PKT_TYPE_*` 중 하나 |
| `robot_id` | robot index. `0 ~ MAX_ROBOTS-1` |
| `frag_idx` | fragment index |
| `frag_total` | frame 전체 fragment 수 |
| `payload_len` | 현재 packet payload 길이 |
| `frame_id` | frame sequence 또는 command sequence |
| `payload_offset` | 재조립 buffer 안에서 payload가 들어갈 offset |
| `timestamp_us` | 송신 측 timestamp |

`__attribute__((packed))`를 붙인 이유는 compiler가 구조체 padding을 넣지 못하게 해서 wire layout을 고정하기 위해서다.

### `OdomPayload`

라인 74-81. robot 위치와 속도.

| 필드 | 의미 |
|---|---|
| `x`, `y`, `theta` | 2D pose |
| `vx`, `vy`, `omega` | 선속도/각속도 |

### `CmdPayload`

라인 84-90. 현재 Jetson 호환 command payload.

| 필드 | 의미 |
|---|---|
| `cmd_type` | command 종류 |
| `vx`, `vy`, `omega` | MOVE command의 속도 값 |
| `seq` | command sequence |

### `CmdPacket`

라인 93-100. Qt/PC가 BridgeDaemon에 보내는 local command 구조다. Qt는 이 구조체를 SHM command queue에 넣고, PC는 UDP 9002로 보낸다. BridgeDaemon 내부에서 이것을 `PktHeader + CmdPayload`로 감싸 Jetson에 보낸다.

### `CommandEnvelope`

라인 102-111. 향후 generic command API용 envelope다. 지금 wire 송신은 legacy 호환 때문에 아직 `CmdPayload`를 쓴다.

### `CmdAckPayload`

라인 113-120. Jetson이 ACK packet으로 돌려줄 payload다.

| 필드 | 의미 |
|---|---|
| `command_id` | BridgeDaemon이 부여한 command id |
| `seq` | command sequence |
| `cmd_type` | 어떤 command에 대한 ACK인지 |
| `status` | 성공/실패 상태 |
| `timestamp_us` | ACK 기준 timestamp |

### `PcStatusPacketV2`

라인 135-156. PC로 보내는 확장 상태 packet이다. 연결, pose, battery, link RTT, FPS, drop count, event sequence를 담는다.

## 함수 설명

### `proto_packet_type_valid(uint8_t type)`

라인 158-164.

기능: `type`이 현재 protocol에서 허용한 packet type인지 검사한다.

파라미터:
- `type`: 수신 header의 `hdr->type`.

반환:
- 유효하면 1.
- 알 수 없는 type이면 0.

### `proto_validate_header(const PktHeader *hdr, uint16_t actual_payload_len)`

라인 166-178.

기능: UDP packet header가 기본 protocol 규칙을 지키는지 검사한다.

파라미터:
- `hdr`: 수신 packet의 header pointer.
- `actual_payload_len`: 실제 수신 byte 수에서 header 크기를 뺀 payload 길이.

검사 내용:
- packet type이 유효한가.
- header의 `payload_len`과 실제 길이가 같은가.
- `frag_total`이 0이 아닌가.
- `frag_idx < frag_total`인가.
- payload가 `PROTO_MTU`를 넘지 않는가.
- odom/cmd/ack 같은 단일 packet이 fragment metadata를 이상하게 쓰지 않는가.

## 수업 포인트

- protocol header 검증은 network boundary에서 가장 먼저 해야 한다.
- UDP는 임의 byte가 들어올 수 있으므로 `payload_len == actual_payload_len` 검사는 필수다.
- `packed` 구조체는 wire layout 고정에는 좋지만 alignment 문제가 생길 수 있으므로 platform이 바뀔 때 검증이 필요하다.

## 실습 질문

1. `PKT_TYPE_CMD_ACK`가 없으면 ESTOP 성공 여부를 GUI가 어떻게 알 수 있을까?
2. image packet에서 `payload_offset` 검증이 빠지면 어떤 문제가 생길까?
3. `CmdPacket`과 `CmdPayload`를 분리한 이유는 무엇일까?
