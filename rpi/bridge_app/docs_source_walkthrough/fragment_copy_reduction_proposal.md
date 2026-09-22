# Image/LiDAR Fragment Copy Reduction Proposal

## 1. 현재 구조와 복사 지점

현재 `bride_app_codex`의 Image/LiDAR 수신 경로는 다음과 같다.

```text
kernel UDP receive buffer
  -> jetson_rx_thread recvfrom() stack buffer pkt[]
  -> frag_queue_push(): FragQueue.entries[]로 copy
  -> frag_queue_pop(): reassembly_shm_thread의 pkt buffer로 copy
  -> reassembly slot rs->buf로 copy
  -> commit_image()/commit_lidar(): SharedData img/lidar slot으로 copy 또는 포맷 변환 copy
  -> Qt reader가 SHM slot에서 자기 버퍼/렌더러로 copy할 가능성 있음
```

코드 기준 주요 지점:

- `jetson_rx.c`: `recvfrom(fd, pkt, sizeof(pkt), ...)`
- `frag_queue.h`: `frag_queue_push()` 내부 `memcpy(e->buf, buf, len)`
- `frag_queue.h`: `frag_queue_pop()` 내부 `memcpy(buf, e->buf, len)`
- `reassembly_shm.c`: fragment payload를 `rs->buf + offset`으로 `memcpy`
- `reassembly_shm.c`: frame 완성 후 `commit_image()` / `commit_lidar()`에서 SHM slot으로 write

즉 daemon 내부에서만 Image 기준으로 최소 4회 copy가 발생한다. LiDAR는 마지막 commit에서 wire format을 SoA 구조(`x[]`, `y[]`, `z[]`, `intensity[]`)로 풀어 쓰기 때문에 단순 copy가 아니라 포맷 변환 write가 추가된다.

목표는 daemon 내부 copy를 줄여서 다음 구조에 가깝게 만드는 것이다.

```text
kernel UDP receive buffer
  -> daemon-owned packet/frame memory
  -> queue는 pointer/index만 전달
  -> reassembly도 같은 memory 참조
  -> 마지막 SHM publish 시에만 copy 또는 변환
```

완전한 zero-copy는 일반 UDP socket API만으로는 어렵다. `recvfrom()` 자체가 kernel buffer에서 user buffer로 한 번 복사하기 때문이다. `mmap` 기반 packet ring, AF_PACKET, io_uring provided buffer 같은 선택지가 있지만 이 프로젝트에서는 구현 복잡도와 안정성 비용이 크다.

## 2. 방식 A: Packet Pool + Pointer Queue

### 개념

`recvfrom()`의 목적지를 stack buffer가 아니라 고정 크기 packet pool slot으로 바꾼다. `FragQueue`에는 packet payload 전체를 복사하지 않고 pool slot index만 넣는다.

```text
kernel
  -> recvfrom(pool[slot].buf)
  -> queue.push(slot_id)
  -> reassembly thread queue.pop()으로 slot_id 획득
  -> slot의 header/payload를 읽어서 reassembly buffer에 copy
  -> pool slot release
  -> frame 완성 시 SHM commit
```

### 바뀌는 복사 횟수

현재:

```text
recv stack copy 1
queue push copy 1
queue pop copy 1
reassembly buffer copy 1
SHM commit copy 1
```

방식 A:

```text
recv pool copy 1
queue push/pop copy 0
reassembly buffer copy 1
SHM commit copy 1
```

daemon 내부에서 `frag_queue_push/pop`의 두 번 copy를 제거한다.

### 구조 변경

새 구조체 예시:

```c
#define RX_POOL_SIZE 1024

typedef struct {
    uint8_t buf[PROTO_PKT_MAX];
    int len;
    uint16_t next;
    uint8_t in_use;
} RxPacketSlot;

typedef struct {
    RxPacketSlot slots[RX_POOL_SIZE];
    int free_stack[RX_POOL_SIZE];
    int free_count;
    pthread_mutex_t mu;
} RxPacketPool;

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

`jetson_rx_thread()`는 free slot을 얻어서 그 slot의 `buf`로 직접 `recvfrom()` 한다. Image/LiDAR인 경우 `slot_id`를 queue에 넣고 ownership을 reassembly thread로 넘긴다. Odom/ACK 같은 작은 패킷은 즉시 처리하고 slot을 바로 반환한다.

### 장점

- 기존 재조립 구조(`ReasmSlot`)와 SHM layout을 거의 유지한다.
- 위험도가 낮다.
- 현재 병목 중 명확한 두 copy, 즉 queue push/pop copy를 제거한다.
- queue entry 크기가 `PROTO_PKT_MAX`에서 `int`로 줄어 cache 효율이 좋아진다.

### 단점

- reassembly buffer copy와 SHM commit copy는 남는다.
- pool slot lifetime 관리가 필요하다.
- pool 고갈 시 drop 정책이 필요하다.

### 주의점

pool slot은 reassembly thread가 해당 fragment를 `rs->buf`로 복사한 직후 반환하면 된다. 따라서 lifetime이 짧고 관리가 단순하다.

이 방식은 1차 개선안으로 가장 적합하다.

## 3. 방식 B: Frame Reassembly Pool + Fragment Direct Write

### 개념

packet queue 자체를 없애거나 최소화하고, RX 스레드가 수신 직후 frame reassembly pool에 바로 payload를 써 넣는다. reassembly thread는 fragment packet을 다시 읽지 않고, "frame이 완성됨" 이벤트만 받아서 SHM에 commit한다.

```text
kernel
  -> recvfrom(rx buffer or packet pool)
  -> RX thread가 header 검증
  -> frame_pool[type, frame_id].buf + offset 에 payload copy
  -> frags_recv == frags_total이면 completed_frame_queue에 frame_id push
  -> commit thread가 completed frame을 SHM으로 copy/변환
```

### 바뀌는 복사 횟수

현재:

```text
recv stack copy 1
queue push copy 1
queue pop copy 1
reassembly buffer copy 1
SHM commit copy 1
```

방식 B:

```text
recv buffer copy 1
reassembly frame buffer copy 1
SHM commit copy 1
```

방식 A와 copy 횟수는 비슷하지만, queue가 fragment packet 단위가 아니라 completed frame 단위가 된다. 따라서 queue traffic과 thread handoff가 크게 줄어든다.

### 구조 변경

현재 `ReasmSlot`은 `reassembly_shm_thread()`의 local heap에 있다. 이것을 robot별 공유 context로 올리고, RX thread가 접근할 수 있게 바꾼다.

필요한 변경:

- robot별 `ReasmPool` 추가
- `ReasmPool` mutex 추가
- RX thread가 Image/LiDAR fragment를 받으면 바로 `ReasmPool`에 write
- frame 완성 시 `CompletedFrameQueue`에 frame slot index push
- commit thread는 completed frame만 pop해서 `commit_image()` / `commit_lidar()` 수행

### 장점

- fragment packet queue가 사라지거나 매우 작아진다.
- reassembly timeout/drop 판단이 수신 시점에 가까워진다.
- reassembly thread가 모든 fragment를 하나씩 깨워서 처리하는 비용이 줄어든다.
- RX thread가 이미 header를 검증하므로 중복 parsing이 줄어든다.

### 단점

- RX thread의 일이 늘어난다. 현재 RX thread는 빠르게 queue에 넣고 빠지는 구조인데, 이 방식은 RX thread가 reassembly state까지 만진다.
- `ReasmPool` mutex contention 가능성이 있다.
- RX thread가 오래 잡히면 UDP kernel buffer overflow 가능성이 오히려 커질 수 있다.
- 구현 변경 범위가 방식 A보다 크다.

### 판단

수신 패킷 rate가 높고 fragment queue handoff 비용이 큰 경우에는 효과가 있다. 하지만 RX thread의 역할이 무거워지는 것이 단점이다. 실시간성 관점에서는 RX thread를 최대한 짧게 유지하는 방식 A가 더 보수적이다.

## 4. 방식 C: SHM Slot Direct Reassembly

### 개념

재조립 buffer를 별도로 두지 않고, 최종 공유메모리 slot에 직접 payload를 조립한다. frame이 완성되면 `ready_idx`만 atomic으로 publish한다.

```text
kernel
  -> recvfrom(...)
  -> target SHM img/lidar slot + offset 에 직접 write
  -> all fragments received
  -> atomic_store(ready_idx, slot)
  -> sem_post()
```

### 바뀌는 복사 횟수

Image 기준:

```text
recv copy 1
SHM slot direct write 1
commit copy 0
```

daemon 내부에서는 가장 적은 copy가 된다.

### 장점

- Image는 가장 큰 copy를 제거할 수 있다.
- 완성 시 commit은 metadata publish만 하면 된다.
- frame 크기가 큰 경우 성능 이점이 가장 크다.

### 단점

- 현재 SHM slot은 Qt가 읽는 최종 publish slot이다. 미완성 frame을 같은 slot에 쓰면 Qt가 잘못 읽지 않도록 slot state가 필요하다.
- triple buffer만으로는 "reader가 읽는 슬롯", "ready 슬롯", "writer가 조립 중인 슬롯"을 안전하게 분리하기 어렵다.
- fragment loss가 발생하면 SHM slot 하나가 timeout까지 점유된다.
- LiDAR는 현재 SHM layout이 SoA(`x[]`, `y[]`, `z[]`, `intensity[]`)이고 wire payload는 AoS interleaved format이다. offset 기반 direct write가 어렵다. LiDAR는 별도 변환 과정이 필요하다.
- Qt 쪽 reader protocol까지 같이 점검해야 한다.

### 필요한 SHM 변경

slot별 상태가 필요하다.

```c
typedef enum {
    SLOT_FREE = 0,
    SLOT_WRITING = 1,
    SLOT_READY = 2,
    SLOT_READING = 3
} SlotState;
```

또는 sequence counter 방식이 필요하다.

```c
typedef struct {
    atomic_uint seq_begin;
    uint32_t size;
    uint64_t timestamp_us;
    uint32_t frame_id;
    uint8_t data[IMG_SLOT_SIZE];
    atomic_uint seq_end;
} ImgSlotV2;
```

writer는 `seq_begin`을 홀수로 만들고 write한 뒤 `seq_end`와 `seq_begin`을 같은 짝수 값으로 publish한다. reader는 begin/end가 같고 짝수일 때만 일관된 frame으로 본다.

### 판단

Image만 보면 성능상 가장 좋다. 하지만 공유메모리 ABI와 Qt reader까지 바뀌므로 리스크가 크다. 현재 코드의 안정성을 유지하면서 바로 적용할 1차 개선안으로는 과하다.

## 5. 추가 선택지: `recvmmsg()` Batch Receive

copy 횟수를 줄이는 방식은 아니지만 syscall 횟수를 줄인다. UDP fragment가 burst로 들어오는 구조라면 `recvfrom()` 반복 대신 `recvmmsg()`로 여러 datagram을 한 번에 받을 수 있다.

```text
recvmmsg(fd, msgs, N, ...)
```

Packet Pool과 조합하면 좋다.

```text
pool slot N개 확보
recvmmsg()로 slot N개에 한 번에 수신
각 slot_id를 queue에 push
```

장점:

- syscall overhead 감소
- burst 처리량 개선

단점:

- 코드 복잡도 증가
- timeout/stop 처리 로직 변경 필요
- copy 횟수 자체는 줄지 않음

## 6. 권장 순서

### 1단계: Packet Pool + Index Queue

가장 먼저 적용할 것을 권장한다.

이유:

- 현재 구조와 책임 분리가 유지된다.
- queue push/pop copy 2회를 제거한다.
- SHM ABI와 Qt 코드를 건드리지 않아도 된다.
- RX thread는 여전히 "수신 후 빠르게 넘김" 역할을 유지한다.

예상 daemon 내부 copy:

```text
Image:
kernel -> pool slot
pool payload -> reassembly buffer
reassembly buffer -> SHM image slot

LiDAR:
kernel -> pool slot
pool payload -> reassembly buffer
reassembly buffer -> SHM lidar SoA slot 변환 write
```

### 2단계: `recvmmsg()` optional 적용

Packet Pool이 안정화된 뒤 burst 상황에서 CPU 사용률이 여전히 높으면 추가한다.

### 3단계: Image 한정 SHM Direct Reassembly 검토

Image frame이 가장 큰 payload이고 Qt reader ABI 변경이 가능하다면 검토한다. LiDAR는 SHM layout 때문에 direct write 이득이 작거나 구현이 복잡하다.

## 7. 권장 설계 상세

### 데이터 소유권

Packet Pool slot 소유권은 다음처럼 이동한다.

```text
FREE
  -> RX thread acquire
  -> recvfrom writes slot.buf
  -> Image/LiDAR이면 queue에 slot_id push
  -> reassembly thread pop
  -> reassembly buffer에 payload copy
  -> reassembly thread release
  -> FREE
```

Odom/ACK는 queue에 넣지 않는다.

```text
FREE
  -> RX thread acquire
  -> recvfrom writes slot.buf
  -> RX thread handles packet
  -> RX thread release
  -> FREE
```

### Drop 정책

pool 고갈 시 선택지는 두 가지다.

1. 새 패킷 drop
2. 가장 오래된 queued packet slot을 drop하고 새 패킷 수신

현재 `FragQueue`는 queue full일 때 가장 오래된 항목을 버린다. 같은 정책을 유지하려면 index queue가 full일 때 old slot을 pop/drop하고 pool에 반환한 뒤 새 slot_id를 push하면 된다.

다만 pool acquire 실패 시점은 `recvfrom()` 전에 발생한다. free slot이 없으면 수신할 user buffer가 없다. 이때는 임시 drain buffer로 `recvfrom()`해서 버리거나, 잠깐 대기하거나, 통계만 올리고 루프를 넘긴다.

권장:

- queue full: oldest queued slot drop 후 release
- pool empty: 작은 stack drain buffer로 1 packet 수신 후 drop count 증가

### 동기화

`FragIndexQueue`는 기존 `FragQueue`와 동일하게 `mutex + condvar`를 쓴다.

pool free list는 별도 mutex로 보호한다. 단, 성능상 pool acquire/release가 자주 발생하므로 추후 lock-free stack으로 바꿀 수 있다. 1차 구현은 mutex가 안전하다.

### timeout 처리

기존 reassembly timeout은 유지한다. 방식 A에서는 reassembly 로직이 그대로 있으므로 `expire_stale()`도 거의 그대로 유지 가능하다.

## 8. 결론

현실적인 1차 개선안은 **Packet Pool + Index Queue**이다. 이 방식은 사용자가 제안한 "커널 버퍼에서 꺼낼 때 바로 메모리풀에 저장하고 포인터/인덱스로 접근한 뒤 마지막 commit에서만 공유메모리에 복사" 방향과 가장 가깝다.

다만 일반 UDP `recvfrom()`을 쓰는 한 kernel-to-user copy는 남는다. 따라서 목표는 "커널 copy 제거"가 아니라 "daemon 내부 중복 copy 제거"로 잡는 것이 맞다.

최종 권장 경로:

```text
현재 구조
  -> Packet Pool + Index Queue 적용
  -> 성능 측정
  -> 필요 시 recvmmsg batch receive 추가
  -> Qt ABI 변경 가능할 때 Image 한정 SHM direct reassembly 검토
```

