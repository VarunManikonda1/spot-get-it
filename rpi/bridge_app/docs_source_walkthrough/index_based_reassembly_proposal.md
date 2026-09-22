# Index-Based Reassembly Proposal

## 1. 목표

현재 Image/LiDAR fragment 경로는 queue와 reassembly 단계에서 큰 패킷 버퍼를 여러 번 복사한다.

현재 흐름:

```text
kernel UDP buffer
  -> jetson_rx_thread stack pkt[]
  -> FragQueue entry copy
  -> reassembly_shm_thread pkt[] copy
  -> ReasmSlot rs->buf copy
  -> SharedData SHM slot copy/write
```

목표 흐름:

```text
kernel UDP buffer
  -> RxPacketPool slot
  -> queue/reassembly는 slot_id만 전달/저장
  -> frame 완성 시 slot_id 순서대로 SharedData SHM slot에 최종 copy/write
```

일반 UDP `recvfrom()`을 쓰는 한 kernel-to-user copy는 피하기 어렵다. 이 제안의 목표는 kernel copy를 없애는 것이 아니라 daemon 내부의 중복 copy를 제거하는 것이다.

## 2. 핵심 아이디어

`recvfrom()`의 수신 버퍼를 stack 임시 버퍼가 아니라 고정 크기 packet pool slot으로 바꾼다.

이후 Image/LiDAR fragment는 실제 데이터 복사 없이 `slot_id`만 큐로 넘긴다.

reassembly slot은 fragment payload를 별도 `rs->buf`에 조립하지 않는다. 대신 `frag_idx -> slot_id` 매핑만 저장한다.

frame이 완성되면 그때 `slot_id`를 `frag_idx` 순서대로 순회하면서 최종 SHM slot에 쓴다.

```text
RX thread
  recvfrom(pool[slot_id].buf)
  queue.push(slot_id)

Reassembly thread
  slot_id = queue.pop()
  hdr = pool[slot_id].buf
  rs->slot_ids[hdr->frag_idx] = slot_id

Commit
  for frag_idx in 0..frag_total-1:
      slot_id = rs->slot_ids[frag_idx]
      payload = pool[slot_id].buf + sizeof(PktHeader)
      write payload to SHM
  release all slot_ids
```

## 3. 예상 복사 횟수

### 현재 Image

```text
1. kernel -> stack pkt[]
2. stack pkt[] -> FragQueue.entries[]
3. FragQueue.entries[] -> reassembly pkt[]
4. reassembly pkt[] payload -> ReasmSlot.buf
5. ReasmSlot.buf -> SHM ImgSlot.data
```

### 제안 Image

```text
1. kernel -> RxPacketPool.slot[slot_id].buf
2. RxPacketPool slots -> SHM ImgSlot.data
```

Image는 daemon 내부 copy를 사실상 최종 publish copy만 남기는 구조가 된다.

### 현재 LiDAR

```text
1. kernel -> stack pkt[]
2. stack pkt[] -> FragQueue.entries[]
3. FragQueue.entries[] -> reassembly pkt[]
4. reassembly pkt[] payload -> ReasmSlot.buf
5. ReasmSlot.buf AoS payload -> SHM LidarSlot SoA arrays
```

### 제안 LiDAR

```text
1. kernel -> RxPacketPool.slot[slot_id].buf
2. RxPacketPool slots AoS payload -> SHM LidarSlot SoA arrays
```

LiDAR는 현재 SHM 구조가 `x[]`, `y[]`, `z[]`, `intensity[]`로 나뉜 SoA layout이므로 마지막 단계는 단순 `memcpy`가 아니라 변환 write다. 그래도 queue copy와 reassembly buffer copy는 제거된다.

## 4. 필요한 자료구조

### 4.1 RxPacketPool

UDP datagram 하나를 저장하는 고정 크기 slot 배열이다.

```c
#define RX_PACKET_POOL_SIZE 2048
#define RX_SLOT_INVALID     (-1)

typedef struct {
    uint8_t buf[PROTO_PKT_MAX];
    int len;
    uint8_t in_use;
} RxPacketSlot;

typedef struct {
    RxPacketSlot slots[RX_PACKET_POOL_SIZE];
    int free_stack[RX_PACKET_POOL_SIZE];
    int free_count;
    pthread_mutex_t mu;
} RxPacketPool;
```

역할:

- RX thread가 `slot_id`를 acquire한다.
- `recvfrom()`은 `pool->slots[slot_id].buf`에 직접 쓴다.
- Reassembly thread가 slot을 다 쓰면 release한다.
- timeout/drop 시 frame이 들고 있던 slot들을 모두 release한다.

### 4.2 FragIndexQueue

기존 `FragQueue`는 packet byte array를 저장한다. 새 queue는 `slot_id`만 저장한다.

```c
typedef struct {
    int slot_ids[FRAG_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int stop;
} FragIndexQueue;
```

기존 `pthread_cond_t` 사용 방식은 유지한다.

- queue empty: consumer가 `pthread_cond_wait()`
- push: producer가 `pthread_cond_signal()`
- stop: `pthread_cond_broadcast()`

### 4.3 IndexReasmSlot

기존 `ReasmSlot`은 큰 `buf[REASM_BUF_MAX]`를 갖고 있다. 새 구조에서는 큰 buffer 대신 slot id 배열을 갖는다.

```c
#define REASM_MAX_FRAGS 512

typedef struct {
    int      in_use;
    uint8_t  type;
    uint32_t frame_id;
    uint16_t frags_recv;
    uint16_t frags_total;
    uint64_t timestamp_us;
    uint64_t first_recv_us;
    uint32_t total_size;
    int      slot_ids[REASM_MAX_FRAGS];      /* frag_idx -> pool slot id */
    uint8_t  frag_received[REASM_MAX_FRAGS];
} IndexReasmSlot;
```

초기화 시 `slot_ids[]`는 `RX_SLOT_INVALID`로 채운다.

## 5. 데이터 수명과 소유권

가장 중요한 규칙은 pool slot ownership이다.

### 5.1 Image/LiDAR 정상 경로

```text
FREE
  -> RX thread acquire
  -> recvfrom writes slot.buf
  -> RX thread validates header
  -> RX thread pushes slot_id to FragIndexQueue
  -> ownership moves to Reassembly thread/frame
  -> Reassembly stores slot_id in IndexReasmSlot
  -> frame complete
  -> commit to SHM
  -> release all slot_ids in frame
  -> FREE
```

### 5.2 Odom/ACK 경로

Odom/ACK는 fragment queue에 넣을 필요가 없다.

```text
FREE
  -> RX thread acquire
  -> recvfrom writes slot.buf
  -> RX thread handles Odom/ACK immediately
  -> RX thread release
  -> FREE
```

### 5.3 잘못된 header

header 검증 실패, robot_id 범위 오류, 알 수 없는 packet이면 즉시 release한다.

```text
RX thread acquire
  -> recvfrom
  -> validate fail
  -> release
```

### 5.4 중복 fragment

같은 `frame_id + type + frag_idx`가 이미 들어와 있으면 새로 온 slot은 버린다.

```text
if (rs->frag_received[frag_idx]) {
    pool_release(slot_id);
    continue;
}
```

기존 slot은 유지한다.

### 5.5 timeout/drop

미완성 frame이 timeout되면 그 frame이 들고 있던 모든 slot을 release해야 한다.

```c
for (int i = 0; i < rs->frags_total; i++) {
    if (rs->slot_ids[i] != RX_SLOT_INVALID)
        rx_pool_release(pool, rs->slot_ids[i]);
}
rs->in_use = 0;
```

이 release 누락이 있으면 pool이 고갈된다.

## 6. 처리 흐름

### 6.1 RX thread

현재 `jetson_rx_thread()`는 stack buffer `pkt[PROTO_PKT_MAX]`에 `recvfrom()` 한다.

변경 후:

```c
int slot_id = rx_pool_acquire(pool);
if (slot_id < 0) {
    /* pool empty 정책 수행 */
    continue;
}

RxPacketSlot *slot = &pool->slots[slot_id];
ssize_t n = recvfrom(fd, slot->buf, sizeof(slot->buf), 0,
                     (struct sockaddr *)&src_addr, &addrlen);
if (n <= 0) {
    rx_pool_release(pool, slot_id);
    continue;
}
slot->len = (int)n;

PktHeader *hdr = (PktHeader *)slot->buf;
uint8_t *payload = slot->buf + sizeof(PktHeader);

if (!proto_validate_header(...)) {
    rx_pool_release(pool, slot_id);
    continue;
}

switch (hdr->type) {
case PKT_TYPE_IMAGE:
case PKT_TYPE_LIDAR:
    frag_index_queue_push(fq, slot_id);
    slot_id = RX_SLOT_INVALID; /* ownership transferred */
    break;
case PKT_TYPE_ODOM:
    handle_odom(...);
    rx_pool_release(pool, slot_id);
    break;
case PKT_TYPE_CMD_ACK:
    handle_ack(...);
    rx_pool_release(pool, slot_id);
    break;
default:
    rx_pool_release(pool, slot_id);
    break;
}
```

### 6.2 Reassembly thread

현재 `frag_queue_pop()`은 packet bytes를 복사해서 넘긴다. 변경 후에는 slot id만 받는다.

```c
int slot_id = frag_index_queue_pop(fq);
if (slot_id < 0) break;

RxPacketSlot *slot = &pool->slots[slot_id];
PktHeader *hdr = (PktHeader *)slot->buf;

expire_stale(...);

IndexReasmSlot *rs = find_or_alloc_slot(...);
if (!rs) {
    rx_pool_release(pool, slot_id);
    continue;
}

if (duplicate_or_invalid) {
    rx_pool_release(pool, slot_id);
    continue;
}

rs->slot_ids[hdr->frag_idx] = slot_id;
rs->frag_received[hdr->frag_idx] = 1;
rs->frags_recv++;
```

완성되면 commit한다.

```c
if (rs->frags_recv == rs->frags_total) {
    if (rs->type == PKT_TYPE_IMAGE)
        commit_image_indexed(ctx, rs);
    else if (rs->type == PKT_TYPE_LIDAR)
        commit_lidar_indexed(ctx, rs);

    release_reasm_slots(pool, rs);
    rs->in_use = 0;
}
```

## 7. Commit 설계

### 7.1 Image commit

Image는 fragment payload를 `payload_offset` 위치에 그대로 복사하면 된다.

```c
static void commit_image_indexed(ReasmCtx *ctx,
                                 IndexReasmSlot *rs,
                                 RxPacketPool *pool) {
    SharedData *shm = ctx->shm;
    int ready = atomic_load(&shm->img_ready_idx);
    int dst_idx = (ready < 0) ? 0 : (ready + 1) % IMG_SLOTS;
    ImgSlot *dst = &shm->img_slots[dst_idx];

    uint32_t total_size = 0;

    for (int i = 0; i < rs->frags_total; i++) {
        int slot_id = rs->slot_ids[i];
        RxPacketSlot *src = &pool->slots[slot_id];
        PktHeader *hdr = (PktHeader *)src->buf;
        uint8_t *payload = src->buf + sizeof(PktHeader);

        if (hdr->payload_offset >= IMG_SLOT_SIZE ||
            hdr->payload_len > IMG_SLOT_SIZE - hdr->payload_offset) {
            return;
        }

        memcpy(dst->data + hdr->payload_offset, payload, hdr->payload_len);

        uint32_t end = hdr->payload_offset + hdr->payload_len;
        if (end > total_size) total_size = end;
    }

    dst->size = total_size;
    dst->frame_id = rs->frame_id;
    dst->timestamp_us = rs->timestamp_us;

    atomic_store(&shm->img_ready_idx, dst_idx);
    sem_post(&shm->img_sem);
}
```

주의:

- commit 도중 검증 실패가 나면 `ready_idx`를 publish하면 안 된다.
- 실패해도 frame이 들고 있던 pool slot들은 release해야 한다.

### 7.2 LiDAR commit

현재 LiDAR SHM은 SoA 구조다.

```c
float x[LIDAR_MAX_PTS];
float y[LIDAR_MAX_PTS];
float z[LIDAR_MAX_PTS];
float intensity[LIDAR_MAX_PTS];
```

wire payload는 AoS 구조다.

```text
[uint32_t count][float x][float y][float z][float intensity]...
```

fragment 경계가 float tuple 중간에서 끊길 수 있다. 따라서 LiDAR는 Image처럼 단순히 fragment payload를 offset 위치로 복사한 뒤 끝낼 수 없다.

선택지는 두 가지다.

#### 선택지 1: commit 시 작은 staging buffer 사용

LiDAR frame만 commit 시 임시 contiguous buffer를 만든다.

```text
pool slots -> lidar staging buffer -> SHM SoA arrays
```

이 경우 LiDAR에는 staging copy가 하나 남지만 구현이 단순하고 안전하다. Image copy는 줄어든다.

#### 선택지 2: slot stream reader 구현

`slot_id` 배열을 하나의 연속 byte stream처럼 읽는 iterator를 만든다.

```c
typedef struct {
    IndexReasmSlot *rs;
    RxPacketPool *pool;
    int frag_idx;
    uint32_t pos_in_payload;
} FragmentStreamReader;
```

이 reader가 `read_exact()` 형태로 `uint32_t`, `float[4]`를 fragment 경계를 넘어서 읽어준다.

```c
uint32_t count;
stream_read(&r, &count, sizeof(count));

for (uint32_t i = 0; i < count; i++) {
    float xyzi[4];
    stream_read(&r, xyzi, sizeof(xyzi));
    dst->x[i] = xyzi[0];
    dst->y[i] = xyzi[1];
    dst->z[i] = xyzi[2];
    dst->intensity[i] = xyzi[3];
}
```

이 방식은 staging buffer 없이 pool slot에서 바로 SHM SoA로 변환 write한다. 구현은 더 복잡하지만 copy가 가장 적다.

권장:

- 1차 구현은 Image indexed commit + LiDAR staging commit
- 2차 최적화로 LiDAR stream reader 적용

## 8. Pool 크기 산정

Index-based reassembly는 frame이 완성될 때까지 pool slot을 오래 붙잡는다. 따라서 pool 크기는 기존 queue보다 넉넉해야 한다.

대략 계산:

```text
image 200KB / 1400B ~= 143 fragments
lidar 128KB / 1400B ~= 92 fragments
robot 1대가 image 1 frame + lidar 1 frame 동시 전송 ~= 235 slots
```

로봇 10대가 동시에 1 frame씩 전송하면:

```text
235 * 10 = 2350 slots
```

여기에 timeout 중인 이전 frame, burst, out-of-order 여유를 고려하면 `2048`은 빠듯할 수 있다.

권장 시작값:

```c
#define RX_PACKET_POOL_SIZE 4096
```

메모리 사용량:

```text
PROTO_PKT_MAX ~= 1424 bytes
4096 slots ~= 5.8 MB + metadata
8192 slots ~= 11.7 MB + metadata
```

RPi5 기준으로 4096~8192 slot은 현실적인 범위다.

## 9. Drop 정책

### 9.1 Queue full

기존 `FragQueue`는 full이면 가장 오래된 항목을 버린다.

새 queue도 같은 정책을 유지할 수 있다.

```text
queue full
  -> old_slot_id = queue.pop_oldest_internal()
  -> rx_pool_release(old_slot_id)
  -> push(new_slot_id)
```

단, old slot이 아직 어떤 `IndexReasmSlot`에 들어가기 전이므로 release가 안전하다.

### 9.2 Pool empty

pool이 비면 `recvfrom()`을 받을 user buffer가 없다.

선택지:

1. 작은 drain buffer로 `recvfrom()`해서 버린다.
2. 잠깐 sleep/yield 후 재시도한다.
3. 기존 stack buffer로 수신 후 즉시 drop 처리한다.

권장:

```text
pool empty
  -> stack drain buffer로 recvfrom()
  -> drop counter 증가
  -> continue
```

이렇게 해야 kernel socket buffer에 쌓인 오래된 packet을 계속 비울 수 있다.

### 9.3 Reassembly slot exhausted

`REASM_SLOTS`가 꽉 차면 새 frame의 첫 fragment도 받을 수 없다.

권장:

- 새 frame drop
- 해당 slot_id release
- drop counter 증가

강제로 오래된 reassembly frame을 버리는 정책도 가능하지만, 1차 구현에서는 timeout 기반 회수가 더 단순하다.

## 10. 동기화 설계

### 10.1 RX thread와 Reassembly thread

`FragIndexQueue`는 기존과 동일한 `mutex + condvar` 구조를 쓴다.

```text
RX thread:
  queue mutex lock
  slot_id push
  cond_signal
  unlock

Reassembly thread:
  queue mutex lock
  while count == 0 && !stop:
      cond_wait
  slot_id pop
  unlock
```

### 10.2 Pool free list

pool acquire/release는 별도 mutex로 보호한다.

```text
rx_pool_acquire()
  pool mutex lock
  pop free_stack
  unlock

rx_pool_release()
  pool mutex lock
  push free_stack
  unlock
```

1차 구현은 mutex 방식이 충분하다. 필요하면 나중에 lock-free stack으로 바꿀 수 있다.

### 10.3 ReassemblySlot

현재 구조처럼 robot별 reassembly thread가 자기 robot의 frame만 조립한다면 `IndexReasmSlot`에는 별도 mutex가 필요 없다. 해당 robot의 reassembly thread가 단독 소유하기 때문이다.

RX thread는 `slot_id`만 queue에 넣고 reassembly state를 직접 만지지 않는다.

## 11. 구현 변경 범위

### 새 파일 또는 기존 파일 교체

권장 파일 구성:

- `rx_packet_pool.h`
- `rx_packet_pool.c`
- `frag_index_queue.h`

또는 기존 `frag_queue.h`를 index queue로 바꿀 수도 있다. 하지만 기존 코드 이해를 위해 새 이름을 권장한다.

### 변경 파일

- `bridge_ctx.h`
  - `JetsonRxCtx`에 `RxPacketPool *rx_pool`
  - `FragQueue *fq_arr[]`를 `FragIndexQueue *fq_arr[]`로 변경
  - `ReasmCtx`에 `RxPacketPool *rx_pool`

- `bridge_main.c`
  - packet pool 초기화/정리 추가
  - robot별 index queue 초기화/정리
  - ctx 연결

- `jetson_rx.c`
  - stack `pkt[PROTO_PKT_MAX]` 제거
  - pool slot acquire 후 `recvfrom(pool slot)`
  - Image/LiDAR는 slot id queue push
  - Odom/ACK/error는 slot release

- `reassembly_shm.c`
  - `frag_queue_pop()` 대신 `frag_index_queue_pop()`
  - `ReasmSlot.buf` 제거
  - `slot_ids[]` 기반 조립
  - `commit_image_indexed()`
  - `commit_lidar_indexed()`
  - timeout/drop 시 slot release

## 12. 구현 순서

1. `RxPacketPool` 구현
2. `FragIndexQueue` 구현
3. `bridge_main.c`에서 pool/queue 초기화 연결
4. `jetson_rx.c`를 pool receive + slot_id push로 변경
5. `reassembly_shm.c`를 slot_id 기반으로 변경
6. Image indexed commit 구현
7. LiDAR는 우선 staging buffer 방식으로 구현
8. drop/timeout/release 누락 검사
9. ASAN 빌드와 burst 테스트
10. 필요 시 LiDAR stream reader 최적화

## 13. 검증 포인트

### 기능 검증

- Image frame이 정상적으로 Qt에 표시되는지
- LiDAR frame count와 좌표가 기존과 같은지
- Odom/ACK 경로가 깨지지 않는지
- robot_id별 queue 분리가 유지되는지

### 안정성 검증

- fragment loss 시 timeout 후 pool slot이 회수되는지
- duplicate fragment 수신 시 새 slot이 release되는지
- invalid header 수신 시 release되는지
- queue full 시 old slot release가 되는지
- 종료 시 queue에 남은 slot과 reassembly slot이 release되는지

### 성능 검증

측정할 값:

- daemon CPU 사용률
- RX thread CPU 사용률
- pool free_count 최저값
- queue count 최고값
- frame latency
- drop count
- socket receive buffer overflow 여부

## 14. 위험 요소

가장 큰 위험은 pool slot release 누락이다. release가 한 경로라도 빠지면 장시간 실행 시 pool이 고갈된다.

특히 다음 경로를 체크해야 한다.

- header validation 실패
- robot_id 범위 오류
- queue full로 old slot drop
- duplicate fragment
- invalid fragment index
- fragment metadata mismatch
- reassembly slot exhausted
- commit 실패
- timeout
- shutdown

두 번째 위험은 pool size 부족이다. index-based reassembly는 frame 완성 전까지 모든 fragment slot을 보관하기 때문에 순간 burst에 취약하다. 시작값은 `4096` 이상을 권장한다.

세 번째 위험은 LiDAR fragment 경계 처리다. LiDAR는 tuple이 fragment 경계에서 잘릴 수 있으므로 staging buffer 또는 stream reader가 필요하다.

## 15. 결론

`Packet Pool + Index Queue + Index-Based Reassembly`가 현재 구조에서 copy를 가장 크게 줄이면서도 SHM/Qt ABI를 유지할 수 있는 최적안이다.

Image 경로는 다음 수준까지 줄어든다.

```text
kernel -> pool slot
pool slot fragments -> SHM image slot
```

LiDAR 경로는 SHM layout 때문에 마지막 변환 write가 필요하지만, queue copy와 reassembly buffer copy는 제거된다.

권장 구현은 다음과 같다.

```text
1차:
  Packet Pool
  FragIndexQueue
  Image indexed commit
  LiDAR staging commit

2차:
  LiDAR stream reader로 staging buffer 제거
  필요 시 recvmmsg batch receive 추가
```

이 방식은 사용자가 제안한 "메모리풀에 저장하고 포인터/인덱스로만 접근하다가 마지막 공유메모리 commit에서만 복사"에 가장 가깝다.

