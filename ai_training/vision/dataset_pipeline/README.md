# 데이터셋 수집 및 정제 스크립트

> 프로젝트 : SPOT Get IT
> 대상 : YOLOv11 person 탐지 모델 학습용 데이터셋

---

## 환경 설정

### 공통 경로 변수

스크립트 실행 전 아래 경로를 본인 환경에 맞게 수정하십시오.

| 변수 | 기본값 | 설명 |
|------|--------|------|
| `BASE_DIR` / `datasets` | `C:\Users\SSAFY\Desktop\03\datasets` | 데이터셋 루트 디렉토리 |
| `OUTPUT_ROOT` | `C:\Users\SSAFY\Desktop\03\datasets\00_result` | 시각화 결과 저장 경로 |

### 설치

```bash
pip install fiftyone ultralytics pillow opencv-python
```

---

## 스크립트 목록

### 01_delete_orphan.py

**역할** : 이미지와 라벨 파일 간 고아 파일 제거

이미지는 있는데 라벨이 없거나, 라벨은 있는데 이미지가 없는 파일을 탐지하여 제거합니다.

**수정 필요 항목**

| 변수 | 설명 |
|------|------|
| `datasets` | 정제할 데이터셋 경로 목록 |
| `DRY_RUN` | `True`: 삭제 없이 출력만 / `False`: 실제 삭제 |

**실행**

```bash
python 01_delete_orphan.py
```

---

### 02_make_all_0_person.py

**역할** : 라벨 파일의 모든 클래스 ID를 0(person)으로 통일 및 `data.yaml` 수정

멀티 클래스 데이터셋을 단일 클래스(person)로 변환합니다.

**수정 필요 항목**

| 변수 | 설명 |
|------|------|
| `datasets` | 변환할 데이터셋 경로 목록 |
| `DRY_RUN` | `True`: 출력만 / `False`: 실제 수정 |

**실행**

```bash
python 02_make_all_0_person.py
```

---

### 03_draw_bbox.py

**역할** : YOLO bbox 라벨을 이미지에 시각화하여 저장

bbox가 올바르게 그려졌는지 육안으로 확인할 때 사용합니다.

**수정 필요 항목**

| 변수 | 설명 |
|------|------|
| `datasets` | 시각화할 데이터셋 경로 목록 |
| `OUTPUT_ROOT` | 결과 이미지 저장 경로 |

**실행**

```bash
python 03_draw_bbox.py
```

---

### 03_01_draw_segment.py

**역할** : YOLO bbox 및 segmentation 라벨을 이미지에 시각화하여 저장

bbox와 세그멘테이션 형식을 모두 지원합니다. 세그멘테이션은 반투명 폴리곤으로 표시됩니다.

**수정 필요 항목**

| 변수 | 설명 |
|------|------|
| `datasets` | 시각화할 데이터셋 경로 목록 |
| `OUTPUT_ROOT` | 결과 이미지 저장 경로 |

**실행**

```bash
python 03_01_draw_segment.py
```

---

### 04_delete_unfit.py

**역할** : 육안 검수 후 `00_result`에서 제거된 이미지를 원본 데이터셋에서도 동기화 삭제

`03_draw_bbox.py` 또는 `03_01_draw_segment.py`로 시각화한 결과에서 불량 이미지를 직접 삭제한 뒤 본 스크립트를 실행하면 원본 이미지와 라벨도 함께 삭제됩니다.

**수정 필요 항목**

| 변수 | 설명 |
|------|------|
| `datasets` | `{ "데이터셋명": Path(경로) }` 형태로 수정 |
| `OUTPUT_ROOT` | 검수 결과 디렉토리 경로 |
| `DRY_RUN` | `True`: 출력만 / `False`: 실제 삭제 |

**실행**

```bash
python 04_delete_unfit.py
```

---

### 05_coco_to_yolov11.py

**역할** : COCO JSON 형식 어노테이션을 YOLO 형식(.txt)으로 변환

`_annotations.coco.json` 파일을 읽어 이미지별 YOLO 라벨 파일을 생성합니다. 모든 클래스는 0(person)으로 고정됩니다.

**수정 필요 항목**

| 변수 | 설명 |
|------|------|
| `INPUT_JSON` | 변환할 `_annotations.coco.json` 경로 |
| `OUTPUT_DIR` | 변환된 라벨 저장 디렉토리 경로 |

**실행**

```bash
python 05_coco_to_yolov11.py
```

---

### 06_segmentation_to_bbox.py

**역할** : 세그멘테이션 형식 라벨을 bbox 형식으로 변환

세그멘테이션 폴리곤의 외접 사각형(bounding box)을 계산하여 YOLO bbox 형식으로 변환합니다.

**수정 필요 항목**

| 변수 | 설명 |
|------|------|
| `datasets` | 변환할 데이터셋 경로 목록 |
| `DRY_RUN` | `True`: 출력만 / `False`: 실제 수정 |

**실행**

```bash
python 06_segmentation_to_bbox.py
```

---

### 07_crawl_from_coco.py

**역할** : COCO 2017 train 세트에서 person 이미지를 수집하고 YOLO 포맷으로 저장

fiftyone을 사용하여 COCO 2017에서 person 클래스 이미지를 랜덤 수집합니다. 라벨 정제는 08에서 수행하므로 원본 라벨 그대로 저장합니다.

**수정 필요 항목**

| 변수 | 설명 |
|------|------|
| `BASE_DIR` | 데이터셋 저장 루트 경로 |
| `MAX_SAMPLES` | 수집할 최대 이미지 수 (현재 30000) |

**출력 구조**

```
datasets/coco_person/
├── images/       ← 원본 이미지
├── labels/       ← 원본 라벨 (person 외 라벨 포함)
└── classes.txt   ← 클래스 ID 매핑
```

**실행**

```bash
pip install fiftyone
python 07_crawl_from_coco.py
```

---

### 08_filt_coco.py

**역할** : `07_crawl_from_coco.py`로 수집한 데이터셋을 다단계 필터링하여 정제

**필터 순서**

| 순서 | 필터 | 기준 |
|------|------|------|
| 1 | 스포츠 카테고리 | 카테고리별 허용 장수 초과 시 이미지 단위 제거 |
| 2 | 해상도 | 짧은 변 < 480px 제거 |
| 3 | person 외 라벨 제거 | - |
| 4 | 인원 수 | person bbox > 6개 제거 |
| 5 | bbox 크기 | 이미지 면적 대비 1% 미만 제거 |
| 6 | pose 검사 | YOLOv11n-pose 기반 전신 판별 |
| 7 | 고아 라벨 삭제 | - |
| 8 | 클래스 ID 통일 | 전부 0으로 통일 |

**스포츠 카테고리 허용 장수**

| 카테고리 | 허용 장수 | 사유 |
|----------|-----------|------|
| motorcycle | 0 | 완전 제거 |
| bicycle | 0 | 완전 제거 |
| horse | 0 | 승마 (헬멧 착용) 완전 제거 |
| baseball bat | 150 | |
| baseball glove | 150 | |
| tennis racket | 300 | |
| sports ball | 600 | |
| skateboard | 100 | 헬멧 착용 |
| surfboard | 300 | |
| skis | 100 | 헬멧 착용 |
| snowboard | 100 | 헬멧 착용 |
| frisbee | 300 | |
| kite | 300 | |

**수정 필요 항목**

| 변수 | 설명 |
|------|------|
| `BASE_DIR` | 데이터셋 루트 경로 |
| `SPORT_LIMITS` | 스포츠 카테고리별 허용 장수 |
| `MODEL_PATH` | YOLOv11n-pose 모델 경로 (기본: `yolo11n-pose.pt`, 최초 실행 시 자동 다운로드) |

**실행**

```bash
python 08_filt_coco.py
```

---

## 전체 실행 순서

```
기존 Roboflow 데이터셋
    02 → 클래스 ID 통일
    06 → 세그멘테이션 → bbox 변환 (해당 데이터셋만)
    05 → COCO JSON → YOLO 변환 (해당 데이터셋만)
    03 / 03_01 → 시각화
    04 → 육안 검수 후 불량 제거
    01 → 고아 파일 정리

COCO 추가 수집
    07 → 수집
    08 → 필터링 및 정제

최종 데이터셋 병합 후 Roboflow 업로드 → 학습
```
