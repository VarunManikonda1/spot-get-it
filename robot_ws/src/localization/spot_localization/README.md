# spot_localization 패키지 README

## 1. 개요

`spot_localization` 패키지는 Spot Micro 기반 4족 보행 로봇의 **gait/IMU 기반 local odometry**와 **공통좌표계 기준 localization pose**를 생성하기 위한 ROS 2 패키지이다.

현재 Localization MVP의 목표는 외부 보정원 없이 STM에서 전달되는 보행 상태, gait phase, IMU yaw 데이터를 이용하여 다음 정보를 안정적으로 제공하는 것이다.

```text
1. odom 기준 로봇 위치 추정
2. odom -> base_link TF publish
3. mission_map -> odom static TF publish
4. mission_map 기준 최종 로봇 pose publish
```

최종적으로 상위 판단 계층, 관제 시스템, Planning 계층은 `/localization/pose` 토픽을 통해 로봇의 현재 위치와 방향을 바로 사용할 수 있다.

---

## 2. 전체 TF 구조

현재 Localization 패키지에서 구성하는 TF tree는 다음과 같다.

```text
mission_map
  └── odom
       └── base_link
            └── laser_frame
```

각 frame의 의미는 다음과 같다.

| Frame         | 의미                                     | 담당 노드                                         |
| ------------- | -------------------------------------- | --------------------------------------------- |
| `mission_map` | 관제/임무 기준 공통 좌표계                        | `mission_tf_node`                             |
| `odom`        | 로봇 시작 위치를 기준으로 하는 local odometry frame | `mission_tf_node`, `gait_odom_estimator_node` |
| `base_link`   | 로봇 본체 중심 frame                         | `gait_odom_estimator_node`                    |
| `laser_frame` | LiDAR 센서 frame                         | LiDAR base/static TF launch                   |

현재 Localization 패키지가 직접 publish하는 TF는 다음 2개이다.

| TF                    | 타입         | 담당 노드                      | 설명                            |
| --------------------- | ---------- | -------------------------- | ----------------------------- |
| `mission_map -> odom` | static TF  | `mission_tf_node`          | 로봇 시작 위치를 mission_map 기준으로 고정 |
| `odom -> base_link`   | dynamic TF | `gait_odom_estimator_node` | gait odometry 기반 로봇 현재 pose   |

`mission_map -> base_link`는 직접 publish되는 TF가 아니라, tf2가 아래 두 transform을 합성하여 계산한다.

```text
mission_map -> odom -> base_link
```

---

## 3. 전체 Localization 파이프라인

### 3.1 Mock 실험용 파이프라인

STM bridge 없이 localization 알고리즘을 검증할 때는 `stm_motion_mock_node`를 사용한다.

```text
stm_motion_mock_node
        ↓
/localization/stm_motion
        ↓
gait_odom_estimator_node
        ↓
/localization/odometry
TF: odom -> base_link

mission_tf_node
        ↓
TF: mission_map -> odom

localization_pose_node
        ↓
/localization/pose
```

### 3.2 실제 STM bridge 연동 파이프라인

팀원 bridge node가 STM 데이터를 `/localization/spot_motion`으로 publish하면, mock node를 제외하고 실제 bridge 데이터를 입력으로 사용한다.

```text
STM
 ↓
SPI bridge node
 ↓
/localization/spot_motion
 ↓
gait_odom_estimator_node
 ↓
/localization/odometry
TF: odom -> base_link

mission_tf_node
 ↓
TF: mission_map -> odom

localization_pose_node
 ↓
/localization/pose
```

---

## 4. 주요 노드 정리

## 4.1 `stm_motion_mock_node`

### 역할

STM bridge가 준비되기 전에 localization 파이프라인을 독립적으로 검증하기 위한 mock 데이터 publish 노드이다.

### 입력

없음.

### 출력

| Topic                      | Type                             | 설명                                    |
| -------------------------- | -------------------------------- | ------------------------------------- |
| `/localization/stm_motion` | `robot_interfaces/msg/StmMotion` | gait odometry 검증용 mock STM motion 데이터 |

### Mock 시나리오 예시

현재 mock node는 다음과 같은 주행 상태를 반복하도록 구성할 수 있다.

```text
0 ~ 2초    STOP
2 ~ 7초    WALK_FORWARD
7 ~ 9초    TURN_LEFT
9 ~ 14초   WALK_FORWARD
14 ~ 16초  STOP
```

이 mock 데이터를 통해 `gait_odom_estimator_node`, `mission_tf_node`, `localization_pose_node`까지 전체 파이프라인을 실제 STM 없이 검증할 수 있다.

---

## 4.2 `gait_odom_estimator_node`

### 역할

STM motion source 데이터를 구독하여 gait phase 진행량과 IMU yaw를 기반으로 odometry를 추정한다.

### 입력

| Topic                       | Type                             | 설명               |
| --------------------------- | -------------------------------- | ---------------- |
| `/localization/stm_motion`  | `robot_interfaces/msg/StmMotion` | mock 실험용 입력      |
| `/localization/spot_motion` | `robot_interfaces/msg/StmMotion` | 실제 STM bridge 입력 |

실제로 어떤 토픽을 구독할지는 `gait_odom.param.yaml`의 `stm_motion_topic` 파라미터 또는 launch override로 결정한다.

### 출력

| Topic / TF               | Type                    | 설명                             |
| ------------------------ | ----------------------- | ------------------------------ |
| `/localization/odometry` | `nav_msgs/msg/Odometry` | odom 기준 base_link pose 및 twist |
| `odom -> base_link`      | TF                      | odom 기준 로봇 현재 위치와 방향           |

### 핵심 로직

STM 메시지에서 다음 값을 사용한다.

```text
total_phase = gait_cycle_count + gait_phase
delta_phase = total_phase - prev_total_phase
```

`motion_state`에 따라 `delta_phase`를 body frame 기준 이동량으로 변환한다.

| Motion State    | 이동 해석                          |
| --------------- | ------------------------------ |
| `WALK_FORWARD`  | `base_link` 기준 `+x`            |
| `WALK_BACKWARD` | `base_link` 기준 `-x`            |
| `STRAFE_LEFT`   | `base_link` 기준 `+y`            |
| `STRAFE_RIGHT`  | `base_link` 기준 `-y`            |
| `TURN_LEFT`     | translation은 0, yaw는 IMU 기반 반영 |
| `TURN_RIGHT`    | translation은 0, yaw는 IMU 기반 반영 |
| `STOP`          | translation 0                  |
| `TRANSITION`    | 보수적으로 translation 0            |
| `UNKNOWN`       | 보수적으로 translation 0            |

body frame 기준 이동량은 현재 yaw를 이용해 odom frame 기준 이동량으로 변환하여 누적한다.

```text
x_odom += dx_body * cos(yaw) - dy_body * sin(yaw)
y_odom += dx_body * sin(yaw) + dy_body * cos(yaw)
```

IMU yaw는 시작 시점의 yaw를 기준으로 상대 yaw로 변환한다.

```text
odom_yaw = normalize(imu_yaw_sign * (imu_yaw_rad - imu_yaw_start_rad) + yaw_offset_rad)
```

---

## 4.3 `mission_tf_node`

### 역할

`mission_map -> odom` static TF를 publish한다.

로봇이 mission_map 기준 어느 위치에서 출발하는지를 정의하는 노드이다.

### 입력

없음.

### 출력

| TF                    | 타입        | 설명                                     |
| --------------------- | --------- | -------------------------------------- |
| `mission_map -> odom` | static TF | odom frame의 시작 위치를 mission_map 기준으로 고정 |

### 예시

`mission_tf.param.yaml`에서 다음과 같이 설정하면:

```yaml
mission_tf_node:
  ros__parameters:
    mission_frame: "mission_map"
    odom_frame: "odom"
    start_x_m: 5.0
    start_y_m: 5.0
    start_z_m: 0.0
    start_yaw_rad: 0.0
```

`odom` frame은 `mission_map` 기준 `(5.0, 5.0, 0.0)` 위치에 고정된다.

---

## 4.4 `localization_pose_node`

### 역할

TF tree에서 `mission_map -> base_link` transform을 조회한 뒤, 관제/상위 판단 계층이 바로 사용할 수 있는 localization pose 메시지로 publish한다.

### 입력

| 입력      | 설명                                 |
| ------- | ---------------------------------- |
| TF tree | `mission_map -> odom -> base_link` |

### 출력

| Topic                | Type                                      | 설명                        |
| -------------------- | ----------------------------------------- | ------------------------- |
| `/localization/pose` | `robot_interfaces/msg/LocalizedRobotPose` | mission_map 기준 최종 로봇 pose |

### 출력 메시지 예시

```yaml
header:
  frame_id: mission_map
robot_id: spot_01
base_frame: base_link
x_m: 5.1500
y_m: 5.1500
z_m: 0.0
yaw_rad: 1.5706
pose:
  position:
    x: 5.1500
    y: 5.1500
    z: 0.0
  orientation:
    z: 0.7070
    w: 0.7071
```

의미는 다음과 같다.

```text
mission_map 기준으로 spot_01의 base_link가
x = 5.15 m
y = 5.15 m
yaw = 약 90도
위치에 있다.
```

---

## 5. 메시지 인터페이스

## 5.1 `robot_interfaces/msg/StmMotion.msg`

STM 또는 STM bridge가 Jetson으로 전달하는 보행/IMU 기반 motion source 데이터이다.

```msg
std_msgs/Header header

uint32 timestamp_ms
uint32 seq

uint8 motion_state

float32 gait_phase
uint32 gait_cycle_count

float32 imu_yaw_rad
float32 gyro_z_rad_s

uint8 STOP=0
uint8 WALK_FORWARD=1
uint8 WALK_BACKWARD=2
uint8 TURN_LEFT=3
uint8 TURN_RIGHT=4
uint8 STRAFE_LEFT=5
uint8 STRAFE_RIGHT=6
uint8 TRANSITION=7
uint8 UNKNOWN=8
```

### 주요 필드 설명

| Field              | 설명                                 |
| ------------------ | ---------------------------------- |
| `timestamp_ms`     | STM 기준 패킷 생성 시간                    |
| `seq`              | 패킷 순번. 누락 확인에 사용                   |
| `motion_state`     | 현재 보행 상태                           |
| `gait_phase`       | 현재 gait cycle 진행률. 일반적으로 0.0 ~ 1.0 |
| `gait_cycle_count` | 완료된 gait cycle 누적 수                |
| `imu_yaw_rad`      | IMU 기준 현재 yaw                      |
| `gyro_z_rad_s`     | z축 각속도. yaw drift 판단 및 twist에 활용   |

---

## 5.2 `robot_interfaces/msg/LocalizedRobotPose.msg`

상위 판단 계층과 관제 시스템이 바로 사용할 수 있는 최종 localization pose 메시지이다.

```msg
std_msgs/Header header

string robot_id
string base_frame

float32 x_m
float32 y_m
float32 z_m
float32 yaw_rad

geometry_msgs/Pose pose
```

### 주요 필드 설명

| Field               | 설명                                                |
| ------------------- | ------------------------------------------------- |
| `header.frame_id`   | pose가 표현되는 기준 좌표계. 현재는 `mission_map`              |
| `robot_id`          | 관제 시스템에서 사용하는 로봇 식별자                              |
| `base_frame`        | 이 pose가 의미하는 로봇 frame. 현재는 `base_link`            |
| `x_m`, `y_m`, `z_m` | mission_map 기준 위치                                 |
| `yaw_rad`           | mission_map 기준 heading                            |
| `pose`              | ROS 표준 pose 형식. position + quaternion orientation |

---

## 6. 주요 토픽 정리

| Topic                       | Type                                      | Publisher                  | Subscriber                 | 설명                     |
| --------------------------- | ----------------------------------------- | -------------------------- | -------------------------- | ---------------------- |
| `/localization/stm_motion`  | `robot_interfaces/msg/StmMotion`          | `stm_motion_mock_node`     | `gait_odom_estimator_node` | mock STM motion 데이터    |
| `/localization/spot_motion` | `robot_interfaces/msg/StmMotion`          | STM bridge node            | `gait_odom_estimator_node` | 실제 STM bridge 데이터      |
| `/localization/odometry`    | `nav_msgs/msg/Odometry`                   | `gait_odom_estimator_node` | RViz, debug, 기타 노드         | odom 기준 로봇 odometry    |
| `/localization/pose`        | `robot_interfaces/msg/LocalizedRobotPose` | `localization_pose_node`   | 상위 판단 계층, 관제 UI            | mission_map 기준 최종 pose |

---

## 7. Config 파일 정리

현재 사용하는 주요 YAML 파일은 다음과 같다.

```text
config/
├── gait_odom.param.yaml
├── localization_pose.param.yaml
├── mission_tf.param.yaml
└── stm_motion_mock.param.yaml
```

---

## 7.1 `gait_odom.param.yaml`

`gait_odom_estimator_node`의 입력 토픽, output topic, frame, 보행 거리 보정값, yaw 보정값, safety parameter를 설정한다.

```yaml
gait_odom_estimator_node:
  ros__parameters:
    stm_motion_topic: "/localization/stm_motion"
    odom_topic: "/localization/odometry"

    odom_frame: "odom"
    base_frame: "base_link"

    forward_step_length_m: 0.030
    backward_step_length_m: 0.025
    strafe_left_step_length_m: 0.018
    strafe_right_step_length_m: 0.018

    yaw_offset_rad: 0.0
    imu_yaw_sign: 1.0

    max_delta_phase: 0.15
    publish_tf: true

    freeze_yaw_when_stopped: true
    gyro_stop_threshold_rad_s: 0.03
```

### 주요 파라미터

| Parameter                    | 설명                                         |
| ---------------------------- | ------------------------------------------ |
| `stm_motion_topic`           | 구독할 STM motion 토픽                          |
| `odom_topic`                 | publish할 odometry 토픽                       |
| `odom_frame`                 | odometry 기준 frame                          |
| `base_frame`                 | 로봇 본체 frame                                |
| `forward_step_length_m`      | gait cycle 1회당 전진 이동거리                     |
| `backward_step_length_m`     | gait cycle 1회당 후진 이동거리                     |
| `strafe_left_step_length_m`  | gait cycle 1회당 좌측 횡이동 거리                   |
| `strafe_right_step_length_m` | gait cycle 1회당 우측 횡이동 거리                   |
| `yaw_offset_rad`             | IMU yaw 기반 odom yaw의 잔여 offset 보정값         |
| `imu_yaw_sign`               | IMU yaw 부호 보정값. ROS 기준과 반대이면 `-1.0`        |
| `max_delta_phase`            | 한 callback에서 허용할 최대 gait phase 변화량         |
| `publish_tf`                 | `odom -> base_link` TF publish 여부          |
| `freeze_yaw_when_stopped`    | STOP 상태에서 gyro_z가 작으면 yaw drift 반영을 막을지 여부 |
| `gyro_stop_threshold_rad_s`  | STOP 상태 yaw drift 판단 threshold             |

---

## 7.2 `mission_tf.param.yaml`

`mission_map -> odom` static TF의 위치와 방향을 설정한다.

```yaml
mission_tf_node:
  ros__parameters:
    mission_frame: "mission_map"
    odom_frame: "odom"
    start_x_m: 5.0
    start_y_m: 5.0
    start_z_m: 0.0
    start_yaw_rad: 0.0
```

예를 들어 `start_x_m=5.0`, `start_y_m=5.0`이면 로봇의 odom 원점은 mission_map 기준 `(5.0, 5.0)`에 배치된다.

---

## 7.3 `localization_pose.param.yaml`

`localization_pose_node`가 어떤 TF를 조회하고 어떤 topic으로 pose를 publish할지 설정한다.

```yaml
localization_pose_node:
  ros__parameters:
    pose_topic: "/localization/pose"

    robot_id: "spot_01"

    mission_frame: "mission_map"
    base_frame: "base_link"

    publish_rate_hz: 30.0
    tf_lookup_timeout_s: 0.05
    warning_throttle_ms: 2000
    pose_qos_depth: 10
```

### 주요 파라미터

| Parameter             | 설명                                 |
| --------------------- | ---------------------------------- |
| `pose_topic`          | 최종 localization pose publish topic |
| `robot_id`            | 로봇 식별자                             |
| `mission_frame`       | 공통좌표계 frame                        |
| `base_frame`          | 조회 대상 로봇 frame                     |
| `publish_rate_hz`     | pose publish 주기                    |
| `tf_lookup_timeout_s` | TF lookup timeout                  |
| `warning_throttle_ms` | TF lookup 실패 warning throttle      |
| `pose_qos_depth`      | pose publisher QoS depth           |

---

## 7.4 `stm_motion_mock.param.yaml`

`stm_motion_mock_node`의 mock publish 주기와 시나리오를 설정한다.

예시:

```yaml
stm_motion_mock_node:
  ros__parameters:
    stm_motion_topic: "/localization/stm_motion"
    publish_rate_hz: 50.0
```

---

## 8. Launch 파일 정리

추천 launch 구성은 mock 실험용과 실제 bridge 연동용을 분리하는 방식이다.

```text
launch/
├── localization_tf_chain_mock.launch.py
├── localization_tf_chain_real.launch.py
└── localization_pose.launch.py
```

---

## 8.1 Mock 실험용 launch

### 파일

```text
launch/localization_tf_chain_mock.launch.py
```

### 실행 노드

```text
stm_motion_mock_node
gait_odom_estimator_node
mission_tf_node
localization_pose_node
```

### 실행

```bash
cd ~/robot_ws
source install/setup.bash
ros2 launch spot_localization localization_tf_chain_mock.launch.py
```

### 설명

STM bridge 없이 mock 데이터만으로 전체 localization pipeline을 검증한다.

---

## 8.2 실제 STM bridge 연동 launch

### 파일

```text
launch/localization_tf_chain_real.launch.py
```

### 실행 노드

```text
gait_odom_estimator_node
mission_tf_node
localization_pose_node
```

### 전제 조건

팀원 bridge node가 먼저 실행되어 있어야 한다.

```text
/localization/spot_motion
Type: robot_interfaces/msg/StmMotion
```

### 실행

```bash
cd ~/robot_ws
source install/setup.bash
ros2 launch spot_localization localization_tf_chain_real.launch.py
```

### real launch에서 입력 토픽 override 예시

`gait_odom.param.yaml`은 mock 기준으로 유지하고, 실제 bridge launch에서만 `stm_motion_topic`을 override한다.

```python
gait_odom_estimator_node = Node(
    package="spot_localization",
    executable="gait_odom_estimator_node",
    name="gait_odom_estimator_node",
    output="screen",
    parameters=[
        gait_odom_param,
        {"stm_motion_topic": "/localization/spot_motion"},
    ],
)
```

이렇게 하면 mock/real 설정을 launch 수준에서 분리할 수 있다.

---

## 8.3 `localization_pose.launch.py`

`mission_tf_node`와 `gait_odom_estimator_node`가 이미 실행 중일 때, `localization_pose_node`만 단독 실행하기 위한 launch이다.

```bash
ros2 launch spot_localization localization_pose.launch.py
```

---

## 9. Build 방법

패키지 빌드:

```bash
cd ~/robot_ws
colcon build --packages-select spot_localization
source install/setup.bash
```

`robot_interfaces` 메시지를 수정한 경우:

```bash
cd ~/robot_ws
colcon build --packages-select robot_interfaces spot_localization --allow-overriding robot_interfaces
source install/setup.bash
```

실행 파일 확인:

```bash
ros2 pkg executables spot_localization
```

예상 결과:

```text
spot_localization stm_motion_mock_node
spot_localization gait_odom_estimator_node
spot_localization mission_tf_node
spot_localization localization_pose_node
```

launch 파일 설치 확인:

```bash
ls ~/robot_ws/install/spot_localization/share/spot_localization/launch
```

---

## 10. Mock 기반 검증 절차

### 10.1 전체 launch 실행

```bash
cd ~/robot_ws
source install/setup.bash
ros2 launch spot_localization localization_tf_chain_mock.launch.py
```

### 10.2 노드 확인

```bash
ros2 node list
```

예상:

```text
/stm_motion_mock_node
/gait_odom_estimator_node
/mission_tf_node
/localization_pose_node
```

### 10.3 토픽 확인

```bash
ros2 topic list | grep localization
```

예상:

```text
/localization/stm_motion
/localization/odometry
/localization/pose
```

### 10.4 토픽 주기 확인

```bash
ros2 topic hz /localization/stm_motion
ros2 topic hz /localization/odometry
ros2 topic hz /localization/pose
```

예상:

```text
/localization/stm_motion   약 50 Hz
/localization/odometry     약 50 Hz
/localization/pose         약 30 Hz
```

### 10.5 TF 확인

```bash
ros2 run tf2_ros tf2_echo mission_map odom
ros2 run tf2_ros tf2_echo odom base_link
ros2 run tf2_ros tf2_echo mission_map base_link
```

정상 기준:

```text
mission_map -> odom
= mission_tf.param.yaml의 start_x_m, start_y_m, start_yaw_rad 값과 일치

odom -> base_link
= gait odometry에 따라 계속 변화

mission_map -> base_link
= mission_map 기준 최종 로봇 위치
```

### 10.6 최종 pose 확인

```bash
ros2 topic echo /localization/pose robot_interfaces/msg/LocalizedRobotPose
```

예상:

```yaml
header:
  frame_id: mission_map
robot_id: spot_01
base_frame: base_link
x_m: 5.1500
y_m: 5.1500
z_m: 0.0
yaw_rad: 1.5706
```

---

## 11. 실제 STM bridge 연동 절차

### 11.1 Bridge 실행

팀원 bridge 패키지를 먼저 실행한다.

```bash
ros2 launch <bridge_package> <bridge_launch_file>.launch.py
```

### 11.2 Bridge 토픽 확인

```bash
ros2 topic info /localization/spot_motion
```

정상 기준:

```text
Type: robot_interfaces/msg/StmMotion
Publisher count: 1
```

데이터 확인:

```bash
ros2 topic echo /localization/spot_motion robot_interfaces/msg/StmMotion
```

확인할 값:

```text
seq가 증가하는가?
timestamp_ms가 증가하는가?
motion_state가 실제 로봇 동작과 맞는가?
gait_phase가 0.0 ~ 1.0 범위에서 변화하는가?
gait_cycle_count가 cycle 완료 시 증가하는가?
imu_yaw_rad가 회전 시 변화하는가?
gyro_z_rad_s가 회전 시 변화하는가?
```

### 11.3 Real localization launch 실행

```bash
cd ~/robot_ws
source install/setup.bash
ros2 launch spot_localization localization_tf_chain_real.launch.py
```

### 11.4 결과 확인

```bash
ros2 topic hz /localization/odometry
ros2 topic hz /localization/pose
ros2 run tf2_ros tf2_echo mission_map base_link
```

---

## 12. 주요 로그 해석

## 12.1 TF lookup warning

실행 직후 다음 warning이 1~2회 발생할 수 있다.

```text
Failed to lookup TF mission_map -> base_link:
"mission_map" passed to lookupTransform argument target_frame does not exist.
```

또는:

```text
"base_link" passed to lookupTransform argument source_frame does not exist.
```

이는 `localization_pose_node`가 실행된 직후 TF listener buffer에 아직 `/tf` 또는 `/tf_static` 데이터가 들어오기 전에 transform을 조회했기 때문에 발생한다.

초기 몇 회만 발생하고 이후 `/localization/pose`가 정상 publish되면 문제로 보지 않는다.

확인:

```bash
ros2 topic hz /localization/pose
```

---

## 12.2 `tf2_echo mission_map odom`에서 `At time 0.0`이 나오는 경우

`mission_map -> odom`은 static TF이므로 다음처럼 나오는 것이 정상이다.

```text
At time 0.0
- Translation: [5.000, 5.000, 0.000]
- Rotation: in RPY (degree) [0.000, -0.000, 0.000]
```

---

## 12.3 `/localization/pose` 값이 반복되는 경우

`localization_pose_node`는 설정된 주기로 계속 pose를 publish한다. 로봇이 STOP 상태이거나 TF 값이 변하지 않는 구간이면 같은 pose 값이 반복될 수 있다.

이는 정상이다.

---

## 12.4 `/localization/pose` echo 시 타입 충돌 오류

다음 오류가 발생할 수 있다.

```text
Cannot echo topic '/localization/pose', as it contains more than one type:
[robot_interfaces/msg/LocalizedRobotPose, robot_interfaces/msg/RobotLocalization]
```

이는 같은 `/localization/pose` 토픽 이름으로 서로 다른 메시지 타입을 publish하는 노드가 동시에 존재한다는 의미이다.

확인:

```bash
ros2 topic info /localization/pose -v
```

해결:

```text
1. 기존 RobotLocalization 타입 publisher 종료
2. 기존 publisher의 topic 이름 변경
3. /localization/pose는 최종 LocalizedRobotPose 전용으로 유지
```

타입을 명시해서 임시 echo할 수도 있다.

```bash
ros2 topic echo /localization/pose robot_interfaces/msg/LocalizedRobotPose
```

---

## 13. Trouble Shooting

## 13.1 `No executable found`

증상:

```text
No executable found
```

원인:

```text
1. CMakeLists.txt에 add_executable/install 누락
2. colcon build 미실행
3. source install/setup.bash 누락
4. 잘못된 workspace 위치에서 build
```

해결:

```bash
cd ~/robot_ws
colcon build --packages-select spot_localization
source install/setup.bash
ros2 pkg executables spot_localization
```

---

## 13.2 launch 파일을 찾지 못하는 경우

증상:

```text
file not found in share directory
```

원인:

```text
1. launch 파일이 install 공간에 복사되지 않음
2. CMakeLists.txt에 install(DIRECTORY launch ...) 누락
3. build 후 source install/setup.bash 누락
```

확인:

```bash
ls ~/robot_ws/install/spot_localization/share/spot_localization/launch
```

---

## 13.3 `/localization/odometry`가 나오지 않는 경우

확인 순서:

```bash
ros2 topic info /localization/stm_motion
ros2 topic info /localization/spot_motion
ros2 node info /gait_odom_estimator_node
```

주요 원인:

```text
1. gait_odom_estimator_node가 잘못된 입력 토픽을 구독 중
2. bridge 토픽 이름 불일치
3. 메시지 타입 불일치
4. StmMotion 값 검증 실패
```

real bridge 사용 시 `gait_odom_estimator_node`가 다음 토픽을 구독해야 한다.

```text
/localization/spot_motion
Type: robot_interfaces/msg/StmMotion
```

---

## 13.4 yaw 방향이 반대로 나오는 경우

ROS 기준:

```text
좌회전: +yaw
우회전: -yaw
```

실제 로봇을 왼쪽으로 회전했는데 `/localization/pose.yaw_rad`가 감소하면 `imu_yaw_sign`을 `-1.0`으로 변경한다.

```yaml
imu_yaw_sign: -1.0
```

---

## 13.5 실제 이동거리와 odometry 이동거리가 맞지 않는 경우

실측을 통해 gait step length 값을 튜닝해야 한다.

예시:

```text
실제 전진 거리: 1.0 m
odometry 전진 거리: 0.8 m
```

이 경우 `forward_step_length_m`을 증가시킨다.

```yaml
forward_step_length_m: 0.030
```

튜닝 대상:

```text
forward_step_length_m
backward_step_length_m
strafe_left_step_length_m
strafe_right_step_length_m
```

---

## 14. 현재 개발 상태

현재 Localization MVP 기준으로 완료된 항목은 다음과 같다.

```text
1. StmMotion 기반 gait odometry 입력 구조 정의
2. mock STM motion publisher 구현
3. gait_odom_estimator_node 구현
4. /localization/odometry publish 구현
5. odom -> base_link TF publish 구현
6. mission_map -> odom static TF publish 구현
7. localization_pose_node 구현
8. /localization/pose publish 구현
9. robot_id 포함 최종 pose 메시지 구성
10. tf2_echo / topic echo / RViz 기반 검증 완료
```

---

## 15. 남은 작업

MVP 기능은 완료되었지만, 실제 로봇 적용을 위해 다음 작업이 남아 있다.

```text
1. 팀원 STM bridge의 /localization/spot_motion 연동
2. 실제 로봇 기준 gait step length 튜닝
3. yaw 부호 및 yaw offset 검증
4. seq/timestamp 기반 통신 품질 확인
5. 장시간 주행 시 odom drift 확인
6. 필요 시 drift correction 계층 추가
```

현재는 외부 보정원이 없으므로 odom drift가 누적될 수 있다. drift correction은 추후 AprilTag, QR checkpoint, LiDAR map matching, SLAM, manual pose reset 등의 방식으로 확장할 수 있다.

---

## 16. 상위 계층 연동 기준

상위 판단 계층은 직접 TF를 조회하지 않고, 다음 topic을 구독하는 방식으로 시작할 수 있다.

```text
/localization/pose
Type: robot_interfaces/msg/LocalizedRobotPose
```

상위 판단 계층에서 사용할 핵심 필드는 다음과 같다.

```text
robot_id
x_m
y_m
yaw_rad
pose
```

예상 연동 구조:

```text
/localization/pose
/perception/lidar/obstacle_model
/perception/lidar/free_space_model
        ↓
local_decision_node
        ↓
/decision/local_decision
```

초기 rule-based decision MVP는 다음 판단 구조로 시작할 수 있다.

```text
front가 비어 있음       -> GO_FORWARD
front 막힘 + left 가능  -> TURN_LEFT 또는 AVOID_LEFT
front 막힘 + right 가능 -> TURN_RIGHT 또는 AVOID_RIGHT
모두 막힘              -> STOP
pose 없음              -> WAIT 또는 STOP
```

---

## 17. 추천 실행 흐름 요약

### Mock 실험

```bash
cd ~/robot_ws
source install/setup.bash
ros2 launch spot_localization localization_tf_chain_mock.launch.py
```

검증:

```bash
ros2 topic hz /localization/stm_motion
ros2 topic hz /localization/odometry
ros2 topic hz /localization/pose
ros2 run tf2_ros tf2_echo mission_map base_link
```

### 실제 STM bridge 연동

```bash
# 1. bridge 실행
ros2 launch <bridge_package> <bridge_launch_file>.launch.py

# 2. bridge topic 확인
ros2 topic info /localization/spot_motion
ros2 topic echo /localization/spot_motion robot_interfaces/msg/StmMotion

# 3. localization 실행
ros2 launch spot_localization localization_tf_chain_real.launch.py

# 4. 결과 확인
ros2 topic echo /localization/pose robot_interfaces/msg/LocalizedRobotPose
ros2 run tf2_ros tf2_echo mission_map base_link
```
