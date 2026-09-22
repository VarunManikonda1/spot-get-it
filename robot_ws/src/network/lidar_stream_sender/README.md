# lidar_stream_sender

## 역할

CygLiDAR D1의 3D 스캔 데이터(`/scan_3D`)를 구독하여  
`proto.h` LiDAR 페이로드 포맷으로 변환 후 UDP로 RPi BridgeDaemon에 송신한다.

Jetson에서 실행되며 `vendor/cyglidar_d1_ros2` 드라이버가 먼저 실행되어 있어야 한다.

## Subscribe

| Topic | Type | 설명 |
|-------|------|------|
| `/scan_3D` | `sensor_msgs/msg/PointCloud2` | D1 3D 포인트클라우드 (vendor 토픽) |

## Publish

없음

## UDP 송신 포맷

```
RPi:9000 (BRIDGE_PORT)

패킷: [ PktHeader (24B) ][ payload 조각 ]

페이로드 포맷 (reassembly_shm.c commit_lidar 기준):
  [ uint32_t count ][ float x, y, z, intensity × count ]
```

## Required Params (`config/lidar_stream_sender.param.yaml`)

| 파라미터 | 타입 | 기본값 | 설명 |
|----------|------|--------|------|
| `rpi_ip` | string | `"192.168.0.10"` | RPi BridgeDaemon IP |
| `robot_id` | int | `0` | BridgeDaemon robot_id와 일치해야 함 |
| `max_pts` | int | `9600` | 송신 포인트 수 상한 (D1: 160×60) |
| `min_dist_m` | float | `0.05` | 유효 거리 하한 (m) |
| `max_dist_m` | float | `2.0` | 유효 거리 상한 (m) |
| `default_intensity` | float | `1.0` | intensity 필드 없을 때 기본값 |

## 실행

```bash
# 빌드
cd ~/robot_ws
colcon build --packages-select lidar_stream_sender
source install/setup.bash

# launch (권장)
ros2 launch lidar_stream_sender lidar_stream_sender.launch.py

# 파라미터 오버라이드
ros2 launch lidar_stream_sender lidar_stream_sender.launch.py \
  params_file:=/path/to/custom.yaml

# 직접 실행
ros2 run lidar_stream_sender lidar_stream_sender_node \
  --ros-args -p rpi_ip:=192.168.0.10 -p robot_id:=0
```

## 의존 패키지

- `rclpy`
- `sensor_msgs`
- `sensor_msgs_py`
- `python3-numpy`

## 전제 조건

- `vendor/cyglidar_d1_ros2` 드라이버 노드 실행 중 (`/D1_Node`)
- RPi BridgeDaemon 실행 중 (포트 9000 수신 대기)
