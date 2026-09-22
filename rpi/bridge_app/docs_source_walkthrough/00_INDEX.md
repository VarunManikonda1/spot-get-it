# bride_app_codex 파일별 수업 자료 목차

작성일: 2026-04-28

이 폴더는 `bride_app_codex`의 각 파일을 수업 자료처럼 따로 설명한다. 단순히 코드가 무엇을 하는지만 적지 않고, 왜 이런 구조가 필요한지, 통신 미들웨어 관점에서 어떤 개념과 연결되는지도 함께 정리했다.

## 읽는 순서

1. `01_proto_h.md`  
   UDP packet, command type, payload ABI를 먼저 이해한다.

2. `02_shm_def_h.md`  
   Qt GUI와 daemon이 공유하는 shared memory layout을 이해한다.

3. `03_bridge_ctx_h.md`  
   thread context와 runtime table이 어떻게 연결되는지 본다.

4. `04_bridge_api_h.md`, `05_bridge_api_c.md`  
   모든 plane이 동일 API로 state/event/command를 다루는 구조를 이해한다.

5. `06_frag_queue_h.md`, `07_reassembly_shm_c.md`  
   image/lidar fragment가 queue를 거쳐 SHM frame으로 publish되는 sensor plane을 본다.

6. `08_bridge_main_c.md`  
   daemon lifecycle, SHM 생성, thread orchestration을 본다.

7. `09_jetson_rx_c.md`  
   Jetson UDP 수신, packet validation, robot별 dispatch를 본다.

8. `10_jetson_tx_c.md`  
   Qt GUI command path와 SHM command queue 소비 구조를 본다.

9. `11_pc_link_c.md`  
   원격 PC command/status gateway를 본다.

10. `12_protocol_timer_c.md`  
    heartbeat와 command timeout polling 구조를 본다.

11. `13_cmd_dispatch.md`, `14_utils_h.md`, `15_Makefile.md`, `16_README_BRIDGE_API.md`  
    보조 파일과 빌드/문서를 본다.

## 전체 아키텍처 한 장 요약

```text
Jetson UDP:9000
  -> jetson_rx.c
     -> ODOM: bridge_api_update_odom()
     -> ACK : bridge_api_handle_ack()
     -> IMAGE/LIDAR: FragQueue
          -> reassembly_shm.c
          -> SharedData image/lidar slots

Qt GUI
  -> /robot_bridge_N SharedData.cmd_queue
  -> jetson_tx.c
  -> bridge_api_send_command()
  -> Jetson UDP:9001

Remote PC
  -> UDP:9002 command
  -> pc_link.c
  -> bridge_api_send_command()

BridgeDaemon
  -> protocol_timer.c heartbeat
  -> pc_link.c PcStatusPacketV2
  -> SharedData RobotState/EventLog/Metrics
```

## 수업에서 강조할 개념

- UDP는 빠르지만 유실될 수 있다. 그래서 command plane에는 ACK, timeout, retry 개념이 필요하다.
- 대용량 image/lidar는 socket으로 GUI에 직접 밀어 넣기보다 SHM triple buffer가 더 적합하다.
- 작은 상태값은 `RobotState`처럼 snapshot model로 만들면 GUI가 단순해진다.
- 이벤트는 “최신 상태”가 아니라 “시간순 기록”이므로 ring buffer가 맞다.
- signal handler 안에서는 mutex를 잡으면 안 된다. 따라서 atomic stop flag만 set하고 일반 thread context에서 정리한다.
- GUI와 C daemon이 같은 구조체를 공유하면 ABI version/magic/size 검증이 중요하다.
