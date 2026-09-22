# Qt 명령 수신부터 Jetson 패킷 송신까지

이 문서는 `bride_app_codex_indexed` BridgeDaemon이 Qt GUI에서 명령 데이터를 받아 연결된 Jetson으로 UDP command packet을 보내는 경로를 코드 기준으로 정리한다.

## 1. 전체 흐름

```text
Qt UI button
  -> MainWindow::sendCommand()
  -> ShmMonitor::sendCommand()
  -> RobotShmConnection::sendCommand()
  -> /robot_bridge_N SharedData.cmd_queue
  -> jetson_tx_thread()
  -> shm_cmd_queue_pop_highest()
  -> send_cmd_to_jetson()
  -> bridge_api_send_command()
  -> send_legacy_cmd()
  -> UDP sendto(dst_ip, port 9001)
  -> Jetson
```

핵심은 Qt가 BridgeDaemon에 직접 socket을 열어 보내지 않고, 같은 라즈베리파이 안의 POSIX shared memory queue에 `ShmCmdEntry`를 넣는다는 점이다. BridgeDaemon의 `jetson_tx_thread()`가 이 queue를 계속 확인하다가 명령을 꺼내 Jetson UDP packet으로 변환해서 보낸다.

## 2. 공유 메모리 준비

BridgeDaemon 시작 시 `bridge_main.c`가 로봇 수만큼 POSIX SHM을 만든다.

- 이름: `/robot_bridge_0`, `/robot_bridge_1`, ...
- 구조체: `SharedData`
- 명령 queue 위치: `SharedData.cmd_queue`
- queue 크기: `SHM_CMD_QUEUE_SIZE = 64`

초기화 순서:

1. `shm_init_one(robot_id)`가 `shm_unlink()`, `shm_open(O_CREAT | O_RDWR)`, `ftruncate(sizeof(SharedData))`, `mmap(MAP_SHARED)`를 수행한다.
2. `pthread_mutexattr_setpshared(PTHREAD_PROCESS_SHARED)`로 process-shared mutex attribute를 만든다.
3. `shm_cmd_queue_init(&shm->cmd_queue, &cmd_mu_attr)`가 queue mutex와 atomic index들을 초기화한다.
4. `bridge_api_init_shm()`가 SHM header, state, metrics 기본값을 넣는다.

관련 코드:

- `bridge_main.c`: `shm_init_one()`
- `shm_def.h`: `SharedData`, `ShmCmdQueue`, `ShmCmdEntry`
- `shm_cmd_queue.c`: `shm_cmd_queue_init()`

## 3. Qt가 명령을 queue에 넣는 방식

Qt 쪽 명령 버튼은 `MainWindow::sendCommand()`로 모인다. 단일 로봇 선택이면 선택된 `robot_id` 하나에 보내고, 전체 로봇 선택이면 `0..m_robotCount-1`을 순회하면서 각 로봇의 SHM queue에 같은 명령을 넣는다.

Qt 호출 경로:

```text
MainWindow::sendCommand(commandType, vx, vy, omega)
  -> m_monitor.sendCommand(robotId, commandType, vx, vy, omega)
  -> ShmMonitor::sendCommand()
  -> RobotShmConnection::sendCommand()
```

`RobotShmConnection::sendCommand()`는 다음 일을 한다.

1. `/robot_bridge_N` 이름으로 `shm_open(O_RDWR)`를 호출한다.
2. `mmap(PROT_READ | PROT_WRITE, MAP_SHARED)`로 `SharedData`를 붙인다.
3. `shm_magic`, `shm_version`, `shared_data_size`를 검사해서 Qt와 BridgeDaemon의 ABI가 맞는지 확인한다.
4. `ShmCmdEntry`를 만든다.
5. `pushCommandToShm()`로 `SharedData.cmd_queue`에 push한다.

Qt가 채우는 `ShmCmdEntry`:

```c
entry.cmd.robot_id = m_robotId;
entry.cmd.cmd_type = commandType;
entry.cmd.vx = vx;
entry.cmd.vy = vy;
entry.cmd.omega = omega;
entry.cmd.seq = seq;
entry.priority = (commandType == CMD_TYPE_ESTOP)
    ? CMD_PRIORITY_CRITICAL : CMD_PRIORITY_NORMAL;
entry.flags = (entry.priority >= CMD_PRIORITY_HIGH) ? CMD_FLAG_REQUIRES_ACK : 0;
```

현재 Qt 기준으로 `CMD_TYPE_ESTOP`만 `CMD_PRIORITY_CRITICAL`이고, 그 외 명령은 `CMD_PRIORITY_NORMAL`이다. 따라서 Qt에서 명시적으로 ACK flag가 붙는 명령도 현재는 E-STOP이다. 다만 BridgeDaemon은 STOP, START_MISSION, CANCEL_MISSION도 별도로 ACK 필요 명령으로 승격한다.

관련 코드:

- `disaster_control_qt/src/mainwindow.cpp`: `MainWindow::sendCommand()`
- `disaster_control_qt/src/shmmonitor.cpp`: `ShmMonitor::sendCommand()`
- `disaster_control_qt/src/robotshmconnection.cpp`: `RobotShmConnection::sendCommand()`, `pushCommandToShm()`

## 4. SHM command queue 정책

Queue 구조는 `shm_def.h`의 `ShmCmdQueue`다.

```c
typedef struct {
    pthread_mutex_t mu;
    ATOMIC_INT head;
    ATOMIC_INT tail;
    ATOMIC_INT count;
    ATOMIC_INT write_seq;
    ATOMIC_INT drop_count;
    ShmCmdEntry entries[SHM_CMD_QUEUE_SIZE];
} ShmCmdQueue;
```

Qt producer와 BridgeDaemon consumer가 같은 `pthread_mutex_t mu`를 사용한다. 이 mutex는 BridgeDaemon이 `PTHREAD_PROCESS_SHARED`로 초기화하므로 프로세스가 달라도 같은 lock으로 동작한다.

Queue가 가득 찬 경우 Qt의 `pushCommandToShm()` 동작:

1. 기존 queue에서 가장 낮은 priority entry를 찾는다.
2. 새 명령 priority가 기존 최저 priority보다 낮으면 새 명령을 버리고 실패한다.
3. 새 명령 priority가 같거나 높으면 기존 최저 priority entry를 제거한다.
4. `drop_count`를 증가시키고 새 명령을 tail에 넣는다.

BridgeDaemon의 `shm_cmd_queue_pop_highest()` 동작:

1. queue 전체에서 가장 높은 priority entry를 찾는다.
2. 그 entry를 pop한다.
3. pop한 위치 앞쪽 entry들을 한 칸씩 이동해 ring queue의 순서를 유지한다.

즉, 여러 명령이 쌓이면 BridgeDaemon은 FIFO만 따르지 않고 priority가 높은 명령을 먼저 보낸다.

## 5. BridgeDaemon이 queue를 읽는 방식

`bridge_main.c`는 `jetson_tx_thread()`를 만든다. 이 thread는 송신 전용 UDP socket을 하나 생성하고 모든 로봇의 command queue를 반복해서 본다.

`jetson_tx_thread()` 루프:

```text
while (!stop) {
  bridge_api_poll_timeouts()

  for offset in 0..num_robots-1:
    rid = (next_robot + offset) % num_robots
    q = shm_arr[rid]->cmd_queue
    if shm_cmd_queue_pop_highest(q, &entry):
      send_shm_cmd_entry(entry)
      next_robot = rid + 1
      break

  if no command:
    usleep(5000)
}
```

로봇 간에는 round-robin으로 공정성을 맞추고, 같은 로봇 queue 안에서는 priority가 높은 명령을 먼저 고른다. 명령이 없으면 5ms sleep한다.

`send_shm_cmd_entry()`는 `ShmCmdEntry`의 priority와 flags를 읽어 `send_cmd_to_jetson()`으로 넘긴다.

관련 코드:

- `jetson_tx.c`: `jetson_tx_thread()`, `send_shm_cmd_entry()`
- `shm_cmd_queue.c`: `shm_cmd_queue_pop_highest()`
- `cmd_dispatch.c`: `send_cmd_to_jetson()`

## 6. Jetson 주소 학습

Jetson으로 보내려면 대상 IP 주소가 필요하다. BridgeDaemon은 설정 파일로 Jetson IP를 갖고 있는 구조가 아니라, Jetson에서 들어온 UDP packet의 source address를 보고 주소를 학습한다.

주소 학습 위치:

- `jetson_rx.c`: `learn_or_validate_addr()`
- 저장 위치: `BridgeApi.addr_table`이 참조하는 `JetsonAddrTable`
- key: `robot_id`
- value: `struct sockaddr_in`
- 송신 port: 항상 `JETSON_CMD_PORT = 9001`로 덮어쓴다.

주의할 점:

- 현재 주소 학습은 모든 수신 packet에서 수행되지 않는다.
- `packet_type_updates_command_addr()` 기준으로 `PKT_TYPE_IMAGE`, `PKT_TYPE_LIDAR`, `PKT_TYPE_CMD_ACK`만 command address를 등록/갱신한다.
- `PKT_TYPE_ODOM`, `PKT_TYPE_GLOBAL_PATH`, `PKT_TYPE_PATH_PROGRESS`만 들어온 상태에서는 `addr_table`이 채워지지 않을 수 있다.
- `bridge_api_send_command()`는 `addr_table`에 주소가 없으면 `robot=N address not learned yet` 로그를 남기고 송신을 실패한다.

즉, 여기서 "연결된 Jetson"은 최소한 `addr_table[robot_id]`가 학습된 Jetson을 의미한다. SHM의 `meta.jetson_connected` 상태가 1이어도 command address가 학습되지 않았으면 명령 송신은 실패할 수 있다.

## 7. UDP command packet 생성

최종 송신은 `bridge_api.c`의 `bridge_api_send_command()`가 처리한다.

처리 순서:

1. `cmd->robot_id`가 `num_robots` 범위 안인지 검사한다.
2. `lookup_addr()`로 `JetsonAddrTable`에서 대상 Jetson 주소를 찾는다.
3. `send_legacy_cmd()`로 UDP wire packet을 만든다.
4. `sendto()`로 Jetson의 `UDP 9001`에 전송한다.
5. `metrics.tx_commands`, `metrics.last_tx_us`를 갱신한다.
6. ACK가 필요한 명령이면 pending table에 등록한다.
7. heartbeat가 아닌 명령은 event log에 `EVENT_TYPE_CMD_SENT`를 기록한다.

Wire packet 구조:

```text
[ PktHeader ][ CmdPayload ]
```

`PktHeader` 값:

```c
hdr->type = PKT_TYPE_CMD;          // 0x04
hdr->robot_id = c->robot_id;
hdr->frag_total = 1;
hdr->payload_len = sizeof(CmdPayload);
hdr->frame_id = c->seq;
hdr->timestamp_us = now_us();
```

`CmdPayload` 값:

```c
cmd->cmd_type = c->cmd_type;
cmd->vx = c->vx;
cmd->vy = c->vy;
cmd->omega = c->omega;
cmd->seq = c->seq;
```

송신 대상:

```text
IP   = Jetson에서 수신된 source IP
Port = JETSON_CMD_PORT = 9001
Type = UDP datagram
```

관련 코드:

- `bridge_api.c`: `bridge_api_send_command()`, `lookup_addr()`, `send_legacy_cmd()`
- `proto.h`: `PktHeader`, `CmdPayload`, `CmdPacket`, `PKT_TYPE_CMD`, `JETSON_CMD_PORT`

## 8. ACK와 재전송

ACK 필요 여부는 `bridge_api_send_command()`에서 최종 결정한다.

ACK가 필요한 경우:

- `ShmCmdEntry.flags`에 `CMD_FLAG_REQUIRES_ACK`가 있는 경우
- 또는 command type이 아래 중 하나인 경우
  - `CMD_TYPE_ESTOP`
  - `CMD_TYPE_STOP`
  - `CMD_TYPE_START_MISSION`
  - `CMD_TYPE_CANCEL_MISSION`

ACK 필요 명령은 `PendingCommand` table에 등록된다. timeout은 `COMMAND_ACK_TIMEOUT_US = 250000`으로 250ms다.

재전송 횟수:

- `priority >= CMD_PRIORITY_CRITICAL`: 3회
- 그 외 ACK 필요 명령: 1회

`bridge_api_poll_timeouts()`는 `jetson_tx_thread()` 루프마다 호출된다. deadline이 지난 pending command가 있으면 `send_legacy_cmd()`로 같은 `CmdPacket`을 다시 보낸다. 재시도 횟수를 다 쓰면 `EVENT_TYPE_CMD_TIMEOUT` event를 기록한다.

Jetson에서 ACK가 오면:

1. `jetson_rx_thread()`가 `PKT_TYPE_CMD_ACK` packet을 받는다.
2. `bridge_api_handle_ack()`가 `PendingCommand`를 찾는다.
3. `cmd.seq == ack->seq` 또는 `command_id == ack->command_id`가 맞으면 pending을 해제한다.
4. `metrics.ack_packets`, `state.last_cmd_ack_us`, `state.link_rtt_ms`를 갱신한다.

현재 legacy command wire format은 `CommandEnvelope`를 사용하지 않고 `PktHeader + CmdPayload`만 보낸다. 따라서 priority, flags, command_id는 Jetson으로 전송되지 않고 BridgeDaemon 내부 제어용으로만 쓰인다. ACK 매칭은 Jetson이 돌려주는 `seq` 기준으로 맞는 것이 현실적인 경로다.

## 9. 실패 지점 체크리스트

명령이 Jetson에 가지 않을 때는 아래 순서로 확인한다.

1. BridgeDaemon이 실행 중인지 확인한다.
   - 로그에 `Qt 명령 IPC: SHM cmd_queue (/robot_bridge_N)`가 떠야 한다.
2. `/robot_bridge_N` SHM이 존재하고 Qt가 열 수 있는지 확인한다.
   - Qt 실패 메시지 예: `shm_open(/robot_bridge_N): ...`
3. Qt가 `cmd_queue`에 push했는지 확인한다.
   - queue full이면 낮은 priority 명령은 버려질 수 있다.
4. `jetson_tx_thread()`가 살아 있는지 확인한다.
   - 이 thread가 queue를 pop하지 않으면 UDP 송신이 발생하지 않는다.
5. Jetson 주소가 학습됐는지 확인한다.
   - 주소가 없으면 `robot=N address not learned yet` 로그가 나온다.
   - 현재는 IMAGE/LIDAR/CMD_ACK 수신이 주소 학습 트리거다.
6. Jetson이 UDP 9001을 listen하고 있는지 확인한다.
7. ACK 필요 명령인데 ACK가 안 오면 `cmd ack timeout` event와 retry 로그를 확인한다.

## 10. 요약

Qt 명령은 `CmdPacket` 형태로 바로 UDP 송신되지 않는다. 먼저 `ShmCmdEntry`로 감싸져 로봇별 `/robot_bridge_N`의 `SharedData.cmd_queue`에 들어간다. BridgeDaemon의 `jetson_tx_thread()`가 이 queue에서 가장 높은 priority 명령을 pop하고, `bridge_api_send_command()`가 이를 `PktHeader + CmdPayload` UDP packet으로 변환해 학습된 Jetson IP의 `9001` 포트로 보낸다.

