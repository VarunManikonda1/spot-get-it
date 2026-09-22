# BridgeDaemon 데이터 흐름 쉬운 비유 보고서

이 시스템은 여러 로봇이 보내는 택배를 RPi5의 `BridgeDaemon` 물류센터가 받아서 Qt 화면과 PC, Jetson으로 나눠 보내는 구조라고 보면 된다.

## 1. 등장인물

| 실제 구성 | 쉬운 비유 | 역할 |
|---|---|---|
| Jetson | 로봇 안의 현장 기사 | 카메라, 라이다, 위치 정보를 보냄 |
| BridgeDaemon | 중앙 물류센터 | 받은 물건을 분류하고 다시 배달함 |
| Qt GUI | 관제실 모니터 | 화면에 이미지, 라이다, 위치, 이벤트를 보여줌 |
| PC | 외부 관제센터 | 명령을 보내고 상태 요약을 받음 |
| Shared Memory | 관제실 앞 진열대 | Qt가 빠르게 집어 볼 수 있게 최신 데이터를 올려둠 |
| UDP | 빠른 오토바이 배송 | 빠르지만 일부 조각이 사라질 수 있음 |
| SHM command queue | 관제실 앞 명령 투입함 | Qt가 BridgeDaemon에게 명령을 넣는 통로 |

## 2. 전체 그림

```text
Jetson 현장 기사
  -> UDP 9000으로 물건 보냄
  -> BridgeDaemon 물류센터가 받음
  -> 이미지/라이다는 조립실로 보냄
  -> 오돔은 바로 장부에 기록
  -> Qt는 진열대(SHM)에서 최신 데이터 확인

Qt/PC가 명령을 보냄
  -> BridgeDaemon이 Jetson 주소를 찾아
  -> UDP 9001로 로봇에게 전달
```

## 3. 이미지 흐름: 큰 사진 택배

이미지는 한 번에 들고 오기 큰 사진이다. 그래서 Jetson은 사진을 여러 조각으로 잘라서 보낸다.

```text
사진 조각들
  -> 접수 창구 jetson_rx_thread
  -> 로봇별 바구니 FragQueue
  -> 조립실 reassembly_shm_thread
  -> 완성 사진
  -> SHM 이미지 진열대
  -> Qt 화면
```

실제 코드로 보면:

- 접수 창구: `jetson_rx.c`의 `jetson_rx_thread()`
- 조각 바구니: `frag_queue.h`의 `frag_queue_push()`, `frag_queue_pop()`
- 조립실: `reassembly_shm.c`의 `reassembly_shm_thread()`
- 완성 사진 올리기: `reassembly_shm.c`의 `commit_image()`
- 진열대: `shm_def.h`의 `img_slots`, `img_ready_idx`, `img_sem`

핵심 비유:

- `FragQueue`는 “조각 택배 임시 바구니”다.
- `ReasmSlot`은 “한 장의 사진을 맞추는 작업대”다.
- `img_slots[3]`는 “최신 사진 진열대 3칸”이다.
- `img_ready_idx`는 “지금 가장 최신 사진이 몇 번 칸에 있는지 적은 표지판”이다.
- `img_sem`은 “새 사진 올라왔으니 보러 오라는 호출벨”이다.

## 4. 라이다 흐름: 점 구름 택배

라이다도 이미지처럼 크다. 그래서 여러 조각으로 오고, 조립실에서 다시 하나로 맞춘다.

```text
라이다 조각들
  -> jetson_rx_thread
  -> FragQueue
  -> reassembly_shm_thread
  -> commit_lidar()
  -> SHM 라이다 진열대
  -> Qt 화면
```

실제 코드로 보면:

- 라이다 접수: `jetson_rx.c`의 `case PKT_TYPE_LIDAR`
- 조립: `reassembly_shm.c`의 `reassembly_shm_thread()`
- 완성 데이터 올리기: `reassembly_shm.c`의 `commit_lidar()`
- 진열대: `shm_def.h`의 `lidar_slots`, `lidar_ready_idx`, `lidar_sem`

라이다 payload는 이렇게 생겼다.

```text
점 개수 count
점1: x, y, z, intensity
점2: x, y, z, intensity
...
```

비유하면, 라이다는 사진이 아니라 “공간에 찍힌 점 좌표 묶음”이다. `commit_lidar()`는 택배 상자 안의 점들을 꺼내서 `x`, `y`, `z`, `intensity` 선반에 나눠 담는다.

## 5. 오돔 흐름: 작은 위치 메모

오돔은 로봇의 현재 위치와 속도다. 데이터가 작아서 조각낼 필요가 없다.

```text
Jetson
  -> 위치 메모 한 장
  -> jetson_rx_thread
  -> handle_odom()
  -> bridge_api_update_odom()
  -> SHM 장부에 바로 기록
```

실제 코드로 보면:

- 수신: `jetson_rx.c`의 `jetson_rx_thread()`
- 오돔 처리: `jetson_rx.c`의 `handle_odom()`
- 공유메모리 기록: `bridge_api.c`의 `bridge_api_update_odom()`
- 저장 위치: `shm_def.h`의 `odom_x`, `odom_y`, `odom_theta`, `odom_vx`, `odom_vy`, `odom_omega`, `state`

비유하면, 이미지와 라이다는 “큰 택배”라서 조립실로 가고, 오돔은 “짧은 메모”라서 접수원이 바로 장부에 적는다.

## 6. Qt 명령 흐름: 관제실에서 로봇에게 지시

Qt GUI가 정지, 이동 같은 명령을 보내면 BridgeDaemon이 받아서 Jetson에게 전달한다.

```text
Qt GUI
  -> SHM 명령 투입함 /robot_bridge_N.cmd_queue
  -> jetson_tx_thread
  -> shm_cmd_queue_pop()
  -> send_cmd_to_jetson()
  -> bridge_api_send_command()
  -> UDP 9001
  -> Jetson
```

실제 코드로 보면:

- 명령 투입함 만들기: `bridge_main.c`의 `shm_cmd_queue_init()`
- Qt 명령 넣기: `disaster_control_qt/src/bridgecommandclient.cpp`의 `pushCommandToShm()`
- 명령 읽기: `jetson_tx.c`의 `shm_cmd_queue_pop()`
- 공통 발송 창구: `cmd_dispatch.c`의 `send_cmd_to_jetson()`
- 실제 UDP 송신: `bridge_api.c`의 `bridge_api_send_command()`, `send_legacy_cmd()`

비유하면, Qt는 관제실 직원이고 `cmd_queue`는 관제실 직원이 지시서를 올려두는 명령 투입함이다. `jetson_tx_thread`는 이 투입함을 계속 확인해서 로봇에게 보낸다.

## 7. PC 명령 흐름: 외부 관제센터에서 로봇에게 지시

PC도 로봇에게 명령을 보낼 수 있다. Qt와 차이는 입구만 다르다.

```text
PC
  -> UDP 9002
  -> pc_link_thread
  -> send_cmd_to_jetson()
  -> bridge_api_send_command()
  -> UDP 9001
  -> Jetson
```

실제 코드로 보면:

- PC 통신 창구: `pc_link.c`의 `pc_link_thread()`
- PC UDP socket: `pc_link.c`의 `create_pc_socket()`
- 공통 발송 창구: `cmd_dispatch.c`의 `send_cmd_to_jetson()`
- 실제 Jetson 송신: `bridge_api.c`의 `bridge_api_send_command()`

비유하면, Qt는 관제실 앞 명령 투입함에 지시서를 넣고, PC는 외부 도로 쪽 창구로 명령을 넣는다. 하지만 최종 발송 담당자는 같다.

## 8. Heartbeat 흐름: 살아있다는 정기 안부 전화

BridgeDaemon은 1초마다 Jetson에게 heartbeat를 보낸다.

```text
protocol_timer_thread
  -> 1초마다
  -> send_heartbeat()
  -> bridge_api_send_command()
  -> Jetson
```

실제 코드로 보면:

- 타이머: `protocol_timer.c`의 `protocol_timer_thread()`
- heartbeat 만들기: `protocol_timer.c`의 `send_heartbeat()`
- 발송: `bridge_api.c`의 `bridge_api_send_command()`

비유하면, 물류센터가 현장 기사에게 “나 아직 살아있고 연결되어 있어”라고 1초마다 보내는 안부 전화다.

## 9. ACK 흐름: 명령 받았다는 영수증

정지나 비상정지 같은 중요한 명령은 Jetson이 “받았다”는 영수증을 보내야 한다.

```text
Jetson
  -> UDP 9000, CMD_ACK
  -> jetson_rx_thread
  -> bridge_api_handle_ack()
  -> 대기 중인 명령 목록에서 제거
```

실제 코드로 보면:

- ACK 접수: `jetson_rx.c`의 `case PKT_TYPE_CMD_ACK`
- ACK 처리: `bridge_api.c`의 `bridge_api_handle_ack()`
- timeout/retry 확인: `bridge_api.c`의 `bridge_api_poll_timeouts()`

비유하면, 중요한 택배는 “배송 완료 서명”이 필요하다. 서명이 오면 대기 목록에서 지우고, 너무 오래 안 오면 다시 보낸다.

## 10. PC 상태 송신 흐름: 외부 관제센터에 보내는 요약 보고

PC가 한 번이라도 BridgeDaemon에게 말을 걸면, BridgeDaemon은 PC 주소를 기억한다. 그 다음 1초마다 전체 로봇 상태를 PC로 보낸다.

```text
pc_link_thread
  -> 1초마다
  -> send_status_all()
  -> bridge_api_snapshot_status()
  -> UDP로 PC에 상태 전송
```

실제 코드로 보면:

- PC 주소 기억: `pc_link.c`의 `pc_link_thread()`
- 상태 패킷 보내기: `pc_link.c`의 `send_status_all()`
- 상태 요약 만들기: `bridge_api.c`의 `bridge_api_snapshot_status()`

비유하면, 외부 관제센터가 물류센터에 한 번 연락처를 남기면 물류센터가 1초마다 “현재 각 로봇 위치, 연결 상태, drop 수, FPS” 같은 요약 보고서를 보내준다.

## 11. 연결 감시 흐름: 현장 기사가 조용해졌는지 확인

모든 정상 패킷이 들어오면 `bridge_api_note_rx()`가 packet count를 올린다. 메인 스레드의 watchdog은 1초마다 이 숫자가 늘었는지 본다.

```text
패킷 수신
  -> pkt_count 증가
  -> watchdog_loop가 1초마다 확인
  -> 3초 동안 증가 없으면 연결 끊김 처리
```

실제 코드로 보면:

- 수신 표시: `bridge_api.c`의 `bridge_api_note_rx()`
- 감시: `bridge_main.c`의 `watchdog_loop()`
- 이벤트 기록: `bridge_api.c`의 `bridge_api_publish_event()`

비유하면, 현장 기사에게서 3초 동안 아무 연락도 안 오면 물류센터가 “이 기사 연결 끊김”으로 장부를 바꾸는 것이다.

## 12. 한눈에 보는 데이터별 비유표

| 데이터 | 비유 | 입구 | 중간 처리 | 출구 |
|---|---|---|---|---|
| 이미지 | 큰 사진 택배 | UDP 9000 | 조각 바구니 + 조립실 | SHM 이미지 진열대 |
| 라이다 | 점 좌표 묶음 택배 | UDP 9000 | 조각 바구니 + 조립실 | SHM 라이다 진열대 |
| 오돔 | 작은 위치 메모 | UDP 9000 | 바로 장부 기록 | SHM odom/state |
| Qt 명령 | 관제실 내부 지시서 | SHM command queue | 공통 발송 창구 | UDP 9001 |
| PC 명령 | 외부 관제 지시서 | UDP 9002 | 공통 발송 창구 | UDP 9001 |
| Heartbeat | 안부 전화 | 타이머 스레드 | 공통 발송 창구 | UDP 9001 |
| ACK | 배송 완료 영수증 | UDP 9000 | pending 목록 정리 | event/state 갱신 |
| PC 상태 | 정기 요약 보고서 | 내부 상태 snapshot | 상태 패킷 작성 | UDP로 PC 송신 |
| Event log | 물류센터 사건 장부 | 내부 함수 호출 | ring buffer 기록 | Qt가 SHM에서 확인 |

## 13. 아직 연결만 정의된 통로

`proto.h`에는 `STATE`, `EVENT`, `MISSION`, `HEALTH`, `CAPABILITY` 같은 이름표가 이미 있다. 하지만 현재 수신 코드에서는 이 이름표를 가진 택배를 어디로 보낼지 정해져 있지 않다.

즉, 이름표는 만들어져 있지만 분류 규칙은 아직 작성되지 않은 상태다. 실제 처리를 추가하려면 `jetson_rx.c`의 `jetson_rx_thread()` switch 문에 각 타입별 `case`를 추가하고, `bridge_api.c` 또는 새 모듈에서 SHM/state/event로 반영하는 함수가 필요하다.
