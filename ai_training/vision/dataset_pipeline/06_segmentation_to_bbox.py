from pathlib import Path

datasets = [
    r"C:\Users\SSAFY\Desktop\03\datasets\HUMAN.yolov11_seg",
]

DRY_RUN = False  # True: 출력만 / False: 실제 수정


def convert_label_file(label_path: Path):
    lines = label_path.read_text().splitlines()
    new_lines = []
    changed = False

    for line in lines:
        line = line.strip()
        if not line:
            continue
        parts = line.split()

        if len(parts) == 5:
            # 이미 bbox 형식 → 그대로
            new_lines.append(line)

        elif len(parts) >= 7:
            # 세그멘테이션 형식 → bbox로 변환
            changed = True
            coords = list(map(float, parts[1:]))
            if len(coords) % 2 != 0:
                coords = coords[:-1]

            xs = coords[0::2]
            ys = coords[1::2]

            x_min, x_max = min(xs), max(xs)
            y_min, y_max = min(ys), max(ys)

            cx = (x_min + x_max) / 2
            cy = (y_min + y_max) / 2
            bw = x_max - x_min
            bh = y_max - y_min

            new_lines.append(f"0 {cx:.6f} {cy:.6f} {bw:.6f} {bh:.6f}")

        else:
            # 파싱 불가 라인 → 그대로 유지
            new_lines.append(line)

    if changed:
        if DRY_RUN:
            print(f"  [DRY] 변환 예정: {label_path.name}")
        else:
            label_path.write_text("\n".join(new_lines) + "\n", encoding="utf-8")
            print(f"  [FIX] 변환 완료: {label_path.name}")


def convert_dataset(dataset_root: str):
    root    = Path(dataset_root)
    lbl_dir = root / "train" / "labels"

    if not lbl_dir.exists():
        print(f"[SKIP] labels 디렉토리 없음: {dataset_root}")
        return

    label_files = list(lbl_dir.glob("*.txt"))
    print(f"\n[{root.name}]  라벨 {len(label_files)}개 처리 중...")

    for lf in label_files:
        convert_label_file(lf)


for ds in datasets:
    convert_dataset(ds)

print("\n완료.")
