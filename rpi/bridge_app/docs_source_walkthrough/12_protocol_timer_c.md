# 12. `protocol_timer.c` 수업 자료

## 이 파일의 역할

`protocol_timer.c`는 주기적으로 Jetson에 heartbeat를 보내고, ACK pending command timeout을 검사한다.

## 개념 설명: Heartbeat와 Watchdog

로봇 제어 시스템에서는 “상대가 살아 있는지”를 계속 확인해야 한다.

- BridgeDaemon -> Jetson heartbeat: Jetson이 RPi/bridge가 살아 있음을 확인.
- BridgeDaemon watchdog: Jetson packet이 계속 들어오는지 확인.

둘은 방향이 다르다.

```text
protocol_timer.c: Bridge -> Jetson heartbeat
bridge_main.c watchdog: Jetson -> Bridge packet activity check
```

## include 설명

| 헤더 | 쓰는 기능 |
|---|---|
| `<arpa/inet.h>` | address type |
| `<pthread.h>` | address table mutex |
| `<stdio.h>` | log |
| `<string.h>` | memory helper |
| `<sys/socket.h>` | UDP socket |
| `<unistd.h>` | `close` |
| `"proto.h"` | command type |
| `"shm_def.h"` | MAX_ROBOTS indirectly |
| `"bridge_ctx.h"` | `ProtoTimerCtx` |
| `"bridge_api.h"` | command send, timeout polling |
| `"utils.h"` | timestamp helper |

## 함수 설명

### `send_heartbeat(ProtoTimerCtx *ctx, int udp_fd, uint8_t robot_id, uint32_t seq)`

기능:
- heartbeat용 `CmdPacket`을 만든다.
- `CMD_TYPE_HEARTBEAT`, velocity 0, sequence 설정.
- `bridge_api_send_command()`으로 low priority, no ACK command 송신.

파라미터:
- `ctx`: timer context.
- `udp_fd`: Jetson command UDP socket.
- `robot_id`: heartbeat 대상.
- `seq`: heartbeat sequence.

### `protocol_timer_thread(void *arg)`

기능:
- UDP socket 생성.
- monotonic clock으로 1초 주기 유지.
- stop flag를 100ms 단위로 확인하며 sleep.
- Jetson address table snapshot 생성.
- 등록된 robot마다 heartbeat 송신.
- `bridge_api_poll_timeouts()`로 command ACK timeout 처리.

파라미터:
- `arg`: `ProtoTimerCtx*`.

## 수업 포인트

- heartbeat는 정확히 1초마다 보내는 것보다 “지나치게 늦지 않게” 보내는 것이 중요하다.
- address table snapshot을 만든 뒤 lock 밖에서 send하는 이유는 network call이 lock을 오래 잡지 않게 하기 위해서다.
- timeout polling은 command 송신 thread에도 있고 timer에도 있다. ACK 처리가 특정 송신 경로에 묶이지 않게 하기 위해서다.

## 실습 질문

1. heartbeat가 끊기면 Jetson은 어떤 안전 동작을 해야 할까?
2. `CLOCK_MONOTONIC`을 쓰는 이유는 무엇인가?
3. system time이 바뀌면 heartbeat 주기에 어떤 문제가 생길 수 있을까?
