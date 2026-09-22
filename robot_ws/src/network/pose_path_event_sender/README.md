# pose_path_event_sender

## 역할

Jetson에서 ROS2 토픽을 구독하여 RPi5 BridgeDaemon으로 UDP 전송한다. 다중 로봇 지원.

```
/localization/pose                       ──┐
/planning/global_path/<robot_name> × N   ──┼──▶ pose_path_event_sender_node ──▶ UDP:9000 ──▶ RPi5
/perception/person_detected/<robot_name> ──┘
```

---

## Subscribe Topics

| 토픽 | 메시지 타입 | QoS | 설명 |
|---|---|---|---|
| `/localization/pose` | `robot_interfaces/msg/LocalizedRobotPose` | BEST_EFFORT, VOLATILE, depth=1 | 전체 로봇 위치 공통 토픽. 메시지 내 `robot_id` 문자열로 로봇 식별 |
| `/planning/global_path/<robot_name>` | `robot_interfaces/msg/GlobalPathWaypoints` | RELIABLE, TRANSIENT_LOCAL, depth=1 | 로봇별 글로벌 경로 |
| `/perception/person_detected/<robot_name>` | `std_msgs/msg/Bool` | RELIABLE, VOLATILE, depth=1 | 로봇별 사람 감지 이벤트 |

---

## UDP 송신 패킷

| 패킷 타입 | 크기 | 설명 |
|---|---|---|
| `PKT_TYPE_ODOM (0x03)` | 52B | PktHeader(24B) + OdomPayload(28B) |
| `PKT_TYPE_GLOBAL_PATH (0x0B)` | 668B | PktHeader(24B) + GlobalPathPayload(644B) |
| `PKT_TYPE_EVENT (0x07)` | 128B | PktHeader(24B) + EventPayload(104B) |

- `GlobalPathPayload` waypoint 최대 수: 40개, 초과 시 전체 경로 구간에서 균등 샘플링
- global path는 새 토픽 수신 시 즉시 전송하고, 마지막 수신 path를 `global_path_send_hz`로 계속 재전송
- `person_detected`: severity=CRITICAL(4), event_type=VICTIM_DETECTED(8)
- `/localization/pose`에 속도 정보 없음 → `vx / vy / omega = 0`

---

## Parameters

| 파라미터 | 타입 | 기본값 | 설명 |
|---|---|---|---|
| `robots` | string[] | `["spot_01:0"]` | 로봇 목록. `"robot_name:robot_id"` 형식 |
| `rpi5_ip` | string | `"192.168.0.13"` | BridgeDaemon 수신 IP |
| `bridge_port` | int | `9000` | BridgeDaemon 수신 포트 |
| `pose_send_hz` | double | `10.0` | odom UDP 전송 주파수 상한 (Hz) |
| `global_path_send_hz` | double | `1.0` | 마지막 global path UDP 재전송 주파수 (Hz), 0 이하이면 재전송 비활성화 |

---

## 실행 명령어

```bash
# 빌드
cd ~/robot_ws
colcon build --packages-select pose_path_event_sender
source install/setup.bash

# 기본 실행
ros2 launch pose_path_event_sender pose_path_event_sender.launch.py

# RPi5 IP 변경
ros2 launch pose_path_event_sender pose_path_event_sender.launch.py rpi5_ip:=192.168.0.100
```

---

## 로봇 추가/변경

`config/pose_path_event_sender.yaml` 에서 `robots` 목록 수정:

```yaml
robots:
  - "spot_01:0"
  - "spot_02:1"
  - "spot_03:2"
  - "spot_04:3"   # 추가
```