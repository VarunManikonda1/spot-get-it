from pathlib import Path
import re

datasets = [
    r"C:\Users\SSAFY\Desktop\03\datasets\Fall Detection.v2i.yolov11",
]

DRY_RUN = False  # True: 출력만 / False: 실제 수정

NEW_YAML = """train: train/images
val: train/images

nc: 1
names: ['person']
"""

def fix_label_file(label_path: Path):
    lines = label_path.read_text().splitlines()
    new_lines = []
    changed = False

    for line in lines:
        line = line.strip()
        if not line:
            continue
        parts = line.split()
        if parts[0] != "0":
            parts[0] = "0"
            changed = True
        new_lines.append(" ".join(parts))

    if changed:
        if DRY_RUN:
            print(f"  [DRY] 클래스 수정 예정: {label_path.name}")
        else:
            label_path.write_text("\n".join(new_lines) + "\n")
            print(f"  [FIX] 클래스 수정: {label_path.name}")

def fix_dataset(dataset_root: str):
    root = Path(dataset_root)
    lbl_dir = root / "train" / "labels"
    yaml_path = root / "data.yaml"

    print(f"\n[{root.name}]")

    # data.yaml 수정
    if yaml_path.exists():
        if DRY_RUN:
            print(f"  [DRY] data.yaml 수정 예정")
        else:
            yaml_path.write_text(NEW_YAML)
            print(f"  [FIX] data.yaml 수정 완료")
    else:
        print(f"  [WARN] data.yaml 없음 - 생성합니다")
        if not DRY_RUN:
            yaml_path.write_text(NEW_YAML)
            print(f"  [NEW] data.yaml 생성 완료")

    # 라벨 전체 클래스 → 0으로 수정
    if not lbl_dir.exists():
        print(f"  [SKIP] labels 디렉토리 없음")
        return

    label_files = list(lbl_dir.glob("*.txt"))
    print(f"  라벨 파일 {len(label_files)}개 처리 중...")

    for lf in label_files:
        fix_label_file(lf)

for ds in datasets:
    fix_dataset(ds)

print("\n완료.")
