# 07. `reassembly_shm.c` 수업 자료

## 이 파일의 역할

`reassembly_shm.c`는 image/lidar fragment packet을 frame으로 재조립한 뒤 `SharedData`의 image/lidar slot에 publish한다.

## 개념 설명: Fragment Reassembly

UDP packet 하나에는 큰 JPEG나 point cloud 전체가 들어가지 않는다. Jetson은 frame을 여러 fragment로 나누어 보낸다.

BridgeDaemon은 fragment를 다음 기준으로 모은다.

- `type`: image인지 lidar인지
- `frame_id`: 같은 frame인지
- `frag_idx`, `frag_total`: 몇 번째 조각인지
- `payload_offset`: 완성 buffer 안에서 어디에 붙일지

모든 fragment가 모이면 SHM triple buffer에 publish한다.

## include 설명

| 라인 | 헤더 | 쓰는 기능 |
|---:|---|---|
| 16 | `<stdio.h>` | log |
| 17 | `<stdlib.h>` | `calloc`, `malloc`, `free` |
| 18 | `<string.h>` | `memset`, `memcpy` |
| 19 | `<stdatomic.h>` | ready index atomic load/store |
| 20 | `<time.h>` | time type |
| 22 | `"proto.h"` | packet header/type |
| 23 | `"shm_def.h"` | image/lidar SHM slot |
| 24 | `"frag_queue.h"` | queue pop |
| 25 | `"bridge_ctx.h"` | `ReasmCtx` |
| 26 | `"bridge_api.h"` | frame/drop event/metrics |
| 27 | `"utils.h"` | `now_us()` |

## 주요 상수

| 이름 | 의미 |
|---|---|
| `REASM_SLOTS` | 동시에 조립 가능한 frame 수 |
| `REASM_BUF_MAX` | frame 조립 buffer 최대 크기 |
| `REASM_TIMEOUT_US` | 미완성 frame 폐기 timeout |

## 구조체: `ReasmSlot`

| 필드 | 의미 |
|---|---|
| `in_use` | slot 사용 여부 |
| `type` | image/lidar |
| `frame_id` | frame id |
| `frags_recv` | 받은 fragment 수 |
| `frags_total` | 전체 fragment 수 |
| `timestamp_us` | 송신 timestamp |
| `first_recv_us` | 첫 fragment 수신 시각 |
| `total_size` | 현재까지 조립된 frame 크기 |
| `buf` | 조립 buffer |
| `frag_received` | 중복 fragment 방지 bitmap 역할 |

## 함수 설명

### `expire_stale(ReasmCtx *ctx, ReasmSlot *slots)`

기능:
- timeout이 지난 미완성 frame을 폐기한다.
- drop event와 drop counter를 API로 기록한다.

개념:
- UDP fragment 하나가 유실되면 frame은 완성되지 않는다. 무한히 기다리면 slot이 고갈되므로 timeout 폐기가 필요하다.

### `find_slot(ReasmSlot *slots, uint8_t type, uint32_t frame_id)`

기능:
- 같은 type/frame id로 조립 중인 slot을 찾는다.

반환:
- 찾으면 `ReasmSlot*`, 없으면 `NULL`.

### `alloc_slot(ReasmSlot *slots)`

기능:
- 비어 있는 재조립 slot을 찾는다.

### `commit_image(ReasmCtx *ctx, SharedData *shm, ReasmSlot *rs)`

기능:
- 완성된 image frame을 SHM image slot에 복사한다.
- `img_ready_idx`를 atomic store한다.
- `img_sem`을 post해서 Qt GUI를 깨운다.
- frame ready metric을 API에 기록한다.

파라미터:
- `ctx`: robot id와 API pointer.
- `shm`: publish 대상 SHM.
- `rs`: 완성된 frame data.

### `commit_lidar(ReasmCtx *ctx, SharedData *shm, ReasmSlot *rs)`

기능:
- 완성된 lidar payload를 해석한다.
- payload 첫 4 byte는 point count다.
- 이후 float x/y/z/intensity 배열을 SHM `LidarSlot`으로 복사한다.
- ready index와 semaphore를 갱신한다.

검증:
- count가 `LIDAR_MAX_PTS`보다 크면 clipping.
- payload 크기가 필요한 크기보다 작으면 overread 방지 후 return.

### `reassembly_shm_thread(void *arg)`

기능:
- queue에서 packet을 계속 pop한다.
- stale frame을 정리한다.
- 새 frame이면 slot을 할당한다.
- fragment index, metadata, offset을 검증한다.
- payload를 조립 buffer에 복사한다.
- 모든 fragment가 모이면 image/lidar commit 함수 호출.

## 수업 포인트

- network input은 항상 offset/length를 검증해야 한다.
- fragment가 중복 도착할 수 있으므로 `frag_received`가 필요하다.
- 첫 fragment가 아닌데 slot이 없으면 앞 fragment가 유실된 것이므로 frame을 버린다.
- frame 완성 후 SHM publish는 “data copy → ready index store → semaphore post” 순서가 중요하다.

## 실습 질문

1. `payload_offset` 검증이 없으면 어떤 메모리 오류가 가능할까?
2. `frag_total`이 frame 중간에 바뀌면 왜 frame을 폐기해야 할까?
3. reader가 오래된 slot을 읽는 중 writer가 overwrite할 가능성을 줄이려면 어떤 개선이 필요할까?
