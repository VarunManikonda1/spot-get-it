# SPOT Get IT - Person Detection

> 프로젝트 : SPOT Get IT
> 목적 : 험지 인명 탐색 구조 로봇의 YOLOv11 기반 person 탐지 모델 개발

---

## 구조

```
.
├── dataset_pipeline/   # 데이터 수집 및 정제 스크립트 (01~08)
├── training/           # YOLOv11 학습, 평가, 변환, 추론 테스트
└── README.md
```

---

## dataset_pipeline

COCO 2017 수집부터 필터링, 포맷 변환, 육안 검수 동기화까지 데이터셋 구축 파이프라인입니다.

자세한 내용은 [`dataset_pipeline/README.md`](./dataset_pipeline/README.md)를 참고하십시오.

---

## training

YOLOv11n 학습, val 평가, ONNX 변환, 추론 테스트를 단계별 셀로 구성한 Jupyter Notebook입니다.

자세한 내용은 [`training/README.md`](./training/README.md)를 참고하십시오.
