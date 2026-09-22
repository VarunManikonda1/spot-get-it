# spot_navigation

`spot_navigation` 패키지는 다중 구조 정찰 로봇 프로젝트에서 Spot 계열 로봇의 **Global Path 진행 상태 추적**, **LiDAR 기반 동적 장애물 회피 Local Path 생성**, **Navigation 상태 판단**, **RViz 디버그 시각화**를 담당하는 ROS 2 패키지이다.

현재 구현은 `mission_map` 기준의 Global Path와 Localization Pose를 입력으로 사용하고, `base_link` 기준 LiDAR Perception 결과를 결합하여 주행 가능한 Local Path를 생성하는 것을 목표로 한다.

---

## 1. 패키지 역할

### 핵심 목적

`spot_navigation`은 아래 흐름을 담당한다.

1. Planning 계층에서 생성한 Global Path를 수신한다.
2. Localization 계층에서 생성한 현재 로봇 Pose를 수신한다.
3. 현재 Pose가 Global Path 상에서 어느 지점까지 진행되었는지 계산한다.
4. PathProgress, Localization Pose, LiDAR Perception 결과를 기반으로 Local Path를 생성한다.
5. 장애물 상황에 따라 Global Path 추종, 회피, 복귀, 정지 상태를 판단한다.
6. RViz에서 확인할 수 있도록 custom message를 표준 시각화 메시지로 변환한다.

### 담당 기능 요약

| 구분 | 설명 |
|---|---|
| Global Path Mock | 실제 Planning 노드 없이 Global Path 검증용 waypoint publish |
| Mock Localization Pose | Global Path를 따라 이동하는 더미 Pose publish |
| Path Progress Tracking | 현재 Pose 기준 nearest waypoint, target waypoint, goal 도달 여부 계산 |
| Local Path Planning | Global sub-goal 추종, LiDAR 기반 회피, 복귀, 정지 상태 처리 |
| Navigation Debug Visualization | RViz 표시용 Path, Pose, MarkerArray publish |

---

## 2. 패키지 구조

```bash
spot_navigation/
├── CMakeLists.txt
├── package.xml
├── include/
│   └── spot_navigation/
│       ├── mock_global_path_publisher_node.hpp
│       ├── mock_localization_pose_publisher_node.hpp
│       ├── path_progress_tracker_node.hpp
│       ├── local_path_planner_node.hpp
│       └── navigation_debug_visualizer_node.hpp
├── src/
│   ├── mock_global_path_publisher_node.cpp
│   ├── mock_localization_pose_publisher_node.cpp
│   ├── path_progress_tracker_node.cpp
│   ├── local_path_planner_node.cpp
│   └── navigation_debug_visualizer_node.cpp
├── config/
│   ├── mock_global_path_publisher.param.yaml
│   ├── mock_localization_pose_publisher.param.yaml
│   ├── path_progress_tracker.param.yaml
│   ├── local_path_planner.param.yaml
│   └── navigation_debug_visualizer.param.yaml
└── launch/
    └── *.launch.py
```

> `launch/` 파일명은 실제 repository 구성에 맞춰 관리한다.

---

## 3. 전체 시스템 흐름

### 3-1. 실제 통합 운용 흐름

```text
/planning/global_path/spot_01
    Type: robot_interfaces/msg/GlobalPathWaypoints

/localization/pose
    Type: robot_interfaces/msg/LocalizedRobotPose

        │
        ▼

[path_progress_tracker_node]
    - nearest_index 계산
    - target_index 계산
    - target_heading_rad 계산
    - heading_error_rad 계산
    - progress_ratio 계산
    - goal_reached 판단

        │
        ▼

/navigation/path_progress/spot_01
    Type: robot_interfaces/msg/PathProgress

        │
        ├───────────────────────────────────────────────┐
        ▼                                               ▼

/perception/lidar/obstacle_model              /perception/lidar/free_space_model
Type: robot_interfaces/msg/ObstacleModel      Type: robot_interfaces/msg/FreeSpaceModel

        │                                               │
        └───────────────────────┬───────────────────────┘
                                ▼

[local_path_planner_node]
    - PathProgress / Pose / Perception 입력 유효성 검사
    - front / side obstacle danger 판단
    - FreeSpaceModel 기반 회피 가능 방향 판단
    - Local Planner FSM 상태 전이
    - 상태별 Local Path 생성
    - LocalPlannerStatus publish

        │
        ├── /navigation/local_path/spot_01
        │       Type: nav_msgs/msg/Path
        │
        └── /navigation/local_planner_status/spot_01
                Type: robot_interfaces/msg/LocalPlannerStatus
```

### 3-2. Mock 기반 단독 검증 흐름

```text
[mock_global_path_publisher_node]
    │
    └── /planning/mock_global_path/spot_01 또는 /planning/global_path/spot_01

[mock_localization_pose_publisher_node]
    │
    └── /localization/mock_pose

[path_progress_tracker_node]
    │
    └── /navigation/path_progress/spot_01

[local_path_planner_node]
    │
    ├── /navigation/local_path/spot_01
    └── /navigation/local_planner_status/spot_01

[navigation_debug_visualizer_node]
    │
    ├── /debug/navigation/global_path
    ├── /debug/navigation/pose
    └── /debug/navigation/progress_markers
```

---

## 4. 좌표계 기준

| Frame | 의미 | 사용 위치 |
|---|---|---|
| `mission_map` | 프로젝트 공통 임무 지도 좌표계 | Global Path, Localization Pose, PathProgress, Local Path, Debug Path/Pose/Marker |
| `base_link` | 로봇 본체 기준 좌표계 | LiDAR ObstacleModel, FreeSpaceModel |
| `laser_frame` | LiDAR 센서 기준 좌표계 | LiDAR 원천 데이터 및 Perception 전처리 |

### 중요 좌표 변환 개념

`local_path_planner_node`는 `FreeSpaceModel.best_heading_angle_rad`를 회피 방향으로 사용한다. 이 값은 `base_link` 기준 상대 방향이므로, Local Path를 `mission_map` 기준으로 생성할 때 다음과 같이 변환한다.

```text
mission_map 기준 회피 heading = current_yaw_rad + free_space_best_heading_rad
```

즉, Perception은 로봇 기준으로 “어느 방향이 비었는가”를 알려주고, Navigation은 현재 로봇의 지도상 yaw를 더해 실제 지도 좌표계의 회피 target을 만든다.

---

## 5. 노드 설명

## 5-1. `mock_global_path_publisher_node`

### 역할

실제 Planning 노드가 없는 상황에서도 Navigation 파이프라인을 검증할 수 있도록 Global Path를 더미로 publish한다.

### 입력 토픽

없음.

### 출력 토픽

| Topic | Type | 설명 |
|---|---|---|
| `/planning/global_path/spot_01` 또는 `/planning/mock_global_path/spot_01` | `robot_interfaces/msg/GlobalPathWaypoints` | Global Path waypoint 배열 |

### 핵심 기능

YAML에 정의된 `waypoint_x`, `waypoint_y`, `waypoint_yaw_rad` 배열을 읽어 `GlobalPathWaypoints` 메시지로 변환한다.

```text
waypoint_x[i], waypoint_y[i], waypoint_yaw_rad[i]
        ↓
robot_interfaces/msg/LocalizedRobotPose waypoint
        ↓
robot_interfaces/msg/GlobalPathWaypoints.waypoints[]
```

각 waypoint에는 다음 값이 채워진다.

```text
header.frame_id = map_frame 또는 mission_map
robot_id        = spot_01
base_frame      = base_link
x_m, y_m, z_m
yaw_rad
pose.position
pose.orientation
```

`global path`는 기준 경로 데이터에 가깝기 때문에 `transient_local` QoS로 publish한다. 따라서 subscriber가 나중에 실행되어도 마지막으로 publish된 path를 받을 수 있다.

### 주요 기능

- YAML에 정의된 `waypoint_x`, `waypoint_y`, `waypoint_yaw_rad` 배열을 읽는다.
- 각 waypoint를 `LocalizedRobotPose` 형식으로 변환한다.
- yaw 값을 quaternion으로 변환해 `pose.orientation`에 채운다.
- waypoint 배열 길이가 맞지 않으면 노드 시작 시 예외 처리한다.
- Global Path는 기준 데이터에 가까우므로 `transient_local` QoS로 publish한다.
- 노드 시작 직후 1회 publish하고, 이후 설정 주기로 반복 publish한다.

### 주요 파라미터

| Parameter | 기본/예시값 | 설명 |
|---|---:|---|
| `robot_id` | `spot_01` | 대상 로봇 ID |
| `global_path_topic` | `/planning/global_path/spot_01` | Global Path 출력 토픽 |
| `map_frame` | `mission_map` 또는 `map` | Global Path 기준 frame |
| `base_frame` | `base_link` | waypoint pose가 의미하는 로봇 body frame |
| `default_z_m` | `0.05` | waypoint z 기본값 |
| `publish_rate_hz` | `1.0` | Global Path publish 주기 |
| `waypoint_x` | 배열 | waypoint x 좌표 |
| `waypoint_y` | 배열 | waypoint y 좌표 |
| `waypoint_yaw_rad` | 배열 | waypoint yaw 방향 |

---

## 5-2. `mock_localization_pose_publisher_node`

### 역할

수신한 Global Path를 따라 로봇이 실제로 이동하는 것처럼 `LocalizedRobotPose`를 연속적으로 publish한다. 실제 Localization 노드 없이 PathProgress 및 LocalPlanner를 검증하기 위한 노드다.

### 입력 토픽

| Topic | Type | 설명 |
|---|---|---|
| `/planning/mock_global_path/spot_01` | `robot_interfaces/msg/GlobalPathWaypoints` | Mock pose가 따라갈 Global Path |

### 출력 토픽

| Topic | Type | 설명 |
|---|---|---|
| `/localization/mock_pose` | `robot_interfaces/msg/LocalizedRobotPose` | Global Path를 따라 이동하는 더미 pose |

### 주요 기능

- Global Path를 수신하면 waypoint 목록을 내부 구조체로 저장한다.
- 현재 segment index와 segment 내 이동 거리를 관리한다.
- `linear_speed_mps`와 실제 timer `dt`를 이용해 이동 거리를 누적한다.
- waypoint 사이를 선형 보간하여 부드럽게 이동하는 pose를 생성한다.
- yaw는 현재 segment 방향 또는 waypoint yaw 보간 방식 중 선택할 수 있다.
- goal 도달 후 hold 또는 loop 동작을 선택할 수 있다.
- Gaussian noise를 추가하여 localization pose 흔들림 상황을 재현할 수 있다.

### 주요 파라미터

| Parameter | 기본/예시값 | 설명 |
|---|---:|---|
| `robot_id` | `spot_01` | 대상 로봇 ID |
| `global_path_topic` | `/planning/mock_global_path/spot_01` | 입력 Global Path 토픽 |
| `localization_pose_topic` | `/localization/mock_pose` | 출력 Mock Pose 토픽 |
| `global_frame` | `mission_map` | Pose 기준 frame |
| `base_frame` | `base_link` | Pose가 의미하는 로봇 body frame |
| `default_z_m` | `0.05` | z 값이 비정상일 때 사용할 기본값 |
| `publish_rate_hz` | `20.0` | Mock pose publish 주기 |
| `linear_speed_mps` | `0.1` | Path를 따라 이동하는 가상 선속도 |
| `loop_path` | `true` | goal 도달 후 처음 waypoint부터 반복할지 여부 |
| `hold_at_goal` | `true` | loop가 아닐 때 goal pose를 계속 publish할지 여부 |
| `use_segment_yaw` | `true` | segment 진행 방향을 yaw로 사용할지 여부 |
| `reset_on_new_path` | `true` | 새 path 수신 시 처음부터 다시 주행할지 여부 |
| `noise_enabled` | `true` | 출력 pose에 Gaussian noise를 추가할지 여부 |
| `position_noise_std_m` | `0.03` | x, y 위치 노이즈 표준편차 |
| `yaw_noise_std_rad` | `0.03` | yaw 노이즈 표준편차 |
| `max_position_noise_m` | `0.10` | 위치 노이즈 최대 절댓값 |
| `max_yaw_noise_rad` | `0.10` | yaw 노이즈 최대 절댓값 |
| `noise_seed` | `42` | 랜덤 seed. `-1`이면 매번 다른 noise |

---

## 5-3. `path_progress_tracker_node`

### 역할

Global Path와 Localization Pose를 입력으로 받아 현재 로봇이 Global Path 상에서 어느 지점까지 진행했는지 계산한다.

### 입력 토픽

| Topic | Type | 설명 |
|---|---|---|
| `/planning/global_path/spot_01` | `robot_interfaces/msg/GlobalPathWaypoints` | 추종할 Global Path |
| `/localization/pose` | `robot_interfaces/msg/LocalizedRobotPose` | 현재 로봇 위치와 yaw |

### 출력 토픽

| Topic | Type | 설명 |
|---|---|---|
| `/navigation/path_progress/spot_01` | `robot_interfaces/msg/PathProgress` | 현재 path 진행 상태 |

### 주요 기능

- Global Path 수신 여부와 유효성을 검사한다.
- Localization Pose 수신 여부와 유효성을 검사한다.
- `robot_id`, `frame_id`, NaN/Inf 여부를 검사한다.
- 마지막 Pose 수신 시각 기준으로 timeout을 판단한다.
- 현재 Pose와 가장 가까운 waypoint를 `nearest_index`로 계산한다.
- `nearest_index` 기준으로 `lookahead_distance_m` 이상 앞의 waypoint를 `target_index`로 선택한다.
- 현재 위치에서 target waypoint를 바라보는 `target_heading_rad`를 계산한다.
- `heading_error_rad`, `yaw_error_rad`, `distance_to_goal_m`, `goal_reached`를 계산한다.
- 입력이 아직 유효하지 않아도 downstream 노드가 원인을 구분할 수 있도록 상태 flag를 포함한 `PathProgress`를 publish한다.

### 핵심 로직

먼저 입력 데이터의 유효성을 검사한다.

```text
path_received
pose_received
robot_id 일치 여부
frame_id 일치 여부
waypoint empty 여부
pose timeout 여부
x, y, yaw finite 여부
```

입력이 유효하면 현재 pose 기준 가장 가까운 waypoint를 찾는다.

```text
current pose = (x, y)
        ↓
각 waypoint와 2D 거리 계산
        ↓
가장 가까운 waypoint index 선택
        ↓
nearest_index
```

`allow_backward_index_jump=false`인 경우 이전 nearest index보다 뒤쪽 구간은 다시 탐색하지 않는다. 이 설정은 localization pose 흔들림 때문에 `nearest_index`가 뒤로 튀는 것을 방지한다.

이후 `nearest_index`부터 path 진행 방향으로 waypoint 간 거리를 누적하여 lookahead target을 선택한다.

```text
nearest_index부터 waypoint 간 거리 누적
        ↓
accumulated_distance_m >= lookahead_distance_m
        ↓
target_index 선택
```

주요 계산 결과는 다음과 같다.

### 주요 출력 필드

| Field | 설명 |
|---|---|
| `path_received` | Global Path 수신 여부 |
| `pose_received` | Localization Pose 수신 여부 |
| `path_valid` | Global Path 계산 가능 여부 |
| `pose_valid` | Pose 계산 가능 여부 |
| `nearest_index` | 현재 pose와 가장 가까운 waypoint index |
| `target_index` | lookahead 기준 target waypoint index |
| `target_x_m`, `target_y_m` | target waypoint 위치 |
| `target_heading_rad` | 현재 위치에서 target 좌표를 바라보는 heading |
| `target_yaw_rad` | target waypoint가 가진 yaw |
| `heading_error_rad` | 현재 yaw와 target heading 사이 오차 |
| `yaw_error_rad` | 현재 yaw와 target yaw 사이 오차 |
| `progress_ratio` | Global Path 진행률 |
| `distance_to_goal_m` | 최종 goal까지 거리 |
| `goal_reached` | goal 도달 여부 |

### 주요 파라미터

| Parameter | 기본/예시값 | 설명 |
|---|---:|---|
| `robot_id` | `spot_01` | 대상 로봇 ID |
| `global_path_topic` | `/planning/global_path/spot_01` | 입력 Global Path 토픽 |
| `localization_pose_topic` | `/localization/pose` | 입력 Localization Pose 토픽 |
| `path_progress_topic` | `/navigation/path_progress/spot_01` | 출력 PathProgress 토픽 |
| `global_frame` | `mission_map` | Path와 Pose의 공통 frame |
| `lookahead_distance_m` | `0.5` | target waypoint 선택 거리 |
| `goal_tolerance_m` | `0.1` 또는 `0.3` | goal 도달 판단 거리 |
| `pose_timeout_sec` | `0.5` | pose stale 판단 시간 |
| `publish_rate_hz` | `10.0` | PathProgress publish 주기 |
| `allow_backward_index_jump` | `false` 또는 `true` | nearest index가 뒤로 튀는 것을 허용할지 여부 |

> 주의: 코드 기준 frame 파라미터명은 `global_frame`이다. YAML에서 `map_frame`을 사용할 경우 코드가 해당 값을 읽지 못하므로, frame 이름을 바꿀 때는 `global_frame`으로 맞추는 것이 안전하다.

---

## 5-4. `local_path_planner_node`

### 역할

`PathProgress`, `Localization Pose`, LiDAR 기반 인지 모델(`ObstacleModel`, `FreeSpaceModel`)을 기반으로 follower/controller가 따라갈 짧은 Local Path를 생성한다.

### 입력 토픽

| Topic | Type | 설명 |
|---|---|---|
| `/navigation/path_progress/spot_01` | `robot_interfaces/msg/PathProgress` | Global Path 진행 상태 |
| `/localization/pose` | `robot_interfaces/msg/LocalizedRobotPose` | 현재 로봇 pose |
| `/perception/lidar/obstacle_model` | `robot_interfaces/msg/ObstacleModel` | front/left/right 대표 장애물 정보 |
| `/perception/lidar/free_space_model` | `robot_interfaces/msg/FreeSpaceModel` | LiDAR 기반 주행 가능 방향 후보 |

### 출력 토픽

| Topic | Type | 설명 |
|---|---|---|
| `/navigation/local_path/spot_01` | `nav_msgs/msg/Path` | follower/controller가 따라갈 Local Path |
| `/navigation/local_planner_status/spot_01` | `robot_interfaces/msg/LocalPlannerStatus` | Local Planner 판단 상태와 근거 |

### Local Planner FSM

현재 local planner는 다음 상태를 사용한다.

| State | 값 | 의미 |
| ----- | -- | ---- |
| `IDLE` | 0 | 초기 상태 |
| `GLOBAL_SUB_GOAL` | 1 | PathProgress target을 local target으로 사용해 global path를 정상 추종 |
| `AVOIDANCE` | 2 | FreeSpaceModel의 best heading을 이용해 장애물 회피 local path 생성 |
| `REJOIN` | 3 | 회피 후 global path target으로 복귀 |
| `BLOCKED` | 4 | 장애물은 있으나 사용할 수 있는 free-space가 없어 hold path 생성 |
| `INVALID_INPUT` | 5 | path progress, pose, perception 입력이 유효하지 않아 planning 불가 |
| `GLOBAL_GOAL_REACHED` | 6 | global path 최종 goal 도착, 현재 pose 유지 |

### 판단 흐름

Local planner의 주기 동작은 다음 순서로 진행된다.

```text
1. PathProgress 유효성 검사
2. Localization pose 유효성 검사
3. goal_reached 우선 확인
4. ObstacleModel / FreeSpaceModel timeout 및 값 유효성 검사
5. front/side obstacle danger 판단
6. free-space heading 사용 가능 여부 판단
7. FSM 상태에 따라 local path 생성
8. local path와 LocalPlannerStatus publish
```

장애물 판단은 전방뿐 아니라 좌/우 측면까지 함께 고려한다.

```text
front_danger = front.valid && front.nearest_distance_xy < front_block_distance_m
left_danger  = left.valid  && left.nearest_distance_xy  < side_block_distance_m
right_danger = right.valid && right.nearest_distance_xy < side_block_distance_m

obstacle_danger = front_danger || left_danger || right_danger
```

장애물 해소 판단은 hysteresis를 위해 block threshold와 clear threshold를 분리한다.

```text
front_clear = front invalid 또는 front.nearest_distance_xy > front_clear_distance_m
left_clear  = left invalid  또는 left.nearest_distance_xy  > side_clear_distance_m
right_clear = right invalid 또는 right.nearest_distance_xy > side_clear_distance_m

obstacle_clear = front_clear && left_clear && right_clear
```

Free-space heading은 다음 조건을 만족할 때만 실제 회피 path에 사용한다.

```text
free_space_model.path_available == true
free_space_model.risk_level != RISK_UNKNOWN
free_space_model.risk_level != RISK_BLOCKED
free_space_model.best_clearance >= avoidance_min_clearance_m
```

### 최근 고도화 반영 사항

현재 local planner에는 다음 고도화가 반영되어 있다.

```text
1. front obstacle뿐 아니라 side obstacle까지 고려
2. front/side block-clear threshold 분리로 hysteresis 구성
3. AVOIDANCE 상태에서 장애물이 옆으로 빠졌을 때 즉시 REJOIN하지 않도록 side_clear 조건 추가
4. avoidance_min_hold_sec 또는 avoidance_min_travel_m 기반 AVOIDANCE latch 적용
5. BLOCKED 상태에서도 free-space가 회복되면 AVOIDANCE로 탈출 가능
6. REJOIN 중 장애물이 다시 가까워지면 AVOIDANCE 또는 BLOCKED로 재전이
7. 입력 invalid, goal reached, blocked 상태에서도 downstream node가 원인을 알 수 있도록 status publish
```

### 주요 기능

- PathProgress, Pose, ObstacleModel, FreeSpaceModel 수신 여부와 timeout을 검사한다.
- `robot_id`, `frame_id`, NaN/Inf 여부를 검사한다.
- `obstacle_model.front.valid == false`는 입력 invalid가 아니라 “전방 장애물 없음”으로 처리한다.
- `free_space_model.path_available == false`는 입력 invalid가 아니라 “현재 통과 가능한 gap 없음”으로 처리한다.
- 전방 장애물 위험 여부를 `front_block_distance_m`, `front_clear_distance_m` 기준으로 판단한다.
- 측면 장애물 위험 여부를 `side_block_distance_m`, `side_clear_distance_m` 기준으로 판단한다.
- 전방 또는 측면 중 하나라도 가까우면 obstacle danger로 판단한다.
- 전방과 측면이 모두 clear일 때만 REJOIN 또는 GLOBAL_SUB_GOAL 복귀를 허용한다.
- FreeSpaceModel의 best heading과 clearance를 기준으로 회피 path 사용 가능 여부를 판단한다.
- AVOIDANCE latch를 통해 회피 직후 너무 빠르게 REJOIN하는 문제를 방지한다.
- 상태에 따라 Global Sub-goal Path, Avoidance Path, Rejoin Path, Hold Path, Empty Path를 생성한다.

---

## 5-5 `navigation_debug_visualizer_node`

### 역할

Navigation 패키지에서 사용하는 custom message를 RViz가 바로 표시할 수 있는 표준 메시지로 변환한다.

### 입력

| Topic | Type | 설명 |
| ----- | ---- | ---- |
| `/planning/global_path/spot_01` 또는 `/planning/mock_global_path/spot_01` | `robot_interfaces/msg/GlobalPathWaypoints` | RViz Path로 변환할 global path |
| `/localization/pose` 또는 `/localization/mock_pose` | `robot_interfaces/msg/LocalizedRobotPose` | RViz Pose로 변환할 현재 pose |
| `/navigation/path_progress/spot_01` | `robot_interfaces/msg/PathProgress` | nearest/target/goal/progress marker 생성용 progress 정보 |

### 출력

| Topic | Type | 설명 |
| ----- | ---- | ---- |
| `/debug/navigation/global_path` | `nav_msgs/msg/Path` | RViz Path display용 global path |
| `/debug/navigation/pose` 또는 `/debug/navigation/mock_pose` | `geometry_msgs/msg/PoseStamped` | RViz Pose display용 현재 pose |
| `/debug/navigation/progress_markers` | `visualization_msgs/msg/MarkerArray` | nearest/target/goal/text/line marker |

### 핵심 기능

```text
GlobalPathWaypoints → nav_msgs/Path
LocalizedRobotPose → geometry_msgs/PoseStamped
PathProgress       → visualization_msgs/MarkerArray
```

MarkerArray에는 다음 정보가 포함된다.

```text
nearest waypoint marker
target waypoint marker
goal marker
current pose -> target point line marker
nearest_index / target_index / progress_ratio / goal_reached text marker
```

RViz에서 직접 보기 어려운 custom message를 표준 visualization message로 변환하기 때문에, Navigation 검증 시 가장 먼저 확인해야 하는 디버그 노드이다.


---

## 6. Local Planner FSM

`local_path_planner_node`는 `LocalPlannerStatus.msg`의 `planner_state`를 기준으로 아래 상태를 사용한다.

| State | 의미 | 동작 |
|---|---|---|
| `IDLE` | 초기 상태 | 입력 수신 전 대기 |
| `GLOBAL_SUB_GOAL` | Global Path 정상 추종 | PathProgress의 target waypoint를 local target으로 사용 |
| `AVOIDANCE` | 장애물 회피 | FreeSpaceModel의 best heading을 이용해 회피 target 생성 |
| `REJOIN` | Global Path 복귀 | 회피 후 PathProgress target 방향으로 복귀 |
| `BLOCKED` | 주행 불가 | 장애물이 위험하고 사용 가능한 free-space가 없으면 hold path publish |
| `INVALID_INPUT` | 입력 이상 | path/pose/perception 입력이 없거나 timeout이면 안전 정지용 path 처리 |
| `GLOBAL_GOAL_REACHED` | 최종 goal 도달 | 현재 pose를 유지하는 hold path publish |

### 상태 전이 개념

```text
입력 invalid
    → INVALID_INPUT

PathProgress.goal_reached == true
    → GLOBAL_GOAL_REACHED

정상 입력 + 장애물 danger 없음
    → GLOBAL_SUB_GOAL

정상 입력 + 장애물 danger 있음 + free-space 사용 가능
    → AVOIDANCE

정상 입력 + 장애물 danger 있음 + free-space 사용 불가
    → BLOCKED

AVOIDANCE 중 obstacle_clear && latch_done
    → REJOIN

REJOIN 중 global path 복귀 완료
    → GLOBAL_SUB_GOAL
```

### 최근 고도화 반영 사항

| 고도화 항목 | 설명 |
|---|---|
| LiDAR Perception 결합 | ObstacleModel과 FreeSpaceModel을 Local Path 판단에 반영 |
| 측면 장애물 hysteresis | 장애물이 front에서 left/right로 빠져도 side clear 전까지 REJOIN 금지 |
| AVOIDANCE latch | 회피 진입 후 최소 시간 또는 최소 이동거리 만족 전까지 회피 유지 |
| Free-space 기반 회피 | `best_heading_angle_rad`, `best_clearance`, `risk_level`을 기준으로 회피 가능성 판단 |
| 입력 timeout 안정화 | 메시지 header stamp가 아니라 실제 수신 시각 기준으로 stale 판단 |
| Invalid와 정상 empty 상태 분리 | 장애물 없음, path_available=false 등을 입력 invalid와 분리 |
| LocalPlannerStatus 강화 | 상태, target, heading error, 사용 근거, blocked/stop 여부 publish |

---

## 7. Local Path 생성 방식

### 7-1. GLOBAL_SUB_GOAL Path

`PathProgress.target_x_m`, `target_y_m`을 local target으로 사용한다.

```text
start = current localization pose
end   = PathProgress target waypoint
```

- target heading은 `PathProgress.target_heading_rad`를 사용한다.
- target yaw는 `use_path_progress_target_yaw` 설정에 따라 다음 중 하나를 사용한다.
  - `true`: `PathProgress.target_yaw_rad`
  - `false`: selected heading

### 7-2. AVOIDANCE Path

`FreeSpaceModel.best_heading_angle_rad`를 로봇 기준 회피 방향으로 사용한다.

```text
avoidance_heading_global = current_yaw + free_space_best_heading
avoidance_target_x = current_x + avoidance_horizon_m * cos(avoidance_heading_global)
avoidance_target_y = current_y + avoidance_horizon_m * sin(avoidance_heading_global)
```

- `avoidance_min_clearance_m` 이상 clearance가 확보되어야 한다.
- `FreeSpaceModel.risk_level`이 너무 높으면 회피 path로 사용하지 않는다.
- 회피 target은 현재 pose 기준 `avoidance_horizon_m` 앞에 생성된다.

### 7-3. REJOIN Path

장애물이 충분히 clear되고 AVOIDANCE latch가 끝나면 Global Path target 방향으로 복귀한다.

- `rejoin_tolerance_m` 이내로 global target에 접근했는지 확인한다.
- `rejoin_heading_tolerance_rad` 기준으로 heading 정렬 여부를 확인한다.

### 7-4. HOLD Path

입력 invalid, blocked, goal reached 상황에서는 현재 pose를 유지하는 hold path를 생성한다.

- pose가 유효하면 현재 pose 1개로 구성된 hold path를 publish한다.
- pose가 유효하지 않으면 empty path를 publish한다.
- `stop_required=true`로 상태를 표시한다.

---

## 8. 주요 파라미터

## 8-1. `local_path_planner.param.yaml`

| Parameter | 예시값 | 설명 |
|---|---:|---|
| `robot_id` | `spot_01` | 대상 로봇 ID |
| `global_frame` | `mission_map` | Navigation 기준 frame |
| `obstacle_model_topic` | `/perception/lidar/obstacle_model` | ObstacleModel 입력 토픽 |
| `free_space_model_topic` | `/perception/lidar/free_space_model` | FreeSpaceModel 입력 토픽 |
| `path_progress_topic` | `/navigation/path_progress/spot_01` | PathProgress 입력 토픽 |
| `localization_pose_topic` | `/localization/pose` | Localization Pose 입력 토픽 |
| `local_path_topic` | `/navigation/local_path/spot_01` | Local Path 출력 토픽 |
| `local_planner_status_topic` | `/navigation/local_planner_status/spot_01` | LocalPlannerStatus 출력 토픽 |
| `publish_rate_hz` | `10.0` | Local Path/Status publish 주기 |
| `obstacle_model_timeout_sec` | `0.5` | ObstacleModel timeout |
| `free_space_model_timeout_sec` | `0.5` | FreeSpaceModel timeout |
| `path_progress_timeout_sec` | `0.5` | PathProgress timeout |
| `pose_timeout_sec` | `0.5` | Localization Pose timeout |
| `front_block_distance_m` | `0.60` | 전방 장애물 danger 진입 기준 |
| `front_clear_distance_m` | `1.00` | 전방 장애물 clear 기준 |
| `side_block_distance_m` | `0.30` | 측면 장애물 danger 진입 기준 |
| `side_clear_distance_m` | `0.70` | 측면 장애물 clear 기준 |
| `avoidance_min_clearance_m` | `0.35` | 회피 path 사용 최소 clearance |
| `avoidance_horizon_m` | `0.70` | 회피 target 생성 거리 |
| `avoidance_min_hold_sec` | `1.5` | AVOIDANCE 최소 유지 시간 |
| `avoidance_min_travel_m` | `0.50` | AVOIDANCE 최소 이동 거리 |
| `rejoin_tolerance_m` | `0.30` | Global Path 복귀 완료 거리 기준 |
| `rejoin_heading_tolerance_rad` | `0.35` | Global Path 복귀 heading 기준 |
| `local_path_spacing_m` | `0.10` | Local Path pose 간격 |
| `max_local_path_length_m` | `1.50` | Local Path 최대 길이 |
| `local_target_tolerance_m` | `0.20` | Local target 도달 판단 거리 |
| `use_path_progress_target_yaw` | `true` | target yaw 사용 정책 |

---

## 8-2. `path_progress_tracker.param.yaml`

| Parameter | 예시값 | 설명 |
|---|---:|---|
| `robot_id` | `spot_01` | 대상 로봇 ID |
| `global_path_topic` | `/planning/global_path/spot_01` | 입력 Global Path 토픽 |
| `localization_pose_topic` | `/localization/pose` | 입력 Pose 토픽 |
| `path_progress_topic` | `/navigation/path_progress/spot_01` | 출력 PathProgress 토픽 |
| `global_frame` | `mission_map` | 공통 frame |
| `lookahead_distance_m` | `0.5` | target waypoint 선택 거리 |
| `goal_tolerance_m` | `0.1` | goal 도달 판단 거리 |
| `allow_backward_index_jump` | `false` 또는 `true` | nearest index 후퇴 허용 여부 |
| `pose_timeout_sec` | `0.5` | Pose timeout |
| `publish_rate_hz` | `10.0` | PathProgress publish 주기 |

---

## 8-3. `navigation_debug_visualizer.param.yaml`

| Parameter | 예시값 | 설명 |
|---|---:|---|
| `robot_id` | `spot_01` | 시각화 대상 로봇 ID |
| `global_frame` | `mission_map` | RViz Fixed Frame과 맞출 frame |
| `global_path_topic` | `/planning/global_path/spot_01` | 입력 Global Path 토픽 |
| `localization_pose_topic` | `/localization/pose` | 입력 Pose 토픽 |
| `path_progress_topic` | `/navigation/path_progress/spot_01` | 입력 PathProgress 토픽 |
| `debug_global_path_topic` | `/debug/navigation/global_path` | RViz Path 표시용 토픽 |
| `debug_pose_topic` | `/debug/navigation/pose` | RViz Pose 표시용 토픽 |
| `debug_marker_topic` | `/debug/navigation/progress_markers` | RViz MarkerArray 표시용 토픽 |
| `waypoint_marker_scale_m` | `0.18` | nearest/target/goal marker 크기 |
| `target_line_width_m` | `0.04` | 현재 pose와 target을 잇는 line 두께 |
| `text_marker_scale_m` | `0.25` | text marker 크기 |
| `marker_lifetime_sec` | `0.0` | marker lifetime. 0이면 계속 갱신 |
| `marker_z_m` | `0.15` | marker를 지도 평면보다 띄울 높이 |

---

## 9. 실행 방법

## 9-1. 빌드

```bash
cd ~/robot_ws
colcon build --packages-select robot_interfaces spot_navigation --symlink-install
source install/setup.bash
```

## 9-2. Mock Global Path 실행

```bash
ros2 run spot_navigation mock_global_path_publisher_node \
  --ros-args \
  --params-file src/navigation/spot_navigation/config/mock_global_path_publisher.param.yaml
```

## 9-3. Mock Localization Pose 실행

```bash
ros2 run spot_navigation mock_localization_pose_publisher_node \
  --ros-args \
  --params-file src/navigation/spot_navigation/config/mock_localization_pose_publisher.param.yaml
```

## 9-4. Path Progress Tracker 실행

```bash
ros2 run spot_navigation path_progress_tracker_node \
  --ros-args \
  --params-file src/navigation/spot_navigation/config/path_progress_tracker.param.yaml
```

## 9-5. Local Path Planner 실행

```bash
ros2 run spot_navigation local_path_planner_node \
  --ros-args \
  --params-file src/navigation/spot_navigation/config/local_path_planner.param.yaml
```

## 9-6. Navigation Debug Visualizer 실행

```bash
ros2 run spot_navigation navigation_debug_visualizer_node \
  --ros-args \
  --params-file src/navigation/spot_navigation/config/navigation_debug_visualizer.param.yaml
```

---

## 10. 통합 실행 전 확인 사항

실제 통합 환경에서는 아래 토픽들이 먼저 정상 publish되어야 한다.

| Topic | 필요 이유 |
|---|---|
| `/planning/global_path/spot_01` | PathProgress 계산용 Global Path |
| `/localization/pose` | 현재 로봇 위치와 yaw |
| `/perception/lidar/obstacle_model` | front/left/right 대표 장애물 판단 |
| `/perception/lidar/free_space_model` | 회피 가능한 방향과 clearance 판단 |

확인 명령어는 다음과 같다.

```bash
ros2 topic list | grep planning
ros2 topic list | grep localization
ros2 topic list | grep perception
ros2 topic list | grep navigation
```

```bash
ros2 topic hz /planning/global_path/spot_01
ros2 topic hz /localization/pose
ros2 topic hz /perception/lidar/obstacle_model
ros2 topic hz /perception/lidar/free_space_model
ros2 topic hz /navigation/path_progress/spot_01
ros2 topic hz /navigation/local_path/spot_01
ros2 topic hz /navigation/local_planner_status/spot_01
```

---

## 11. 디버깅 명령어

### PathProgress 확인

```bash
ros2 topic echo /navigation/path_progress/spot_01
```

중점 확인 필드:

```text
path_received
pose_received
path_valid
pose_valid
nearest_index
target_index
target_heading_rad
heading_error_rad
progress_ratio
distance_to_goal_m
goal_reached
```

### LocalPlannerStatus 확인

```bash
ros2 topic echo /navigation/local_planner_status/spot_01
```

중점 확인 필드:

```text
planner_state
local_path_available
stop_required
blocked
local_target_x_m
local_target_y_m
selected_heading_rad
distance_to_local_target_m
local_heading_error_rad
used_global_sub_goal
used_free_space_heading
used_rejoin_target
reason
```

### Local Path 확인

```bash
ros2 topic echo /navigation/local_path/spot_01
```

### Perception 입력 확인

```bash
ros2 topic echo /perception/lidar/obstacle_model
ros2 topic echo /perception/lidar/free_space_model
```

### RViz 표시 토픽

RViz Fixed Frame은 `mission_map`으로 설정한다.

| RViz Display | Topic |
|---|---|
| Path | `/debug/navigation/global_path` |
| Pose | `/debug/navigation/pose` |
| MarkerArray | `/debug/navigation/progress_markers` |
| Path | `/navigation/local_path/spot_01` |

---

## 12. QoS 설계

| 데이터 | QoS 방향 | 이유 |
|---|---|---|
| Global Path | `reliable + transient_local` | 경로는 기준 데이터이므로 늦게 실행된 subscriber도 마지막 path를 받아야 함 |
| Localization Pose | `best_effort` 또는 `reliable` | 실시간 상태값이므로 최신 pose 수신이 중요 |
| PathProgress | `reliable` | Local Planner 판단 입력이므로 안정적으로 전달 필요 |
| Local Path / Status | `reliable` | Controller, Debug, Safety 계층에서 상태 누락 방지 필요 |
| Debug Path | `reliable + transient_local` | RViz를 늦게 켜도 마지막 path 확인 가능 |
| Debug Marker | `reliable` | 시각화 상태 안정성 확보 |

---

## 13. 안전 동작 기준

| 상황 | 처리 |
|---|---|
| PathProgress invalid | `INVALID_INPUT`, stop required |
| Pose invalid | hold path 생성 불가 시 empty path publish |
| ObstacleModel timeout | `INVALID_INPUT` |
| FreeSpaceModel timeout | `INVALID_INPUT` |
| 장애물 danger + free-space 없음 | `BLOCKED`, hold path publish |
| goal reached | `GLOBAL_GOAL_REACHED`, hold path publish |
| 회피 중 side obstacle 미해소 | AVOIDANCE 유지 또는 BLOCKED 유지 |
| 회피 직후 latch 미완료 | REJOIN 금지, AVOIDANCE 유지 |

---

## 14. 현재 구현 한계 및 TODO

| 항목 | 설명 |
|---|---|
| 실제 Controller 연동 | 현재 Local Path 생성까지 담당하며, 최종 cmd_vel 생성은 follower/controller 계층에서 수행해야 함 |
| 파라미터 튜닝 | `front_clear_distance_m`, `side_clear_distance_m`, `avoidance_min_clearance_m`, `avoidance_horizon_m`은 실측 주행에서 조정 필요 |
| Progress Ratio | 현재는 index 기반 진행률이며, 추후 waypoint 누적 거리 기반으로 개선 가능 |
| 측면 장애물 민감도 | `side_block_distance_m`이 크면 벽이나 주변 구조물에 과민 반응할 수 있음 |
| FreeSpaceModel 의존성 | 회피 성능은 LiDAR OccupancyGrid 및 FreeSpaceModel 품질에 크게 의존함 |
| Launch 통합 | 실제 perception/localization/planning launch와 함께 최종 bringup launch 정리 필요 |
| 다중 로봇 확장 | `robot_id`, topic namespace, frame 정책을 robot별로 확장해야 함 |

---

## 15. 패키지 개발 기준

`spot_navigation`은 다음 원칙을 기준으로 개발되었다.

1. **입력 유효성 분리**
   - 데이터를 받지 못한 상태와 정상적으로 “장애물이 없음” 상태를 구분한다.

2. **좌표계 책임 분리**
   - Global Path와 Local Path는 `mission_map` 기준이다.
   - LiDAR Perception은 `base_link` 기준이다.
   - 좌표계 변환 책임은 Local Planner에서 명시적으로 처리한다.

3. **상태와 판단 근거 publish**
   - 단순히 Path만 publish하지 않고, `LocalPlannerStatus`를 함께 publish하여 왜 해당 path가 생성되었는지 확인 가능하게 한다.

4. **안전 우선**
   - 입력이 불안정하거나 회피 가능한 free-space가 없으면 hold path와 stop flag를 사용한다.

5. **Mock 기반 독립 검증**
   - 실제 Planning/Localization 노드가 완성되지 않아도 Navigation 파이프라인을 독립적으로 검증할 수 있도록 mock node를 제공한다.

---

## 16. 담당 구현 파일

| 파일 | 역할 |
|---|---|
| `mock_global_path_publisher_node.cpp/hpp` | Mock Global Path publish |
| `mock_localization_pose_publisher_node.cpp/hpp` | Mock Localization Pose publish |
| `path_progress_tracker_node.cpp/hpp` | Global Path 진행 상태 계산 |
| `local_path_planner_node.cpp/hpp` | Local Path 생성 및 FSM 판단 |
| `navigation_debug_visualizer_node.cpp/hpp` | RViz 디버그 시각화 변환 |
| `*.param.yaml` | 각 노드별 실행 파라미터 관리 |

---

## 17. 요약

`spot_navigation`은 프로젝트에서 Planning, Localization, LiDAR Perception을 연결하여 실제 로봇이 따라갈 수 있는 Local Path를 생성하는 Navigation 패키지다. 초기 MVP에서는 Global Path target을 Local Path로 변환하는 기능에서 시작했으며, 현재는 LiDAR 기반 ObstacleModel과 FreeSpaceModel을 결합하여 장애물 회피, 회피 유지, Global Path 복귀, 주행 불가 상태까지 판단하는 구조로 고도화되었다.

이 패키지는 향후 Safety Supervisor, Motion Controller, Mission Executor와 연결되어 자율주행 로봇의 판단-주행 계층을 구성하는 핵심 모듈로 확장될 수 있다.
