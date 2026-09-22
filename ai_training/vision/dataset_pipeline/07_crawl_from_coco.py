"""
07_crawl_from_coco.py

COCO 2017에서 person 클래스 이미지를 수집합니다.
라벨 정제는 08_filt_coco.py에서 수행합니다.

출력 구조:
  C:/Users/SSAFY/Desktop/03/datasets/coco_person/
  ├── images/  ← 원본 이미지
  └── labels/  ← 원본 라벨 (person 외 라벨 포함)

실행 전 설치:
  pip install fiftyone
"""

import shutil
from pathlib import Path

import fiftyone as fo
import fiftyone.zoo as foz

# ── 설정 ───────────────────────────────────────────────────────────────────────
BASE_DIR    = Path(r"C:\Users\SSAFY\Desktop\03\datasets")
MAX_SAMPLES = 30000
COCO_OUT    = BASE_DIR / "coco_person"


# ── 변환 함수 ──────────────────────────────────────────────────────────────────
def save_raw(dataset, out_dir: Path):
    """
    fiftyone dataset → YOLO 포맷으로 저장 (라벨 정제 없이 원본 그대로)
    - 클래스 ID는 fiftyone 기본 정수 인덱스로 저장
    - 라벨명 매핑 파일(classes.txt) 함께 저장
    """
    img_dir = out_dir / "images"
    lbl_dir = out_dir / "labels"
    img_dir.mkdir(parents=True, exist_ok=True)
    lbl_dir.mkdir(parents=True, exist_ok=True)

    # 전체 클래스 목록 수집
    all_labels = sorted({
        det.label
        for sample in dataset
        if sample.ground_truth
        for det in sample.ground_truth.detections
    })
    label_to_id = {label: idx for idx, label in enumerate(all_labels)}

    # classes.txt 저장
    classes_path = out_dir / "classes.txt"
    classes_path.write_text("\n".join(all_labels))
    print(f"[INFO] 클래스 목록 저장: {classes_path}")

    saved   = 0
    skipped = 0

    for sample in dataset:
        if sample.filepath is None:
            skipped += 1
            continue

        src_img = Path(sample.filepath)
        if not src_img.exists():
            skipped += 1
            continue

        detections = sample.ground_truth
        if detections is None:
            skipped += 1
            continue

        lines = []
        for det in detections.detections:
            bx = det.bounding_box
            if bx is None:
                continue

            cls_id   = label_to_id[det.label]
            x_center = max(0.0, min(1.0, bx[0] + bx[2] / 2))
            y_center = max(0.0, min(1.0, bx[1] + bx[3] / 2))
            w        = max(0.0, min(1.0, bx[2]))
            h        = max(0.0, min(1.0, bx[3]))

            lines.append(f"{cls_id} {x_center:.6f} {y_center:.6f} {w:.6f} {h:.6f}")

        if not lines:
            skipped += 1
            continue

        shutil.copy2(str(src_img), img_dir / src_img.name)
        (lbl_dir / (src_img.stem + ".txt")).write_text("\n".join(lines))
        saved += 1

    print(f"[COCO] 저장 완료 : {saved}장 | 건너뜀: {skipped}장")
    print(f"       출력 경로 : {out_dir}")


# ── COCO 2017 ──────────────────────────────────────────────────────────────────
import time
import shutil as _shutil

# fiftyone 캐시 삭제
coco_cache = Path.home() / "fiftyone" / "coco-2017"
if coco_cache.exists():
    _shutil.rmtree(coco_cache)
    print(f"[INFO] fiftyone 캐시 삭제: {coco_cache}")

# 실행마다 다른 seed (현재 시간 기반)
_seed = int(time.time()) % (2**31)
print(f"[INFO] shuffle seed: {_seed}")

print("=" * 60)
print("COCO 2017 - person 다운로드 시작")
print("=" * 60)

coco_dataset = foz.load_zoo_dataset(
    "coco-2017",
    split="train",
    label_types=["detections"],
    classes=["person"],
    max_samples=MAX_SAMPLES,
    shuffle=True,
    seed=_seed,
    dataset_name="coco_person_tmp",
    overwrite=True,
)

print(f"[COCO] 수집된 샘플 수: {len(coco_dataset)}")
save_raw(coco_dataset, COCO_OUT)
fo.delete_dataset("coco_person_tmp")

print("\n" + "=" * 60)
print("[DONE] 수집 완료")
print(f"  COCO : {COCO_OUT}")
print("=" * 60)
