from pathlib import Path

IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}

OUTPUT_ROOT = Path(r"C:\Users\SSAFY\Desktop\03\datasets\00_result")

datasets = {
    "coco_person":           Path(r"C:\Users\SSAFY\Desktop\03\datasets\coco_person"),
}

DRY_RUN = False # True: 출력만 / False: 실제 삭제

def sync_deletions(ds_name: str, ds_root: Path):
    result_dir = OUTPUT_ROOT / ds_name
    img_dir    = ds_root / "train" / "images"
    lbl_dir    = ds_root / "train" / "labels"

    if not result_dir.exists():
        print(f"[SKIP] 결과 디렉토리 없음: {result_dir}")
        return

    # 00_result에 현재 남아있는 파일 stem
    remaining_stems = {
        p.stem for p in result_dir.iterdir()
        if p.suffix.lower() in IMAGE_EXTS
    }

    # 원본 이미지 stem
    original_stems = {
        p.stem for p in img_dir.iterdir()
        if p.suffix.lower() in IMAGE_EXTS
    } if img_dir.exists() else set()

    # 검수 후 지워진 것 = 원본에는 있는데 result에는 없는 것
    deleted_stems = original_stems - remaining_stems

    print(f"\n[{ds_name}]")
    print(f"  삭제 대상 {len(deleted_stems)}개")

    for stem in deleted_stems:
        # 이미지 삭제
        for ext in IMAGE_EXTS:
            candidate = img_dir / f"{stem}{ext}"
            if candidate.exists():
                if DRY_RUN:
                    print(f"  [DRY] 이미지 삭제 예정: {candidate.name}")
                else:
                    candidate.unlink()
                    print(f"  [DEL] 이미지 삭제: {candidate.name}")
                break

        # 라벨 삭제
        lbl_path = lbl_dir / f"{stem}.txt"
        if lbl_path.exists():
            if DRY_RUN:
                print(f"  [DRY] 라벨 삭제 예정: {lbl_path.name}")
            else:
                lbl_path.unlink()
                print(f"  [DEL] 라벨 삭제: {lbl_path.name}")


for ds_name, ds_root in datasets.items():
    sync_deletions(ds_name, ds_root)

print("\n완료.")
