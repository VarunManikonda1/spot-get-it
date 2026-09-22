# person_detection_yolo11.ipynb

> 프로젝트 : SPOT Get IT
> 목적 : YOLOv11n 기반 person 탐지 모델 학습, 평가, 변환, 추론 테스트

---

## 환경

| 항목 | 내용 |
|------|------|
| Python | 3.11 |
| Framework | Ultralytics YOLOv11 |
| GPU | NVIDIA GeForce RTX 4050 Laptop (6GB) |
| CUDA | 12.4 |
| 배포 타겟 | Jetson Orin Nano (TensorRT FP16) |

---

## 실행 전 수정 항목

셀2에서 다음 두 항목을 매 학습마다 직접 수정합니다.

| 변수 | 예시 | 설명 |
|------|------|------|
| `dataset_root` | `r"C:\...\person.v2i.yolov11"` | Roboflow에서 다운로드한 데이터셋 경로 |
| `run_name` | `"yolo11n_480_v2"` | 학습 버전명 (규칙: `yolo(모델버전)_(이미지크기)_(학습버전)`) |

---

## 셀 구성

### 셀1 - 환경 확인 및 캐시 삭제

Python, PyTorch, Ultralytics, CUDA 버전을 출력하고 이전 학습의 labels.cache를 삭제합니다.

### 셀2 - 경로 설정 및 data.yaml 업데이트

`dataset_root`, `run_name`을 설정하고 `data.yaml`을 단일 클래스(person) 기준으로 업데이트합니다. train/valid/test 이미지 수를 출력합니다.

### 셀3 - 학습

YOLOv11n 모델을 학습합니다. `RESUME = True`로 설정하면 이전 학습을 이어서 진행합니다.

**주요 학습 설정**

| 파라미터 | 값 | 설명 |
|----------|-----|------|
| `epochs` | 100 | 최대 에포크 |
| `imgsz` | 480 | 입력 이미지 크기 |
| `batch` | 16 | 배치 크기 (OOM 시 8로 낮출 것) |
| `patience` | 30 | Early stopping |
| `optimizer` | AdamW | |
| `mosaic` | 1.0 | Roboflow 증강과 성격이 달라 유지 |

> Roboflow에서 x3 증강 적용 완료. degrees, fliplr, hsv 계열은 Ultralytics에서 추가 적용.

### 셀4 - 학습 결과 시각화

`results.png`(loss/mAP 그래프)와 `confusion_matrix_normalized.png`를 출력합니다.

### 셀5 - val set 추론 샘플 시각화

validation 이미지 9장을 랜덤 샘플링하여 best.pt로 추론한 결과를 시각화합니다.

### 셀6 - ONNX 변환

`best.pt`를 ONNX 형식으로 변환하여 `weights/onnx/best.onnx`에 저장합니다.

**변환 설정**

| 파라미터 | 값 | 설명 |
|----------|-----|------|
| `opset` | 17 | TensorRT 변환 호환성 |
| `dynamic` | False | Orin Nano 고정 배치 추론 |

변환 후 Orin Nano에서 TensorRT 엔진으로 추가 변환이 필요합니다.

```bash
trtexec --onnx=best.onnx --saveEngine=best_fp16.engine --fp16 --memPoolSize=workspace:512M
```

### 셀7 - (마크다운) TensorRT 변환 명령어

Orin Nano에서 실행할 trtexec 명령어를 기록합니다.

### 셀8 - .pt 모델 추론 테스트

`C:\Users\SSAFY\Desktop\03\models\test\` 하위의 이미지 전체에 대해 best.pt로 추론을 수행합니다. bbox와 confidence를 시각화하여 `pt_test/` 폴더에 저장합니다.

**수정 필요 항목**

| 변수 | 설명 |
|------|------|
| `TEST_DIR` | 테스트 이미지 디렉토리 경로 |
| `CONF_THRES` | confidence 임계값 (기본 0.25) |
| `IOU_THRES` | NMS IOU 임계값 (기본 0.45) |

### 셀9 - .onnx 모델 추론 테스트

`C:\Users\SSAFY\Desktop\03\models\test\` 하위의 이미지 전체에 대해 best.onnx로 추론을 수행합니다. NMS 적용 후 bbox와 confidence를 시각화하여 `onnx_test/` 폴더에 저장합니다.

**수정 필요 항목**

| 변수 | 설명 |
|------|------|
| `TEST_DIR` | 테스트 이미지 디렉토리 경로 |
| `ONNX_PATH` | ONNX 파일 경로 |
| `CONF_THRES` | confidence 임계값 (기본 0.25) |
| `IOU_THRES` | NMS IOU 임계값 (기본 0.45) |

---

## 결과물 저장 경로

```
C:\Users\SSAFY\Desktop\03\models\{run_name}\
├── weights\
│   ├── best.pt
│   ├── last.pt
│   └── onnx\
│       ├── best.pt
│       └── best.onnx
├── pt_test\       ← 셀8 추론 결과
├── onnx_test\     ← 셀9 추론 결과
├── results.png
├── confusion_matrix_normalized.png
└── ...
```
