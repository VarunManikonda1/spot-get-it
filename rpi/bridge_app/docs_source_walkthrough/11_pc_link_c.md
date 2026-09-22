# 11. `pc_link.c` 수업 자료

## 이 파일의 역할

`pc_link.c`는 원격 PC와 BridgeDaemon 사이의 UDP gateway다. PC에서 command를 받으면 Jetson으로 전달하고, 주기적으로 robot status를 PC로 보낸다.

## 개념 설명: Remote Gateway

Qt GUI는 local IPC를 쓰지만, 원격 PC는 network를 통해 접근한다. 그래서 UDP:9002를 gateway로 둔다.

```text
PC -> UDP:9002 -> BridgeDaemon -> UDP:9001 -> Jetson
PC <- UDP status <- BridgeDaemon
```

PC 주소는 처음 command를 보낸 source address로 학습한다.

## include 설명

| 헤더 | 쓰는 기능 |
|---|---|
| `<arpa/inet.h>` | IP address 변환/log |
| `<errno.h>` | socket timeout/error |
| `<pthread.h>` | lock 관련 type |
| `<stdatomic.h>` | stop flag |
| `<stdio.h>` | log |
| `<string.h>` | `memset` |
| `<sys/socket.h>` | UDP socket |
| `<unistd.h>` | `close` |
| `"proto.h"` | `CmdPacket`, `PcStatusPacketV2` |
| `"shm_def.h"` | SHM type |
| `"bridge_ctx.h"` | `PcLinkCtx` |
| `"cmd_dispatch.h"` | command forwarding |
| `"bridge_api.h"` | status snapshot, timeout polling |
| `"utils.h"` | `now_us()` |

## 주요 상수

`STATUS_INTERVAL_MS = 1000`

PC status packet 송신 주기다.

## 함수 설명

### `send_status_all(int udp_fd, BridgeApi *api, const struct sockaddr_in *pc_addr)`

기능:
- 모든 robot의 상태를 `bridge_api_snapshot_status()`로 snapshot한다.
- `PcStatusPacketV2`를 PC address로 UDP 송신한다.

파라미터:
- `udp_fd`: PC link socket.
- `api`: status를 만들 공통 API.
- `pc_addr`: 송신 대상 PC address.

### `create_pc_socket(void)`

기능:
- UDP socket 생성.
- `SO_REUSEADDR` 설정.
- 100ms receive timeout 설정.
- `PC_LINK_PORT` 9002에 bind.

### `pc_link_thread(void *arg)`

기능:
- UDP:9002 socket 생성.
- PC command 수신.
- 최초 수신 source를 PC address로 저장.
- command를 `send_cmd_to_jetson()`으로 Jetson에 전달.
- 1초마다 status packet을 PC로 보낸다.
- timeout polling도 수행한다.

파라미터:
- `arg`: `PcLinkCtx*`.

## 수업 포인트

- PC status packet은 `PcStatusPacketV2`로 확장되어 GUI/관제에서 richer state를 볼 수 있다.
- UDP gateway는 빠르지만 인증이 없다. 실전에서는 token/HMAC/VPN이 필요하다.
- PC 주소 학습 방식은 간단하지만, 여러 PC가 붙는 구조로 가려면 client table이 필요하다.

## 실습 질문

1. PC가 command를 한 번도 보내지 않으면 status packet이 나가지 않는 이유는 무엇인가?
2. PC link에 인증이 없으면 어떤 위험이 있는가?
3. `PcStatusPacketV2`와 SHM `RobotState`의 관계는 무엇인가?
