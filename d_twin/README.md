# d_twin

Isaac Sim 기반 디지털 트윈 구성입니다. 로봇 상태를 ROS2로 수신하여 Isaac Sim 내 Spot prim에 반영하고, 시뮬레이션 로봇의 이동을 시각화합니다.

## 구조

```
d_twin/
├── isaac_projects/   # Isaac Sim 씬 파일(.usd) 및 실행 스크립트
└── scripts/          # Isaac Sim Script Node용 Python 스크립트
```

## 하위 디렉토리 요약

### `isaac_projects/`
- Isaac Sim 씬 파일과 실행 스크립트 관리
- `launch_isaac.sh`로 Isaac Sim 실행 환경을 준비
- `scenes/`에 시뮬레이션 씬이 위치

### `scripts/`
- Isaac Sim Action Graph의 Script Node에서 사용되는 Python 코드
- ROS2 토픽 구독 및 prim 위치/방향 반영 로직 포함
- 스크립트 수정 시 Isaac Sim 재로드 또는 씬 재생성 필요

## 요구 환경
- Isaac Sim 5.1.0
- ROS2 Humble
- Python 3.11 (Isaac Sim 내장 Python 사용)
- `robot_ws_311/` 또는 동일한 ROS2 인터페이스 빌드 환경

## 설정 및 실행

### 1. ROS2 환경 준비
- `robot_ws_311` 또는 ROS2 Humble 워크스페이스를 빌드하고 소스합니다.
  ```bash
  cd /home/ubuntu/SPOT_GET_IT/robot_ws_311
  rosdep install --from-paths src --ignore-src -r -y
  colcon build --symlink-install
  source install/setup.bash
  ```

### 2. d_twin 실행
- ROS2 노드 실행
  ```bash
  ros2 launch global_path_manager global_path_manager.launch.py
  ros2 launch sim_executor sim_executor.launch.py
  ```
- Isaac Sim 실행
  ```bash
  /home/ubuntu/SPOT_GET_IT/d_twin/isaac_projects/launch_isaac.sh
  ```
- Isaac Sim에서 Play 버튼 실행 후 ScriptNode 경고가 뜨면 `Yes` 선택

## 빌드 / 포팅 가이드

### 새로운 머신으로 포팅
1. 동일한 Isaac Sim 버전 설치
2. ROS2 Humble 및 `robot_ws_311` 환경 구성
3. `d_twin` 디렉토리 전체 복제
4. `isaac_projects/launch_isaac.sh` 내 경로가 로컬 경로와 일치하는지 확인
5. Isaac Sim 실행 후 `scripts/` 경로가 씬 내 Script Node에 올바르게 설정되었는지 검증

### ROS2/Isaac Sim 연동 포인트
- ROS2 토픽 수신 경로 확인: `/localization/robot/state` 등
- `scripts/`의 구독 노드와 메시지 타입이 `robot_interfaces` 빌드와 일치해야 함
- Isaac Sim 씬 내부의 Script Node가 `scripts/` 경로를 참조하도록 설정

### 포팅 시 주의 사항
- `robot_ws_311`과 `d_twin` 간 ROS2 메시지/서비스 정의 버전 일치
- Isaac Sim에서 사용하는 Python 버전과 외부 패키지 호환성
- 네트워크 연결이 필요한 경우 ROS2 DDS 설정, IP/멀티캐스트 구성 확인
- 씬 파일 경로가 바뀌었을 때 `launch_isaac.sh`와 Isaac Sim 프로젝트 설정을 함께 수정

## 참고
- `d_twin/isaac_projects/README.md`에 Isaac Sim 씬별 세부 안내가 있을 수 있습니다.
- `scripts/` 코드 변경 시 Isaac Sim을 재실행하거나 씬을 재로드하여 변경 사항이 반영되었는지 확인하십시오.
