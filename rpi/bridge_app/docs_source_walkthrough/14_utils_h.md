# 14. `utils.h` 수업 자료

## 이 파일의 역할

`utils.h`는 공통 utility 함수가 들어가는 작은 헤더다. 현재는 monotonic clock 기반 microsecond timestamp 함수 `now_us()`만 제공한다.

## 개념 설명: Monotonic Time

시간을 측정할 때 wall clock과 monotonic clock을 구분해야 한다.

- wall clock: 사람이 보는 현재 시각. NTP나 사용자가 시간을 바꾸면 점프할 수 있다.
- monotonic clock: 부팅 이후 계속 증가하는 시간. timeout, latency 측정에 적합하다.

이 프로젝트의 timeout, FPS, latency 계산은 monotonic clock을 써야 한다.

## include 설명

| 헤더 | 쓰는 기능 |
|---|---|
| `<stdint.h>` | `uint64_t` |
| `<time.h>` | `clock_gettime`, `CLOCK_MONOTONIC`, `timespec` |

## 함수 설명

### `now_us(void)`

```c
static inline uint64_t now_us(void)
```

기능:
- `CLOCK_MONOTONIC` 값을 읽어 microsecond 단위로 반환한다.

동작:
1. `struct timespec ts` 선언.
2. `clock_gettime(CLOCK_MONOTONIC, &ts)` 호출.
3. `tv_sec * 1000000 + tv_nsec / 1000` 반환.

반환:
- 부팅 이후 단조 증가한 microsecond timestamp.

## 수업 포인트

- timeout 계산에 wall clock을 쓰면 system time 변경 시 timeout이 이상해질 수 있다.
- `static inline`으로 header에 두면 여러 `.c`에서 include해도 link 충돌이 없다.
- error handling은 현재 생략되어 있다. `clock_gettime` 실패 가능성은 낮지만 strict하게는 검사할 수 있다.

## 실습 질문

1. `time(NULL)`로 timeout을 계산하면 어떤 문제가 생길 수 있을까?
2. `CLOCK_REALTIME`과 `CLOCK_MONOTONIC` 차이는 무엇인가?
3. microsecond timestamp가 overflow되는 시점은 현실적으로 문제가 될까?
