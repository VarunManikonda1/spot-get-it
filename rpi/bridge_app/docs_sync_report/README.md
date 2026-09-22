# BridgeDaemon 동기화 및 임계영역 처리 보고서

작성일: 2026-04-29

## 1. 보고서 목적

이 보고서는 `bride_app_codex` 코드에서 사용된 동기화 기법과 임계영역 처리 방법을 정리한다. 대상 코드는 Raspberry Pi 5에서 BridgeDaemon이 여러 스레드로 Jetson, Qt GUI, PC와 통신하는 구조이며, 공유 메모리와 스레드 간 큐를 사용한다.

코드에서 실제로 확인되는 주요 동기화 기법은 다음과 같다.

| 분류 | 사용 라이브러리/기능 | 대표 사용 위치 |
|---|---|---|
| 원자 연산 | C11 `<stdatomic.h>`, C++ `<atomic>` | `shm_def.h`, `bridge_main.c`, `bridge_api.c`, `reassembly_shm.c` |
| 일반 mutex | POSIX pthread mutex | `frag_queue.h`, `bridge_api.c`, `jetson_rx.c`, `protocol_timer.c`, `shm_def.h`의 command queue |
| condition variable | POSIX pthread condvar | `frag_queue.h` |
| reader-writer lock | POSIX pthread rwlock | `shm_def.h`, `bridge_api.c`, `bridge_main.c` |
| 세마포어 | POSIX unnamed semaphore `sem_t` | `shm_def.h`, `bridge_main.c`, `reassembly_shm.c` |
| 공유 메모리 | POSIX SHM + `mmap` | `bridge_main.c`, `shm_def.h` |
| 스레드 생성/종료 | POSIX pthread thread | `bridge_main.c` |
| 시그널 기반 종료 통지 | POSIX signal + atomic flag | `bridge_main.c` |
| lock-free buffer publication | triple buffer + atomic ready index | `shm_def.h`, `reassembly_shm.c` |

## 2. 전체 동시성 구조

BridgeDaemon은 로봇 수 N개 기준으로 여러 스레드를 실행한다.

| 스레드 | 역할 | 공유 자원 |
|---|---|---|
| `jetson_rx_thread` | Jetson UDP 패킷 수신 | Jetson 주소 테이블, SHM, FragQueue |
| `reassembly_shm_thread` | image/lidar fragment 재조립 후 SHM publish | FragQueue, SHM image/lidar slot |
| `jetson_tx_thread` | SHM command queue 수신 및 Jetson 송신 | stop flag, BridgeApi, `SharedData.cmd_queue` |
| `protocol_timer_thread` | heartbeat 송신, ACK timeout poll | Jetson 주소 테이블, pending command table |
| `pc_link_thread` | PC command/status gateway | BridgeApi, SHM |
| main/watchdog | daemon lifecycle, 연결 상태 감시 | SHM meta/state, stop flag |

동기화가 필요한 이유는 여러 스레드가 같은 메모리 영역을 동시에 읽거나 쓰기 때문이다. 예를 들어 `jetson_rx_thread`가 패킷 수신 수를 증가시키는 동안 watchdog이 같은 값을 읽고, `protocol_timer_thread`가 주소 테이블을 읽는 동안 `jetson_rx_thread`가 새 주소를 등록할 수 있다.

## 3. 원자 연산: `<stdatomic.h>`, `<atomic>`

### 3.1 기능

atomic은 여러 스레드가 같은 변수에 동시에 접근해도 데이터 레이스가 발생하지 않도록 보장하는 동기화 기법이다. 일반 변수에 대해 한 스레드가 쓰고 다른 스레드가 동시에 읽으면 C 언어 기준으로 data race가 되지만, `_Atomic` 변수와 atomic 함수를 사용하면 읽기/쓰기/증가 연산이 쪼개지지 않는 하나의 연산처럼 처리된다.

이 프로젝트에서는 C daemon과 C++ Qt GUI가 같은 `shm_def.h`를 공유할 수 있도록 다음 매크로를 사용한다.

```c
#ifdef __cplusplus
#include <atomic>
#define ATOMIC_UINT8 std::atomic<uint8_t>
#define ATOMIC_INT std::atomic<int>
#else
#include <stdatomic.h>
#define ATOMIC_UINT8 _Atomic uint8_t
#define ATOMIC_INT _Atomic int
#endif
```

C로 빌드할 때는 `_Atomic`과 `<stdatomic.h>`를 쓰고, C++로 빌드할 때는 `std::atomic<T>`를 쓴다. 덕분에 daemon과 Qt reader가 같은 공유 메모리 구조체를 같은 의미로 해석할 수 있다.

### 3.2 내부 동작 개념

atomic 연산은 CPU의 원자 명령어 또는 컴파일러가 삽입하는 메모리 장벽을 사용한다. 정수 읽기/쓰기처럼 단순한 경우에는 CPU가 한 번에 처리할 수 있는 원자 load/store 명령을 사용할 수 있고, 증가 같은 read-modify-write 연산은 compare-and-swap, fetch-and-add 계열의 원자 명령으로 구현된다.

중요한 점은 atomic이 항상 mutex처럼 잠자는 lock을 거는 것은 아니라는 점이다. 대부분의 작은 정수 atomic은 lock-free로 처리될 수 있다. 다만 플랫폼이나 타입 크기에 따라 내부적으로 lock을 사용할 수도 있다.

### 3.3 코드에서 사용된 atomic 변수

| 변수 | 위치 | 목적 |
|---|---|---|
| `atomic_bool stop` | `bridge_main.c`, `bridge_ctx.h` | 전체 스레드 종료 요청 flag |
| `img_ready_idx` | `shm_def.h` | 최신 image slot 번호 |
| `lidar_ready_idx` | `shm_def.h` | 최신 lidar slot 번호 |
| `meta.jetson_connected` | `shm_def.h` | Jetson 연결 상태 |
| `meta.pkt_count` | `shm_def.h` | 수신 packet 수 |
| `event_log.write_seq` | `shm_def.h` | event ring buffer write sequence |
| `metrics.rx_packets` 등 | `shm_def.h` | 통계 counter |
| `BridgeApi.next_command_id` | `bridge_ctx.h` | command id 발급 |

### 3.4 대표 함수와 사용 의미

#### `atomic_init(obj, value)`

atomic 객체를 초기값으로 초기화한다.

형태:

```c
atomic_init(&g_ctx.stop, false);
atomic_init(&api->next_command_id, 1);
```

의미:

- `obj`: 초기화할 atomic 변수 주소
- `value`: 초기값
- 이 프로젝트에서는 daemon 시작 시 stop flag와 command id counter를 초기화한다.

#### `atomic_store(obj, value)`

atomic 변수에 값을 저장한다. 기본 memory order는 `memory_order_seq_cst`이다.

형태:

```c
atomic_store(&shm->img_ready_idx, -1);
atomic_store(&shm->meta.jetson_connected, 1);
```

의미:

- `obj`: 값을 저장할 atomic 변수 주소
- `value`: 저장할 값
- 이 프로젝트에서는 ready slot index 갱신, 연결 상태 갱신, counter 초기화에 사용한다.

#### `atomic_load(obj)`

atomic 변수의 값을 읽는다. 기본 memory order는 `memory_order_seq_cst`이다.

형태:

```c
int ready = atomic_load(&shm->img_ready_idx);
uint8_t connected = atomic_load(&shm->meta.jetson_connected);
```

의미:

- `obj`: 읽을 atomic 변수 주소
- 반환값: 현재 atomic 변수 값
- 이 프로젝트에서는 최신 image/lidar slot 확인, 연결 상태 확인, event sequence snapshot에 사용한다.

#### `atomic_fetch_add(obj, value)`

atomic 변수에 값을 더하고, 더하기 전의 이전 값을 반환한다.

형태:

```c
int seq = atomic_fetch_add(&shm->event_log.write_seq, 1);
atomic_fetch_add(&shm->metrics.rx_packets, 1);
uint32_t command_id = atomic_fetch_add(&api->next_command_id, 1);
```

의미:

- `obj`: 증가시킬 atomic 변수 주소
- `value`: 더할 값
- 반환값: 증가 전 값
- event ring buffer에서는 반환된 `seq`를 이용해 `events[seq % EVENT_LOG_SIZE]` 위치를 선택한다.

#### `atomic_store_explicit(obj, value, memory_order)`

명시적인 memory order로 atomic store를 수행한다.
#### `atomic_load_explicit(obj, memory_order)`

atomic_store_explicit와 atomic_load_explicit은 연관이 깊다.
주로 atomic_load_explicit할 때  atomic_store_explicit의 release이전 작업을 보장하는데 사용한다.

직관적으로

release는:

“여기까지 내가 만든 데이터 스냅샷을 공개한다”

acquire는:

“그 스냅샷을 통째로 가져온다


형태:

```c
atomic_store_explicit(&g_ctx.stop, true, memory_order_release);
```

의미:

- `memory_order_release`: stop flag를 true로 쓰기 전에 수행한 메모리 작업이 이후 acquire load를 하는 스레드에게 보이도록 하는 release 의미를 준다.
- 이 프로젝트에서는 signal handler와 `request_stop_all()`에서 종료 요청을 게시할 때 사용한다.

#### `atomic_load_explicit(obj, memory_order)`

명시적인 memory order로 atomic load를 수행한다.

형태:

```c
while (!atomic_load_explicit(ctx->stop, memory_order_acquire)) {
    ...
}
```

의미:

- `memory_order_acquire`: release store와 짝을 이루어 종료 요청을 안정적으로 관찰한다.
- 모든 worker thread의 main loop에서 stop flag를 확인할 때 사용된다.

### 3.5 이 코드에서 atomic을 쓰는 대표 패턴

#### stop flag

`bridge_main.c`의 signal handler는 `SIGINT`, `SIGTERM`을 받으면 `g_ctx.stop`을 true로 바꾼다. 각 스레드는 loop 조건에서 이 값을 `atomic_load_explicit(..., memory_order_acquire)`로 읽고 종료한다.

장점:

- signal handler에서 mutex를 잠그지 않는다.
- 여러 스레드가 동시에 읽어도 data race가 없다.
- 종료 요청 전파가 단순하다.

#### triple buffer ready index

`reassembly_shm.c`는 image/lidar 데이터를 빈 slot에 모두 복사한 뒤 마지막에 `img_ready_idx` 또는 `lidar_ready_idx`를 atomic store로 바꾼다.

순서:

1. writer가 새 slot에 데이터 복사
2. metadata 작성
3. `atomic_store(&shm->img_ready_idx, slot)`
4. `sem_post(&shm->img_sem)`으로 reader 깨움

reader는 atomic load로 최신 slot 번호를 확인한다. 큰 image/lidar payload 전체에 mutex를 걸지 않고, slot 번호만 atomic으로 publish하는 방식이다.

## 4. pthread mutex: `<pthread.h>`

### 4.1 기능

mutex는 한 번에 하나의 스레드만 특정 임계영역에 들어가도록 막는 lock이다. 공유 구조체의 여러 필드를 함께 읽거나 써야 할 때 사용한다.

이 프로젝트에서는 다음 자원을 mutex로 보호한다.

| 보호 대상 | mutex | 사용 위치 |
|---|---|---|
| Jetson 주소 테이블 | `JetsonAddrTable.mu` | `jetson_rx.c`, `bridge_api.c`, `protocol_timer.c` |
| pending command table | `BridgeApi.pending_mu` | `bridge_api.c` |
| FragQueue 내부 배열/index/count | `FragQueue.mu` | `frag_queue.h` |

### 4.2 내부 동작 개념

pthread mutex는 사용자 공간의 빠른 경로와 커널 대기 경로를 함께 사용한다. lock이 비어 있으면 atomic compare-and-swap 같은 CPU 원자 연산으로 빠르게 획득한다. 이미 다른 스레드가 잡고 있으면 운영체제 futex 또는 유사한 커널 대기 메커니즘으로 스레드를 잠재워 CPU 낭비를 줄인다.

### 4.3 대표 함수와 사용 의미

#### `pthread_mutex_init(mutex, attr)`

mutex를 초기화한다.

형태:

```c
pthread_mutex_init(&g_ctx.addr_table.mu, NULL);
pthread_mutex_init(&api->pending_mu, NULL);
pthread_mutex_init(&q->mu, NULL);
```

의미:

- `mutex`: 초기화할 `pthread_mutex_t *`
- `attr`: mutex 속성, `NULL`이면 기본 속성
- 이 코드에서는 일반 process-local mutex로 사용한다.

#### `pthread_mutex_lock(mutex)`

mutex를 획득한다. 이미 다른 스레드가 잡고 있으면 해제될 때까지 대기한다.

형태:

```c
pthread_mutex_lock(&tbl->mu);
```

의미:

- 임계영역 시작
- lock을 획득한 스레드만 보호 대상 데이터를 읽거나 쓸 수 있다.

#### `pthread_mutex_unlock(mutex)`

mutex를 해제한다.

형태:

```c
pthread_mutex_unlock(&tbl->mu);
```

의미:

- 임계영역 종료
- 대기 중인 다른 스레드가 lock을 획득할 수 있게 된다.

#### `pthread_mutex_destroy(mutex)`

mutex 자원을 정리한다.

형태:

```c
pthread_mutex_destroy(&g_ctx.addr_table.mu);
pthread_mutex_destroy(&api->pending_mu);
pthread_mutex_destroy(&q->mu);
```

의미:

- 더 이상 사용하지 않는 mutex를 해제한다.
- destroy 이후에는 다시 init하기 전까지 사용하면 안 된다.

### 4.4 Jetson 주소 테이블 임계영역

`jetson_rx.c`의 `learn_addr()`는 처음 수신한 Jetson IP를 `addr_table`에 저장한다. 동시에 `protocol_timer.c`나 `bridge_api.c`가 같은 주소를 읽을 수 있으므로 mutex로 보호한다.

```c
pthread_mutex_lock(&tbl->mu);
if (!tbl->set[rid]) {
    tbl->addr[rid] = *src;
    tbl->addr[rid].sin_port = htons(JETSON_CMD_PORT);
    tbl->set[rid] = 1;
}
pthread_mutex_unlock(&tbl->mu);
```

이렇게 하지 않으면 `set[rid]`는 1인데 `addr[rid]` 복사가 아직 끝나지 않은 중간 상태를 다른 스레드가 읽을 수 있다.

### 4.5 pending command table 임계영역

`BridgeApi.pending[]`는 ACK 대기 중인 command 목록이다. command 송신, ACK 수신, timeout poll이 서로 다른 스레드에서 발생할 수 있으므로 `pending_mu`로 보호한다.

사용 함수:

- `track_pending()`: 빈 pending slot에 command 등록
- `bridge_api_handle_ack()`: ACK를 받은 command를 `in_use = 0`으로 제거
- `bridge_api_poll_timeouts()`: deadline이 지난 command를 retry 또는 timeout 처리

## 5. condition variable: pthread condvar

### 5.1 기능

condition variable은 어떤 조건이 만족될 때까지 스레드를 잠재우고, 다른 스레드가 조건 변화를 알려주면 깨어나게 하는 동기화 기법이다. mutex와 함께 사용한다.

이 프로젝트에서는 `FragQueue`가 producer-consumer queue로 사용한다.

- producer: `jetson_rx_thread`
- consumer: `reassembly_shm_thread`
- queue: `FragQueue`
- 조건: `q->count > 0` 또는 `q->stop == 1`

### 5.2 내부 동작 개념

condition variable은 직접 데이터를 보호하지 않는다. 실제 데이터 보호는 mutex가 담당하고, condvar는 “기다림과 깨움”만 담당한다. `pthread_cond_wait()`는 호출 시 mutex를 원자적으로 풀고 대기 상태로 들어간다. 깨어나면 다시 mutex를 획득한 뒤 반환한다.

이 원자적 unlock-and-wait가 중요하다. unlock과 wait가 분리되어 있으면 signal을 놓치는 race가 생길 수 있다.

### 5.3 대표 함수와 사용 의미

#### `pthread_cond_init(cond, attr)`

condition variable을 초기화한다.

형태:

```c
pthread_cond_init(&q->cv, NULL);
```

의미:

- `cond`: 초기화할 `pthread_cond_t *`
- `attr`: 속성, `NULL`이면 기본 속성

#### `pthread_cond_wait(cond, mutex)`

조건이 만족될 때까지 대기한다.

형태:

```c
while (q->count == 0 && !q->stop)
    pthread_cond_wait(&q->cv, &q->mu);
```

의미:

- 호출 전 mutex를 잡고 있어야 한다.
- 대기 중에는 mutex를 풀어 producer가 queue에 push할 수 있게 한다.
- 깨어난 뒤에는 mutex를 다시 잡은 상태로 반환한다.
- spurious wakeup이 있을 수 있으므로 `if`가 아니라 `while`로 조건을 다시 검사한다.

#### `pthread_cond_signal(cond)`

condition variable을 기다리는 스레드 하나를 깨운다.

형태:

```c
pthread_cond_signal(&q->cv);
```

의미:

- `frag_queue_push()`에서 새 packet을 넣은 뒤 consumer 하나를 깨운다.

#### `pthread_cond_broadcast(cond)`

condition variable을 기다리는 모든 스레드를 깨운다.

형태:

```c
pthread_cond_broadcast(&q->cv);
```

의미:

- `frag_queue_stop()`에서 종료를 위해 대기 중인 consumer들을 모두 깨운다.

#### `pthread_cond_destroy(cond)`

condition variable 자원을 정리한다.

형태:

```c
pthread_cond_destroy(&q->cv);
```

### 5.4 FragQueue producer-consumer 구조

`frag_queue_push()`:

1. mutex lock
2. queue가 가득 차면 오래된 항목 drop
3. 새 packet 복사
4. `count++`
5. `pthread_cond_signal()`
6. mutex unlock

`frag_queue_pop()`:

1. mutex lock
2. queue가 비어 있고 stop이 아니면 condvar wait
3. packet 복사
4. `count--`
5. mutex unlock
6. packet length 반환

이 구조 덕분에 reassembly thread는 busy waiting으로 CPU를 낭비하지 않고, packet이 들어올 때만 깨어난다.

## 6. reader-writer lock: pthread rwlock

### 6.1 기능

reader-writer lock은 읽는 스레드가 여러 개일 때는 동시에 진입을 허용하고, 쓰는 스레드가 있을 때는 배타적으로 막는 lock이다.

이 프로젝트에서는 SHM 안의 작은 구조체를 보호하는 데 사용한다.

| lock | 보호 데이터 | 사용 목적 |
|---|---|---|
| `odom_lock` | `odom_x`, `odom_y`, `odom_theta`, velocity, timestamp | odometry snapshot |
| `meta_lock` | drop count 등 일부 meta field | 통계 읽기/쓰기 |
| `state_lock` | `RobotState state` | GUI/PC가 읽는 robot 상태 |

### 6.2 내부 동작 개념

rwlock은 내부적으로 reader 수, writer 대기 상태, lock 상태를 관리한다. read lock은 writer가 없으면 여러 스레드가 동시에 획득할 수 있다. write lock은 reader와 writer가 모두 없을 때만 획득한다.

일반 mutex보다 유리한 경우는 읽기가 많고 쓰기가 상대적으로 적을 때이다. 이 프로젝트의 `RobotState`, `BridgeMeta`, odom은 GUI/PC가 상태 snapshot을 자주 읽고 daemon이 갱신하므로 rwlock이 적합하다.

### 6.3 대표 함수와 사용 의미

#### `pthread_rwlockattr_init(attr)`

rwlock attribute 객체를 초기화한다.

형태:

```c
pthread_rwlockattr_t rw_attr;
pthread_rwlockattr_init(&rw_attr);
```

#### `pthread_rwlockattr_setpshared(attr, PTHREAD_PROCESS_SHARED)`

rwlock을 프로세스 간 공유 가능하도록 설정한다.

형태:

```c
pthread_rwlockattr_setpshared(&rw_attr, PTHREAD_PROCESS_SHARED);
```

의미:

- `SharedData`는 POSIX shared memory 안에 있으므로 daemon process와 Qt process가 같은 rwlock을 사용할 수 있어야 한다.
- 기본값인 `PTHREAD_PROCESS_PRIVATE`는 같은 프로세스 내부 스레드 사이에서만 쓰는 용도이다.

#### `pthread_rwlock_init(lock, attr)`

rwlock을 초기화한다.

형태:

```c
pthread_rwlock_init(&shm->odom_lock, &rw_attr);
pthread_rwlock_init(&shm->meta_lock, &rw_attr);
pthread_rwlock_init(&shm->state_lock, &rw_attr);
```

#### `pthread_rwlock_rdlock(lock)`

read lock을 획득한다.

형태:

```c
pthread_rwlock_rdlock(&shm->state_lock);
out->connected = shm->state.connected;
...
pthread_rwlock_unlock(&shm->state_lock);
```

의미:

- 여러 reader가 동시에 들어갈 수 있다.
- writer가 쓰는 중이면 대기한다.

#### `pthread_rwlock_wrlock(lock)`

write lock을 획득한다.

형태:

```c
pthread_rwlock_wrlock(&shm->state_lock);
shm->state.seq++;
shm->state.connected = 1;
pthread_rwlock_unlock(&shm->state_lock);
```

의미:

- 단 하나의 writer만 들어갈 수 있다.
- reader도 동시에 들어오지 못한다.

#### `pthread_rwlock_unlock(lock)`

read 또는 write lock을 해제한다.

#### `pthread_rwlock_destroy(lock)`

rwlock 자원을 정리한다.

형태:

```c
pthread_rwlock_destroy(&shm->odom_lock);
pthread_rwlock_destroy(&shm->meta_lock);
pthread_rwlock_destroy(&shm->state_lock);
```

#### `pthread_rwlockattr_destroy(attr)`

rwlock attribute 객체를 정리한다.

### 6.4 이 코드의 rwlock 사용 예

`bridge_api_update_odom()`은 odom 값을 한 묶음으로 갱신한다.

```c
pthread_rwlock_wrlock(&shm->odom_lock);
shm->odom_x = odom->x;
shm->odom_y = odom->y;
shm->odom_theta = odom->theta;
...
pthread_rwlock_unlock(&shm->odom_lock);
```

여러 필드를 함께 갱신하는 중간에 reader가 들어오면 x는 새 값이고 y는 이전 값인 불일치 snapshot을 볼 수 있다. write lock은 이런 중간 상태 노출을 막는다.

`bridge_api_snapshot_status()`는 PC status packet을 만들기 위해 `state_lock` read lock을 잡고 여러 필드를 한 번에 복사한다.

## 7. POSIX semaphore: `<semaphore.h>`

### 7.1 기능

세마포어는 카운터 기반 동기화 객체이다. `sem_post()`는 카운터를 증가시키고, `sem_wait()`는 카운터가 0보다 클 때 감소시키며 진행한다. 카운터가 0이면 wait 쪽은 대기한다.

이 프로젝트에서는 image/lidar 새 frame 알림에 사용한다.

| semaphore | 의미 |
|---|---|
| `img_sem` | 새 image frame 준비 알림 |
| `lidar_sem` | 새 lidar scan 준비 알림 |

### 7.2 내부 동작 개념

세마포어는 내부 counter를 원자적으로 조작한다. counter가 양수이면 `sem_wait()`는 즉시 통과하고 counter를 1 줄인다. counter가 0이면 커널 대기 큐에서 잠들었다가 `sem_post()`가 호출될 때 깨어난다.

이 프로젝트에서는 `sem_t`를 `SharedData` 안에 넣고 `sem_init(..., pshared=1, ...)`로 초기화한다. 따라서 daemon과 Qt GUI가 서로 다른 process여도 같은 semaphore를 사용할 수 있다.

### 7.3 대표 함수와 사용 의미

#### `sem_init(sem, pshared, value)`

unnamed semaphore를 초기화한다.

형태:

```c
sem_init(&shm->img_sem, 1, 0);
sem_init(&shm->lidar_sem, 1, 0);
```

의미:

- `sem`: 초기화할 `sem_t *`
- `pshared`: 0이면 process 내부 공유, 1이면 process 간 공유
- `value`: 초기 counter 값
- 이 코드에서는 SHM 안에 semaphore가 있으므로 `pshared=1`, 최초 frame은 없으므로 `value=0`이다.

#### `sem_post(sem)`

semaphore counter를 1 증가시키고 대기 중인 reader를 깨운다.

형태:

```c
sem_post(&shm->img_sem);
sem_post(&shm->lidar_sem);
```

의미:

- image/lidar frame이 SHM slot에 완전히 기록된 뒤 호출된다.
- Qt reader는 이 알림을 받고 atomic ready index를 읽어 최신 slot을 가져갈 수 있다.

#### `sem_wait(sem)`

semaphore counter가 0보다 클 때까지 기다린 뒤 counter를 1 감소시킨다.

형태:

```c
sem_wait(&shm->img_sem);
```

의미:

- 이 repository의 daemon 코드에는 직접 호출이 없지만, Qt reader 쪽에서 새 frame 대기용으로 사용하는 함수이다.

#### `sem_trywait(sem)`

기다리지 않고 semaphore 획득을 시도한다.

의미:

- counter가 양수이면 감소시키고 성공한다.
- counter가 0이면 즉시 실패하며 `errno`가 `EAGAIN`이 된다.
- GUI가 blocking 없이 새 frame 유무만 확인하고 싶을 때 사용할 수 있다.

#### `sem_timedwait(sem, abs_timeout)`

지정한 절대 시간까지만 기다린다.

의미:

- GUI가 일정 시간 이상 멈추지 않게 만들 때 사용할 수 있다.

#### `sem_destroy(sem)`

semaphore 자원을 정리한다.

형태:

```c
sem_destroy(&shm->img_sem);
sem_destroy(&shm->lidar_sem);
```

## 8. POSIX shared memory와 `mmap`

### 8.1 기능

POSIX shared memory는 서로 다른 process가 같은 물리 메모리 영역을 공유하도록 하는 기능이다. 이 프로젝트에서는 BridgeDaemon이 writer이고 Qt GUI가 reader이다. 로봇마다 `/robot_bridge_N` 형태의 SHM 객체를 만든다.

공유 메모리 자체는 동기화 기능이 아니다. 단지 같은 메모리를 여러 process가 볼 수 있게 해준다. 따라서 내부 데이터 일관성을 위해 atomic, rwlock, semaphore가 함께 필요하다.

### 8.2 대표 함수와 사용 의미

#### `shm_open(name, oflag, mode)`

POSIX shared memory 객체를 열거나 생성한다.

형태:

```c
int fd = shm_open(name, O_CREAT | O_RDWR, 0666);
```

의미:

- `name`: `/robot_bridge_0` 같은 SHM 이름
- `oflag`: 생성/읽기/쓰기 flag
- `mode`: 권한
- 반환값은 file descriptor이다.

#### `ftruncate(fd, length)`

SHM 객체의 크기를 지정한다.

형태:

```c
ftruncate(fd, sizeof(SharedData));
```

의미:

- `SharedData` 구조체 전체가 들어갈 만큼 SHM 크기를 확장한다.

#### `mmap(addr, length, prot, flags, fd, offset)`

SHM 객체를 process 주소 공간에 매핑한다.

형태:

```c
SharedData *shm = mmap(NULL, sizeof(SharedData),
                       PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
```

의미:

- `PROT_READ | PROT_WRITE`: 읽기/쓰기 허용
- `MAP_SHARED`: 변경 사항이 같은 SHM을 mapping한 다른 process에도 보임
- 반환된 포인터로 일반 구조체처럼 접근할 수 있다.

#### `munmap(addr, length)`

mapping을 해제한다.

형태:

```c
munmap(shm, sizeof(SharedData));
```

#### `shm_unlink(name)`

SHM 이름을 제거한다.

형태:

```c
shm_unlink(name);
```

의미:

- 파일 시스템 namespace에서 SHM 객체 이름을 제거한다.
- 이미 mapping한 process가 있으면 mapping은 즉시 사라지지 않고, 마지막 참조가 사라질 때 정리된다.

### 8.3 이 코드의 SHM 초기화 순서

`bridge_main.c`의 `shm_init_one()`은 다음 순서로 SHM을 만든다.

1. 이전 SHM 제거: `shm_unlink(name)`
2. 새 SHM 생성: `shm_open(name, O_CREAT | O_RDWR, 0666)`
3. 크기 지정: `ftruncate(fd, sizeof(SharedData))`
4. 주소 공간 매핑: `mmap(..., MAP_SHARED, ...)`
5. fd 닫기: `close(fd)`
6. 메모리 초기화: `memset(shm, 0, sizeof(SharedData))`
7. atomic index 초기화
8. process-shared semaphore 초기화
9. process-shared rwlock 초기화
10. SHM ABI header 초기화

## 9. POSIX thread 생성과 종료

### 9.1 기능

pthread는 하나의 process 안에서 여러 실행 흐름을 만드는 POSIX thread 라이브러리이다. 이 프로젝트는 네트워크 수신, command 송신, timer, 재조립 작업을 별도 thread로 분리한다.

### 9.2 대표 함수와 사용 의미

#### `pthread_create(thread, attr, start_routine, arg)`

새 thread를 생성한다.

형태:

```c
pthread_create(&tid, &attr, fn, arg);
```

의미:

- `thread`: 생성된 thread id를 받을 포인터
- `attr`: thread 속성
- `start_routine`: thread entry 함수
- `arg`: entry 함수에 넘길 인자

이 코드에서는 `create_thread()` helper가 `jetson_rx_thread`, `jetson_tx_thread`, `protocol_timer_thread`, `pc_link_thread`, `reassembly_shm_thread`를 생성한다.

#### `pthread_join(thread, retval)`

thread가 종료될 때까지 기다린다.

형태:

```c
pthread_join(g_ctx.rx_tid, NULL);
```

의미:

- main thread가 worker thread 종료를 기다린다.
- join하지 않으면 resource cleanup 시 아직 worker가 공유 자원에 접근할 수 있다.

#### `pthread_attr_init(attr)`

thread attribute 객체를 초기화한다.

#### `pthread_attr_setaffinity_np(attr, cpusetsize, cpuset)`

thread CPU affinity를 설정한다.

형태:

```c
pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
```

의미:

- 특정 thread를 특정 CPU core에 고정한다.
- 이 코드에서는 `jetson_rx_thread`를 core 2에 고정하려고 시도한다.

#### `pthread_attr_setschedpolicy(attr, policy)`

thread scheduling policy를 설정한다.

형태:

```c
pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
```

의미:

- realtime scheduling policy를 요청한다.
- 권한이 없으면 실패할 수 있으며, 이 코드에서는 실패 시 일반 thread 생성으로 재시도한다.

#### `pthread_attr_setschedparam(attr, param)`

thread scheduling priority를 설정한다.

형태:

```c
struct sched_param param = { .sched_priority = priority };
pthread_attr_setschedparam(&attr, &param);
```

#### `pthread_attr_setinheritsched(attr, PTHREAD_EXPLICIT_SCHED)`

부모 thread의 scheduling 속성을 상속하지 않고 attribute에 지정한 scheduling 값을 사용하도록 한다.

#### `pthread_attr_destroy(attr)`

thread attribute 객체를 정리한다.

## 10. signal과 atomic stop flag

### 10.1 기능

`bridge_main.c`는 `SIGINT`, `SIGTERM`을 받아 daemon을 종료한다.

```c
signal(SIGINT,  sig_handler);
signal(SIGTERM, sig_handler);
```

signal handler는 복잡한 cleanup을 직접 하지 않고 atomic stop flag만 true로 바꾼다.

```c
static void sig_handler(int sig) {
    (void)sig;
    atomic_store_explicit(&g_ctx.stop, true, memory_order_release);
}
```

### 10.2 왜 signal handler에서 mutex를 잡지 않는가

signal handler에서는 async-signal-safe 함수만 호출해야 한다. mutex lock, printf, malloc 같은 함수는 signal handler 안에서 안전하지 않다. 예를 들어 어떤 thread가 mutex를 잡은 상태에서 signal handler가 같은 mutex를 잡으려 하면 deadlock이 발생할 수 있다.

따라서 이 코드처럼 signal handler에서는 종료 flag만 설정하고, 실제 cleanup은 main 흐름에서 처리하는 방식이 좋다.

### 10.3 대표 함수

#### `signal(signum, handler)`

signal handler를 등록한다.

형태:

```c
signal(SIGINT, sig_handler);
signal(SIGTERM, sig_handler);
```

의미:

- Ctrl+C 또는 종료 요청을 받았을 때 `sig_handler()`가 호출된다.

## 11. triple buffer + atomic ready index

### 11.1 기능

image/lidar는 데이터 크기가 크기 때문에 매 frame마다 전체 payload에 lock을 거는 방식은 비효율적이다. 이 프로젝트는 여러 slot을 두고 writer가 다음 slot에 쓰고, 마지막에 atomic index만 바꾸는 방식으로 publish한다.

`shm_def.h`:

```c
ImgSlot img_slots[IMG_SLOTS];
ATOMIC_INT img_ready_idx;

LidarSlot lidar_slots[LIDAR_SLOTS];
ATOMIC_INT lidar_ready_idx;
```

### 11.2 동작 순서

writer인 `reassembly_shm_thread`:

1. 현재 ready slot을 `atomic_load()`로 확인
2. 다음 slot 선택
3. 새 slot에 frame data 복사
4. slot metadata 작성
5. `atomic_store()`로 ready index publish
6. `sem_post()`로 reader에게 알림

reader인 Qt GUI:

1. `sem_wait()` 또는 polling으로 새 frame 감지
2. `atomic_load()`로 ready index 확인
3. 해당 slot에서 frame data 읽기

### 11.3 장점

- 큰 image/lidar buffer를 읽고 쓰는 동안 mutex를 오래 잡지 않는다.
- writer는 새 slot에 쓴 뒤 index만 바꾸므로 reader가 이전 slot을 읽는 동안 다음 frame을 준비할 수 있다.
- `sem_t`와 함께 사용하면 busy waiting 없이 새 frame 도착을 알릴 수 있다.

### 11.4 주의점

현재 daemon writer 쪽 `atomic_store(&shm->img_ready_idx, slot)`은 기본 memory order인 sequential consistency를 사용한다. 이론적으로는 frame data 복사 후 ready index store가 publish 역할을 하므로 release store, reader 쪽은 acquire load를 명시하면 의도가 더 분명해진다.

예:

```c
atomic_store_explicit(&shm->img_ready_idx, slot, memory_order_release);
int slot = atomic_load_explicit(&shm->img_ready_idx, memory_order_acquire);
```

현재 기본 atomic도 강한 순서를 제공하므로 동작 의도와 충돌하지는 않는다.

## 12. EventLog ring buffer와 atomic sequence

### 12.1 기능

`EventLog`는 고정 크기 ring buffer이다.

```c
typedef struct {
    ATOMIC_INT write_seq;
    RobotEvent events[EVENT_LOG_SIZE];
} EventLog;
```

event를 기록할 때 `atomic_fetch_add()`로 sequence를 하나 얻고, `seq % EVENT_LOG_SIZE` 위치에 기록한다.

```c
int seq = atomic_fetch_add(&shm->event_log.write_seq, 1);
RobotEvent *ev = &shm->event_log.events[seq % EVENT_LOG_SIZE];
```

### 12.2 장점

- 여러 thread가 동시에 event를 발행해도 같은 index를 할당받지 않는다.
- mutex 없이 event index allocation을 처리한다.

### 12.3 주의점

`write_seq` 증가와 `RobotEvent` 필드 작성은 하나의 transaction은 아니다. reader가 아주 정확한 event snapshot을 요구한다면 sequence 전후 확인 또는 별도 per-entry sequence 같은 보강이 필요할 수 있다. 현재 구조는 daemon 내부 event log를 간단히 공유하는 목적에는 충분하지만, lock-free ring buffer로서 완전한 multi-producer/multi-consumer 일관성을 보장하는 설계는 아니다.

## 13. 동기화 기법별 코드 사용 요약

| 파일 | 사용 기법 | 설명 |
|---|---|---|
| `shm_def.h` | atomic, rwlock, semaphore, process-shared mutex | SHM ABI에 동기화 객체 포함 |
| `bridge_main.c` | atomic stop, SHM 생성, sem/rwlock/cmd mutex init, pthread create/join | daemon lifecycle 관리 |
| `frag_queue.h` | mutex, condvar | RX thread와 reassembly thread 사이 blocking queue |
| `jetson_rx.c` | atomic stop, mutex, FragQueue | UDP 수신, 주소 학습, packet dispatch |
| `reassembly_shm.c` | atomic ready index, sem_post | fragment 재조립 후 image/lidar frame publish |
| `bridge_api.c` | mutex, rwlock, atomic counter | command ACK table, state/meta 갱신, metrics |
| `protocol_timer.c` | atomic stop, mutex | heartbeat 송신 전 주소 table snapshot |
| `pc_link.c` | atomic stop, BridgeApi snapshot | PC command/status 통신 |
| `jetson_tx.c` | atomic stop, SHM command queue mutex | Qt command queue polling 및 종료 제어 |

## 14. 대표 라이브러리 함수 목록

### `<stdatomic.h>` / `<atomic>`

| 함수/기능 | 설명 | 이 코드의 목적 |
|---|---|---|
| `_Atomic T` | atomic 타입 선언 | C daemon atomic field |
| `std::atomic<T>` | C++ atomic 타입 | Qt reader 호환 |
| `atomic_init(p, v)` | atomic 초기화 | stop flag, command id |
| `atomic_store(p, v)` | atomic 저장 | ready index, connected flag |
| `atomic_load(p)` | atomic 읽기 | ready index, connected flag |
| `atomic_fetch_add(p, n)` | atomic 증가 후 이전 값 반환 | metrics, event sequence, command id |
| `atomic_store_explicit(p, v, order)` | memory order 지정 저장 | stop flag release |
| `atomic_load_explicit(p, order)` | memory order 지정 읽기 | stop flag acquire |

### `<pthread.h>` mutex

| 함수 | 설명 |
|---|---|
| `pthread_mutex_init(m, attr)` | mutex 초기화 |
| `pthread_mutex_lock(m)` | 임계영역 진입 |
| `pthread_mutex_unlock(m)` | 임계영역 종료 |
| `pthread_mutex_destroy(m)` | mutex 정리 |

### `<pthread.h>` condition variable

| 함수 | 설명 |
|---|---|
| `pthread_cond_init(c, attr)` | condvar 초기화 |
| `pthread_cond_wait(c, m)` | mutex를 풀고 조건 대기, 깨어나면 mutex 재획득 |
| `pthread_cond_signal(c)` | 대기 thread 하나 깨움 |
| `pthread_cond_broadcast(c)` | 대기 thread 전체 깨움 |
| `pthread_cond_destroy(c)` | condvar 정리 |

### `<pthread.h>` reader-writer lock

| 함수 | 설명 |
|---|---|
| `pthread_rwlockattr_init(a)` | rwlock 속성 초기화 |
| `pthread_rwlockattr_setpshared(a, PTHREAD_PROCESS_SHARED)` | process 간 공유 가능 설정 |
| `pthread_rwlock_init(l, a)` | rwlock 초기화 |
| `pthread_rwlock_rdlock(l)` | read lock 획득 |
| `pthread_rwlock_wrlock(l)` | write lock 획득 |
| `pthread_rwlock_unlock(l)` | read/write lock 해제 |
| `pthread_rwlock_destroy(l)` | rwlock 정리 |
| `pthread_rwlockattr_destroy(a)` | 속성 객체 정리 |

### `<semaphore.h>`

| 함수 | 설명 |
|---|---|
| `sem_init(s, pshared, value)` | unnamed semaphore 초기화 |
| `sem_post(s)` | counter 증가, 대기자 깨움 |
| `sem_wait(s)` | counter가 양수일 때까지 대기 후 감소 |
| `sem_trywait(s)` | 대기 없이 획득 시도 |
| `sem_timedwait(s, timeout)` | 제한 시간까지 대기 |
| `sem_destroy(s)` | semaphore 정리 |

### POSIX shared memory / memory mapping

| 함수 | 설명 |
|---|---|
| `shm_open(name, flags, mode)` | POSIX SHM 객체 생성/열기 |
| `ftruncate(fd, size)` | SHM 크기 설정 |
| `mmap(addr, len, prot, flags, fd, off)` | SHM을 process 주소 공간에 매핑 |
| `munmap(addr, len)` | mapping 해제 |
| `shm_unlink(name)` | SHM 이름 제거 |
| `close(fd)` | file descriptor 닫기 |

### pthread thread

| 함수 | 설명 |
|---|---|
| `pthread_create(t, attr, fn, arg)` | 새 thread 생성 |
| `pthread_join(t, ret)` | thread 종료 대기 |
| `pthread_attr_init(a)` | thread attribute 초기화 |
| `pthread_attr_setaffinity_np(a, size, cpuset)` | CPU affinity 설정 |
| `pthread_attr_setschedpolicy(a, policy)` | scheduling policy 설정 |
| `pthread_attr_setschedparam(a, param)` | scheduling priority 설정 |
| `pthread_attr_setinheritsched(a, mode)` | scheduling 속성 상속 방식 설정 |
| `pthread_attr_destroy(a)` | attribute 정리 |

### signal

| 함수 | 설명 |
|---|---|
| `signal(signum, handler)` | signal handler 등록 |

## 15. 결론

이 프로젝트는 단일 동기화 방식만 사용하는 것이 아니라, 데이터의 성격에 맞게 여러 기법을 조합한다.

큰 image/lidar frame은 `triple buffer + atomic ready index + semaphore`로 처리해 lock 시간을 줄인다. 작은 상태 구조체는 `pthread_rwlock`으로 snapshot 일관성을 보장한다. queue처럼 producer-consumer 관계가 명확한 곳은 `mutex + condition variable`을 사용한다. command ACK table처럼 여러 필드를 함께 수정해야 하는 공유 배열은 `pthread_mutex`로 보호한다. 전체 종료 제어는 signal handler에서 안전하게 다룰 수 있도록 `atomic_bool stop`으로 구현한다.

즉, 이 코드의 임계영역 처리는 “큰 데이터는 lock-free publish, 작은 구조체는 rwlock, 복합 table은 mutex, 대기는 condvar/semaphore”라는 기준으로 설계되어 있다.
