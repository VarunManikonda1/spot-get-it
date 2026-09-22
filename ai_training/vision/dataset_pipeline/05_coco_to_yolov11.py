import json
from pathlib import Path

# -----------------------------------------------
# 입력 경로 설정
# 변환할 _annotations.coco.json 경로를 입력하세요
INPUT_JSON = r"C:\Users\SSAFY\Desktop\03\datasets\person-Folder- coco_person.coco\valid\_annotations.coco.json"

# 변환된 라벨(.txt)을 저장할 디렉토리 경로를 입력하세요
OUTPUT_DIR = r"C:\Users\SSAFY\Desktop\03\datasets\person-Folder- fall_detection.yolov11.coco\labels"
# -----------------------------------------------

def coco_to_yolo(input_json: str, output_dir: str):
    json_path = Path(input_json)
    out_dir   = Path(output_dir)

    if not json_path.exists():
        print(f"[ERROR] JSON 파일 없음: {json_path}")
        return

    out_dir.mkdir(parents=True, exist_ok=True)

    with open(json_path, "r", encoding="utf-8") as f:
        coco = json.load(f)

    # image_id → (file_name, width, height) 매핑
    image_map = {
        img["id"]: {
            "file_name": img["file_name"],
            "width":     img["width"],
            "height":    img["height"],
        }
        for img in coco["images"]
    }

    # image_id → [annotation, ...] 매핑
    anno_map = {}
    for anno in coco["annotations"]:
        iid = anno["image_id"]
        anno_map.setdefault(iid, []).append(anno)

    converted = 0
    skipped   = 0

    for image_id, image_info in image_map.items():
        w = image_info["width"]
        h = image_info["height"]
        stem = Path(image_info["file_name"]).stem

        annos = anno_map.get(image_id, [])
        if not annos:
            skipped += 1
            continue

        lines = []
        for anno in annos:
            # COCO bbox: [x_min, y_min, width, height] (픽셀 절대값)
            x_min, y_min, bw, bh = anno["bbox"]

            # YOLO: cx cy bw bh (정규화)
            cx = (x_min + bw / 2) / w
            cy = (y_min + bh / 2) / h
            nw = bw / w
            nh = bh / h

            # 클래스는 항상 0 (person)
            lines.append(f"0 {cx:.6f} {cy:.6f} {nw:.6f} {nh:.6f}")

        out_path = out_dir / f"{stem}.txt"
        out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        converted += 1

    print(f"완료: {converted}개 변환 / {skipped}개 어노테이션 없음 (라벨 미생성)")
    print(f"저장 위치: {out_dir}")


coco_to_yolo(INPUT_JSON, OUTPUT_DIR)
