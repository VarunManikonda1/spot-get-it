# 09. `jetson_rx.c` 수업 자료

## 이 파일의 역할

`jetson_rx.c`는 Jetson에서 BridgeDaemon으로 들어오는 UDP:9000 packet을 받는 RX thread다. 모든 robot packet을 하나의 socket에서 받고, `PktHeader.robot_id`로 robot을 구분한다.

## 개념 설명: Network Boundary

UDP 수신 코드는 외부 입력과 처음 만나는 경계다. 따라서 다음 방어가 중요하다.

- packet 길이가 header보다 작은지 검사
- header의 payload 길이와 실제 길이 일치 검사
- robot id 범위 검사
- packet type 검사
- fragment metadata 검사

이 파일은 `proto_validate_header()`를 통해 기본 protocol 검증을 수행한다.

## include 설명

| 헤더 | 쓰는 기능 |
|---|---|
| `<arpa/inet.h>` | `sockaddr_in`, `htons`, `htonl`, `inet_ntop` |
| `<errno.h>` | timeout/interrupt error 처리 |
| `<pthread.h>` | address table mutex |
| `<stdatomic.h>` | stop flag, metrics atomic |
| `<stdio.h>` | log |
| `<string.h>` | `memset` |
| `<sys/socket.h>` | UDP socket API |
| `<unistd.h>` | `close` |
| `"proto.h"` | packet header/type |
| `"shm_def.h"` | SHM type |
| `"frag_queue.h"` | image/lidar queue |
| `"bridge_ctx.h"` | `JetsonRxCtx` |
| `"bridge_api.h"` | state/event/ACK API |

## 함수 설명

### `create_jetson_socket(void)`

기능:
- UDP socket 생성.
- `SO_REUSEADDR` 설정.
- 수신 buffer를 8MB로 키움.
- `SO_RCVTIMEO` 100ms 설정.
- `BRIDGE_PORT` 9000에 bind.

반환:
- 성공 시 socket fd.
- 실패 시 -1.

개념:
- timeout이 있어야 `recvfrom()`이 영원히 막히지 않고 stop flag를 확인할 수 있다.

### `learn_addr(JetsonAddrTable *tbl, uint8_t rid, const struct sockaddr_in *src)`

기능:
- robot이 처음 packet을 보내면 source IP를 저장한다.
- port는 Jetson command port인 9001로 바꿔 저장한다.

파라미터:
- `tbl`: 주소 table.
- `rid`: robot id.
- `src`: UDP sender address.

개념:
- BridgeDaemon은 Jetson IP를 설정 파일 없이도 “수신 packet의 source”로 학습한다.

### `handle_odom(JetsonRxCtx *ctx, uint8_t rid, const PktHeader *hdr, const uint8_t *payload, int plen)`

기능:
- payload 길이가 `OdomPayload` 이상인지 확인.
- payload를 `OdomPayload*`로 해석.
- `bridge_api_update_odom()`으로 SHM state 갱신.

파라미터:
- `ctx`: RX context.
- `rid`: robot id.
- `hdr`: packet header.
- `payload`: odom payload byte pointer.
- `plen`: payload length.

### `jetson_rx_thread(void *arg)`

기능:
- UDP socket 생성.
- `recvfrom()` loop.
- header validation.
- robot id validation.
- Jetson 주소 학습.
- `bridge_api_note_rx()`로 수신 상태 기록.
- packet type별 dispatch.

type별 동작:

| type | 처리 |
|---|---|
| `PKT_TYPE_ODOM` | `handle_odom()` |
| `PKT_TYPE_CMD_ACK` | `bridge_api_handle_ack()` |
| `PKT_TYPE_IMAGE` | fragment queue push |
| `PKT_TYPE_LIDAR` | fragment queue push |
| 기타 | log 후 무시 |

종료 시:
- 모든 fragment queue에 stop 전달.
- socket close.

## 수업 포인트

- RX thread는 최대한 짧게 처리해야 한다. 큰 image/lidar 재조립은 별도 thread로 넘긴다.
- 주소 학습은 편리하지만 보안상 spoofing 가능성이 있다. 운영망에서는 인증/HMAC/VPN이 필요하다.
- packet validation은 가장 바깥쪽 경계에서 수행해야 이후 코드가 단순해진다.

## 실습 질문

1. RX thread에서 image JPEG decode까지 하면 어떤 문제가 생길까?
2. `SO_RCVTIMEO`가 없으면 종료 시 어떤 현상이 생길 수 있을까?
3. robot id가 범위 밖인데 검증하지 않으면 어떤 memory bug가 생길까?
