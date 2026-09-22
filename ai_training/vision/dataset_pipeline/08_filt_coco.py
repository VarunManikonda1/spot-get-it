"""
08_filt_coco.py

07_crawl_from_coco.py로 수집한 coco_person 데이터셋을 정제합니다.

작업 순서:
  1. 스포츠 카테고리 소프트 필터링 (카테고리별 개별 LIMIT, 이미지 단위 제거)
  2. 해상도 필터 (짧은 변 < MIN_SIDE px 제거)
  3. person 외 라벨 제거
  4. 인원 수 필터 (person bbox > MAX_PERSONS개 제거)
  5. bbox 크기 필터 (이미지 면적 대비 MIN_BBOX_AREA 미만 제거)
  6. pose 검사 (detection conf + keypoint 기반 전신 판별)
  7. 고아 라벨 삭제
  8. 클래스 ID 0으로 통일
"""

from pathlib import Path
from collections import defaultdict
from PIL import Image
from ultralytics import YOLO

# ── 설정 ───────────────────────────────────────────────────────────────────────
BASE_DIR   = Path(r"C:\Users\SSAFY\Desktop\03\datasets")
TARGET_DIR = BASE_DIR / "coco_person"
IMG_EXTS   = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}

# 스포츠 카테고리별 최대 허용 장수 (판단 순서 반영)
SPORT_LIMITS = {
    "motorcycle":      0,
    "bicycle":         0,
    "horse":           0,    # 승마 완전 제거 (헬멧 착용으로 인체 특성 가려짐)
    "baseball bat":   150,
    "baseball glove": 150,
    "tennis racket":  300,
    "sports ball":    600,
    "skateboard":     100,   # 스케이트보드 (헬멧 착용) 300 → 100
    "surfboard":      300,
    "skis":           100,   # 스키 (헬멧 착용) 300 → 100
    "snowboard":      100,   # 보드 (헬멧 착용) 300 → 100
    "frisbee":        300,
    "kite":           300,
}

MIN_SIDE       = 480
MAX_PERSONS    = 6
MIN_BBOX_AREA  = 0.01
DET_CONF_THRES = 0.5
KP_CONF_THRES  = 0.3

PERSON_LABELS = {"person", "Person"}

FACE_KPS = [0, 1, 2, 3, 4]
BODY_KPS = [5, 6, 11, 12, 13, 14, 15, 16]

MODEL_PATH = "yolo11n-pose.pt"


# ── classes.txt 로드 ───────────────────────────────────────────────────────────
def load_classes(target_dir: Path) -> dict:
    classes_path = target_dir / "classes.txt"
    if not classes_path.exists():
        raise FileNotFoundError(f"classes.txt 없음: {classes_path}")
    lines = classes_path.read_text().strip().splitlines()
    return {idx: label for idx, label in enumerate(lines)}


# ── 공통 라벨 파싱 ─────────────────────────────────────────────────────────────
def get_labels(lbl_path: Path) -> list:
    if not lbl_path.exists():
        return []
    result = []
    for line in lbl_path.read_text().strip().splitlines():
        if not line.strip():
            continue
        parts = line.split()
        result.append((int(parts[0]), *map(float, parts[1:])))
    return result


def write_labels(lbl_path: Path, labels: list):
    lines = [f"{cls_id} {x:.6f} {y:.6f} {w:.6f} {h:.6f}" for cls_id, x, y, w, h in labels]
    lbl_path.write_text("\n".join(lines))


# ── 필터 함수들 ────────────────────────────────────────────────────────────────
def check_sport(lbl_path: Path, id_to_label: dict, sport_counts: dict) -> bool:
    """
    스포츠 카테고리 소프트 필터.
    판단 순서: baseball bat → baseball glove → tennis racket → sports ball → 나머지
    하나라도 LIMIT 초과이면 이미지 전체 제거 (False)
    """
    labels = get_labels(lbl_path)
    sport_in_image = {
        id_to_label.get(cls_id)
        for cls_id, *_ in labels
        if id_to_label.get(cls_id) in SPORT_LIMITS
    }

    # 판단 순서대로 체크
    for sport in SPORT_LIMITS:
        if sport in sport_in_image:
            if sport_counts[sport] >= SPORT_LIMITS[sport]:
                return False

    # 통과 시 카운트 누적
    for sport in sport_in_image:
        sport_counts[sport] += 1

    return True


def check_resolution(img_path: Path) -> bool:
    try:
        with Image.open(img_path) as img:
            w, h = img.size
        return min(w, h) >= MIN_SIDE
    except Exception:
        return False


def remove_non_person(lbl_path: Path, id_to_label: dict):
    """person 외 라벨 제거 (클래스 ID는 아직 원본 유지)"""
    labels = get_labels(lbl_path)
    kept = [
        (cls_id, x, y, w, h) for cls_id, x, y, w, h in labels
        if id_to_label.get(cls_id) in PERSON_LABELS
    ]
    write_labels(lbl_path, kept)


def check_person_count(lbl_path: Path) -> bool:
    """person bbox 수가 0이거나 MAX_PERSONS 초과이면 False"""
    labels = get_labels(lbl_path)
    return 0 < len(labels) <= MAX_PERSONS


def filter_bbox_size(lbl_path: Path) -> bool:
    """1% 미만 bbox 제거. 남은 bbox 없으면 False"""
    labels = get_labels(lbl_path)
    kept = [(cls_id, x, y, w, h) for cls_id, x, y, w, h in labels if w * h >= MIN_BBOX_AREA]
    if not kept:
        return False
    write_labels(lbl_path, kept)
    return True


def is_full_body(results) -> bool:
    for result in results:
        if result.keypoints is None or result.boxes is None:
            continue
        kps       = result.keypoints.data
        det_confs = result.boxes.conf
        if kps is None or len(kps) == 0:
            continue
        for i, person_kps in enumerate(kps):
            if det_confs[i] < DET_CONF_THRES:
                continue
            confs   = person_kps[:, 2]
            face_ok = any(confs[j] >= KP_CONF_THRES for j in FACE_KPS)
            body_ok = all(confs[j] >= KP_CONF_THRES for j in BODY_KPS)
            if face_ok and body_ok:
                return True
    return False


def unify_class_id(lbl_path: Path):
    """클래스 ID를 전부 0으로 통일"""
    labels = get_labels(lbl_path)
    unified = [(0, x, y, w, h) for _, x, y, w, h in labels]
    write_labels(lbl_path, unified)


# ── 메인 ───────────────────────────────────────────────────────────────────────
def main():
    img_dir = TARGET_DIR / "images"
    lbl_dir = TARGET_DIR / "labels"

    if not img_dir.exists():
        print(f"[ERROR] 폴더 없음: {img_dir}")
        return

    id_to_label  = load_classes(TARGET_DIR)
    img_files    = [f for f in img_dir.iterdir() if f.suffix.lower() in IMG_EXTS]
    total        = len(img_files)
    sport_counts = defaultdict(int)
    model        = YOLO(MODEL_PATH)

    cnt = {
        "sport": 0, "resolution": 0, "person_count": 0,
        "bbox_size": 0, "pose": 0, "kept": 0
    }

    print(f"\n{'=' * 60}")
    print(f"[coco_person] 이미지 수: {total}")
    print(f"{'=' * 60}")

    for idx, img_path in enumerate(img_files, 1):
        if idx % 200 == 0:
            print(f"  진행: {idx}/{total} | 유지: {cnt['kept']} | "
                  f"스포츠: {cnt['sport']} | 해상도: {cnt['resolution']} | "
                  f"인원수: {cnt['person_count']} | bbox크기: {cnt['bbox_size']} | "
                  f"pose: {cnt['pose']}")

        lbl_path = lbl_dir / (img_path.stem + ".txt")

        def remove():
            img_path.unlink()
            if lbl_path.exists():
                lbl_path.unlink()

        # ── 1. 스포츠 필터 ──────────────────────────────────────────────────
        if not check_sport(lbl_path, id_to_label, sport_counts):
            remove(); cnt["sport"] += 1; continue

        # ── 2. 해상도 필터 ──────────────────────────────────────────────────
        if not check_resolution(img_path):
            remove(); cnt["resolution"] += 1; continue

        # ── 3. person 외 라벨 제거 ──────────────────────────────────────────
        remove_non_person(lbl_path, id_to_label)

        # ── 4. 인원 수 필터 ─────────────────────────────────────────────────
        if not check_person_count(lbl_path):
            remove(); cnt["person_count"] += 1; continue

        # ── 5. bbox 크기 필터 ───────────────────────────────────────────────
        if not filter_bbox_size(lbl_path):
            remove(); cnt["bbox_size"] += 1; continue

        # ── 6. pose 검사 ────────────────────────────────────────────────────
        try:
            results = model(str(img_path), verbose=False)
        except Exception as e:
            print(f"  [WARN] 추론 실패, 삭제: {img_path.name} ({e})")
            remove(); cnt["pose"] += 1; continue

        if not is_full_body(results):
            remove(); cnt["pose"] += 1; continue

        cnt["kept"] += 1

    # ── 7. 고아 라벨 삭제 ───────────────────────────────────────────────────
    remaining_stems = {
        f.stem for f in img_dir.iterdir() if f.suffix.lower() in IMG_EXTS
    }
    orphan_removed = 0
    for lbl_path in lbl_dir.glob("*.txt"):
        if lbl_path.stem not in remaining_stems:
            lbl_path.unlink()
            orphan_removed += 1

    # ── 8. 클래스 ID 0 통일 ─────────────────────────────────────────────────
    for lbl_path in lbl_dir.glob("*.txt"):
        unify_class_id(lbl_path)

    print(f"\n[coco_person] 처리 완료")
    print(f"  유지         : {cnt['kept']}장")
    print(f"  스포츠 제거  : {cnt['sport']}장")
    print(f"  해상도 제거  : {cnt['resolution']}장")
    print(f"  인원수 제거  : {cnt['person_count']}장")
    print(f"  bbox크기 제거: {cnt['bbox_size']}장")
    print(f"  pose 제거    : {cnt['pose']}장")
    print(f"  고아 라벨 삭제: {orphan_removed}개")

    print(f"\n  스포츠 카테고리별 1번 필터 통과 장수:")
    for sport, limit in SPORT_LIMITS.items():
        c = sport_counts.get(sport, 0)
        print(f"    {sport}: {c}장 / {limit}장")

    print(f"\n{'=' * 60}")
    print("[DONE] 전체 필터링 완료")
    print(f"{'=' * 60}")


if __name__ == "__main__":
    main()
