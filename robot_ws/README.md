# robot_ws

Jetson Orin Nano용 ROS2 Humble 워크스페이스입니다.

## 개요
`robot_ws`는 ROS2 패키지의 빌드, 실행, 배포를 위한 메인 워크스페이스입니다. 주로 `bringup`, `control`, `localization`, `navigation`, `network`, `perception` 등의 핵심 기능을 포함합니다.

## 지원 환경
- ROS2 배포판: Humble Hawksbill
- 빌드 도구: `colcon`
- 타겟 OS: Ubuntu 22.04 (aarch64)
- 주 개발 플랫폼: Jetson Orin Nano

## 디렉토리 구조
- `src/`
  - `bringup/` — 통합 실행 및 런치 파일
  - `control/` — actuator_bridge, motion_manager, classic_control, rl_locomotion
  - `experimenter/` — 비디오, 라이다 수집기
  - `interfaces/robot_interfaces/` — 모든 `msg`, `srv`, `action`
  - `localization/` — 측위 모듈
  - `navigation/` — 경로 계획 및 네비게이션
  - `network/` — 카메라/라이다 스트리밍, 통신 브리징
  - `perception/` — 카메라/라이다 인식
  - `vendor/` — 외부 드라이버 (예: `oak_camera_driver`)

## 준비 사항
1. Ubuntu 22.04 aarch64 환경 설정
2. ROS2 Humble 설치
3. 필요한 시스템 패키지 설치
   ```bash
   sudo apt update
   sudo apt install -y build-essential python3-colcon-common-extensions python3-rosdep python3-vcstool git
   ```
4. `rosdep` 초기화 및 갱신
   ```bash
   sudo rosdep init
   rosdep update
   ```

## 워크스페이스 구성 및 빌드
1. 워크스페이스 루트로 이동
   ```bash
   cd /home/ubuntu/SPOT_GET_IT/robot_ws
   ```
2. 의존성 설치
   ```bash
   rosdep install --from-paths src --ignore-src -r -y
   ```
3. 빌드
   ```bash
   colcon build --symlink-install
   ```
4. 빌드 후 환경 설정
   ```bash
   source install/setup.bash
   ```

## 실행 예시
- 전체 워크스페이스 환경 소스
  ```bash
  source /home/ubuntu/SPOT_GET_IT/robot_ws/install/setup.bash
  ```
- 특정 런치 파일 실행
  ```bash
  ros2 launch bringup robot_bringup.launch.py
  ```

## 포팅 가이드
### 새로운 머신으로 포팅
1. 동일한 Ubuntu 22.04 aarch64 기반 환경 구성
2. ROS2 Humble 설치
3. 동일한 워크스페이스 구조 복제
4. `rosdep install --from-paths src --ignore-src -r -y` 실행
5. `colcon build --symlink-install`로 다시 빌드

### 다른 아키텍처에서 빌드할 때
- 이 워크스페이스는 aarch64 타겟으로 설계되어 있습니다.
- x86_64에서 개발하거나 검사할 경우, ROS2 Humble 및 패키지 호환성을 확인해야 합니다.
- 플랫폼 별 드라이버(`src/vendor`, `camera_stream_sender` 등)와 네이티브 바이너리 의존성을 주의하십시오.

### 빌드 이슈 해결 팁
- `rosdep` 오류가 발생하면 `rosdep update`를 다시 실행하고, `package.xml` 의존성을 확인하십시오.
- `colcon build`에서 특정 패키지만 다시 빌드하려면:
  ```bash
  colcon build --packages-select <package_name> --symlink-install
  ```
- `install/setup.bash`를 항상 새 터미널에서 소스하여 최신 환경을 반영하십시오.

## 참고
- 패키지 간 메시지/서비스 변경 시 `interfaces/robot_interfaces`부터 다시 빌드하십시오.
- 외부 드라이버는 `src/vendor/`에 위치하며, 가능한 원본을 유지하여 패치 관리를 간소화합니다.
