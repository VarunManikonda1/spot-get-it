# Obstacle Model - lidar_perception

`lidar_perception` 패키지는 CygLiDAR D1 기반 LiDAR 데이터를 로봇의 `base_link` 기준으로 전처리하고, 장애물 클러스터, 장애물 요약 모델, 로컬 OccupancyGrid, FreeSpaceModel, RViz Marker를 생성하는 Perception 패키지이다.

본 패키지는 다중 정찰 로봇 시스템에서 로봇 주변의 장애물과 통과 가능 공간을 판단하기 위한 저수준/중간수준 환경 모델을 제공한다.

---

## 1. Package Goal

이 패키지의 목표는 다음과 같다.

```text
Raw LiDAR PointCloud
        ↓
base_link 기준 전처리 PointCloud
        ↓
장애물 클러스터링
        ↓
장애물 방향/거리 요약
        ↓
로컬 2D OccupancyGrid 생성
        ↓
통과 가능한 Free-Space Gap 추출
        ↓
Decision / Planning 계층 입력 제공
```

즉, 이 패키지는 로봇에게 직접 제어 명령을 내리는 패키지가 아니라, 후속 `decision`, `planning`, `control` 계층이 사용할 수 있는 Perception 모델을 생성하는 역할을 담당한다.

---

## 2. Coordinate Convention

모든 주요 출력은 `base_link` 기준으로 해석한다.

```text
base_link 기준 좌표계

x+ : 로봇 전방
y+ : 로봇 좌측
y- : 로봇 우측
z+ : 위쪽
```

LiDAR 센서는 `laser_frame`에 있고, launch에서 static TF로 `base_link → laser_frame` 관계를 제공한다.

현재 기본 static TF 예시:

```text
base_link → laser_frame
translation: x=0.14, y=0.00, z=0.05
rotation   : roll=0, pitch=0, yaw=0
```

---

## 2.1 Coordinate Frame Relationship

`lidar_perception` 패키지의 핵심 좌표계는 `base_link`와 `laser_frame`이다.

현재 launch 파일에서는 `tf2_ros/static_transform_publisher`를 통해 `base_link → laser_frame` 정적 TF를 제공한다.

```text
parent frame : base_link
child frame  : laser_frame

translation:
  x = 0.14 m
  y = 0.00 m
  z = 0.05 m

rotation:
  roll  = 0
  pitch = 0
  yaw   = 0
```

즉, `laser_frame`은 `base_link` 기준으로 전방 `0.14m`, 위쪽 `0.05m` 위치에 있다고 가정한다.

### Frame Tree

```text
base_link
   └── laser_frame
```

현재 `lidar_perception` 패키지에서는 `map`, `odom` 기준의 전역 좌표계를 사용하지 않는다.
이 패키지의 출력은 대부분 로봇 중심의 local perception 결과이며, 기준 frame은 `base_link`이다.

```text
Global frame 계층
  map / odom
     └── 아직 사용하지 않음

Local perception frame
  base_link
     └── laser_frame
```

### Top View

```text
위에서 내려다본 좌표계

                    x+ 전방
                      ↑
                      |
                      |
              laser_frame
                  ▲
                  | 0.14 m
                  |
        y+ ← ----- base_link ----- → y-
       좌측             R             우측


base_link:
  - 로봇 중심 기준 frame
  - x+ : 전방
  - y+ : 좌측
  - y- : 우측

laser_frame:
  - LiDAR 센서 frame
  - base_link보다 0.14m 앞에 위치
```

### Side View

```text
측면에서 본 좌표계

                 z+ 위쪽
                   ↑
                   |
             laser_frame
                   ▲
                   | 0.05 m
                   |
                base_link
                   |
                   └────────────→ x+ 전방
```

### Data Frame Flow

Raw LiDAR 데이터는 처음에는 LiDAR 센서 frame 기준으로 들어오고, `pointcloud_preprocess_node`에서 `base_link` 기준으로 변환된다. `pointcloud_preprocess_node`는 `target_frame` 파라미터를 사용하며, 현재 기본 설정은 `base_link`이다.

```text
/scan_3D
  frame_id = laser_frame
        ↓
TF transform
  laser_frame → base_link
        ↓
/perception/lidar/points_filtered
  frame_id = base_link
```

이후 노드들은 `/perception/lidar/points_filtered`가 이미 `base_link` 기준이라고 가정한다.

```text
/perception/lidar/points_filtered
  frame_id = base_link
        ↓
obstacle_cluster_node
        ↓
obstacle_model_node

/perception/lidar/points_filtered
  frame_id = base_link
        ↓
local_occupancy_grid_node
        ↓
free_space_model_node
        ↓
free_space_marker_node
```

### OccupancyGrid Frame

`local_occupancy_grid_node`가 생성하는 `/perception/lidar/local_occupancy_grid`도 `base_link` 기준 local map이다.

즉, OccupancyGrid의 `origin`은 전역 map origin이 아니라, `base_link` 기준 local grid의 시작점이다.

예시:

```text
origin_x = -0.5
origin_y = -2.5
resolution = 0.1
width  = 45
height = 50
```

이 경우 local grid 범위는 다음과 같다.

```text
x range = -0.5 m ~ 4.0 m
y range = -2.5 m ~ 2.5 m
```

좌표계 관점에서 보면 다음과 같다.

```text
OccupancyGrid top view

                   x+ 전방
                     ↑
                     |
      y+ 좌측        |        y- 우측
          ←----------R----------→
                     |
                     |

grid x:
  base_link 기준 전후 방향

grid y:
  base_link 기준 좌우 방향

cell value:
  -1  = unknown
   0  = free
 100  = occupied
```

### FreeSpaceModel Angle Convention

`free_space_model_node`는 OccupancyGrid cell의 중심 좌표 `(x, y)`를 이용해 방향각을 계산한다.

```text
angle = atan2(y, x)
```

각도 기준은 다음과 같다.

```text
0 deg     : 정면, x+
positive  : 좌측, y+
negative  : 우측, y-
```

예시:

```text
+30 deg  → 좌측 전방
  0 deg  → 정면
-30 deg  → 우측 전방
```

FreeSpaceModel의 `best_heading_angle_deg`도 같은 기준을 따른다.

```text
best_heading_angle_deg > 0
  → 좌측 방향이 더 좋은 진행 후보

best_heading_angle_deg = 0
  → 정면 방향이 가장 좋은 진행 후보

best_heading_angle_deg < 0
  → 우측 방향이 더 좋은 진행 후보
```

### Important Notes

현재 `lidar_perception` 패키지는 전역 위치 추정이나 SLAM을 수행하지 않는다.

따라서 `/perception/lidar/local_occupancy_grid`, `/perception/lidar/free_space_model`, `/perception/lidar/free_space_markers`는 모두 로봇 주변의 **local perception 결과**로 해석해야 한다.

```text
현재 범위:
  base_link 기준 local obstacle / free-space perception

아직 포함하지 않는 범위:
  map 기준 global localization
  odom 누적 이동
  global path planning
```

향후 localization 패키지가 추가되면 다음과 같은 구조로 확장할 수 있다.

```text
map
 └── odom
      └── base_link
            └── laser_frame
```

---

## 3. Overall Pipeline

### 3.1 Raw LiDAR Pipeline

```text
CygLiDAR Driver
  └── publish: /scan_3D
        ↓
Static TF
  └── base_link ↔ laser_frame 관계 제공
        ↓
pointcloud_preprocess_node
  └── subscribe: /scan_3D
  └── publish  : /perception/lidar/points_filtered
```

### 3.2 Obstacle Model Pipeline

```text
/perception/lidar/points_filtered
        ↓
obstacle_cluster_node
  └── publish: /perception/lidar/obstacle_clusters
  └── publish: /perception/lidar/clustered_points_colored
        ↓
obstacle_model_node
  └── publish: /perception/lidar/obstacle_model
```

### 3.3 Local OccupancyGrid / FreeSpace Pipeline

```text
/perception/lidar/points_filtered
        ↓
local_occupancy_grid_node
  └── publish: /perception/lidar/local_occupancy_grid
        ↓
free_space_model_node
  └── publish: /perception/lidar/free_space_model
        ↓
free_space_marker_node
  └── publish: /perception/lidar/free_space_markers
```

---

## 4. Topic Summary

| Topic | Type | Publisher | Description |
|---|---|---|---|
| `/scan_3D` | `sensor_msgs/msg/PointCloud2` | CygLiDAR Driver | Raw LiDAR point cloud |
| `/perception/lidar/points_filtered` | `sensor_msgs/msg/PointCloud2` | `pointcloud_preprocess_node` | ROI, TF, voxel filtering 완료 point cloud |
| `/perception/lidar/obstacle_clusters` | `robot_interfaces/msg/ObstacleClusters` | `obstacle_cluster_node` | 클러스터 descriptor 목록 |
| `/perception/lidar/clustered_points_colored` | `sensor_msgs/msg/PointCloud2` | `obstacle_cluster_node` | RViz 디버깅용 colored cluster cloud |
| `/perception/lidar/obstacle_model` | `robot_interfaces/msg/ObstacleModel` | `obstacle_model_node` | front/left/right 대표 장애물 요약 |
| `/perception/lidar/local_occupancy_grid` | `nav_msgs/msg/OccupancyGrid` | `local_occupancy_grid_node` | base_link 기준 2D local occupancy grid |
| `/perception/lidar/free_space_model` | `robot_interfaces/msg/FreeSpaceModel` | `free_space_model_node` | 통과 가능한 gap, best heading, risk level |
| `/perception/lidar/free_space_markers` | `visualization_msgs/msg/MarkerArray` | `free_space_marker_node` | FreeSpaceModel RViz 시각화 |

---

## 5. Node Details

---

## 5.1 `pointcloud_preprocess_node`

### Role

Raw LiDAR PointCloud2를 수신하여 `base_link` 기준으로 변환하고, ROI crop 및 voxel downsampling을 수행한다.

### Input

```text
/scan_3D
```

### Output

```text
/perception/lidar/points_filtered
```

### Processing Flow

```text
[1] /scan_3D 수신
[2] TF lookup: source frame → base_link
[3] PointCloud2 TF transform
[4] ROS PointCloud2 → PCL PointCloud 변환
[5] ROI crop
[6] voxel downsampling
[7] PCL PointCloud → ROS PointCloud2 변환
[8] /perception/lidar/points_filtered publish
```

### Main Parameters

`config/pointcloud_preprocess.param.yaml`

```yaml
pointcloud_preprocess_node:
  ros__parameters:
    target_frame: "base_link"

    roi_min_x: 0.35
    roi_max_x: 1.5

    roi_min_y: -1.0
    roi_max_y:  1.0

    roi_min_z: 0.05
    roi_max_z: 1.0

    voxel_leaf_size: 0.05
```

### Notes

- 현재 ROI는 Spot 기준 전방 제한 영역이다.
- `roi_max_x`가 1.5m이면 후속 LocalOccupancyGrid/FreeSpaceModel도 실제 관측 evidence는 주로 1.5m 이내에서 생성된다.
- FreeSpaceModel의 `max_check_range`는 전처리 ROI와 함께 튜닝하는 것이 좋다.

---

## 5.2 `obstacle_cluster_node`

### Role

`/perception/lidar/points_filtered`를 입력으로 받아 PCL Euclidean Clustering을 수행하고, 각 장애물 클러스터의 descriptor를 계산한다.

### Input

```text
/perception/lidar/points_filtered
```

### Output

```text
/perception/lidar/obstacle_clusters
/perception/lidar/clustered_points_colored
```

### Processing Flow

```text
[1] PointCloud2 → PCL PointCloud 변환
[2] KD-Tree 생성
[3] Euclidean Cluster Extraction
[4] 각 cluster의 nearest point 계산
[5] nearest distance 기준 cluster 정렬
[6] cluster descriptor 계산
    - centroid
    - nearest point
    - centroid distance
    - nearest distance
    - azimuth
    - elevation
    - bbox size
    - sector
    - sector_mask
[7] cluster tracking 적용
[8] colored point cloud 생성
[9] ObstacleClusters publish
[10] clustered_points_colored publish
```

### Cluster Descriptor

각 cluster는 다음 정보를 가진다.

| Field | Meaning |
|---|---|
| `cluster_id` | 현재 frame에서 nearest distance 기준으로 부여되는 ID |
| `track_id` | tracking 기반으로 유지되는 ID |
| `tracked` | 기존 track과 매칭되었는지 여부 |
| `point_count` | cluster를 구성하는 point 개수 |
| `centroid_x/y/z` | cluster 중심점 |
| `nearest_x/y/z` | base_link 기준 가장 가까운 point |
| `centroid_distance_xy` | centroid의 xy 평면 거리 |
| `nearest_distance_xy` | nearest point의 xy 평면 거리 |
| `azimuth_angle_rad` | nearest point 기준 수평 방향각 |
| `elevation_angle_rad` | nearest point 기준 수직 방향각 |
| `bbox_size_x/y/z` | cluster bounding box 크기 |
| `sector` | nearest point 기준 대표 sector |
| `sector_mask` | cluster 내부 point들이 점유하는 모든 sector bit mask |

### Sector Bit Mask

현재 sector mask는 bit flag 방식이다.

```text
RIGHT = 1
FRONT = 2
LEFT  = 4
```

예시:

```text
FRONT only             = 2
RIGHT only             = 1
LEFT only              = 4
FRONT + RIGHT          = 2 | 1 = 3
FRONT + LEFT           = 2 | 4 = 6
LEFT + FRONT + RIGHT   = 7
```

### Representative Sector

`sector`는 cluster의 nearest point 방향각 기준으로 하나의 대표 sector를 나타낸다.

`sector_mask`는 cluster 내부 모든 point의 sector 점유 정보를 표현한다.

따라서 큰 물체가 front/right 경계에 걸친 경우:

```text
sector      = nearest point가 속한 대표 방향
sector_mask = 실제 점유한 모든 방향
```

으로 해석한다.

### Tracking

`obstacle_cluster_node`는 간단한 centroid 기반 tracking을 적용한다.

| Parameter | Meaning |
|---|---|
| `tracking_enabled` | tracking 사용 여부 |
| `color_by_track_id` | RViz 색상을 track_id 기준으로 부여할지 여부 |
| `tracking_match_distance` | 기존 track과 새 cluster 매칭 허용 거리 |
| `tracking_max_missed_frames` | 몇 frame 동안 미검출되어도 track 유지할지 |
| `tracking_smoothing_alpha` | track centroid smoothing 계수 |

`cluster_id`는 현재 frame에서 가까운 순서로 부여되는 디버깅용 ID이고, `track_id`는 시간적으로 동일 물체를 유지하기 위한 ID이다.

---

## 5.3 `obstacle_model_node`

### Role

`ObstacleClusters`를 입력으로 받아 front/left/right sector별 대표 장애물을 선택하고, `ObstacleModel`로 요약한다.

### Input

```text
/perception/lidar/obstacle_clusters
```

### Output

```text
/perception/lidar/obstacle_model
```

### Processing Flow

```text
[1] ObstacleClusters 수신
[2] front/left/right별 대표 cluster 초기화
[3] 각 cluster의 sector_mask 확인
[4] sector별 nearest_distance_xy가 가장 가까운 cluster 선택
[5] ObstacleModel publish
```

### Selection Rule

```text
해당 sector_mask를 점유하는 cluster 중
nearest_distance_xy가 가장 작은 cluster를 대표 장애물로 선택
```

따라서 하나의 cluster가 여러 sector를 점유하면, 여러 sector의 대표 후보가 될 수 있다.

### Output Meaning

| Field | Meaning |
|---|---|
| `obstacle_detected` | front/left/right 중 하나라도 장애물이 있는지 |
| `front` | front sector 대표 obstacle cluster |
| `left` | left sector 대표 obstacle cluster |
| `right` | right sector 대표 obstacle cluster |

---

## 5.4 `local_occupancy_grid_node`

### Role

`/perception/lidar/points_filtered`를 base_link 기준 2D local OccupancyGrid로 변환한다.

### Input

```text
/perception/lidar/points_filtered
```

### Output

```text
/perception/lidar/local_occupancy_grid
```

### Grid Convention

Local grid는 LiDAR FOV 모양의 부채꼴 grid가 아니라, `base_link` 기준 직사각형 local map이다.

예시 기본 설정:

```text
origin_x = -0.5
origin_y = -2.5
size_x   = 4.5
size_y   = 5.0
resolution = 0.10
```

결과:

```text
x range = -0.5m ~ 4.0m
y range = -2.5m ~ 2.5m

width  = 45 cells
height = 50 cells
total  = 2250 cells
```

### Cell Value

| Value | Meaning |
|---|---|
| `-1` | unknown |
| `0` | free |
| `100` | occupied |

### Processing Flow

```text
[1] PointCloud2 수신
[2] OccupancyGrid 생성
[3] grid 초기화
    - use_ray_tracing=true이면 unknown 초기화
    - use_ray_tracing=false이면 free 초기화
[4] 각 point의 x,y를 grid index로 변환
[5] sensor_origin → endpoint ray 경로를 free로 표시
[6] endpoint cell을 occupied 후보로 저장
[7] occupied 후보 중복 제거
[8] occupied 또는 inflated occupied 처리
[9] final unknown/free/occupied cell count 계산
[10] OccupancyGrid publish
```

### Ray Tracing

현재 LocalOccupancyGrid는 endpoint 기반 ray tracing 구조이다.

```text
sensor_origin → point endpoint
```

경로는 free로 표시하고, endpoint는 occupied로 표시한다.

중요한 한계:

```text
return point가 없으면 free ray도 생성되지 않는다.
따라서 장애물이 없는 평지라도 해당 방향이 unknown으로 남을 수 있다.
```

이 한계는 후속 `free_space_model_node`에서 unknown을 caution 후보로 해석하여 완화한다.

### Optimizations Applied

- `occupied_candidates` 중복 제거
- 같은 grid cell에 여러 point가 들어오는 경우 occupied 후보를 1번만 저장
- `final_unknown/free/occupied` cell count 로그 추가
- frame mismatch warning 추가
- sensor origin outside grid warning 추가

---

## 5.5 `free_space_model_node`

### Role

`local_occupancy_grid_node`가 생성한 2D grid를 기반으로, 실제 통과 가능한 free-space gap 후보를 추출하고 대표 진행 방향을 산출한다.

기존 front/left/right 3-sector 방식이 아니라, Angular Bin + Gap Extraction 구조로 재설계되었다.

### Input

```text
/perception/lidar/local_occupancy_grid
```

### Output

```text
/perception/lidar/free_space_model
```

### Core Concept

```text
OccupancyGrid
    ↓
cell별 distance / angle 계산
    ↓
LiDAR FOV를 angular bin 단위로 분할
    ↓
각 bin별 unknown/free/occupied 통계 계산
    ↓
blocked / candidate bin 판단
    ↓
연속된 candidate bin을 gap으로 묶음
    ↓
가장 높은 score의 gap 선택
    ↓
best_heading, clearance, risk_level publish
```

### Angular Bin

LiDAR FOV를 일정 각도 단위로 나눈 하나의 방향 구간이다.

예시:

```yaml
fov_min_deg: -60.0
fov_max_deg: 60.0
angular_bin_size_deg: 5.0
```

결과:

```text
-60~-55
-55~-50
...
-5~0
0~5
...
55~60

총 24개 bin
```

각 bin은 다음 통계를 가진다.

```text
total_count
unknown_count
free_count
occupied_count

unknown_ratio
free_ratio
occupied_ratio

nearest_occupied_distance
score
blocked
candidate
```

### GapCandidate

연속된 candidate bin들을 하나의 통과 가능 공간 후보로 묶은 것이다.

예시:

```text
bin 상태:
X X O O O O X X

O O O O = 하나의 candidate gap
```

Gap은 다음 정보를 가진다.

```text
start_angle
end_angle
center_angle
width_angle
min_clearance
free_ratio
unknown_ratio
occupied_ratio
score
```

### Score Formula

기본 score 공식:

```text
score =
  free_weight      * free_ratio
+ unknown_weight   * unknown_ratio
- occupied_weight  * occupied_ratio
+ clearance_weight * normalized_clearance
- heading_bias_weight * normalized_heading_penalty
```

기본 weight 기준:

```yaml
free_weight: 1.0
unknown_weight: 0.3
occupied_weight: 2.0
clearance_weight: 0.8
heading_bias_weight: 0.2
```

이론상 최고점은 다음 조건에서 나온다.

```text
free_ratio = 1.0
unknown_ratio = 0.0
occupied_ratio = 0.0
normalized_clearance = 1.0
heading_penalty = 0.0
```

기본 weight 기준 최고점:

```text
1.0 + 0.8 = 1.8
```

### Risk Level

`FreeSpaceModel.msg` 기준:

```text
0 = RISK_UNKNOWN
1 = RISK_SAFE
2 = RISK_CAUTION
3 = RISK_BLOCKED
```

해석:

| Risk | Meaning |
|---|---|
| `RISK_SAFE` | selected gap이 명확히 free이고 occupied/unknown 위험이 낮음 |
| `RISK_CAUTION` | path는 가능하지만 unknown 비율이 높거나 일부 위험 요소 있음 |
| `RISK_BLOCKED` | 통과 가능한 gap이 없거나 clearance가 너무 작음 |
| `RISK_UNKNOWN` | 명확한 판단 불가 |

### Main Output Fields

| Field | Meaning |
|---|---|
| `path_available` | 통과 가능한 gap이 존재하는지 |
| `risk_level` | SAFE / CAUTION / BLOCKED |
| `best_heading_angle_rad` | 선택된 gap의 중심 방향 |
| `best_heading_angle_deg` | 선택된 gap의 중심 방향 degree |
| `best_clearance` | selected gap 내부 최소 clearance |
| `best_score` | selected gap score |
| `best_free_ratio` | selected gap 내부 free 비율 |
| `best_unknown_ratio` | selected gap 내부 unknown 비율 |
| `best_occupied_ratio` | selected gap 내부 occupied 비율 |
| `candidate_gap_count` | 후보 gap 개수 |
| `selected_gap` | 최종 선택된 gap |
| `candidate_gaps` | 전체 후보 gap 목록 |
| `bin_count` | angular bin 개수 |
| `candidate_bin_count` | candidate bin 개수 |
| `blocked_bin_count` | blocked bin 개수 |

### Recommended Parameters

`config/free_space_model.param.yaml`

```yaml
free_space_model_node:
  ros__parameters:
    input_topic: "/perception/lidar/local_occupancy_grid"
    output_topic: "/perception/lidar/free_space_model"
    frame_id: "base_link"

    unknown_value: -1
    free_value: 0
    occupied_value: 100

    fov_min_deg: -60.0
    fov_max_deg: 60.0
    angular_bin_size_deg: 5.0

    min_check_range: 0.20
    max_check_range: 1.50

    occupied_ratio_block_threshold: 0.08
    min_clearance_block_threshold: 0.40
    min_gap_width_deg: 10.0

    free_ratio_candidate_threshold: 0.10
    unknown_ratio_caution_threshold: 0.80

    free_weight: 1.0
    unknown_weight: 0.3
    occupied_weight: 2.0
    clearance_weight: 0.8
    heading_bias_weight: 0.2
```

---

## 5.6 `free_space_marker_node`

### Role

`FreeSpaceModel`은 커스텀 숫자 메시지이므로 RViz에서 바로 시각화되지 않는다.

`free_space_marker_node`는 이를 `visualization_msgs/msg/MarkerArray`로 변환하여 RViz에서 다음 요소를 시각화한다.

```text
1. best heading arrow
2. selected gap sector
3. candidate gaps
4. risk text
```

### Input

```text
/perception/lidar/free_space_model
```

### Output

```text
/perception/lidar/free_space_markers
```

### RViz Display

RViz에서 다음 display를 추가한다.

```text
Add → MarkerArray
Topic → /perception/lidar/free_space_markers
```

### Marker Meaning

| Marker | Meaning |
|---|---|
| arrow | best heading |
| selected gap sector | 최종 선택된 통과 가능 gap |
| candidate gap sectors | 후보 gap들 |
| text | risk level, path, heading, clearance, gap count |

### Recommended Parameters

`config/free_space_marker.param.yaml`

```yaml
free_space_marker_node:
  ros__parameters:
    input_topic: "/perception/lidar/free_space_model"
    output_topic: "/perception/lidar/free_space_markers"
    frame_id: "base_link"

    marker_z: 0.08

    sector_radius: 1.20
    arrow_length: 1.00

    text_x: 1.20
    text_y: 0.90
    text_z: 0.55

    text_follow_best_heading: true
    text_radius: 1.45
    text_scale_z: 0.08

    candidate_alpha: 0.35
    selected_alpha: 0.65

    marker_lifetime_sec: 0.30
```

---

## 6. Message Interfaces

This package uses custom messages from `robot_interfaces`.

### Obstacle Pipeline Messages

```text
ObstacleCluster.msg
ObstacleClusters.msg
ObstacleModel.msg
```

### FreeSpace Pipeline Messages

```text
FreeSpaceGap.msg
FreeSpaceModel.msg
```

### FreeSpaceGap

Represents one candidate free-space gap.

Main fields:

```text
start_bin
end_bin

start_angle_rad / deg
end_angle_rad / deg
center_angle_rad / deg
width_angle_rad / deg

min_clearance

free_ratio
unknown_ratio
occupied_ratio

score
selected
```

### FreeSpaceModel

Represents final free-space decision summary.

Main fields:

```text
path_available
risk_level

best_heading_angle_rad
best_heading_angle_deg

best_clearance
best_score
best_free_ratio
best_unknown_ratio
best_occupied_ratio

candidate_gap_count
selected_gap
candidate_gaps

bin_count
candidate_bin_count
blocked_bin_count
analyzed_cell_count

total_unknown_count
total_free_count
total_occupied_count

total_unknown_ratio
total_free_ratio
total_occupied_ratio
```

---

## 7. Build

새로운 msg를 추가했거나 수정했다면 `robot_interfaces`를 먼저 빌드한다.

```bash
cd ~/robot_ws
colcon build --packages-select robot_interfaces
source install/setup.bash
```

그 다음 `lidar_perception`을 빌드한다.

```bash
colcon build --packages-select lidar_perception
source install/setup.bash
```

한 번에 빌드할 수도 있다.

```bash
cd ~/robot_ws
colcon build --packages-select robot_interfaces lidar_perception
source install/setup.bash
```

---

## 8. Run

### 8.1 Base LiDAR Pipeline

```bash
ros2 launch lidar_perception lidar_base.launch.py
```

이 launch는 다음을 실행한다.

```text
[1] CygLiDAR driver
[2] base_link → laser_frame static TF
[3] pointcloud_preprocess_node
```

### 8.2 Obstacle Pipeline

```bash
ros2 run lidar_perception obstacle_cluster_node \
  --ros-args \
  --params-file ~/robot_ws/src/perception/lidar_perception/config/obstacle_cluster.param.yaml
```

```bash
ros2 run lidar_perception obstacle_model_node
```

### 8.3 Local OccupancyGrid Pipeline

```bash
ros2 run lidar_perception local_occupancy_grid_node \
  --ros-args \
  --params-file ~/robot_ws/src/perception/lidar_perception/config/local_occupancy_grid.param.yaml
```

### 8.4 FreeSpace Pipeline

```bash
ros2 run lidar_perception free_space_model_node \
  --ros-args \
  --params-file ~/robot_ws/src/perception/lidar_perception/config/free_space_model.param.yaml
```

```bash
ros2 run lidar_perception free_space_marker_node \
  --ros-args \
  --params-file ~/robot_ws/src/perception/lidar_perception/config/free_space_marker.param.yaml
```

---

## 9. Verification Commands

### Topic List

```bash
ros2 topic list | grep perception
```

### PointCloud Preprocess

```bash
ros2 topic hz /perception/lidar/points_filtered
ros2 topic echo /perception/lidar/points_filtered --once --field header
```

Expected:

```text
frame_id: base_link
```

### Obstacle Clusters

```bash
ros2 topic hz /perception/lidar/obstacle_clusters
ros2 topic echo /perception/lidar/obstacle_clusters --once
```

### Obstacle Model

```bash
ros2 topic hz /perception/lidar/obstacle_model
ros2 topic echo /perception/lidar/obstacle_model --once
```

### Local OccupancyGrid

```bash
ros2 topic type /perception/lidar/local_occupancy_grid
ros2 topic hz /perception/lidar/local_occupancy_grid
ros2 topic echo /perception/lidar/local_occupancy_grid --once --field info
```

Expected:

```text
type: nav_msgs/msg/OccupancyGrid
frame_id: base_link
resolution: 0.1
width: 45
height: 50
```

### FreeSpaceModel

```bash
ros2 topic type /perception/lidar/free_space_model
ros2 topic hz /perception/lidar/free_space_model
ros2 topic echo /perception/lidar/free_space_model --once
```

Key fields to check:

```text
path_available
risk_level
best_heading_angle_deg
best_clearance
best_score
candidate_gap_count
selected_gap
candidate_gaps
bin_count
candidate_bin_count
blocked_bin_count
```

### FreeSpace Marker

```bash
ros2 topic type /perception/lidar/free_space_markers
ros2 topic hz /perception/lidar/free_space_markers
```

Expected:

```text
visualization_msgs/msg/MarkerArray
```

---

## 10. RViz Setup

Recommended RViz fixed frame:

```text
Fixed Frame: base_link
```

Recommended Displays:

```text
Grid
PointCloud2: /perception/lidar/points_filtered
PointCloud2: /perception/lidar/clustered_points_colored
Map       : /perception/lidar/local_occupancy_grid
MarkerArray: /perception/lidar/free_space_markers
TF
```

### RViz Interpretation

```text
Map
- 회색: unknown
- 흰색: free
- 검정: occupied

Clustered PointCloud
- cluster별 색상 표시

FreeSpace Marker
- 부채꼴: selected/candidate gap
- 화살표: best heading
- 텍스트: SAFE / CAUTION / BLOCKED, heading, clearance, gap count
```

---

## 11. Verification Scenarios

### Scenario 1. No Obstacle

Expected:

```text
path_available = true
risk_level = CAUTION or SAFE
best_heading ≈ 0 deg
candidate_gap_count >= 1
```

LocalOccupancyGrid 특성상 장애물이 없어도 unknown이 많을 수 있으므로, `RISK_CAUTION`은 정상적인 결과일 수 있다.

### Scenario 2. Front Obstacle

Expected:

```text
front 방향 bin blocked 증가
best_heading이 좌측 또는 우측 열린 방향으로 이동
risk_level = CAUTION or BLOCKED
```

### Scenario 3. Left Obstacle

Expected:

```text
best_heading_angle_deg < 0
```

음수 heading은 오른쪽 방향을 의미한다.

### Scenario 4. Right Obstacle

Expected:

```text
best_heading_angle_deg > 0
```

양수 heading은 왼쪽 방향을 의미한다.

### Scenario 5. Fully Blocked

Expected:

```text
path_available = false
risk_level = RISK_BLOCKED
candidate_gap_count = 0
```

---

## 12. Tuning Guide

### `occupied_ratio_block_threshold`

Bin 내부 occupied cell 비율이 이 값 이상이면 blocked 처리한다.

```text
낮을수록 보수적
높을수록 관대
```

예시:

```text
0.03 = occupied가 3% 이상이면 blocked
0.08 = occupied가 8% 이상이면 blocked
```

### `min_clearance_block_threshold`

Bin 내부 가장 가까운 occupied cell이 이 거리보다 가까우면 blocked 처리한다.

```text
높을수록 보수적
낮을수록 관대
```

### `min_gap_width_deg`

연속 candidate bin의 각도 폭이 이 값보다 작으면 gap 후보에서 제외한다.

```text
높을수록 넓은 통로만 인정
낮을수록 좁은 통로도 인정
```

### `unknown_ratio_caution_threshold`

Selected gap의 unknown 비율이 이 값 이상이면 `RISK_CAUTION` 처리한다.

```text
낮을수록 caution이 쉽게 뜸
높을수록 safe가 쉽게 뜸
```

### `max_check_range`

FreeSpaceModel이 분석할 최대 거리이다.

전처리 ROI의 `roi_max_x`와 너무 크게 차이 나면, 관측되지 않은 unknown 영역이 많이 포함될 수 있다.

현재 전처리 ROI가 `roi_max_x: 1.5`라면 초기 검증에서는 다음이 적절하다.

```yaml
max_check_range: 1.50
```

---

## 13. Known Limitations

### 13.1 Endpoint-Based Ray Tracing Limitation

현재 LocalOccupancyGrid는 point endpoint 기반 ray tracing 구조이다.

```text
point return이 있는 방향:
  sensor_origin → endpoint 경로를 free로 표시

point return이 없는 방향:
  ray 자체를 생성하지 못하므로 unknown 유지
```

따라서 장애물이 없는 평지라도 free가 충분히 생성되지 않고 unknown이 많을 수 있다.

### 13.2 Unknown Handling

FreeSpaceModel은 unknown을 occupied처럼 즉시 blocked로 보지 않고, caution 후보로 남긴다.

```text
unknown 많음 + occupied 없음
→ path_available=true
→ risk_level=CAUTION
```

### 13.3 FreeSpaceModel은 Planning이 아님

FreeSpaceModel은 실제 path를 생성하지 않는다.

```text
FreeSpaceModel:
  "어느 방향이 통과 가능해 보이는가?"

Planning:
  "그 방향으로 어떤 경로점을 따라 이동할 것인가?"
```

### 13.4 Marker Node는 Debug Visualization

`free_space_marker_node`는 perception 결과를 RViz에 보기 좋게 표현하기 위한 디버깅 노드이다. 실제 주행 판단은 `/perception/lidar/free_space_model` 메시지를 기준으로 한다.

---

## 14. Current Status

### Completed

```text
[Raw LiDAR]
- /scan_3D 수신
- base_link TF 변환
- ROI crop
- voxel downsampling
- /points_filtered publish

[Obstacle Pipeline]
- Euclidean clustering
- nearest point 기반 descriptor
- sector_mask 기반 multi-sector 표현
- cluster tracking
- ObstacleModel sector 대표 장애물 선택

[Local OccupancyGrid]
- base_link 기준 local 2D OccupancyGrid 생성
- ray tracing free marking
- occupied endpoint marking
- obstacle inflation
- occupied candidate 중복 제거
- final cell count 로그 추가

[FreeSpaceModel]
- front/left/right 단순 sector 방식에서 angular bin + gap extraction 구조로 재설계
- candidate gap 추출
- selected gap 선택
- best heading 산출
- risk level 산출
- RViz MarkerArray 시각화
```

### Future Work

```text
- raw organized LiDAR beam 기반 no-return ray 처리
- LocalOccupancyGrid temporal smoothing
- robot footprint 기반 inflation
- FreeSpaceMarker에 blocked bin 시각화 추가
- FreeSpaceModel candidate gap score 튜닝
- LocalPathGenerator와 연동
- 통합 launch 파일 작성
```

---

## 15. Recommended Development Order

다음 계층 개발 순서는 아래를 권장한다.

```text
[1] lidar_perception README 문서화
[2] FreeSpaceModel threshold 튜닝
[3] FreeSpaceMarker blocked bin 시각화 추가
[4] Localization 패키지 설계
[5] Decision / Planning 인터페이스 정의
[6] LocalPathGenerator 개발
```

---

## 16. Quick Start

```bash
cd ~/robot_ws
source install/setup.bash
```

Terminal 1:

```bash
ros2 launch lidar_perception lidar_base.launch.py
```

Terminal 2:

```bash
ros2 run lidar_perception local_occupancy_grid_node \
  --ros-args \
  --params-file ~/robot_ws/src/perception/lidar_perception/config/local_occupancy_grid.param.yaml
```

Terminal 3:

```bash
ros2 run lidar_perception free_space_model_node \
  --ros-args \
  --params-file ~/robot_ws/src/perception/lidar_perception/config/free_space_model.param.yaml
```

Terminal 4:

```bash
ros2 run lidar_perception free_space_marker_node \
  --ros-args \
  --params-file ~/robot_ws/src/perception/lidar_perception/config/free_space_marker.param.yaml
```

Terminal 5:

```bash
rviz2
```

RViz Displays:

```text
Fixed Frame: base_link
Map: /perception/lidar/local_occupancy_grid
PointCloud2: /perception/lidar/points_filtered
MarkerArray: /perception/lidar/free_space_markers
```

