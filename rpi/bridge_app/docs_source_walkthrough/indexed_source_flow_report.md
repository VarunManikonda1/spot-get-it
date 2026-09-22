# bride_app_codex_indexed Source Flow Report

## 1. 전체 개요

`bride_app_codex_indexed`는 기존 `bride_app_codex`를 백업으로 유지한 채, Image/LiDAR fragment 경로를 **Packet Pool + Index Queue + Index-Based Reassembly** 구조로 바꾼 버전이다.

기존 구조는 UDP 수신 후 fragment packet byte array를 queue에 복사하고, pop할 때 다시 복사하고, 재조립 buffer에 다시 복사한 뒤, 마지막에 SHM으로 복사했다.

indexed 구조는 다음처럼 동작한다.

```text
Jetson UDP:9000
  -> jetson_rx_thread
  -> recvfrom()이 RxPacketPool slot에 직접 수신
  -> Image/LiDAR는 FragIndexQueue에 slot_id만 push
  -> reassembly_shm_thread가 slot_id를 pop
  -> IndexReasmSlot에 frag_idx -> slot_id만 저장
  -> frame 완성 시 pool slot들을 frag_idx 순서대로 읽어서 SHM에 commit
  -> 사용한 pool slot release
  -> Qt가 SHM img/lidar ready index와 semaphore로 읽음
```

Image는 daemon 내부에서 다음 수준까지 copy가 줄었다.

```text
kernel UDP buffer -> RxPacketPool slot
RxPacketPool slots -> SHM ImgSlot
```

LiDAR는 현재 SHM layout이 `x[]`, `y[]`, `z[]`, `intensity[]`로 나뉜 SoA 구조라서 commit 단계에서 staging buffer를 한 번 거친 뒤 SoA 배열에 쓴다.

```text
kernel UDP buffer -> RxPacketPool slot
RxPacketPool slots -> LiDAR staging buffer
staging buffer -> SHM LidarSlot SoA arrays
```

## 2. 실행 시 스레드 구성

`bridge_main.c`가 전체 daemon을 시작한다.

로봇 N대 기준:

```text
main/watchdog thread       1개
jetson_rx_thread           1개
jetson_tx_thread           1개
protocol_timer_thread      1개
pc_link_thread             1개
reassembly_shm_thread      N개, 로봇별 1개
```

중요한 공유 객체:

```text
RxPacketPool               전체 daemon 공용 packet pool
FragIndexQueue[MAX_ROBOTS] 로봇별 Image/LiDAR slot_id queue
SharedData *shm[MAX_ROBOTS] 로봇별 POSIX shared memory
JetsonAddrTable            robot_id -> Jetson UDP address table
BridgeApi                  SHM, event, ACK, command 공통 API
```

## 3. 주요 데이터 흐름

### 3.1 Image 수신 흐름

```text
Jetson
  -> UDP 9000, PKT_TYPE_IMAGE fragments
  -> jetson_rx.c
  -> rx_packet_pool_acquire()
  -> recvfrom(pool->slots[slot_id].buf)
  -> proto_validate_header()
  -> robot_id 확인
  -> bridge_api_note_rx()
  -> frag_index_queue_push(robot queue, slot_id)
  -> reassembly_shm.c
  -> frag_index_queue_pop()
  -> IndexReasmSlot.slot_ids[frag_idx] = slot_id
  -> 모든 fragment 도착
  -> commit_image_indexed()
  -> SHM img_slots[next].data + payload_offset 에 최종 copy
  -> atomic_store(img_ready_idx)
  -> sem_post(img_sem)
  -> pool slot 전체 release
```

Image는 `IndexReasmSlot` 내부에 큰 image buffer를 만들지 않는다. `frag_idx`별로 pool slot 번호만 저장한다.

### 3.2 LiDAR 수신 흐름

```text
Jetson
  -> UDP 9000, PKT_TYPE_LIDAR fragments
  -> jetson_rx.c
  -> pool slot 직접 수신
  -> slot_id queue 전달
  -> reassembly_shm.c
  -> frag_idx -> slot_id mapping
  -> 모든 fragment 도착
  -> build_lidar_staging()
  -> staging buffer에 offset 기준 조립
  -> commit_lidar_indexed()
  -> count + xyzi AoS payload를 SHM SoA arrays로 변환 write
  -> atomic_store(lidar_ready_idx)
  -> sem_post(lidar_sem)
  -> pool slot 전체 release
```

LiDAR는 wire payload가 다음 형태다.

```text
[uint32_t count][float x][float y][float z][float intensity]...
```

하지만 SHM은 다음 형태다.

```c
float x[LIDAR_MAX_PTS];
float y[LIDAR_MAX_PTS];
float z[LIDAR_MAX_PTS];
float intensity[LIDAR_MAX_PTS];
```

그래서 Image처럼 단순히 최종 buffer에 `memcpy`만 할 수 없다. 현재 indexed 1차 구현은 안전성을 위해 staging buffer를 사용한다.

### 3.3 Odom 수신 흐름

```text
Jetson
  -> UDP 9000, PKT_TYPE_ODOM
  -> jetson_rx.c
  -> pool slot 직접 수신
  -> bridge_api_note_rx()
  -> bridge_api_update_odom()
  -> SHM odom fields + RobotState 갱신
  -> pool slot 즉시 release
```

Odom은 fragment queue나 reassembly를 거치지 않는다.

### 3.4 Command ACK 수신 흐름

```text
Jetson
  -> UDP 9000, PKT_TYPE_CMD_ACK
  -> jetson_rx.c
  -> pool slot 직접 수신
  -> bridge_api_note_rx()
  -> bridge_api_handle_ack()
  -> pending command table에서 해당 command 제거
  -> SHM metrics/state/event 갱신
  -> pool slot 즉시 release
```

### 3.5 Qt command 송신 흐름

```text
Qt
  -> SHM SharedData.cmd_queue
  -> jetson_tx.c
  -> shm_cmd_queue_pop_highest()
  -> send_cmd_to_jetson()
  -> bridge_api_send_command()
  -> UDP 9001
  -> Jetson
```

`ESTOP` 같은 명령은 priority가 높게 계산된다. ACK가 필요한 command는 `BridgeApi.pending[]`에 등록된다.

### 3.6 PC command/status 흐름

```text
PC
  -> UDP 9002 CmdPacket
  -> pc_link.c
  -> PC address learn
  -> send_cmd_to_jetson()
  -> UDP 9001 Jetson
```

상태 송신:

```text
pc_link.c
  -> 1초마다 bridge_api_snapshot_status()
  -> PcStatusPacketV2
  -> UDP로 마지막으로 학습한 PC 주소에 송신
```

### 3.7 Heartbeat 흐름

```text
protocol_timer.c
  -> 1Hz
  -> JetsonAddrTable snapshot
  -> robot별 heartbeat CmdPacket 생성
  -> bridge_api_send_command()
  -> UDP 9001 Jetson
```

Jetson 주소는 `jetson_rx.c`가 Jetson에서 처음 packet을 받았을 때 학습한다. 따라서 아직 한 번도 수신하지 않은 robot에는 command/heartbeat를 보낼 주소가 없다.

## 4. 파일별 시스템 흐름

## 4.1 `Makefile`

빌드 대상은 `bridge_daemon`이다.

컴파일 source:

```text
bridge_main.c
jetson_rx.c
jetson_tx.c
reassembly_shm.c
protocol_timer.c
pc_link.c
cmd_dispatch.c
bridge_api.c
shm_cmd_queue.c
rx_packet_pool.c
```

indexed 버전에서 새로 추가된 핵심 빌드 파일은 `rx_packet_pool.c`다. `frag_index_queue.h`는 header-only inline queue라 별도 `.c` 파일이 없다.

빌드 명령:

```bash
cd /home/pi/robot_project/bride_app_codex_indexed
make
```

ASAN 빌드:

```bash
make asan
```

## 4.2 `proto.h`

UDP wire protocol의 공통 정의 파일이다.

주요 상수:

```text
BRIDGE_PORT      9000  Jetson -> RPi 수신
JETSON_CMD_PORT  9001  RPi -> Jetson command
PC_LINK_PORT     9002  PC <-> RPi
PROTO_MTU        1400
PROTO_PKT_MAX    sizeof(PktHeader) + 1400
```

모든 UDP packet은 다음 구조다.

```text
[ PktHeader ][ payload ]
```

`PktHeader` 핵심 필드:

```text
type            IMAGE/LIDAR/ODOM/CMD_ACK 등
robot_id        대상 robot index
frag_idx        현재 fragment 번호
frag_total      전체 fragment 개수
payload_len     이번 packet payload 길이
frame_id        frame 식별자 또는 seq
payload_offset  전체 frame 안에서 payload 위치
timestamp_us    송신 측 timestamp
```

`proto_validate_header()`는 type, payload length, fragment index, MTU, 단일 packet 타입의 fragment 조건을 검증한다.

## 4.3 `shm_def.h`

BridgeDaemon과 Qt가 공유하는 POSIX shared memory layout이다.

로봇별로 SHM 파일이 하나씩 만들어진다.

```text
/robot_bridge_0
/robot_bridge_1
...
```

Image:

```text
ImgSlot img_slots[3]
atomic img_ready_idx
sem_t img_sem
```

LiDAR:

```text
LidarSlot lidar_slots[3]
atomic lidar_ready_idx
sem_t lidar_sem
```

Odom/Meta/State:

```text
pthread_rwlock_t odom_lock
pthread_rwlock_t meta_lock
pthread_rwlock_t state_lock
```

Qt command queue:

```text
ShmCmdQueue cmd_queue
```

indexed 변경은 daemon 내부 packet 이동 방식만 바꾸므로 SHM ABI는 기존과 동일하다. Qt 쪽에서 읽는 구조는 바뀌지 않는다.

## 4.4 `bridge_ctx.h`

스레드 간 context 구조를 정의한다.

indexed 버전의 핵심 변경:

```c
FragIndexQueue *fq_arr[MAX_ROBOTS];
RxPacketPool   *rx_pool;
```

`JetsonRxCtx`는 RX thread가 필요로 하는 모든 포인터를 갖는다.

```text
shm_arr[]       robot별 SHM
fq_arr[]        robot별 slot_id queue
rx_pool         전체 packet pool
addr_table      Jetson address table
num_robots
stop
api
```

`ReasmCtx`는 robot별 reassembly thread에 전달된다.

```text
shm             해당 robot SHM
fq              해당 robot FragIndexQueue
rx_pool         전체 packet pool
api
robot_id
```

## 4.5 `rx_packet_pool.h` / `rx_packet_pool.c`

UDP packet 하나당 pool slot 하나를 사용하는 고정 크기 memory pool이다.

```c
#define RX_PACKET_POOL_SIZE 4096
```

slot 하나:

```c
typedef struct {
    uint8_t buf[PROTO_PKT_MAX];
    int     len;
    uint8_t in_use;
} RxPacketSlot;
```

pool:

```c
RxPacketSlot slots[4096];
int free_stack[4096];
int free_count;
pthread_mutex_t mu;
```

동작:

```text
rx_packet_pool_init()
  -> 모든 slot을 free_stack에 넣음

rx_packet_pool_acquire()
  -> free_stack에서 slot_id 하나 pop
  -> in_use = 1
  -> len = 0

recvfrom()
  -> pool->slots[slot_id].buf에 직접 수신

rx_packet_pool_release()
  -> in_use = 0
  -> len = 0
  -> free_stack에 slot_id push
```

중요한 점:

- pool slot들은 frame 단위로 연속 배치되지 않는다.
- Image 한 장이 35개 fragment라면 35개의 임의 slot을 점유한다.
- reassembly가 `frag_idx -> slot_id` mapping으로 순서를 복원한다.

## 4.6 `frag_index_queue.h`

기존 `frag_queue.h`는 packet bytes를 queue entry에 복사했다. 새 `FragIndexQueue`는 packet data를 복사하지 않고 `slot_id`만 전달한다.

queue entry:

```c
int slot_ids[FRAG_QUEUE_SIZE];
```

동기화:

```text
pthread_mutex_t mu
pthread_cond_t  cv
```

producer:

```text
jetson_rx_thread
  -> frag_index_queue_push(fq, slot_id, &dropped_slot_id)
```

consumer:

```text
reassembly_shm_thread
  -> frag_index_queue_pop(fq)
```

queue full 정책:

```text
if count == FRAG_QUEUE_SIZE:
  oldest slot_id를 queue에서 제거
  dropped_slot_id로 caller에게 반환
  caller가 해당 pool slot release
```

종료:

```text
frag_index_queue_stop()
  -> stop = 1
  -> pthread_cond_broadcast()
```

## 4.7 `bridge_main.c`

daemon의 lifecycle을 담당한다.

주요 초기화 순서:

```text
1. robot 수 파싱
2. stop atomic 초기화
3. signal handler 등록
4. JetsonAddrTable 초기화
5. RxPacketPool 초기화
6. robot별 SHM 생성
7. robot별 FragIndexQueue 초기화
8. BridgeApi 초기화
9. 각 thread context 연결
10. thread 생성
11. watchdog_loop 진입
```

SHM 생성:

```text
shm_unlink()
shm_open()
ftruncate(sizeof(SharedData))
mmap()
sem_init()
pthread_rwlock_init()
shm_cmd_queue_init()
bridge_api_init_shm()
```

thread 생성:

```text
jetson_rx_thread       priority 60, core 2 시도
jetson_tx_thread       priority 80
protocol_timer_thread  priority 80
pc_link_thread         priority 40
reassembly_shm_thread  robot별 1개
```

실시간 priority/affinity 설정이 실패하면 일반 pthread 생성으로 재시도한다.

watchdog:

```text
1초마다 robot별 pkt_count 확인
3초 동안 증가 없으면 jetson_connected = 0
disconnect event 발행
drop count 로그 출력
```

종료:

```text
stop = true
robot별 FragIndexQueue stop
thread join
SHM cleanup
FragIndexQueue destroy
RxPacketPool destroy
BridgeApi destroy
```

## 4.8 `jetson_rx.c`

Jetson에서 들어오는 UDP 9000 packet을 받는 핵심 RX thread다.

소켓 설정:

```text
socket(AF_INET, SOCK_DGRAM)
SO_REUSEADDR
SO_RCVBUF = 8MB
SO_RCVTIMEO = 100ms
bind(INADDR_ANY:9000)
```

수신 루프:

```text
1. rx_packet_pool_acquire()
2. pool slot이 있으면 pkt = pool->slots[slot_id].buf
3. pool이 비었으면 drain_buf로 받아서 drop 처리
4. recvfrom(fd, pkt, PROTO_PKT_MAX)
5. packet length/header 검증
6. proto_validate_header()
7. robot_id 범위 검증
8. learn_addr()
9. bridge_api_note_rx()
10. type별 dispatch
```

type별 처리:

```text
PKT_TYPE_ODOM
  -> bridge_api_update_odom()
  -> pool slot release

PKT_TYPE_CMD_ACK
  -> bridge_api_handle_ack()
  -> pool slot release

PKT_TYPE_IMAGE / PKT_TYPE_LIDAR
  -> frag_index_queue_push()
  -> ownership transferred to reassembly
  -> 여기서는 release하지 않음

default/error
  -> pool slot release
```

ownership 규칙:

```text
RX thread가 pool slot acquire
Image/LiDAR queue push 성공 후부터 reassembly thread가 slot owner
Odom/ACK/error는 RX thread가 즉시 release
```

pool exhausted:

```text
slot acquire 실패
  -> drain_buf로 recvfrom()
  -> bridge_api_note_drop()
  -> packet은 버림
```

## 4.9 `reassembly_shm.c`

indexed 구조의 중심 파일이다.

기존 reassembly buffer:

```text
ReasmSlot.buf[REASM_BUF_MAX]
```

indexed reassembly slot:

```c
int slot_ids[REASM_MAX_FRAGS];
uint8_t frag_received[REASM_MAX_FRAGS];
```

즉 payload 자체를 저장하지 않고, 각 fragment가 들어있는 pool slot 번호만 저장한다.

상수:

```text
REASM_SLOTS       6
REASM_MAX_FRAGS   512
REASM_BUF_MAX     512KB
REASM_TIMEOUT_US  100ms
```

main loop:

```text
1. frag_index_queue_pop()으로 slot_id 획득
2. pool->slots[slot_id]에서 PktHeader 확인
3. expire_stale()
4. validate_fragment_header()
5. frame_id/type 기준 reassembly slot 탐색
6. 없으면 새 IndexReasmSlot 할당
7. duplicate fragment이면 새 slot release
8. rs->slot_ids[frag_idx] = slot_id
9. rs->frag_received[frag_idx] = 1
10. rs->frags_recv++
11. frags_recv == frags_total이면 commit
12. commit 후 frame이 들고 있던 모든 pool slot release
```

timeout:

```text
first_recv_us 기준 100ms 초과
  -> drop event
  -> 해당 frame이 들고 있던 slot_id 전부 release
  -> IndexReasmSlot 초기화
```

duplicate:

```text
이미 받은 frag_idx가 다시 오면
  -> 새로 들어온 slot만 release
  -> 기존 slot 유지
```

metadata mismatch:

```text
같은 frame_id/type인데 frag_total이 다르면
  -> 기존 frame slot 전체 release
  -> 새로 들어온 slot도 release
  -> drop event
```

Image commit:

```text
ready = atomic_load(img_ready_idx)
dst_idx = ready < 0 ? 0 : (ready + 1) % IMG_SLOTS

for frag_idx in 0..frags_total-1:
  slot_id = rs->slot_ids[frag_idx]
  hdr = pool->slots[slot_id].buf
  payload = buf + sizeof(PktHeader)
  memcpy(shm->img_slots[dst_idx].data + hdr->payload_offset,
         payload,
         hdr->payload_len)

dst metadata 기록
atomic_store(img_ready_idx, dst_idx)
sem_post(img_sem)
bridge_api_note_frame_ready()
```

LiDAR commit:

```text
build_lidar_staging()
  -> pool slots를 payload_offset 기준으로 staging buffer에 조립

commit_lidar_indexed()
  -> count 읽기
  -> count clipping
  -> xyzi AoS를 SHM x/y/z/intensity SoA arrays로 변환
  -> atomic_store(lidar_ready_idx)
  -> sem_post(lidar_sem)
  -> bridge_api_note_frame_ready()
```

## 4.10 `bridge_api.h` / `bridge_api.c`

SHM, event, metrics, command 송신, ACK tracking을 묶는 공통 API다.

초기화:

```text
bridge_api_init()
  -> addr_table 연결
  -> shm_arr 연결
  -> pending_mu 초기화
  -> next_command_id 초기화

bridge_api_init_shm()
  -> SHM magic/version/header size 기록
  -> 초기 state/metrics/event 초기화
```

RX 기록:

```text
bridge_api_note_rx()
  -> jetson_connected = 1
  -> pkt_count++
  -> metrics.rx_packets++
  -> state.connected/last_rx_us 갱신
  -> 처음 연결이면 connected event 발행
```

Odom 갱신:

```text
bridge_api_update_odom()
  -> odom_lock write lock
  -> odom_x/y/theta/vx/vy/omega 갱신
  -> state_lock write lock
  -> RobotState 위치/속도/odom_seq 갱신
```

Frame ready:

```text
bridge_api_note_frame_ready()
  -> image_fps 또는 lidar_fps 계산
  -> RobotState 갱신
```

Drop:

```text
bridge_api_note_drop()
  -> metrics.dropped_packets++
  -> image/lidar drop count 증가
  -> packet drop event 발행
```

Command 송신:

```text
bridge_api_send_command()
  -> addr_table에서 robot 주소 조회
  -> send_legacy_cmd()로 UDP 9001 송신
  -> tx metrics 증가
  -> ACK 필요 시 pending[] 등록
  -> event/log 출력
```

ACK 처리:

```text
bridge_api_handle_ack()
  -> ack metrics 증가
  -> pending[]에서 seq 또는 command_id 일치 항목 제거
  -> link_rtt_ms 갱신
  -> ACK event 발행
```

Timeout/retry:

```text
bridge_api_poll_timeouts()
  -> pending command deadline 확인
  -> retries_left 있으면 재전송
  -> 없으면 timeout event 발행
```

PC status snapshot:

```text
bridge_api_snapshot_status()
  -> RobotState + BridgeMeta를 PcStatusPacketV2로 복사
```

## 4.11 `jetson_tx.c`

Qt가 SHM command queue에 넣은 command를 Jetson UDP 9001로 보내는 thread다.

loop:

```text
1. UDP socket 생성
2. bridge_api_poll_timeouts()
3. robot별 cmd_queue를 round-robin 순회
4. shm_cmd_queue_pop_highest()
5. send_cmd_to_jetson()
6. 처리한 command가 없으면 5ms sleep
```

priority:

```text
ShmCmdEntry.priority가 있으면 사용
없으면 ESTOP은 CRITICAL, 나머지는 NORMAL
```

실제 송신은 `cmd_dispatch.c`를 거쳐 `bridge_api_send_command()`가 수행한다.

## 4.12 `shm_cmd_queue.h` / `shm_cmd_queue.c`

Qt producer와 BridgeDaemon consumer가 공유하는 command queue다.

초기화:

```text
pthread_mutex_init(process-shared attr)
head/tail/count/write_seq/drop_count = 0
entries clear
```

pop:

```text
shm_cmd_queue_pop_highest()
  -> count > 0이면 queue 안에서 priority가 가장 높은 entry 탐색
  -> best entry를 out으로 복사
  -> best 위치 앞의 entry들을 한 칸 밀어서 제거
  -> head 이동
  -> count--
```

이 queue는 Qt 쪽 producer 코드와 ABI가 맞아야 한다.

## 4.13 `cmd_dispatch.h` / `cmd_dispatch.c`

현재는 얇은 wrapper다.

```text
send_cmd_to_jetson()
  -> bridge_api_send_command()
```

역할은 command 송신 호출부가 `BridgeApi` 내부 구현에 직접 강하게 묶이지 않도록 중간 layer를 제공하는 것이다.

## 4.14 `pc_link.c`

PC와 UDP 9002로 command/status를 주고받는다.

socket:

```text
socket(AF_INET, SOCK_DGRAM)
SO_REUSEADDR
SO_RCVTIMEO = 100ms
bind(INADDR_ANY:9002)
```

PC command 수신:

```text
recvfrom(fd, CmdPacket)
PC 주소 최초 학습
send_cmd_to_jetson(cmd, priority NORMAL, REQUIRES_ACK)
bridge_api_poll_timeouts()
```

PC status 송신:

```text
PC 주소가 학습된 뒤 1초마다
  for robot:
    bridge_api_snapshot_status()
    sendto(PcStatusPacketV2)
```

## 4.15 `protocol_timer.c`

1Hz heartbeat와 command timeout/retry polling을 담당한다.

loop:

```text
clock_gettime(CLOCK_MONOTONIC)
1초 주기를 계산
100ms 단위 nanosleep으로 stop 확인
addr_table snapshot
주소가 학습된 robot마다 heartbeat command 송신
bridge_api_poll_timeouts()
```

heartbeat도 일반 command와 같은 `bridge_api_send_command()` 경로를 탄다.

## 4.16 `utils.h`

공통 시간 유틸리티를 제공한다. 주요 사용처는 timestamp, timeout, FPS, watchdog, status packet 작성이다.

대표적으로 `now_us()`가 현재 시간을 microsecond 단위로 반환하는 역할을 한다.

## 4.17 `frag_queue.h`

이 파일은 기존 앱에서 쓰던 byte-copy 기반 queue다.

indexed 버전에서는 빌드 dependency와 source include가 `frag_index_queue.h`로 바뀌었기 때문에, 현재 indexed runtime 경로에서는 사용하지 않는다. 기존 비교/백업용으로 복사본이 남아 있는 상태다.

혼동을 줄이려면 나중에 안정화 후 삭제하거나 `legacy_frag_queue.h`로 이름을 바꾸는 것이 좋다.

## 5. Pool Slot Ownership 상세

indexed 구조에서 가장 중요한 안정성 조건은 pool slot 소유권이다.

### 5.1 RX thread 소유

```text
rx_packet_pool_acquire()
  -> slot owner = jetson_rx_thread
```

RX thread가 직접 release하는 경우:

```text
recvfrom 실패
short packet
header invalid
robot_id invalid
ODOM 처리 완료
CMD_ACK 처리 완료
unknown type
```

### 5.2 Reassembly thread 소유

Image/LiDAR는 queue push 후 소유권이 넘어간다.

```text
frag_index_queue_push(fq, slot_id)
slot_id = RX_SLOT_INVALID
owner = reassembly_shm_thread
```

Reassembly thread가 release하는 경우:

```text
invalid fragment
duplicate fragment
metadata mismatch
reassembly slot exhausted
frame complete commit 후
timeout
shutdown cleanup
```

### 5.3 Queue full

queue가 꽉 차면 oldest slot_id가 drop된다.

```text
frag_index_queue_push()
  -> dropped_slot_id 반환
jetson_rx_thread
  -> rx_packet_pool_release(dropped_slot_id)
```

## 6. 복사 경로 비교

### 기존 byte-copy 경로

```text
kernel -> stack pkt[]
stack pkt[] -> FragQueue.entries[]
FragQueue.entries[] -> reassembly pkt[]
reassembly pkt[] -> ReasmSlot.buf
ReasmSlot.buf -> SHM slot
```

### indexed Image 경로

```text
kernel -> RxPacketPool slot
RxPacketPool slot payloads -> SHM ImgSlot
```

### indexed LiDAR 경로

```text
kernel -> RxPacketPool slot
RxPacketPool slot payloads -> staging buffer
staging buffer -> SHM LidarSlot arrays
```

LiDAR에서 staging을 제거하려면 fragment 경계를 넘어서 `uint32_t count`와 `float[4]` tuple을 읽는 stream reader를 구현하면 된다.

## 7. 동기화 요약

```text
RxPacketPool
  pthread_mutex_t mu
  free_stack/free_count 보호

FragIndexQueue
  pthread_mutex_t mu
  pthread_cond_t cv
  empty wait / push signal / stop broadcast

JetsonAddrTable
  pthread_mutex_t mu
  주소 학습/조회 보호

SharedData image/lidar
  atomic ready_idx
  sem_t frame notification

SharedData odom/meta/state
  pthread_rwlock_t

BridgeApi pending command table
  pthread_mutex_t pending_mu

stop flag
  atomic_bool
```

## 8. 실행 방법

```bash
cd /home/pi/robot_project/bride_app_codex_indexed
make
./bridge_daemon 1
```

로봇 10대:

```bash
./bridge_daemon 10
```

ASAN:

```bash
make asan
./bridge_daemon_asan 1
```

종료:

```text
Ctrl+C
```

## 9. 현재 구현상 주의점

1. `frag_queue.h`는 indexed runtime에서 사용하지 않는 legacy 파일이다.
2. LiDAR는 아직 staging buffer를 사용한다. Image만 완전한 index-based final commit 구조에 가깝다.
3. `RxPacketPool` 크기는 4096 slot이다. 로봇 수, image/lidar burst, timeout 상황에 따라 부족할 수 있다.
4. pool slot release 누락은 장시간 실행 시 pool 고갈로 이어진다. drop/timeout/error 경로는 계속 테스트해야 한다.
5. `bridge_api_note_drop(ctx->api, 0, 0, "rx packet pool exhausted")`는 pool exhausted 시 robot_id를 특정할 수 없어 robot 0에 기록한다. 운영 통계 정확도를 높이려면 pool-level global metric이 따로 필요하다.
6. 실제 UDP packet 수신 검증은 라즈베리파이 실제 네트워크 환경에서 Jetson 송신과 함께 확인해야 한다.

## 10. 권장 다음 작업

1. 실제 Jetson Image/LiDAR burst로 pool free count 최저값 측정
2. reassembly timeout/drop 상황에서 pool slot 회수 검증
3. LiDAR stream reader 구현으로 staging buffer 제거 검토
4. `rx_packet_pool_free_count()`를 watchdog log 또는 metrics에 노출
5. legacy `frag_queue.h` 삭제 또는 이름 변경
6. queue full / pool exhausted / duplicate fragment 테스트 패킷 작성

