# 10. `jetson_tx.c` 수업 자료

## 이 파일의 역할

`jetson_tx.c`는 Qt GUI가 SHM command queue에 넣은 command를 읽어서 Jetson UDP:9001로 전달한다.

## 개념 설명: SHM Command Queue Gateway

Qt GUI와 BridgeDaemon은 같은 장비에서 동작하고, 이미 sensor/state/event를 `/robot_bridge_N` shared memory로 공유한다. command 경로도 같은 SHM ABI에 `SharedData.cmd_queue`를 두어 통일했다.

구조는 다음과 같다.

```text
Qt GUI
  -> /robot_bridge_N SharedData.cmd_queue
  -> jetson_tx_thread()
  -> send_cmd_to_jetson()
  -> bridge_api_send_command()
  -> UDP 9001
  -> Jetson
```

command queue는 단일 버퍼가 아니라 ring queue다. Qt가 짧은 시간에 여러 명령을 넣어도 `head/tail/count`로 보관하고, `jetson_tx_thread()`가 pop할 때 queue 안에서 가장 높은 priority 항목을 먼저 꺼낸다. 같은 priority끼리는 먼저 들어온 명령이 먼저 처리된다.

여러 robot queue 사이에서는 round-robin으로 공정성을 맞춘다. 한 번에 한 robot의 command 하나만 보내고, 다음 확인은 방금 처리한 robot 다음 번호부터 시작한다.

## include 설명

| 헤더 | 쓰는 기능 |
|---|---|
| `<arpa/inet.h>` | network type |
| `<errno.h>` | error 처리 |
| `<stdio.h>` | log |
| `<string.h>` | 기본 memory/string 처리 |
| `<sys/socket.h>` | UDP socket |
| `<unistd.h>` | `close`, `usleep` |
| `"proto.h"` | command packet/type |
| `"shm_def.h"` | `ShmCmdQueue`, `SharedData` |
| `"bridge_ctx.h"` | `JetsonTxCtx` |
| `"cmd_dispatch.h"` | command dispatch wrapper |
| `"bridge_api.h"` | timeout polling |

## 함수 설명

### `create_udp_sock(void)`

기능:
- Jetson command 송신용 UDP socket 생성.

반환:
- 성공 시 fd.
- 실패 시 -1.

### `shm_cmd_queue_pop(ShmCmdQueue *q, ShmCmdEntry *out)`

기능:
- process-shared mutex를 잡고 command queue에서 가장 높은 priority entry 하나를 꺼낸다.
- 선택된 entry 앞쪽 항목들을 한 칸씩 밀어 ring queue의 상대 순서를 유지한다.
- `count`를 1 줄인다.

반환:
- 1: command를 하나 꺼냄.
- 0: queue가 비어 있음.

이 함수는 `static` helper라서 `jetson_tx.c` 내부에서만 사용한다.

### `jetson_tx_thread(void *arg)`

기능:
- Jetson 송신 UDP socket을 만든다.
- 모든 robot의 `SharedData.cmd_queue`를 round-robin으로 확인한다.
- queue에서 priority 순서로 나온 `ShmCmdEntry`를 `send_cmd_to_jetson()`으로 넘긴다.
- command timeout/retry도 주기적으로 `bridge_api_poll_timeouts()`로 처리한다.
- 처리할 command가 없으면 `usleep(5000)`으로 짧게 쉰다.

priority/flag 처리:
- Qt가 entry에 priority와 flag를 넣어 보낸다.
- priority가 0이면 `jetson_tx_thread()`가 fallback으로 ESTOP은 critical, 나머지는 normal로 보정한다.
- queue가 꽉 찬 상태에서 Qt가 새 command를 넣으면 Qt producer는 queue 안의 가장 낮은 priority 항목을 버리고 새 항목을 넣는다. 새 command가 기존 최저 priority보다 낮으면 enqueue를 거절한다.

## 수업 포인트

- SHM queue는 command IPC를 sensor/state와 같은 공유메모리 ABI로 통일하지만, queue 동기화를 직접 관리해야 한다.
- `pthread_mutex_t`가 shared memory 안에 있으므로 `bridge_main.c`에서 `PTHREAD_PROCESS_SHARED`로 초기화해야 한다.
- command 송신 정책은 여전히 `bridge_api_send_command()`에 있으므로 GUI 경로와 PC 경로가 같은 ACK/retry 정책을 공유한다.
- pending command에는 `CmdPacket` 전체를 저장해야 retry 때 `vx/vy/omega` 같은 payload가 보존된다.
- robot 0 queue가 많이 쌓여도 다른 robot queue가 굶지 않도록 한 loop에서 하나의 command만 처리하고 시작 robot을 회전시킨다.

## 실습 질문

1. command를 단일 SHM 버퍼 하나로 두지 않고 ring queue로 둔 이유는 무엇인가?
2. SHM queue가 꽉 찼을 때 가장 낮은 priority 명령을 버리는 정책의 장단점은 무엇인가?
3. ESTOP command에 priority를 높게 주는 이유는 무엇인가?
