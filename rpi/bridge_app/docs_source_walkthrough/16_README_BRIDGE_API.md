# 16. `README_BRIDGE_API.md` 수업 자료

## 이 파일의 역할

`README_BRIDGE_API.md`는 `bridge_api.h`를 중심으로 한 새 middleware 구조를 짧게 설명하는 개발자용 안내 문서다. 코드 상세 설명보다는 “어떤 경로에서 어떤 API를 호출하는가”에 초점이 있다.

## 문서 내용 설명

### 제목과 목적

문서 첫 부분은 `bridge_api.h`가 Sensor/State/Command/Event/Ops plane이 공유하는 통합 인터페이스라고 설명한다.

핵심 문장:

```text
각 plane의 내부 동작은 달라도 GUI/PC/daemon 내부 코드는 같은 API로 상태, 이벤트, 명령을 다룬다.
```

이것이 이번 구조 변경의 핵심이다.

### 공통 데이터 경로

문서의 “공통 데이터 경로”는 코드 흐름을 한 줄씩 연결한다.

| 경로 | 설명 |
|---|---|
| RX | Jetson UDP packet 수신 후 `bridge_api_note_rx()` |
| Sensor | reassembly 완료 후 `bridge_api_note_frame_ready()` |
| State | odom 수신 후 `bridge_api_update_odom()` |
| Command | GUI/PC command가 `bridge_api_send_command()`로 Jetson 송신 |
| ACK | Jetson ACK가 `bridge_api_handle_ack()`로 pending table 처리 |
| Event | 모든 plane이 `bridge_api_publish_event()`로 event 기록 |

### SHM ABI

문서는 `SharedData` v2가 기존 sensor slot을 유지하면서 뒤쪽에 다음을 추가했다고 설명한다.

- `shm_magic`
- `shm_version`
- `shared_data_size`
- `RobotState`
- `EventLog`
- `BridgeMetrics`

GUI는 attach 시 이 값을 먼저 확인해야 한다.

### Command API

문서는 기존 `CmdPacket`을 유지한다고 설명한다. 즉 Qt/PC command sender가 당장 완전히 바뀌지 않아도 된다.

하지만 daemon 내부는 다음 API로 통일된다.

```c
bridge_api_send_command(api, udp_fd, &cmd, priority, flags, tag);
```

## 수업 포인트

- README는 코드를 처음 보는 사람에게 길잡이 역할을 한다.
- 상세 line-by-line 문서와 README는 목적이 다르다.
- architecture README는 “왜 이렇게 만들었는지”를 짧게 설명해야 한다.

## 실습 질문

1. README에 실행 방법과 API 설명을 모두 넣으면 어떤 장단점이 있을까?
2. SHM ABI 변경 사항을 README에 적어야 하는 이유는 무엇인가?
3. Jetson 측이 `CommandEnvelope`를 지원하게 되면 README의 어느 부분을 고쳐야 할까?
