# camera_perception

## 역할

`oak_camera_driver`가 publish하는 RGB RAW 스트림을 subscribe하여 YOLOv11 + TensorRT(Jetson) 또는 ONNX Runtime(로컬 PC) 기반으로 인명 탐지를 수행한다.

N프레임 연속 탐지 조건을 충족하면 `/perception/person_detected/{robot_id}` 토픽에 `True`를 publish하고, 탐지가 끊기면 `False`를 publish한다. 상태가 유지되는 동안에는 발행하지 않는다.

---

## Subscribe 토픽

| 토픽 | 타입 | 설명 |
|------|------|------|
| `/vendor/camera/image_raw` | `sensor_msgs/Image` | RAW BGR 스트림 (oak_camera_driver 발행, 5fps) |

---

## Publish 토픽

| 토픽 | 타입 | 설명 |
|------|------|------|
| `/perception/person_detected/{robot_id}` | `std_msgs/Bool` | 인명 탐지 상태. 상태 변화 시점에만 발행 |

### 발행 규칙

| 상황 | 발행 | data |
|------|------|------|
| N프레임 연속 탐지 달성 시점 | ✅ 1회 | `True` |
| N프레임 초과 탐지 지속 | ❌ | - |
| 탐지 끊기는 시점 (True → False) | ✅ 1회 | `False` |
| 탐지 없는 상태 지속 | ❌ | - |

> `{robot_id}`는 파라미터로 주입된다. **기기마다 다른 값을 설정해야 한다.**

---

## 파라미터

| 파라미터 | 타입 | 기본값 | 설명 |
|----------|------|--------|------|
| `engine_path` | str | `/home/jetson/models/best.engine` | 엔진 파일 경로. 확장자로 백엔드 자동 분기 (`.engine` → TRT, `.onnx` → ONNX) |
| `conf_threshold` | float | 0.7 | 탐지 confidence 임계값 |
| `iou_threshold` | float | 0.45 | NMS IOU 임계값 |
| `infer_size` | int | 480 | 추론 입력 크기 (정사각형) |
| `robot_id` | str | `spot_01` | 기기 식별자. publish 토픽 경로에 사용. **기기마다 다른 값 설정** |
| `detect_consec_n` | int | 5 | 인명 발견 확정 기준 연속 프레임 수. 5fps 기준 5프레임 = 1초 |
| `show_preview` | bool | false | bbox 미리보기 창 활성화. 원격 접속 환경에서는 false 유지 |
| `preview_width` | int | 480 | 미리보기 창 너비 |
| `preview_height` | int | 270 | 미리보기 창 높이 |

---

## 모델 파일

TensorRT 엔진 파일은 패키지 내부에 포함하지 않는다. 크기가 크고 버전 관리가 별도로 필요하기 때문이다.

```
/home/jetson/models/
└── best.engine
```

| 환경 | engine_path |
|------|-------------|
| Jetson Orin Nano | `/home/jetson/models/best.engine` (TensorRT FP16) |
| 로컬 PC | `/home/ubuntu/models/best.onnx` (ONNX Runtime) |

yaml의 `engine_path`만 변경하면 환경 전환이 가능하다. 코드 수정 불필요.

---

## 빌드 및 실행

```bash
cd ~/robot_ws
colcon build --packages-select camera_perception
source install/setup.bash
```

```bash
ros2 launch camera_perception camera_perception.launch.py
```

---

## 토픽 확인

```bash
ros2 topic list
ros2 topic echo /perception/person_detected/spot_01
```

---

## 의존성

| 패키지 | 용도 |
|--------|------|
| `rclpy` | ROS2 Python 클라이언트 |
| `sensor_msgs` | Image 메시지 타입 |
| `std_msgs` | Bool 메시지 타입 |
| `cv_bridge` | OpenCV ↔ ROS2 이미지 변환 |
| `tensorrt` | TensorRT FP16 추론 (Jetson) |
| `onnxruntime` | ONNX 추론 (로컬 PC 개발용) |
| `numpy` | 배열 연산 |
| `opencv-python` | 이미지 처리, NMS |

---

## 설계 결정

**TensorRT FP16** : INT8은 캘리브레이션 데이터 없이 변환 시 conf ≈ 0으로 탐지 실패. FP16은 정상 탐지 확인. Jetson Orin Nano가 FP16 연산을 하드웨어 가속으로 지원한다.

**Letterbox 전처리** : 480x270 프레임을 480x480으로 단순 리사이즈하면 종횡비가 깨져 탐지 성능이 저하된다. 비율을 유지하면서 회색(114) 패딩을 추가한다. YOLO 학습 시 기본 letterbox 패딩 색상과 동일하게 맞췄다.

**queue_size=1** : 추론 시간이 프레임 간격보다 길 경우 큐가 쌓여 레이턴시가 누적된다. queue_size=1로 항상 최신 프레임만 유지한다.

**N프레임 연속 탐지** : 단일 프레임 오탐(유사 형체, 조명 변화 등)을 필터링한다. 탐지가 끊기면 카운터가 리셋되어 재진입 시 N프레임을 다시 충족해야 발행된다.

**상태 변화 시점에만 발행** : 상태가 유지되는 동안 반복 발행하지 않는다. subscriber가 이벤트 기반으로 처리할 수 있도록 True/False 전환 시점에만 1회 발행한다.

**토픽 경로에 robot_id 포함** : 로봇마다 독립된 sender가 본인 데이터만 subscribe하는 구조를 위해 `/perception/person_detected/{robot_id}` 형태로 네임스페이스를 분리한다.
