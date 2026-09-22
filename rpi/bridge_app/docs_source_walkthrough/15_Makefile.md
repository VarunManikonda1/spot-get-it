# 15. `Makefile` 수업 자료

## 이 파일의 역할

`Makefile`은 `bride_app_codex`의 BridgeDaemon을 빌드하는 규칙이다. 일반 빌드, AddressSanitizer 빌드, ThreadSanitizer 빌드를 제공한다.

## 개념 설명: Build Target

C 프로젝트는 여러 `.c` 파일을 하나의 실행 파일로 link한다. 이 프로젝트는 아직 단순 Makefile 구조다.

```text
*.c + *.h
  -> gcc compile/link
  -> bridge_daemon
```

## 변수 설명

| 라인 | 변수 | 의미 |
|---:|---|---|
| 1 | `CC = gcc` | C compiler |
| 2 | `CFLAGS` | 최적화, warning, pthread, GNU extension |
| 3 | `LIBS` | link library. pthread, rt, math |
| 5 | `SRCS` | 빌드에 포함할 source list |
| 6 | `TARGET` | 생성 binary 이름 |

## target 설명

### 기본 target: `bridge_daemon`

```bash
make
```

기능:
- `SRCS` 전체를 compile/link해서 `bridge_daemon` 생성.

의존 파일:
- source files
- `shm_def.h`, `proto.h`, `frag_queue.h`, `bridge_ctx.h`, `cmd_dispatch.h`, `bridge_api.h`, `utils.h`

### `asan`

```bash
make asan
```

기능:
- AddressSanitizer를 켜고 `bridge_daemon_asan` 생성.

용도:
- heap overflow, use-after-free, memory leak 계열 문제 탐지.

### `tsan`

```bash
make tsan
```

기능:
- ThreadSanitizer를 켜고 `bridge_daemon_tsan` 생성.

용도:
- data race 탐지.

주의:
- ASAN과 TSAN은 보통 동시에 쓰지 않는다. 그래서 별도 target으로 분리되어 있다.

### `clean`

```bash
make clean
```

기능:
- 생성 binary 삭제.

## 수업 포인트

- header를 dependency에 넣어야 header 변경 시 rebuild된다.
- sanitizer build는 현장 배포용이 아니라 디버깅용이다.
- `-pthread`는 compile flag와 link flag 양쪽에 들어가는 것이 안전하다.

## 실습 질문

1. `bridge_api.c`를 `SRCS`에서 빼면 어떤 link error가 날까?
2. ASAN과 TSAN을 분리한 이유는 무엇인가?
3. source가 많아지면 Makefile을 어떻게 개선할 수 있을까?
