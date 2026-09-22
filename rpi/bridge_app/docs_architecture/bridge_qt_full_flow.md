# BridgeDaemon + Qt GUI 전체 흐름도

이미지 파일:

- `docs_architecture/bridge_qt_full_flow.svg`

관련 설계 문서:

- `docs_architecture/qt_bridgeclient_encapsulation_plan.md`

## 그림 읽는 법

이 그림은 `bride_app_codex`의 BridgeDaemon과 Qt GUI가 어떻게 연결되는지 한 장으로 정리한 것이다.

## 핵심 흐름

1. Jetson은 UDP `9000`으로 image/lidar fragment, odom, command ACK를 보낸다.
2. `jetson_rx.c`가 packet을 받고 `proto_validate_header()`로 검증한다.
3. odom과 ACK는 `bridge_api.c`로 바로 들어간다.
4. image/lidar는 `FragQueue`를 거쳐 `reassembly_shm.c`에서 frame으로 조립된다.
5. 완성된 image/lidar frame은 `SharedData v2`의 triple buffer에 publish된다.
6. Qt GUI는 `/robot_bridge_N` SHM을 attach해서 image/lidar/state/event를 읽는다.
7. Qt GUI command는 로봇별 SHM `/robot_bridge_N`의 `SharedData.cmd_queue`에 들어가고, `jetson_tx.c`가 이를 읽는다.
8. Remote PC command는 UDP `9002`로 `pc_link.c`에 들어온다.
9. GUI/PC/heartbeat command는 모두 `bridge_api_send_command()`를 거쳐 Jetson UDP `9001`로 나간다.
10. `protocol_timer.c`는 1Hz heartbeat와 ACK timeout/retry를 담당한다.

## 수업용 핵심 개념

- Sensor plane은 대용량 데이터라 SHM triple buffer를 쓴다.
- State plane은 GUI가 바로 읽을 수 있도록 `RobotState` snapshot을 만든다.
- Event plane은 시간순 기록이 필요하므로 `EventLog` ring buffer를 쓴다.
- Command plane은 Qt/PC/heartbeat가 같은 API를 사용해 ACK/retry 정책을 공유한다. Qt -> BridgeDaemon 구간은 SHM command queue를 사용한다.
- Ops plane은 `Metrics`와 watchdog으로 연결 상태와 drop/retry 상태를 관측한다.

## 열어보는 방법

```bash
cd /home/pi/robot_project/bride_app_codex
xdg-open docs_architecture/bridge_qt_full_flow.svg
```

GUI 환경이 없으면 브라우저에서 SVG 파일을 직접 열면 된다.
