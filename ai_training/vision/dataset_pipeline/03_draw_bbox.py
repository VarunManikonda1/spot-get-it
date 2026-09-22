from pathlib import Path
import cv2

datasets = [
    r"C:\Users\SSAFY\Desktop\03\datasets\person-Folder- coco_person.coco",
]

IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}
OUTPUT_ROOT = Path(r"C:\Users\SSAFY\Desktop\03\datasets\00_result")

BBOX_COLOR = (0, 255, 0)   # BGR 기준 초록
BBOX_THICK = 5
LABEL_TEXT = "person"
FONT       = cv2.FONT_HERSHEY_SIMPLEX
FONT_SCALE = 0.6
FONT_THICK = 2


def draw_bboxes(dataset_root: str):
    root     = Path(dataset_root)
    img_dir  = root / "train" / "images"
    lbl_dir  = root / "train" / "labels"
    out_dir  = OUTPUT_ROOT / root.name

    if not img_dir.exists() or not lbl_dir.exists():
        print(f"[SKIP] 경로 없음: {dataset_root}")
        return

    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"\n[{root.name}]  →  {out_dir}")

    image_files = [p for p in img_dir.iterdir() if p.suffix.lower() in IMAGE_EXTS]
    print(f"  이미지 {len(image_files)}개 처리 중...")

    for img_path in image_files:
        lbl_path = lbl_dir / f"{img_path.stem}.txt"

        img = cv2.imread(str(img_path))
        if img is None:
            print(f"  [WARN] 이미지 읽기 실패: {img_path.name}")
            continue

        h, w = img.shape[:2]

        if lbl_path.exists():
            for line in lbl_path.read_text().splitlines():
                line = line.strip()
                if not line:
                    continue
                parts = line.split()
                if len(parts) < 5:
                    continue

                _, cx, cy, bw, bh = map(float, parts[:5])

                x1 = int((cx - bw / 2) * w)
                y1 = int((cy - bh / 2) * h)
                x2 = int((cx + bw / 2) * w)
                y2 = int((cy + bh / 2) * h)

                cv2.rectangle(img, (x1, y1), (x2, y2), BBOX_COLOR, BBOX_THICK)
                cv2.putText(img, LABEL_TEXT, (x1, y1 - 6),
                            FONT, FONT_SCALE, BBOX_COLOR, FONT_THICK)

        out_path = out_dir / img_path.name
        cv2.imwrite(str(out_path), img)

    print(f"  저장 완료: {out_dir}")


for ds in datasets:
    draw_bboxes(ds)

print("\n완료.")
