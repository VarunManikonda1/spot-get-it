# 03. `bridge_ctx.h` 수업 자료

## 이 파일의 역할

`bridge_ctx.h`는 각 thread가 공유해야 하는 runtime context를 정의한다. C에서 thread function은 `void *arg` 하나만 받을 수 있기 때문에, 필요한 포인터와 설정값을 구조체로 묶어서 전달한다.

## 개념 설명: Thread Context

멀티스레드 daemon에서는 모든 thread가 global variable을 직접 만지게 만들면 유지보수가 어려워진다. 대신 thread마다 필요한 데이터만 context로 넘긴다.

예:

- RX thread는 SHM 배열, fragment queue 배열, Jetson 주소 테이블이 필요하다.
- TX thread는 Jetson 주소 테이블, robot별 SHM, stop flag, API pointer가 필요하다.
- reassembly thread는 robot 1대의 SHM과 queue만 필요하다.

이렇게 context를 나누면 책임 경계가 명확해진다.

## include 설명

| 라인 | 헤더 | 출처 | 쓰는 기능 |
|---:|---|---|---|
| 7 | `<arpa/inet.h>` | POSIX/Linux | `struct sockaddr_in` |
| 8 | `<pthread.h>` | POSIX | `pthread_mutex_t` |
| 9 | `<stdatomic.h>` | C11 | `atomic_bool`, `atomic_uint` |
| 10 | `"shm_def.h"` | project | `SharedData`, `MAX_ROBOTS` |
| 11 | `"frag_queue.h"` | project | `FragQueue` |

## 구조체 설명

### `JetsonAddrTable`

라인 14-18.

robot별 Jetson command 목적지 주소를 저장한다.

| 필드 | 의미 |
|---|---|
| `mu` | 주소 테이블 보호 mutex |
| `addr[MAX_ROBOTS]` | robot별 `sockaddr_in` |
| `set[MAX_ROBOTS]` | 해당 robot 주소를 학습했는지 여부 |

주소는 `jetson_rx.c`가 Jetson packet을 처음 받을 때 학습한다. command 송신 thread는 이 table을 보고 Jetson으로 보낸다.

### `PendingCommand`

라인 20-30.

ACK가 필요한 command를 추적하는 table entry다.

| 필드 | 의미 |
|---|---|
| `in_use` | 이 entry가 사용 중인지 |
| `requires_ack` | ACK 필요 여부 |
| `cmd` | 재전송까지 보존할 전체 `CmdPacket` |
| `command_id` | BridgeDaemon 내부 command id |
| `sent_us` | 최초 송신 시각 |
| `deadline_us` | ACK timeout 시각 |
| `retries_left` | 남은 retry 횟수 |

### `BridgeApi`

라인 34-41.

모든 plane이 공유하는 공통 runtime이다.

| 필드 | 의미 |
|---|---|
| `addr_table` | Jetson address table |
| `shm_arr` | robot별 SHM pointer |
| `num_robots` | 운용 robot 수 |
| `pending_mu` | pending command table mutex |
| `pending` | ACK 대기 command 배열 |
| `next_command_id` | command id atomic counter |

### Thread context 구조체들

| 구조체 | 라인 | 사용 thread | 핵심 내용 |
|---|---:|---|---|
| `JetsonRxCtx` | 44-51 | `jetson_rx_thread` | SHM 배열, queue 배열, addr table, stop, API |
| `JetsonTxCtx` | 54-60 | `jetson_tx_thread` | addr table, SHM 배열, robot 수, stop, API |
| `ProtoTimerCtx` | 62-67 | `protocol_timer_thread` | addr table, robot 수, stop, API |
| `PcLinkCtx` | 70-76 | `pc_link_thread` | addr table, SHM 배열, robot 수, stop, API |
| `ReasmCtx` | 79-84 | `reassembly_shm_thread` | robot 1대의 SHM, queue, API, robot id |

## 수업 포인트

- thread context는 “이 thread가 무엇을 알아야 하는가”를 드러내는 설계 문서 역할도 한다.
- 모든 thread가 같은 `BridgeApi*`를 공유하므로 command/event/state 동작이 통일된다.
- `stop`은 `atomic_bool*`이다. thread 간 종료 신호는 atomic으로 전달해야 한다.
- 주소 테이블은 여러 thread가 동시에 읽고 쓰므로 mutex가 필요하다.

## 실습 질문

1. `JetsonAddrTable`에 mutex가 없으면 어떤 race condition이 생길까?
2. `ReasmCtx`가 robot별로 따로 있는 이유는 무엇일까?
3. `BridgeApi`를 global로 직접 쓰지 않고 context에 넣어 넘기는 장점은 무엇일까?
