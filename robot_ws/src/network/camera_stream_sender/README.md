# camera_stream_sender

## 역할

`/perception/camera/encoded` (CompressedImage) 를 구독하여
JPEG 바이트를 proto.h IMAGE 포맷으로 분할 후 UDP로 RPi BridgeDaemon에 송신한다.
재압축 없이 그대로 전송하므로 CPU 부담 최소.

## Subscribe

| Topic | Type | 설명 |
|-------|------|------|
| `/perception/camera/encoded` | `sensor_msgs/msg/CompressedImage` | 압축된 카메라 이미지 |

## Publish

없음

## Required Params

| 파라미터 | 기본값 | 설명 |
|----------|--------|------|
| `rpi_ip` | `"192.168.0.13"` | RPi BridgeDaemon IP |
| `robot_id` | `0` | BridgeDaemon robot_id와 일치 |
| `max_size_bytes` | `200000` | 프레임 크기 상한 (200KB) |

## 실행

```bash
cd ~/robot_ws
colcon build --packages-select camera_stream_sender
source install/setup.bash
ros2 launch camera_stream_sender camera_stream_sender.launch.py
```

## 전제 조건

- `/oak_camera_driver_node` 실행 중
- RPi BridgeDaemon 실행 중 (포트 9000 수신 대기)
