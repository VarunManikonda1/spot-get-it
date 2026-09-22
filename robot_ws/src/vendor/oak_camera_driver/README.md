# oak_camera_driver

## 역할

OAK-D Lite 카메라를 직접 점유하여 두 가지 출력을 담당한다.

- RGB RAW 스트림 → `/vendor/camera/image_raw` 토픽 (perception용)
- MJPEG 인코딩 스트림 → UDP 직접 송신 → RPi BridgeDaemon (영상 송출용)

카메라 제어 로직을 이 패키지에 격리함으로써 `camera_perception`이 독립적으로 스트림을 subscribe할 수 있다.

---

## Publish 토픽

| 토픽 | 타입 | fps | 설명 |
|------|------|-----|------|
| `/vendor/camera/image_raw` | `sensor_msgs/Image` | `image_raw_fps` (기본 5) | RAW BGR 스트림. perception 전용 |

> MJPEG encoded 스트림은 토픽으로 올리지 않고 UDP로 직접 송신한다.

---

## UDP 송신

| 항목 | 내용 |
|------|------|
| 목적지 | `rpi_ip:9000` (RPi BridgeDaemon) |
| 포맷 | proto.h PktHeader (24B) + JPEG payload |
| fps | 캡처 `fps`와 동일 (기본 15) |
| 분할 | MTU 1400B 단위 fragmentation |

---

## 파라미터

| 파라미터 | 타입 | 기본값 | 설명 |
|----------|------|--------|------|
| `fps` | int | 15 | 카메라 캡처 fps. encoded UDP 송신 fps와 동일 |
| `image_raw_fps` | int | 5 | `image_raw` publish fps. `fps`보다 크면 `fps`로 클램프 |
| `rgb_width` | int | 480 | RGB 출력 너비. `camera_perception` bbox 정규화 기준 해상도 |
| `rgb_height` | int | 270 | RGB 출력 높이. `camera_perception` bbox 정규화 기준 해상도 |
| `show_preview` | bool | false | imshow 미리보기 활성화. 원격 접속 환경에서는 false 유지 |
| `preview_width` | int | 480 | 미리보기 창 너비 |
| `preview_height` | int | 270 | 미리보기 창 높이 |
| `rpi_ip` | str | 필수 | RPi BridgeDaemon IP. 미설정 시 노드 fatal 종료 |
| `robot_id` | int | 0 | BridgeDaemon robot_id와 일치해야 함 |
| `max_size_bytes` | int | 200000 | 프레임 크기 상한 (bytes). 초과 시 해당 프레임 skip |

---

## 빌드 및 실행

```bash
cd ~/robot_ws
colcon build --packages-select oak_camera_driver
source install/setup.bash
```

```bash
ros2 launch oak_camera_driver oak_camera_driver.launch.py
```

---

## 토픽 확인

```bash
ros2 topic hz /vendor/camera/image_raw
```

---

## 의존성

| 패키지 | 용도 |
|--------|------|
| `depthai` | OAK-D Lite SDK (v3.5.0). **v2와 API 호환 불가, 반드시 v3 사용** |
| `rclpy` | ROS2 Python 클라이언트 |
| `sensor_msgs` | Image 메시지 타입 |
| `cv_bridge` | OpenCV ↔ ROS2 이미지 변환 |
| `opencv-python` | 이미지 처리 |

---

## 설계 결정

**encoded 토픽 제거, UDP 직접 송신 채택** : 토픽으로 올렸다가 별도 sender 노드가 subscribe하는 구조는 데이터 파이프가 불필요하게 길다. camera_stream_sender 패키지를 제거하고 송신 로직을 이 노드에 통합했다.

**depthai-ros 공식 드라이버 미사용** : DepthAI v3 API 기준 공식 ROS2 드라이버 지원 여부가 불확실하고, SDK 기반 동작이 이미 검증되어 SDK 직접 사용 방식을 채택했다.

**Stereo Depth 미포함** : OAK-D Lite 온보드 메모리 한계로 RGB + MJPEG + Stereo 동시 운용이 불가능하다. 영상 송출 우선순위가 거리 추정보다 높다고 판단하여 Stereo를 제외했다.

**image_raw와 encoded fps 분리** : `image_raw`는 perception 추론용으로 Jetson 부하 경감을 위해 낮은 fps로 publish하고, encoded는 영상 송출용으로 캡처 fps 그대로 유지한다.

---

## 트러블슈팅

| 증상 | 원인 | 해결 |
|------|------|------|
| `X_LINK_DEVICE_NOT_FOUND` | OAK-D Lite 미연결 또는 udev rules 미설정 | udev rules 추가 후 재연결 |
| H.264 인코더 해상도 오류 | H264는 width 32의 배수, height 8의 배수 필요 | 인코더 입력 해상도 자동 보정 (현재 코드에 적용됨) |
| Stereo 메모리 부족 | RGB + MJPEG + Stereo 동시 운용 불가 | Stereo 제거 |
| 원격 접속 끊김 | imshow 부하 | `show_preview: false` 유지 |
| 노드 fatal 종료 | `rpi_ip` 미설정 | yaml에 `rpi_ip` 값 입력 |
