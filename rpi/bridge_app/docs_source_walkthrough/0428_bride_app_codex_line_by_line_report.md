# bride_app_codex 소스 라인별 구현 보고서

작성일: 2026-04-28  
대상 경로: `/home/pi/robot_project/bride_app_codex`

이 문서는 `bride_app_codex`에 있는 소스/헤더/빌드 파일을 기준으로, 각 파일이 무엇을 하는지, 헤더는 어디서 불러오며 어떤 기능을 쓰는지, 함수는 어떤 기능이고 파라미터가 무엇인지 설명한다. 실행 바이너리 `bridge_daemon`, `bridge_daemon_asan`은 생성 산출물이므로 제외한다.

## 1. 전체 구조

| Plane | 담당 파일 | 역할 |
|---|---|---|
| Lifecycle/Ops | `bridge_main.c` | SHM 생성, thread 생성/종료, watchdog, cleanup |
| Common API | `bridge_api.c/h` | RX/TX/SHM/Event/ACK를 같은 API로 다루는 통합 계층 |
| Wire Protocol | `proto.h` | UDP packet type, command type, payload ABI 정의 |
| SHM ABI | `shm_def.h` | Qt GUI와 daemon이 공유하는 POSIX SHM 구조체 정의 |
| Context | `bridge_ctx.h` | thread context, 주소 테이블, pending command table 정의 |
| Sensor RX | `jetson_rx.c` | Jetson UDP:9000 수신, robot별 dispatch |
| GUI Command | `jetson_tx.c` | Qt SHM command queue 수신, Jetson UDP:9001 송신 |
| PC Gateway | `pc_link.c` | PC UDP:9002 command/status 송수신 |
| Heartbeat | `protocol_timer.c` | Jetson heartbeat 및 ACK timeout polling |
| Reassembly | `reassembly_shm.c` | image/lidar fragment 재조립 후 SHM publish |
| Fragment Queue | `frag_queue.h` | RX thread와 reassembly thread 사이 blocking queue |
| Dispatch Wrapper | `cmd_dispatch.c/h` | legacy 호출부가 `bridge_api_send_command()`를 쓰게 하는 얇은 wrapper |
| Utility | `utils.h` | monotonic microsecond timestamp |
| Build | `Makefile` | 일반/ASAN/TSAN 빌드 타깃 |

## 2. 헤더 의존성 요약

| 헤더 | 어디서 제공 | 사용 목적 |
|---|---|---|
| `<arpa/inet.h>` | Linux/POSIX | `sockaddr_in`, `htons`, `htonl`, `inet_ntop` |
| `<errno.h>` | C/POSIX | `EAGAIN`, `EWOULDBLOCK`, `EINTR`, error handling |
| `<fcntl.h>` | POSIX | `O_CREAT`, `O_RDWR`, `shm_open` flag |
| `<pthread.h>` | POSIX threads | mutex, condvar, rwlock, thread creation |
| `<sched.h>` | Linux/POSIX | CPU affinity, `SCHED_FIFO` |
| `<semaphore.h>` | POSIX | process-shared semaphore in SHM |
| `<signal.h>` | C/POSIX | `SIGINT`, `SIGTERM`, signal handler |
| `<stdatomic.h>` | C11 | `atomic_bool`, atomic counters, memory ordering |
| `<stdbool.h>` | C99 | `true`, `false` |
| `<stdint.h>` | C99 | fixed-width integer types |
| `<stddef.h>` | C99 | `offsetof` |
| `<stdio.h>` | C stdlib | `fprintf`, `perror`, `snprintf` |
| `<stdlib.h>` | C stdlib | `calloc`, `malloc`, `free`, `atoi` |
| `<string.h>` | C stdlib | `memset`, `memcpy`, `strerror`, `strncpy` |
| `<sys/mman.h>` | POSIX | `mmap`, `munmap`, `shm_open`, `shm_unlink` |
| `<sys/socket.h>` | POSIX sockets | `socket`, `bind`, `recvfrom`, `sendto`, `setsockopt` |
| `<sys/stat.h>` | POSIX | `umask`, mode bits |
| `<time.h>` | C/POSIX | `clock_gettime`, `nanosleep`, `timespec` |
| `<unistd.h>` | POSIX | `close`, `unlink`, `ftruncate`, `getpid`, `sleep` |

## 3. `proto.h`

목적: Jetson, BridgeDaemon, Qt/PC command 경로가 공유하는 UDP wire protocol과 packet payload ABI를 정의한다.

| 라인 | 설명 |
|---:|---|
| 1-9 | 파일 주석. packet layout은 `[PktHeader][payload]`이고 image/lidar는 fragment 방식임을 설명한다. |
| 11 | `#pragma once`: 중복 include 방지. |
| 13 | `<stdint.h>` include. `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t` 사용. |
| 16-25 | packet type 정의. image/lidar/odom/cmd 외에 ACK, state, event, mission, health, capability 확장 type을 추가했다. |
| 28-39 | command type 정의. 기존 ESTOP/STOP/MOVE/HEARTBEAT 외 mode/return/waypoint/route/mission/sensor command 추가. |
| 41-42 | command flag. ACK 필요 여부, broadcast 의미를 표현한다. |
| 44-47 | command priority. retry 정책이나 command scheduling에 쓰기 위한 우선순위 상수다. |
| 50-52 | UDP port. Jetson RX는 9000, Jetson command는 9001, PC link는 9002. |
| 55 | Qt GUI command는 `shm_def.h`의 `SharedData.cmd_queue`를 사용한다는 주석. |
| 58-59 | UDP payload MTU와 packet 최대 크기. `PROTO_PKT_MAX = sizeof(PktHeader) + PROTO_MTU`. |
| 62-71 | `PktHeader`. 모든 UDP packet 앞에 붙는 packed header. |
| 74-81 | `OdomPayload`. pose/twist payload. |
| 84-90 | `CmdPayload`. 현재 Jetson 호환을 위해 유지하는 legacy command payload. |
| 93-100 | `CmdPacket`. Qt SHM command queue와 PC UDP:9002에서 BridgeDaemon으로 들어오는 command 구조. |
| 102-111 | `CommandEnvelope`. 미래 확장용 generic command envelope. 현재 wire 송신은 legacy `CmdPayload` 유지. |
| 113-120 | `CmdAckPayload`. Jetson이 command ACK를 회신할 때 사용하는 payload. |
| 123-133 | `PcStatusPacket`. 기존 v1 status packet. 호환용 정의. |
| 135-156 | `PcStatusPacketV2`. mode/fault/battery/fps/event sequence까지 포함하는 확장 status packet. |
| 158-164 | `proto_packet_type_valid(type)`: packet type이 허용된 값인지 검사한다. |
| 166-178 | `proto_validate_header(hdr, actual_payload_len)`: payload 길이, fragment index, 단일 packet 조건을 검증한다. |

### `proto.h` 함수 파라미터

`proto_packet_type_valid(uint8_t type)`
- `type`: `PKT_TYPE_*` 값. 유효하면 1, 아니면 0.

`proto_validate_header(const PktHeader *hdr, uint16_t actual_payload_len)`
- `hdr`: 수신 packet의 header pointer.
- `actual_payload_len`: `recvfrom()` byte 수에서 `sizeof(PktHeader)`를 뺀 실제 payload 길이.
- 반환: 정상 packet이면 1, 잘못된 packet이면 0.

## 4. `shm_def.h`

목적: BridgeDaemon writer와 Qt GUI reader가 같은 memory layout을 보도록 POSIX SHM ABI를 정의한다.

| 라인 | 설명 |
|---:|---|
| 1-18 | 파일 주석. robot별 SHM 이름, triple buffer, rwlock, semaphore 전략 설명. |
| 20 | include guard. |
| 22 | `<stdint.h>` include. 고정 폭 integer 사용. |
| 23-31 | C++에서는 `std::atomic`, C에서는 `_Atomic`을 쓰도록 `ATOMIC_*` macro 정의. Qt와 C daemon이 같은 헤더를 include하기 위한 처리다. |
| 32 | `<pthread.h>` include. `pthread_rwlock_t` 사용. |
| 33 | `<semaphore.h>` include. `sem_t` 사용. |
| 36-39 | robot 최대 수, SHM 이름 format, SHM magic/version 정의. GUI attach 시 ABI 검증용. |
| 42-47 | image/lidar/event buffer 크기 상수. |
| 50-55 | `ImgSlot`: JPEG byte array, size, timestamp, frame_id. |
| 67-75 | `LidarSlot`: point count와 x/y/z/intensity 배열. |
| 78-85 | `BridgeMeta`: 연결 상태, packet count, drop count, latency. |
| 87-119 | `RobotState`: GUI가 바로 읽을 수 있는 통합 robot 상태. 연결, pose, battery, mode, mission, fault, ACK time 포함. |
| 121-128 | `RobotEvent`: event log entry. timestamp, robot, severity, type, code, message. |
| 130-133 | `EventLog`: atomic write sequence와 ring buffer. |
| 135-144 | `BridgeMetrics`: RX/TX/ACK/retry/drop counters와 마지막 timestamp. |
| 147-185 | `SharedData`: robot 1대분 전체 SHM layout. header, image/lidar slots, odom/meta/state/event/metrics 포함. |

## 5. `bridge_ctx.h`

목적: 각 thread에 넘기는 context와 공통 runtime table을 정의한다.

| 라인 | 설명 |
|---:|---|
| 5 | include guard. |
| 7 | `<arpa/inet.h>`: `sockaddr_in` 주소 저장용. |
| 8 | `<pthread.h>`: mutex type. |
| 9 | `<stdatomic.h>`: `atomic_bool`, `atomic_uint`. |
| 10 | `shm_def.h`: `SharedData`, `MAX_ROBOTS`. |
| 11 | `frag_queue.h`: `FragQueue`. |
| 14-18 | `JetsonAddrTable`: robot별 Jetson command 송신 주소와 set 여부, mutex. |
| 20-30 | `PendingCommand`: ACK를 기다리는 command table entry. |
| 32 | pending command 최대 128개. |
| 34-41 | `BridgeApi`: 모든 plane이 공유하는 API runtime. 주소 테이블, SHM 배열, pending table, command id counter. |
| 44-51 | `JetsonRxCtx`: RX thread context. SHM/queue 배열, 주소 테이블, robot 수, stop flag, API pointer. |
| 54-59 | `JetsonTxCtx`: GUI command thread context. |
| 62-67 | `ProtoTimerCtx`: heartbeat/timer thread context. |
| 70-76 | `PcLinkCtx`: PC gateway thread context. |
| 79-84 | `ReasmCtx`: robot별 reassembly thread context. |

## 6. `bridge_api.h`

목적: sensor/state/command/event/ops plane이 공통으로 호출할 API 선언부다.

| 라인 | 설명 |
|---:|---|
| 1-6 | 파일 주석. plane별 직접 SHM/UDP 조작을 줄이고 공통 API를 쓰게 하는 의도. |
| 8 | include guard. |
| 10 | `<stdint.h>`: integer type. |
| 11 | `<netinet/in.h>`: socket address type. |
| 13 | `bridge_ctx.h`: `BridgeApi`, context type. |
| 14 | `proto.h`: command/status/payload type. |
| 16-19 | event severity 값. |
| 21-27 | event type 값. 연결, command, timeout, drop, frame ready. |
| 29-64 | `bridge_api_*` 함수 prototype. |

### `bridge_api.h` 함수 파라미터

`bridge_api_init(BridgeApi *api, JetsonAddrTable *addr_table, SharedData **shm_arr, int num_robots)`
- `api`: 초기화할 runtime API 객체.
- `addr_table`: robot별 Jetson 주소 테이블.
- `shm_arr`: robot별 `SharedData*` 배열.
- `num_robots`: 운용 robot 수.

`bridge_api_destroy(BridgeApi *api)`
- `api`: mutex를 정리할 API 객체.

`bridge_api_init_shm(SharedData *shm, int robot_id)`
- `shm`: 초기화할 robot SHM.
- `robot_id`: 이 SHM이 담당하는 robot 번호.

`bridge_api_publish_event(BridgeApi *api, uint8_t robot_id, uint8_t severity, uint16_t event_type, uint32_t code, const char *message)`
- `severity`: info/warn/error/critical.
- `event_type`: event category.
- `code`: type별 부가 code.
- `message`: event text. 96 byte 내부 buffer에 복사된다.

`bridge_api_note_rx(BridgeApi *api, uint8_t robot_id, uint8_t pkt_type, uint64_t timestamp_us)`
- packet 수신 사실을 기록한다. 연결 상태, packet counter, last_rx를 갱신한다.

`bridge_api_update_odom(BridgeApi *api, uint8_t robot_id, const PktHeader *hdr, const OdomPayload *odom)`
- odom SHM 영역과 `RobotState` pose/twist를 동시에 갱신한다.

`bridge_api_note_frame_ready(BridgeApi *api, uint8_t robot_id, uint8_t pkt_type, uint32_t frame_id, uint64_t timestamp_us)`
- image/lidar frame publish 완료 후 FPS와 state timestamp를 갱신한다.

`bridge_api_note_drop(BridgeApi *api, uint8_t robot_id, uint8_t pkt_type, const char *reason)`
- drop counter 증가와 drop event 발행.

`bridge_api_send_command(BridgeApi *api, int udp_fd, const CmdPacket *cmd, uint8_t priority, uint8_t flags, const char *tag)`
- 공통 command 송신 API. Jetson 주소 조회, UDP 송신, ACK pending 등록, event 기록을 수행한다.

`bridge_api_handle_ack(BridgeApi *api, uint8_t robot_id, const CmdAckPayload *ack)`
- ACK packet을 pending table과 state/metrics에 반영한다.

`bridge_api_poll_timeouts(BridgeApi *api, int udp_fd, const char *tag)`
- ACK timeout을 검사하고 retry 또는 timeout event를 처리한다.

`bridge_api_snapshot_status(BridgeApi *api, uint8_t robot_id, PcStatusPacketV2 *out)`
- PC 송신용 status snapshot을 만든다.

## 7. `bridge_api.c`

목적: 통합 API 실제 구현. SHM state/event/metrics 갱신, command 송신, ACK tracking, timeout/retry를 담당한다.

| 라인 | 설명 |
|---:|---|
| 1 | `bridge_api.h` include. public prototype과 type 사용. |
| 3 | `<arpa/inet.h>`: network address helper. |
| 4 | `<pthread.h>`: pending table mutex. |
| 5 | `<stdatomic.h>`: counters, command id. |
| 6 | `<stddef.h>`: `offsetof`. |
| 7-9 | stdio/string/socket API 사용. |
| 11 | `utils.h`: `now_us()`. |
| 13 | ACK timeout을 250ms로 정의. |
| 15-31 | `cmd_name(type)`: command type을 log string으로 변환. |
| 33-41 | `bridge_api_init`: API runtime memory clear, pointer 저장, mutex/counter 초기화. |
| 43-45 | `bridge_api_destroy`: pending mutex destroy. |
| 47-60 | `bridge_api_init_shm`: SHM magic/version/size, initial battery, robot_id, event/metrics counter 초기화. |
| 62-75 | `bridge_api_publish_event`: event ring buffer에 새 event 기록. |
| 77-100 | `bridge_api_note_rx`: 연결 상태와 RX metrics 갱신, 최초 연결 event 발행. |
| 102-129 | `bridge_api_update_odom`: odom lock과 state lock으로 pose/twist 갱신. |
| 131-152 | `bridge_api_note_frame_ready`: image/lidar FPS 추정과 state timestamp 갱신. |
| 154-165 | `bridge_api_note_drop`: drop metric/meta 증가 및 event 발행. |
| 167-173 | `lookup_addr`: mutex로 Jetson address snapshot. |
| 175-194 | `send_legacy_cmd`: `PktHeader + CmdPayload` 구성 후 UDP sendto. |
| 196-214 | `track_pending`: ACK 필요한 command를 pending table 빈 slot에 등록. |
| 216-261 | `bridge_api_send_command`: robot id 검증, 주소 조회, 송신, TX metrics, ACK 필요 판단, pending 등록, event/log 처리. |
| 263-290 | `bridge_api_handle_ack`: ACK metrics 증가, pending command 완료 처리, RTT 계산, ACK event 기록. |
| 292-329 | `bridge_api_poll_timeouts`: pending table을 돌며 timeout command retry 또는 failure event 처리. |
| 331-361 | `bridge_api_snapshot_status`: `RobotState`, meta, event seq를 `PcStatusPacketV2`로 복사. |

## 8. `bridge_main.c`

목적: daemon process lifecycle. SHM, queue, context, thread, watchdog, signal, cleanup 전체를 조율한다.

| 라인 | 설명 |
|---:|---|
| 1-14 | 파일 주석. thread 구성과 실행 인자 설명. |
| 16-28 | POSIX/C header include. SHM, thread, signal, atomic, mmap, stat, errno 등을 사용한다. |
| 30-34 | project header include. SHM layout, queue, protocol, context, common API. |
| 37-41 | 다른 `.c` 파일의 thread entry 함수 extern 선언. |
| 44-63 | `BridgeCtx`: process 전체 runtime state. robot 수, SHM 배열, queue, stop flag, API, thread context, thread id. |
| 65 | global context `g_ctx`. signal handler와 main loop에서 공유. |
| 68-152 | `shm_init_one(robot_id)`: robot 1대분 SHM 생성/초기화. |
| 155-166 | `shm_cleanup_one(shm, robot_id)`: semaphore/rwlock/mmap/SHM name cleanup. |
| 169-176 | `cleanup_all(num_robots)`: 모든 queue/SHM/address/API cleanup. |
| 179-213 | `create_thread(fn,arg,priority,core)`: thread attr, affinity/realtime 설정, 실패 시 normal thread 재시도. |
| 216-219 | `sig_handler(sig)`: signal-safe하게 atomic stop flag만 set. |
| 221-225 | `request_stop_all()`: stop flag set 및 모든 fragment queue wakeup. |
| 228-270 | `watchdog_loop()`: packet count 변화로 robot 연결 상태 판단, disconnect event 발행, drop count log. |
| 273-405 | `main(argc,argv)`: robot 수 파싱, signal 등록, SHM/queue/API/context/thread 생성, watchdog 실행, join/cleanup. |

### `bridge_main.c` 주요 함수 파라미터

`shm_init_one(int robot_id)`
- `robot_id`: `/robot_bridge_%d` 이름에 들어가는 robot index.
- 반환: 초기화된 `SharedData*`, 실패 시 `NULL`.

`shm_cleanup_one(SharedData *shm, int robot_id)`
- `shm`: 해제할 mapped SHM pointer.
- `robot_id`: unlink할 SHM 이름 계산용.

`cleanup_all(int num_robots)`
- `num_robots`: cleanup 대상 robot 수.

`create_thread(void *(*fn)(void *), void *arg, int priority, int core)`
- `fn`: thread entry function.
- `arg`: thread에 넘길 context pointer.
- `priority`: `SCHED_FIFO` priority. 0이면 realtime 설정 안 함.
- `core`: CPU core index. 음수면 affinity 설정 안 함.

`sig_handler(int sig)`
- `sig`: 수신 signal 번호. 사용하지 않고 stop flag만 set.

`main(int argc, char *argv[])`
- `argc/argv`: `argv[1]`에 robot 수를 받을 수 있다.

## 9. `jetson_rx.c`

목적: Jetson에서 오는 UDP:9000 packet을 수신하고 robot별로 SHM/API/fragment queue에 dispatch한다.

| 라인 | 설명 |
|---:|---|
| 1-13 | 파일 주석. 역할과 우선순위 설명. |
| 15-22 | socket, errno, pthread, atomic, stdio/string, close 사용 header. |
| 24-28 | protocol, SHM, queue, context, API include. |
| 31-56 | `create_jetson_socket()`: UDP socket 생성, reuse/receive buffer/timeout 설정, `BRIDGE_PORT` bind. |
| 59-71 | `learn_addr(tbl,rid,src)`: 최초 수신 source IP를 Jetson command 주소로 저장. port는 `JETSON_CMD_PORT`. |
| 74-79 | `handle_odom(ctx,rid,hdr,payload,plen)`: payload를 `OdomPayload`로 보고 API에 odom 갱신 위임. |
| 82-165 | `jetson_rx_thread(arg)`: socket 생성, recv loop, header validation, robot id 검증, 주소 학습, API RX 기록, packet type별 처리. |

### `jetson_rx.c` 함수 파라미터

`learn_addr(JetsonAddrTable *tbl, uint8_t rid, const struct sockaddr_in *src)`
- `tbl`: robot별 Jetson address table.
- `rid`: robot id.
- `src`: 방금 packet을 보낸 source address.

`handle_odom(JetsonRxCtx *ctx, uint8_t rid, const PktHeader *hdr, const uint8_t *payload, int plen)`
- `ctx`: RX context.
- `rid`: robot id.
- `hdr`: packet header.
- `payload`: odom payload 시작 주소.
- `plen`: payload 길이.

`jetson_rx_thread(void *arg)`
- `arg`: `JetsonRxCtx*`로 casting된다.

## 10. `jetson_tx.c`

목적: Qt GUI가 로봇별 SHM `SharedData.cmd_queue`에 넣은 command를 받아 Jetson UDP:9001로 전달한다.

| 라인 | 설명 |
|---:|---|
| 1-19 | 파일 주석. Qt -> SHM command queue -> UDP 흐름 설명. |
| 21-28 | UDP socket, errno, stdio/string, close/usleep 사용 header. |
| 30-34 | protocol, SHM, context, dispatch, API include. |
| 36 | GUI client 최대 8개. |
| 39-43 | `create_udp_sock()`: Jetson command 송신용 UDP socket 생성. |
| 46-64 | `shm_cmd_queue_pop(q,out)`: SHM ring queue에서 가장 높은 priority command entry 1개 pop. |
| 66-120 | `jetson_tx_thread(arg)`: robot별 SHM command queue를 round-robin으로 polling, command 송신, timeout polling, cleanup. |

### `jetson_tx.c` 함수 파라미터

`shm_cmd_queue_pop(ShmCmdQueue *q, ShmCmdEntry *out)`
- `q`: robot별 SHM command queue.
- `out`: pop한 command entry를 저장할 위치.
- 반환: command를 꺼냈으면 1, queue가 비었으면 0.

`jetson_tx_thread(void *arg)`
- `arg`: `JetsonTxCtx*`.

## 11. `pc_link.c`

목적: 원격 PC와 UDP:9002로 command/status를 교환한다.

| 라인 | 설명 |
|---:|---|
| 1-17 | 파일 주석. PC command 수신, status 주기 송신 설명. |
| 19-26 | socket, errno, pthread, atomic, stdio/string, close header. |
| 28-33 | protocol, SHM, context, command dispatch, API, time utility include. |
| 35 | status 송신 주기 1000ms. |
| 38-47 | `send_status_all(udp_fd,api,pc_addr)`: 모든 robot의 `PcStatusPacketV2` snapshot을 PC로 송신. |
| 50-70 | `create_pc_socket()`: UDP:9002 socket 생성, reuse/timeout 설정, bind. |
| 73-126 | `pc_link_thread(arg)`: PC address 학습, command 수신, status 주기 송신, command forwarding, timeout polling. |

### `pc_link.c` 함수 파라미터

`send_status_all(int udp_fd, BridgeApi *api, const struct sockaddr_in *pc_addr)`
- `udp_fd`: PC link UDP socket.
- `api`: status snapshot을 만들 API runtime.
- `pc_addr`: status를 받을 PC address.

`pc_link_thread(void *arg)`
- `arg`: `PcLinkCtx*`.

## 12. `protocol_timer.c`

목적: 등록된 Jetson에 1Hz heartbeat를 보내고, 주기적으로 command ACK timeout을 처리한다.

| 라인 | 설명 |
|---:|---|
| 1-17 | 파일 주석. heartbeat 역할과 향후 ACK 확장 설명. |
| 19-24 | network/thread/stdio/string/socket/unistd header. |
| 26-30 | protocol, SHM, context, API, time utility include. |
| 33-44 | `send_heartbeat(ctx,udp_fd,robot_id,seq)`: heartbeat `CmdPacket`을 만들어 `bridge_api_send_command()` 호출. |
| 47-116 | `protocol_timer_thread(arg)`: UDP socket 생성, monotonic clock 기반 1초 주기, 주소 table snapshot, heartbeat 송신, timeout polling. |

### `protocol_timer.c` 함수 파라미터

`send_heartbeat(ProtoTimerCtx *ctx, int udp_fd, uint8_t robot_id, uint32_t seq)`
- `ctx`: timer context.
- `udp_fd`: Jetson command UDP socket.
- `robot_id`: heartbeat 대상 robot.
- `seq`: heartbeat sequence.

`protocol_timer_thread(void *arg)`
- `arg`: `ProtoTimerCtx*`.

## 13. `reassembly_shm.c`

목적: image/lidar fragment를 robot별 queue에서 꺼내 frame으로 재조립하고 SHM triple buffer에 publish한다.

| 라인 | 설명 |
|---:|---|
| 1-14 | 파일 주석. fragment queue pop, type별 재조립, SHM publish 설명. |
| 16-20 | stdio/stdlib/string/atomic/time header. |
| 22-27 | protocol, SHM, queue, context, API, time utility include. |
| 30-35 | 재조립 slot 수, buffer 크기, timeout 정의. |
| 37-48 | `ReasmSlot`: 재조립 중인 frame 1개의 상태와 buffer. |
| 51-67 | `expire_stale(ctx,slots)`: timeout 지난 미완성 frame 폐기 및 drop event. |
| 70-78 | `find_slot(slots,type,frame_id)`: 같은 type/frame_id로 재조립 중인 slot 탐색. |
| 80-85 | `alloc_slot(slots)`: 비어 있는 재조립 slot 반환. |
| 88-116 | `commit_image(ctx,shm,rs)`: image slot 선택, JPEG bytes 복사, ready index atomic store, semaphore post, frame metric 갱신. |
| 119-165 | `commit_lidar(ctx,shm,rs)`: lidar payload count 검증, xyzI 배열 복사, ready index/semaphore/metric 갱신. |
| 168-290 | `reassembly_shm_thread(arg)`: queue pop loop, stale cleanup, slot allocation, fragment validation, OOB 방어, frame complete 시 commit. |

### `reassembly_shm.c` 함수 파라미터

`expire_stale(ReasmCtx *ctx, ReasmSlot *slots)`
- `ctx`: robot id와 API pointer를 가진 reassembly context.
- `slots`: 재조립 slot 배열.

`find_slot(ReasmSlot *slots, uint8_t type, uint32_t frame_id)`
- `slots`: 재조립 slot 배열.
- `type`: image/lidar packet type.
- `frame_id`: 찾을 frame id.

`commit_image(ReasmCtx *ctx, SharedData *shm, ReasmSlot *rs)`
- `ctx`: event/metric 갱신용 context.
- `shm`: publish 대상 SHM.
- `rs`: 완성된 image frame slot.

`commit_lidar(ReasmCtx *ctx, SharedData *shm, ReasmSlot *rs)`
- `ctx`: event/metric 갱신용 context.
- `shm`: publish 대상 SHM.
- `rs`: 완성된 lidar frame slot.

`reassembly_shm_thread(void *arg)`
- `arg`: `ReasmCtx*`.

## 14. `frag_queue.h`

목적: `jetson_rx` producer와 `reassembly_shm` consumer 사이 packet queue. mutex+condvar 기반 blocking queue다.

| 라인 | 설명 |
|---:|---|
| 1-8 | 파일 주석. producer/consumer 관계 설명. |
| 10 | include guard. |
| 12 | `<stdint.h>`: byte buffer type. |
| 13 | `<pthread.h>`: mutex/condvar. |
| 14 | `proto.h`: `PROTO_PKT_MAX`. |
| 16 | queue entry 최대 512개. |
| 18-21 | `FragEntry`: packet byte buffer와 길이. |
| 23-31 | `FragQueue`: circular queue index/count, mutex, condvar, stop flag. |
| 33-37 | `frag_queue_init(q)`: index/stop 초기화, mutex/condvar init. |
| 39-42 | `frag_queue_destroy(q)`: mutex/condvar destroy. |
| 45-61 | `frag_queue_push(q,buf,len)`: full이면 oldest drop 후 새 packet copy, condvar signal. |
| 64-81 | `frag_queue_pop(q,buf,max_len)`: empty면 wait, stop이면 -1, packet copy 후 length 반환. |
| 84-89 | `frag_queue_stop(q)`: stop flag set 후 모든 waiter wakeup. |

## 15. `cmd_dispatch.h` / `cmd_dispatch.c`

목적: 기존 코드가 부르던 `send_cmd_to_jetson()` 이름을 유지하면서 내부는 `bridge_api_send_command()`로 통일한다.

`cmd_dispatch.h`

| 라인 | 설명 |
|---:|---|
| 1-8 | 파일 주석. Qt 경로와 PC 경로가 같은 송신 logic을 쓰게 하는 의도. |
| 10 | include guard. |
| 12 | `proto.h`: `CmdPacket`, priority/flag type. |
| 13 | `bridge_ctx.h`: `BridgeApi`. |
| 16-26 | legacy 주석 일부가 남아 있다. 실제 prototype은 API/priority/flags 기반이다. |
| 27 | `send_cmd_to_jetson` prototype 끝. |

`cmd_dispatch.c`

| 라인 | 설명 |
|---:|---|
| 1-3 | 파일 주석. |
| 5-8 | network/stdio/string/socket header. 현재 wrapper 구현에서는 일부 include가 과거 구조의 잔재다. |
| 10 | `cmd_dispatch.h`. |
| 11 | `bridge_api.h`. |
| 12 | `utils.h`. 현재 직접 사용하지 않는 잔재 include다. |
| 14-21 | `send_cmd_to_jetson(...)`: 모든 인자를 그대로 `bridge_api_send_command()`에 전달한다. |

### `send_cmd_to_jetson` 파라미터

- `cmd`: GUI/PC에서 들어온 command.
- `api`: 공통 API runtime.
- `udp_fd`: Jetson 송신 UDP socket.
- `priority`: command priority.
- `flags`: ACK 필요 여부 등 command flag.
- `tag`: log prefix.

## 16. `utils.h`

목적: 공통 timestamp helper.

| 라인 | 설명 |
|---:|---|
| 1-3 | 파일 주석. |
| 5 | include guard. |
| 7 | `<stdint.h>`: `uint64_t`. |
| 8 | `<time.h>`: `clock_gettime`, `CLOCK_MONOTONIC`. |
| 11-15 | `now_us()`: monotonic clock을 microsecond 단위로 반환. |

## 17. `Makefile`

목적: daemon build target 정의.

| 라인 | 설명 |
|---:|---|
| 1 | C compiler는 `gcc`. |
| 2 | optimization, warning, pthread, GNU extension flag. |
| 3 | link library. pthread, rt, math. |
| 5 | build 대상 source list. `bridge_api.c` 포함. |
| 6 | 기본 target binary 이름 `bridge_daemon`. |
| 8-10 | 기본 build rule. source/header 변경 시 compile/link. |
| 12-15 | ASAN build target. memory bug 확인용. |
| 17-20 | TSAN build target. data race 확인용. |
| 22-23 | clean target. 생성 binary 삭제. |
| 25 | phony target 선언. |

## 18. `README_BRIDGE_API.md`

목적: 통합 API 사용 흐름을 짧게 설명하는 개발자 문서.

| 라인 | 설명 |
|---:|---|
| 1 | 문서 제목. |
| 3-4 | `bridge_api.h`가 공통 인터페이스라는 설명. |
| 6-13 | RX/Sensor/State/Command/ACK/Event 경로를 API 함수와 연결해 설명. |
| 15-26 | SHM ABI v2 추가 영역과 GUI attach 원칙. |
| 28-42 | command API 사용법, ACK pending, legacy payload 유지와 envelope 전환 방향. |

## 19. 실행 흐름 요약

1. `main()`이 robot 수를 읽는다.
2. robot별 `/robot_bridge_N` SHM을 만들고 `SharedData` v2 header와 semaphore/rwlock을 초기화한다.
3. `BridgeApi`를 만들고 thread context에 같은 `api` pointer를 넣는다.
4. `jetson_rx_thread`가 UDP:9000 packet을 받아 header validation 후 robot별 처리한다.
5. image/lidar는 `FragQueue`로 들어가고 `reassembly_shm_thread`가 frame으로 조립해 SHM slot에 publish한다.
6. odom은 `bridge_api_update_odom()`으로 odom area와 `RobotState`를 같이 갱신한다.
7. Qt command는 `jetson_tx_thread`가 robot 간 round-robin, robot 내부 priority 순서로 받고 `bridge_api_send_command()`로 Jetson에 보낸다.
8. PC command는 `pc_link_thread`가 UDP:9002에서 받고 같은 API로 Jetson에 보낸다.
9. `protocol_timer_thread`는 heartbeat를 보내고 ACK timeout/retry를 poll한다.
10. watchdog은 packet count 변화를 보고 연결 상태와 disconnect event를 갱신한다.

## 20. 현재 설계상 주의점

- `CommandEnvelope`는 정의되어 있지만 현재 Jetson wire 송신은 기존 `CmdPayload` 호환을 유지한다.
- `PcStatusPacketV2`를 송신하므로 PC 쪽 receiver도 v2 크기와 layout에 맞춰야 한다.
- Qt는 반드시 `../bride_app_codex/shm_def.h`로 빌드해야 SHM offset이 맞는다.
- `RobotState`는 SHM ABI 이름이므로 Qt 내부 타입과 충돌하지 않게 UI 쪽은 `UiRobotState` 같은 별도 이름을 써야 한다.
- ACK retry는 command type/seq 기반의 기본 골격이다. 실제 Jetson이 `PKT_TYPE_CMD_ACK`와 `CmdAckPayload`를 회신해야 완전 동작한다.
