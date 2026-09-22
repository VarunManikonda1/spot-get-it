# 13. `cmd_dispatch.h` / `cmd_dispatch.c` 수업 자료

## 이 파일들의 역할

`cmd_dispatch.*`는 command 송신 wrapper다. 원래 구조에서는 여기서 `PktHeader + CmdPayload`를 직접 만들어 Jetson으로 보냈다. 지금은 command 정책이 `bridge_api.c`로 이동했기 때문에, 이 파일은 기존 호출부 이름을 유지하면서 내부적으로 `bridge_api_send_command()`를 호출하는 얇은 adapter 역할을 한다.

## 개념 설명: Adapter Layer

큰 구조를 바꿀 때 모든 호출부를 한 번에 바꾸면 위험하다. 기존 함수 이름을 유지하고 내부 구현만 새 API로 연결하면 migration이 쉬워진다.

```text
old caller
  -> send_cmd_to_jetson()
      -> bridge_api_send_command()
```

## `cmd_dispatch.h` include 설명

| 헤더 | 쓰는 기능 |
|---|---|
| `"proto.h"` | `CmdPacket`, command priority/flag |
| `"bridge_ctx.h"` | `BridgeApi` type |

## `cmd_dispatch.c` include 설명

| 헤더 | 쓰는 기능 |
|---|---|
| `<arpa/inet.h>` | 과거 구현 잔재. 현재 wrapper에서는 직접 사용 거의 없음 |
| `<stdio.h>` | 과거 log 구현 잔재 |
| `<string.h>` | 과거 packet 구성 잔재 |
| `<sys/socket.h>` | 과거 `sendto` 구현 잔재 |
| `"cmd_dispatch.h"` | 함수 prototype |
| `"bridge_api.h"` | `bridge_api_send_command()` |
| `"utils.h"` | 과거 timestamp 구현 잔재 |

## 함수 설명

### `send_cmd_to_jetson(...)`

```c
void send_cmd_to_jetson(const CmdPacket *cmd,
                        BridgeApi       *api,
                        int              udp_fd,
                        uint8_t          priority,
                        uint8_t          flags,
                        const char      *tag);
```

기능:
- 전달받은 인자를 그대로 `bridge_api_send_command()`에 넘긴다.

파라미터:
- `cmd`: 송신할 command.
- `api`: 공통 middleware API runtime.
- `udp_fd`: Jetson command UDP socket.
- `priority`: command priority.
- `flags`: ACK 필요 여부 등.
- `tag`: log prefix.

## 수업 포인트

- adapter는 refactoring 중 안정성을 높이는 기법이다.
- 지금은 wrapper가 얇기 때문에 장기적으로는 호출부가 직접 `bridge_api_send_command()`를 불러도 된다.
- include 중 일부는 과거 구현 잔재이므로 정리 가능하다.

## 실습 질문

1. 기존 함수 이름을 유지하면 어떤 migration 장점이 있는가?
2. wrapper가 너무 많아지면 어떤 단점이 생길까?
3. 이 파일을 완전히 제거하려면 어떤 호출부를 바꿔야 할까?
