# 08. `bridge_main.c` 수업 자료

## 이 파일의 역할

`bridge_main.c`는 BridgeDaemon의 main program이다. SHM 생성, queue 생성, thread context 구성, thread start/join, watchdog, cleanup을 담당한다.

## 개념 설명: Daemon Lifecycle

daemon은 단순히 `main()`에서 loop 하나만 도는 프로그램이 아니다. 여러 worker thread와 OS resource를 관리한다.

이 파일의 lifecycle은 다음과 같다.

```text
parse arguments
init signal handler
init address table
init SHM and queues
init BridgeApi
build thread contexts
start threads
run watchdog loop
on stop: wake queues, join threads, cleanup resources
```

## include 설명

| 헤더 | 쓰는 기능 |
|---|---|
| `<fcntl.h>` | `shm_open` flag |
| `<pthread.h>` | thread/rwlock/mutex |
| `<sched.h>` | affinity/realtime scheduling |
| `<signal.h>` | SIGINT/SIGTERM |
| `<stdatomic.h>` | stop flag |
| `<stdbool.h>` | true/false |
| `<stdio.h>` | log |
| `<stdlib.h>` | `atoi` |
| `<string.h>` | `memset`, `strerror` |
| `<errno.h>` | thread creation error text |
| `<sys/mman.h>` | mmap/munmap/shm |
| `<unistd.h>` | close/ftruncate/getpid/sleep |
| `<sys/stat.h>` | umask |
| `"shm_def.h"` | SHM layout |
| `"frag_queue.h"` | fragment queue |
| `"proto.h"` | port/socket constants |
| `"bridge_ctx.h"` | contexts |
| `"bridge_api.h"` | common API |

## 구조체: `BridgeCtx`

process 전체 runtime을 모아둔 global context다.

| 필드 | 의미 |
|---|---|
| `num_robots` | 운용 robot 수 |
| `shm` | robot별 SHM pointer |
| `fq` | robot별 fragment queue |
| `stop` | process-wide atomic stop flag |
| `addr_table` | Jetson address table |
| `api` | common API runtime |
| `rx_ctx`, `tx_ctx`, `timer_ctx`, `pc_ctx`, `reasm_ctx` | thread context |
| `*_tid` | thread id |

## 함수 설명

### `shm_init_one(int robot_id)`

기능:
- `/robot_bridge_N` SHM 생성.
- `ftruncate()`로 `sizeof(SharedData)` 크기 확보.
- `mmap()`으로 process address에 mapping.
- memory zero clear.
- image/lidar ready index를 -1로 초기화.
- process-shared semaphore/rwlock 초기화.
- `bridge_api_init_shm()`으로 v2 header와 metrics 초기화.

개념:
- SHM name은 filesystem path가 아니라 POSIX SHM namespace 이름이다.
- `sem_init(..., 1, 0)`의 두 번째 인자 1은 process-shared 의미다.

### `shm_cleanup_one(SharedData *shm, int robot_id)`

기능:
- semaphore/rwlock destroy.
- mmap 해제.
- SHM name unlink.

### `cleanup_all(int num_robots)`

기능:
- 모든 robot queue와 SHM 정리.
- address table mutex와 API 정리.

### `create_thread(void *(*fn)(void *), void *arg, int priority, int core)`

기능:
- thread attribute 구성.
- core affinity와 realtime priority를 시도.
- 실패하면 normal thread로 재시도.

파라미터:
- `fn`: thread entry.
- `arg`: thread context.
- `priority`: realtime priority. 0이면 설정하지 않음.
- `core`: pinning할 CPU core. 음수면 설정하지 않음.

수업 포인트:
- embedded/robotics에서는 realtime scheduling을 원할 수 있지만 권한이 없을 수 있다. fallback이 있어야 field에서 덜 깨진다.

### `sig_handler(int sig)`

기능:
- signal이 오면 atomic stop flag만 set한다.

중요:
- signal handler 안에서 `fprintf`, mutex lock, condvar signal 같은 작업을 하면 안전하지 않다.

### `request_stop_all()`

기능:
- stop flag set.
- 모든 fragment queue를 stop시켜 blocking consumer를 깨운다.

### `watchdog_loop()`

기능:
- 1초마다 robot별 packet count 변화를 확인한다.
- 3초 이상 packet 변화가 없으면 disconnected로 판단한다.
- disconnect event를 발행한다.
- drop count를 log로 출력한다.

### `main(int argc, char *argv[])`

기능:
- robot 수 파싱.
- signal handler 등록.
- address table 초기화.
- SHM/queue/API/context 초기화.
- RX/TX/timer/PC/reassembly thread 생성.
- watchdog 실행.
- 종료 시 join/cleanup.




bridge_main.c의 main()은 한마디로 BridgeDaemon 전체 자원 만들기 → thread context 배선 → thread 실행 → watchdog 대기 → 종
  료 신호 처리 → 자원 정리 흐름이야.

  아래는 main() 순서 그대로 설명할게.

  ## 1. 프로세스 기본 설정

  umask(0);

  새로 만드는 파일/SHM/socket 등의 권한이 umask 때문에 깎이지 않게 한다. 이 daemon은 /dev/shm 공유메모리와 UDP socket 같은
  IPC 자원을 만들기 때문에 권한 문제를 줄이려는 의도다.

  int num_robots = (argc >= 2) ? atoi(argv[1]) : 1;

  실행 인자로 로봇 수를 받는다.

  ./bridge_app 3

  이면 num_robots = 3.

  인자가 없으면 기본값은 1.

  if (num_robots < 1 || num_robots > MAX_ROBOTS)

  로봇 수가 범위를 벗어나면 바로 종료한다. 여기서 MAX_ROBOTS는 shm_def.h 쪽에 정의된 최대 로봇 수다.

  ———

  ## 2. 전역 context 초기화

  g_ctx.num_robots = num_robots;
  atomic_init(&g_ctx.stop, false);

  g_ctx는 daemon 전체 상태를 들고 있는 전역 구조체다.

  중요한 필드는 이런 것들:

  SharedData *shm[MAX_ROBOTS];
  FragQueue fq[MAX_ROBOTS];
  atomic_bool stop;

  JetsonAddrTable addr_table;
  BridgeApi api;

  JetsonRxCtx rx_ctx;
  JetsonTxCtx tx_ctx;
  ProtoTimerCtx timer_ctx;
  PcLinkCtx pc_ctx;
  ReasmCtx reasm_ctx[MAX_ROBOTS];

  즉 g_ctx는 모든 thread가 공유할 자원과 thread별 context를 한곳에 모아둔 중앙 배선판이다.

  stop은 모든 thread가 종료 여부를 확인하는 공통 flag다.

  ———

  ## 3. SIGINT/SIGTERM 등록

  signal(SIGINT,  sig_handler);
  signal(SIGTERM, sig_handler);

  Ctrl+C 또는 종료 신호가 오면 sig_handler()가 호출된다.

  static void sig_handler(int sig) {
      (void)sig;
      atomic_store_explicit(&g_ctx.stop, true, memory_order_release);
  }

  시그널 핸들러 안에서는 복잡한 정리를 하지 않고 stop = true만 한다.

  이게 중요한 이유는 signal handler 안에서 mutex lock, malloc, printf 같은 걸 함부로 하면 deadlock이나 undefined behavior
  위험이 있기 때문이다. 그래서 “종료 요청 표시”만 하고 실제 정리는 main 흐름에서 한다.

  ———

  ## 4. Jetson 주소 table 초기화

  memset(&g_ctx.addr_table, 0, sizeof(g_ctx.addr_table));
  pthread_mutex_init(&g_ctx.addr_table.mu, NULL);

  addr_table은 robot id별 Jetson IP/port를 저장하는 table이다.

  이 프로젝트는 Jetson 주소를 설정파일에서 읽는 게 아니라, RX thread가 Jetson으로부터 UDP packet을 받으면 source IP를 보고
  학습한다.

  대략 이런 역할이다:

  robot 0 -> 192.168.x.x:9001
  robot 1 -> 192.168.x.y:9001

  여러 thread가 이 table을 읽고 쓸 수 있으므로 mutex가 붙어 있다.

  ———

  ## 5. 로봇별 SHM + FragQueue 초기화

  for (int i = 0; i < num_robots; i++) {
      g_ctx.shm[i] = shm_init_one(i);
      ...
      frag_queue_init(&g_ctx.fq[i]);
  }

  로봇마다 두 가지를 만든다.

  robot i
    -> SharedData SHM
    -> FragQueue

  shm_init_one(i)는 /robot_bridge_i 같은 이름의 POSIX shared memory를 만든다.

  그 안에는:

  - image triple buffer
  - lidar triple buffer
  - odom
  - meta
  - RobotState
  - EventLog
  - BridgeMetrics
  - semaphore
  - rwlock

  같은 것들이 들어간다.

  frag_queue_init(&g_ctx.fq[i])는 image/lidar fragment packet을 reassembly thread로 넘기기 위한 queue를 초기화한다.

  흐름은 이렇게 된다:

  jetson_rx_thread
    -> UDP image/lidar fragment 수신
    -> robot_id별 FragQueue에 push

  reassembly_shm_thread
    -> FragQueue에서 pop
    -> frame 재조립
    -> SharedData SHM에 publish

  초기화 중간에 실패하면 이미 만든 로봇들의 queue와 SHM을 정리하고 종료한다.

  ———

  ## 6. BridgeApi 초기화

  bridge_api_init(&g_ctx.api, &g_ctx.addr_table, g_ctx.shm, num_robots);

  BridgeApi는 여러 thread가 공통으로 쓰는 middleware API다.

  예를 들면:

  - bridge_api_note_rx()
  - bridge_api_update_odom()
  - bridge_api_send_command()
  - bridge_api_handle_ack()
  - bridge_api_publish_event()
  - bridge_api_poll_timeouts()

  같은 함수들이 공통 state/event/command 처리를 담당한다.

  즉 각 thread가 SHM이나 pending command table을 제멋대로 만지지 않고, BridgeApi를 통해 같은 방식으로 처리하게 만든다.

  ———

  ## 7. RX context 구성

  g_ctx.rx_ctx.num_robots = num_robots;
  g_ctx.rx_ctx.stop       = &g_ctx.stop;
  g_ctx.rx_ctx.addr_table = &g_ctx.addr_table;
  g_ctx.rx_ctx.api        = &g_ctx.api;

  jetson_rx_thread에 넘길 재료를 채운다.

  RX thread는 다음이 필요하다:

  - 로봇 수
  - 종료 flag
  - Jetson 주소 table
  - BridgeApi
  - robot별 SHM pointer
  - robot별 FragQueue pointer

  그래서 아래도 채운다.

  for (int i = 0; i < num_robots; i++) {
      g_ctx.rx_ctx.shm_arr[i] = g_ctx.shm[i];
      g_ctx.rx_ctx.fq_arr[i]  = &g_ctx.fq[i];
  }

  RX thread는 packet type에 따라 다르게 행동한다.

  ODOM
    -> bridge_api_update_odom()

  ACK
    -> bridge_api_handle_ack()

  IMAGE/LIDAR fragment
    -> FragQueue push

  ———

  ## 8. TX context 구성

  g_ctx.tx_ctx.addr_table = &g_ctx.addr_table;
  g_ctx.tx_ctx.num_robots = num_robots;
  g_ctx.tx_ctx.stop       = &g_ctx.stop;
  g_ctx.tx_ctx.api        = &g_ctx.api;

  jetson_tx_thread는 Qt GUI가 robot별 SHM command queue에 넣은 command를 받아 Jetson UDP로 보낸다.

  대략 흐름:

  Qt GUI
    -> /robot_bridge_N SharedData.cmd_queue
    -> jetson_tx_thread
    -> bridge_api_send_command()
    -> Jetson UDP:9001

  그래서 TX thread는 robot별 SHM 배열, Jetson 주소 table, BridgeApi가 필요하다.

  ———

  ## 9. Timer context 구성

  g_ctx.timer_ctx.addr_table = &g_ctx.addr_table;
  g_ctx.timer_ctx.num_robots = num_robots;
  g_ctx.timer_ctx.stop       = &g_ctx.stop;
  g_ctx.timer_ctx.api        = &g_ctx.api;

  protocol_timer_thread는 주기적으로 heartbeat나 command timeout을 처리한다.

  예상 흐름은:

  주기적으로 heartbeat command 전송
  pending ACK timeout 확인
  필요하면 retry 또는 timeout event 발행

  이것도 command를 보내야 하므로 주소 table과 BridgeApi가 필요하다.

  ———

  ## 10. PC link context 구성

  g_ctx.pc_ctx.addr_table = &g_ctx.addr_table;
  g_ctx.pc_ctx.num_robots = num_robots;
  g_ctx.pc_ctx.stop       = &g_ctx.stop;
  g_ctx.pc_ctx.api        = &g_ctx.api;

  pc_link_thread는 원격 PC와 통신하는 gateway다.

  for (int i = 0; i < num_robots; i++)
      g_ctx.pc_ctx.shm_arr[i] = g_ctx.shm[i];

  PC로 status를 보내야 하므로 robot별 SHM도 필요하다.

  흐름은 대략:

  Remote PC command
    -> pc_link_thread
    -> bridge_api_send_command()

  BridgeDaemon status
    -> pc_link_thread
    -> Remote PC

  ———

  ## 11. Reassembly context 구성
```
  for (int i = 0; i < num_robots; i++) {
      g_ctx.reasm_ctx[i].shm = g_ctx.shm[i];
      g_ctx.reasm_ctx[i].fq  = &g_ctx.fq[i];
      g_ctx.reasm_ctx[i].api = &g_ctx.api;
      g_ctx.reasm_ctx[i].robot_id = (uint8_t)i;
  }
```
  로봇마다 reassembly thread가 하나씩 있다.

  각 reassembly thread는 자기 robot만 담당한다.

  robot 0 reassembly thread
    -> fq[0]에서 pop
    -> shm[0]에 image/lidar publish

  robot 1 reassembly thread
    -> fq[1]에서 pop
    -> shm[1]에 image/lidar publish

  그래서 robot_id, shm, fq, api를 넣어준다.

  ———

  ## 12. Thread 생성

  `g_ctx.rx_tid = create_thread(jetson_rx_thread, &g_ctx.rx_ctx, 60, 2);`

  RX thread 생성.

  priority 60, core 2를 요청한다.

  다만 create_thread()는 realtime priority나 CPU affinity 설정이 실패하면 normal thread로 재시도한다.

  즉 “가능하면 고정/고우선순위, 안 되면 일반 thread” 전략이다.

  그 다음 순서대로 만든다.
```
  g_ctx.tx_tid = create_thread(jetson_tx_thread, &g_ctx.tx_ctx, 80, -1);
  g_ctx.timer_tid = create_thread(protocol_timer_thread, &g_ctx.timer_ctx, 80, -1);
  g_ctx.pc_tid = create_thread(pc_link_thread, &g_ctx.pc_ctx, 40, -1);
```
  의미:

  RX      priority 60, core 2
  TX      priority 80
  Timer   priority 80
  PC link priority 40

  그리고 robot 수만큼 reassembly thread 생성:

  for (int i = 0; i < num_robots; i++) {
      g_ctx.reasm_tid[i] = create_thread(reassembly_shm_thread,
                                         &g_ctx.reasm_ctx[i], 0, -1);
  }

  reassembly는 priority 0, 즉 realtime 설정 없이 일반 thread로 생성한다.

  중간에 thread 생성 실패하면:

  1. request_stop_all()
  2. 이미 만든 thread join
  3. cleanup_all()
  4. return 1

  순서로 정리한다.

  ———

  ## 13. 시작 로그 출력
```
  fprintf(stderr,
      "[main] BridgeDaemon 시작 (PID=%d, robots=%d)\n"
      "[main] 수신 포트: %d | 커맨드 포트: %d | PC 포트: %d\n"
      "[main] Qt 명령 IPC: SHM cmd_queue (/robot_bridge_N)\n",
      getpid(), num_robots,
      BRIDGE_PORT, JETSON_CMD_PORT, PC_LINK_PORT);
```
  여기서 운영자가 확인할 수 있는 정보를 출력한다.

  PID
  robot 수
  Jetson 수신 포트
  Jetson command 포트
  PC link 포트
  Qt 명령 IPC 방식

  ———

  ## 14. watchdog loop 진입

  watchdog_loop();

  main thread는 여기서 watchdog 역할로 남는다.

  watchdog_loop()는 1초마다 robot별 상태를 확인한다.

  핵심은 packet count다.

  int cur_pkt = atomic_load(&shm->meta.pkt_count);

  이 값이 3초 동안 변하지 않으면:

  atomic_store(&shm->meta.jetson_connected, 0);

  연결 끊김으로 판단한다.

  그리고 기존에 connected였는데 disconnected로 바뀌면:

  bridge_api_publish_event(... EVENT_TYPE_DISCONNECTED ...)

  disconnect event를 남긴다.

  이 loop는 g_ctx.stop == true가 되면 빠져나온다.

  즉 Ctrl+C, SIGTERM, 또는 내부 종료 요청이 오기 전까지 main thread는 watchdog으로 계속 돈다.

  ———

  ## 15. 전체 종료 요청

  request_stop_all();

  watchdog에서 빠져나오면 모든 thread에게 종료하라고 알린다.

  static void request_stop_all(void) {
      atomic_store_explicit(&g_ctx.stop, true, memory_order_release);
      for (int i = 0; i < g_ctx.num_robots; i++)
          frag_queue_stop(&g_ctx.fq[i]);
  }

  여기서 중요한 게 `frag_queue_stop()`이다.

  reassembly thread는 frag_queue_pop()에서 condvar로 기다릴 수 있다. 그냥 stop = true만 하면 queue에서 잠든 thread가 못 깨
  어날 수 있다.

  그래서 frag_queue_stop()이 condvar broadcast/signal을 해서 대기 중인 reassembly thread를 깨운다.

  ———

  ## 16. Thread join
```
  pthread_join(g_ctx.rx_tid,    NULL);
  pthread_join(g_ctx.tx_tid,    NULL);
  pthread_join(g_ctx.timer_tid, NULL);
  pthread_join(g_ctx.pc_tid,    NULL);
  for (int i = 0; i < num_robots; i++)
      pthread_join(g_ctx.reasm_tid[i], NULL);
```
  모든 thread가 종료될 때까지 기다린다.

  이걸 해야 하는 이유:

  - thread가 아직 SHM을 쓰고 있는데 munmap()하면 위험함
  - socket/thread 내부 자원 정리가 끝나기 전에 process가 정리되면 상태가 꼬일 수 있음
  - 깨끗한 shutdown 보장

  ———

  ## 17. 전체 자원 정리

  `cleanup_all(num_robots);`

  내부에서:
```
  for (int i = 0; i < num_robots; i++) {
      frag_queue_destroy(&g_ctx.fq[i]);
      shm_cleanup_one(g_ctx.shm[i], i);
  }
  pthread_mutex_destroy(&g_ctx.addr_table.mu);
  bridge_api_destroy(&g_ctx.api);
```
  로봇별 queue 제거, SHM semaphore/rwlock destroy, munmap, shm_unlink, 주소 table mutex destroy, BridgeApi destroy를 수행한
  다.

  마지막으로:
```
  fprintf(stderr, "[main] BridgeDaemon 완전 종료\n");
  return 0;
```
  정상 종료.

  ———

  전체 흐름을 그림으로 보면 이렇다.
```
  main()
    |
    |-- parse num_robots
    |-- init stop flag / signal handler
    |-- init addr_table
    |
    |-- for each robot:
    |     |-- shm_init_one()
    |     |-- frag_queue_init()
    |
    |-- bridge_api_init()
    |
    |-- fill rx_ctx / tx_ctx / timer_ctx / pc_ctx / reasm_ctx[]
    |
    |-- create jetson_rx_thread
    |-- create jetson_tx_thread
    |-- create protocol_timer_thread
    |-- create pc_link_thread
    |-- create reassembly_shm_thread per robot
    |
    |-- watchdog_loop()
    |     `-- waits until stop == true
    |
    |-- request_stop_all()
    |-- pthread_join(all)
    |-- cleanup_all()
    |
    `-- return 0
```
  핵심은 main()이 직접 packet을 처리하지 않는다는 점이야.
  main()은 자원 준비, thread 배선, lifecycle 관리, 종료 정리를 담당하고, 실제 통신 처리는 각 worker thread가 맡는다.

## 수업 포인트

- signal handler와 cleanup logic을 분리하는 것은 daemon 안정성의 기본이다.
- resource를 만든 순서와 반대로 정리해야 leak을 줄일 수 있다.
- thread 생성 실패 경로에서도 이미 만든 thread/resource를 정리해야 한다.

## 실습 질문

1. `volatile int stop` 대신 `atomic_bool`을 쓰는 이유는 무엇인가?
2. signal handler 안에서 `frag_queue_stop()`을 직접 호출하면 왜 위험한가?
3. SHM을 `shm_unlink()`하지 않으면 다음 실행에서 어떤 문제가 생길 수 있는가?
