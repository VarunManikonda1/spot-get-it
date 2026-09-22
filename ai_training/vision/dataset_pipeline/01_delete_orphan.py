import os
from pathlib import Path

datasets = [
    r"C:\Users\SSAFY\Desktop\03\datasets\coco_person",
]

IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}

DRY_RUN = False  # True: 삭제 없이 출력만 / False: 실제 삭제

def clean_orphans(dataset_root: str):
    root = Path(dataset_root)
    img_dir = root / "train" / "images"
    lbl_dir = root / "train" / "labels"

    if not img_dir.exists() or not lbl_dir.exists():
        print(f"[SKIP] 경로 없음: {dataset_root}")
        return

    image_stems = {
        p.stem for p in img_dir.iterdir()
        if p.suffix.lower() in IMAGE_EXTS
    }
    label_stems = {
        p.stem for p in lbl_dir.iterdir()
        if p.suffix.lower() == ".txt"
    }

    orphan_labels = label_stems - image_stems
    orphan_images = image_stems - label_stems

    print(f"\n[{root.name}]")
    print(f"  고아 라벨  {len(orphan_labels)}개")
    print(f"  고아 이미지 {len(orphan_images)}개")

    for stem in orphan_labels:
        target = lbl_dir / f"{stem}.txt"
        if DRY_RUN:
            print(f"  [DRY] 삭제 예정 라벨: {target.name}")
        else:
            target.unlink()
            print(f"  [DEL] 라벨 삭제: {target.name}")

    for stem in orphan_images:
        for ext in IMAGE_EXTS:
            candidate = img_dir / f"{stem}{ext}"
            if candidate.exists():
                if DRY_RUN:
                    print(f"  [DRY] 삭제 예정 이미지: {candidate.name}")
                else:
                    candidate.unlink()
                    print(f"  [DEL] 이미지 삭제: {candidate.name}")
                break

for ds in datasets:
    clean_orphans(ds)

print("\n완료.")
