# lidar_sparse

## 역할

CygLiDAR D1의 3D 스캔 데이터(`/scan_3D`)를 구독하여
2단계 다운샘플링으로 공간 균일 포인트클라우드를 생성 후
`/network/lidar/points_sparse`로 퍼블리시한다.

`pointcloud_preprocess_node`(장애물 검출용)와 독립적으로
`/scan_3D`를 구독하므로 충돌하지 않는다.

**2단계 파이프라인:**
1. Voxel Grid (numpy 벡터화, O(N)) — 9600pts → ~300pts
2. Poisson Disk Sampling (O(M), M≪N) — ~300pts → ~150pts

## Subscribe

| Topic | Type | 설명 |
|-------|------|------|
| `/scan_3D` | `sensor_msgs/msg/PointCloud2` | D1 3D raw (vendor 토픽) |

## Publish

| Topic | Type | 설명 |
|-------|------|------|
| `/network/lidar/points_sparse` | `sensor_msgs/msg/PointCloud2` | 균일 다운샘플링 포인트 |

## Required Params

| 파라미터 | 기본값 | 설명 |
|----------|--------|------|
| `min_dist_m` | `0.05` | 거리 필터 하한 (m) |
| `max_dist_m` | `2.0` | 거리 필터 상한 (m) |
| `voxel_pre_r` | `0.08` | 1단계 voxel 셀 크기 (m) |
| `poisson_r` | `0.30` | 2단계 Poisson 최소 간격 (m) |
| `max_pts` | `400` | 출력 포인트 수 상한 |

## 실행

```bash
cd ~/robot_ws
colcon build --packages-select lidar_sparse
source install/setup.bash
ros2 launch lidar_sparse lidar_sparse.launch.py
```

## 의존 패키지

- `rclpy`, `sensor_msgs`, `python3-numpy`

## 전제 조건

- `/D1_Node` 실행 중
- `lidar_stream_sender`가 `/network/lidar/points_sparse` 구독 중
