# 06. `frag_queue.h` 수업 자료

## 이 파일의 역할

`frag_queue.h`는 `jetson_rx.c`와 `reassembly_shm.c` 사이의 packet queue다. UDP 수신 thread는 빠르게 packet을 받아 queue에 넣고, reassembly thread는 queue에서 꺼내 frame을 조립한다.

## 개념 설명: Producer-Consumer Queue

네트워크 RX thread가 수신과 재조립을 모두 하면 오래 걸리는 재조립 때문에 socket receive buffer가 밀릴 수 있다. 그래서 역할을 분리한다.

```text
Producer: jetson_rx_thread
  recvfrom()
  frag_queue_push()

Consumer: reassembly_shm_thread
  frag_queue_pop()
  frame reassembly
  SHM publish
```

이 queue는 mutex와 condition variable을 사용한다.

- mutex: queue 내부 상태 보호.
- condition variable: queue가 비었을 때 consumer를 잠들게 하고, push 시 깨운다.

## include 설명

| 라인 | 헤더 | 쓰는 기능 |
|---:|---|---|
| 12 | `<stdint.h>` | byte buffer type |
| 13 | `<pthread.h>` | mutex/condvar |
| 14 | `"proto.h"` | `PROTO_PKT_MAX` |

## 구조체 설명

### `FragEntry`

| 필드 | 의미 |
|---|---|
| `buf` | UDP packet byte copy |
| `len` | packet 길이 |

### `FragQueue`

| 필드 | 의미 |
|---|---|
| `entries` | circular buffer |
| `head` | pop 위치 |
| `tail` | push 위치 |
| `count` | 현재 entry 수 |
| `mu` | queue 보호 mutex |
| `cv` | consumer wakeup condition variable |
| `stop` | 종료 flag |

## 함수 설명

### `frag_queue_init(FragQueue *q)`

기능:
- queue index와 count, stop flag 초기화.
- mutex와 condvar 초기화.

### `frag_queue_destroy(FragQueue *q)`

기능:
- mutex와 condvar destroy.

주의:
- queue를 쓰는 thread가 모두 종료된 뒤 호출해야 한다.

### `frag_queue_push(FragQueue *q, const uint8_t *buf, int len)`

기능:
- packet을 queue에 넣는다.
- queue가 꽉 차면 가장 오래된 entry를 버리고 새 entry를 넣는다.

파라미터:
- `q`: 대상 queue.
- `buf`: 복사할 packet byte.
- `len`: packet 길이.

개념:
- sensor stream은 최신성이 중요하다. queue가 꽉 찼을 때 producer를 오래 block하는 것보다 오래된 fragment를 버리는 정책을 택했다.

### `frag_queue_pop(FragQueue *q, uint8_t *buf, int max_len)`

기능:
- queue에서 packet 하나를 꺼낸다.
- queue가 비어 있으면 condvar로 대기한다.
- stop이고 더 이상 data가 없으면 -1 반환.

파라미터:
- `buf`: packet을 복사받을 output buffer.
- `max_len`: output buffer 최대 크기.

### `frag_queue_stop(FragQueue *q)`

기능:
- stop flag를 set하고 condvar wait 중인 consumer를 모두 깨운다.

## 수업 포인트

- `pthread_cond_wait()`는 mutex와 함께 써야 한다.
- condvar 대기는 `if`가 아니라 `while`로 조건을 다시 확인해야 한다.
- queue full 정책은 application 성격에 따라 다르다. 이 프로젝트는 sensor 최신성을 우선한다.

## 실습 질문

1. queue가 full일 때 최신 packet을 버리는 정책과 오래된 packet을 버리는 정책의 차이는 무엇인가?
2. `frag_queue_stop()`이 condvar broadcast를 하지 않으면 종료 시 어떤 일이 생길까?
3. 이 queue를 SPSC lock-free ring으로 바꾸면 장단점은 무엇인가?
