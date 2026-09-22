#!/usr/bin/env python3

"""
경로 : lidar_collector/lidar_collector/pointcloud2_jsonl_recorder.py
역할 : /perception/lidar/points_filtered 토픽의 sensor_msgs/msg/PointCloud2 데이터를 JSONL 파일로 녹화한다.
입력 토픽 :
  - /perception/lidar/points_filtered
    Type: sensor_msgs/msg/PointCloud2
출력 파일 :
  - /home/jetson/data/{session_index}/lidar/{robot_id}_points_filtered.jsonl
주요 기능 :
  - PointCloud2의 header, height, width, fields, point_step, row_step, is_dense 정보를 보존한다.
  - PointCloud2 내부 binary data를 x, y, z point 배열로 변환해 저장한다.
  - JSONL 한 줄에 LiDAR 한 프레임을 저장한다.
  - frame_index와 elapsed_sec를 함께 저장해 관제 PC에서 원래 시간 간격대로 재생할 수 있게 한다.
  - 기본 실행 시 /home/jetson/data 아래의 숫자 폴더를 확인해 다음 번호에 저장한다.
  - session_index 파라미터를 지정하면 특정 실험 번호 폴더에 저장할 수 있다.
"""

import json
import math
from datetime import datetime
from pathlib import Path

import rclpy
from rclpy.node import Node

from sensor_msgs.msg import PointCloud2
from sensor_msgs_py import point_cloud2


class PointCloud2JsonlRecorder(Node):
    def __init__(self):
        super().__init__("pointcloud2_jsonl_recorder")

        # =========================
        # [1] Parameter Declare
        # =========================

        # 관제 시스템에서 이 LiDAR 데이터가 어떤 가상 로봇의 데이터인지 구분하기 위한 ID
        self.declare_parameter("robot_id", "spot_02")

        # 녹화할 원본 PointCloud2 토픽
        self.declare_parameter("source_topic", "/perception/lidar/points_filtered")

        # 관제 담당자가 이 데이터를 어떤 가상 토픽으로 취급하면 되는지 알려주기 위한 메타데이터
        # 비워두면 /perception/lidar/points_filtered/{robot_id} 형태로 자동 생성한다.
        self.declare_parameter("virtual_topic", "")

        # 저장 root 경로
        # 최종 저장 구조:
        #   /home/jetson/data/1/lidar/spot_02_points_filtered.jsonl
        #   /home/jetson/data/2/lidar/spot_02_points_filtered.jsonl
        self.declare_parameter("output_root", "/home/jetson/data")

        # data/{번호} 아래에 생성할 센서별 하위 폴더 이름
        self.declare_parameter("sensor_subdir", "lidar")

        # 저장 파일명
        # 비워두면 {robot_id}_points_filtered.jsonl 로 자동 생성한다.
        self.declare_parameter("output_filename", "")

        # 0이면 자동으로 다음 번호를 선택한다.
        # 예: /home/jetson/data/1, /home/jetson/data/2가 있으면 3 선택
        # 1 이상이면 해당 번호 폴더에 저장한다.
        # 카메라 담당자와 같은 실험 번호를 맞춰야 하면 이 값을 명시적으로 넣으면 된다.
        self.declare_parameter("session_index", 0)

        # 기존 파일이 있을 때 덮어쓸지 여부
        # 기본값 false: 실수로 기존 녹화 파일을 덮어쓰는 것을 방지한다.
        self.declare_parameter("overwrite_existing", False)

        # 0 이하이면 한 프레임의 모든 point를 저장한다.
        # 파일 용량을 줄이고 싶을 때만 1000, 2000 같은 값으로 제한한다.
        self.declare_parameter("max_points_per_frame", 0)

        # NaN / Inf point를 JSON에 저장하지 않기 위한 옵션
        self.declare_parameter("skip_nan", True)

        # =========================
        # [2] Parameter Read
        # =========================

        self.robot_id = self.get_parameter("robot_id").value
        self.source_topic = self.get_parameter("source_topic").value
        self.virtual_topic = self.get_parameter("virtual_topic").value

        self.output_root = self.get_parameter("output_root").value
        self.sensor_subdir = self.get_parameter("sensor_subdir").value
        self.output_filename = self.get_parameter("output_filename").value

        self.session_index = int(self.get_parameter("session_index").value)
        self.overwrite_existing = bool(self.get_parameter("overwrite_existing").value)

        self.max_points_per_frame = int(self.get_parameter("max_points_per_frame").value)
        self.skip_nan = bool(self.get_parameter("skip_nan").value)

        if not self.virtual_topic:
            self.virtual_topic = f"/perception/lidar/points_filtered/{self.robot_id}"

        if not self.output_filename:
            self.output_filename = f"{self.robot_id}_points_filtered.jsonl"

        # =========================
        # [3] Output Path 생성
        # =========================

        self.output_dir, self.output_file = self.create_output_path()

        # line buffering을 사용한다.
        # Ctrl+C로 종료해도 이미 기록된 줄은 파일에 바로 남는다.
        self.file = open(self.output_file, "w", buffering=1, encoding="utf-8")

        self.frame_index = 0
        self.first_stamp_unix_sec = None

        # =========================
        # [4] Subscriber 생성
        # =========================

        self.sub = self.create_subscription(
            PointCloud2,
            self.source_topic,
            self.pointcloud_callback,
            10
        )

        self.get_logger().info("pointcloud2_jsonl_recorder started")
        self.get_logger().info(f"robot_id          : {self.robot_id}")
        self.get_logger().info(f"source_topic      : {self.source_topic}")
        self.get_logger().info(f"virtual_topic     : {self.virtual_topic}")
        self.get_logger().info(f"output_root       : {self.output_root}")
        self.get_logger().info(f"sensor_subdir     : {self.sensor_subdir}")
        self.get_logger().info(f"session_index     : {self.session_index}")
        self.get_logger().info(f"output_dir        : {self.output_dir}")
        self.get_logger().info(f"output_file       : {self.output_file}")

    def create_output_path(self):
        """
        저장 경로를 생성한다.

        기본 자동 모드:
          /home/jetson/data 아래의 숫자 폴더를 확인하고 다음 번호를 선택한다.

          예:
            기존 폴더:
              /home/jetson/data/1
              /home/jetson/data/2

            새 저장 경로:
              /home/jetson/data/3/lidar/spot_02_points_filtered.jsonl

        수동 session_index 모드:
          session_index:=3 으로 실행하면
              /home/jetson/data/3/lidar/spot_02_points_filtered.jsonl
          에 저장한다.

        수동 모드는 카메라 담당자가 이미 /home/jetson/data/3/camera를 만든 상황에서
        LiDAR도 같은 3번 실험 폴더에 맞춰 저장하고 싶을 때 사용한다.
        """

        root_path = Path(self.output_root).expanduser()
        root_path.mkdir(parents=True, exist_ok=True)

        if self.session_index > 0:
            selected_index = self.session_index
        else:
            selected_index = self.find_next_session_index(root_path)

        data_dir = root_path / str(selected_index)
        output_dir = data_dir / self.sensor_subdir
        output_file = output_dir / self.output_filename

        output_dir.mkdir(parents=True, exist_ok=True)

        if output_file.exists() and not self.overwrite_existing:
            raise RuntimeError(
                f"Output file already exists: {output_file}. "
                f"Use -p overwrite_existing:=true if you really want to overwrite it."
            )

        return str(output_dir), str(output_file)

    def find_next_session_index(self, root_path):
        """
        /home/jetson/data 아래에서 숫자 폴더만 확인해 다음 번호를 반환한다.

        예:
          /home/jetson/data/1
          /home/jetson/data/2
          /home/jetson/data/test

        위 구조에서는 숫자 폴더 1, 2만 보고 다음 번호 3을 반환한다.
        """

        existing_indices = []

        for child in root_path.iterdir():
            if not child.is_dir():
                continue

            if child.name.isdigit():
                existing_indices.append(int(child.name))

        return max(existing_indices) + 1 if existing_indices else 1

    def pointcloud_callback(self, msg):
        """
        PointCloud2 메시지 1개를 JSONL 한 줄로 저장한다.
        """

        # =========================
        # [1] PointCloud2 필드 유효성 확인
        # =========================

        available_fields = {field.name for field in msg.fields}
        required_fields = {"x", "y", "z"}
        missing_fields = required_fields - available_fields

        if missing_fields:
            self.get_logger().warn(
                f"PointCloud2 does not contain required fields: {sorted(missing_fields)}"
            )
            return

        # =========================
        # [2] Timestamp 계산
        # =========================

        stamp_sec = int(msg.header.stamp.sec)
        stamp_nanosec = int(msg.header.stamp.nanosec)
        stamp_unix_sec = float(stamp_sec) + float(stamp_nanosec) * 1e-9

        if self.first_stamp_unix_sec is None:
            self.first_stamp_unix_sec = stamp_unix_sec

        elapsed_sec = stamp_unix_sec - self.first_stamp_unix_sec

        # =========================
        # [3] PointCloud2 fields 메타데이터 저장
        # =========================

        fields = []

        for field in msg.fields:
            fields.append({
                "name": field.name,
                "offset": int(field.offset),
                "datatype": int(field.datatype),
                "count": int(field.count)
            })

        # =========================
        # [4] Binary PointCloud2 data → x/y/z 배열 변환
        # =========================

        points = []

        point_iter = point_cloud2.read_points(
            msg,
            field_names=("x", "y", "z"),
            skip_nans=self.skip_nan
        )

        for i, point in enumerate(point_iter):
            if self.max_points_per_frame > 0 and i >= self.max_points_per_frame:
                break

            x = float(point[0])
            y = float(point[1])
            z = float(point[2])

            if self.skip_nan:
                if not (math.isfinite(x) and math.isfinite(y) and math.isfinite(z)):
                    continue

            points.append({
                "x": x,
                "y": y,
                "z": z
            })

        original_point_count = int(msg.width) * int(msg.height)

        truncated = (
            self.max_points_per_frame > 0 and
            len(points) >= self.max_points_per_frame and
            original_point_count > self.max_points_per_frame
        )

        # =========================
        # [5] JSONL Record 생성
        # =========================

        record = {
            "robot_id": self.robot_id,
            "source_topic": self.source_topic,
            "virtual_topic": self.virtual_topic,
            "msg_type": "sensor_msgs/msg/PointCloud2",

            # 관제 담당자가 파일 자체를 추적할 때 참고하기 위한 기록 시각이다.
            # 실제 재생 타이밍 기준은 header.stamp 또는 elapsed_sec를 사용하면 된다.
            "recorded_at": datetime.now().isoformat(timespec="milliseconds"),

            # 한 줄이 몇 번째 LiDAR frame인지 나타낸다.
            "frame_index": self.frame_index,

            # 첫 프레임 timestamp 기준 상대 시간이다.
            # 관제 PC에서 이 값을 이용하면 원래 LiDAR 주기에 가깝게 재생할 수 있다.
            "elapsed_sec": elapsed_sec,

            # PointCloud2 header 정보이다.
            "header": {
                "stamp": {
                    "sec": stamp_sec,
                    "nanosec": stamp_nanosec,
                    "unix_sec": stamp_unix_sec
                },
                "frame_id": msg.header.frame_id
            },

            # PointCloud2 구조 메타데이터이다.
            "height": int(msg.height),
            "width": int(msg.width),
            "fields": fields,
            "is_bigendian": bool(msg.is_bigendian),
            "point_step": int(msg.point_step),
            "row_step": int(msg.row_step),
            "is_dense": bool(msg.is_dense),

            # 원본 point 개수와 실제 JSON에 저장된 point 개수를 구분한다.
            "original_point_count": original_point_count,
            "point_count": len(points),
            "truncated": truncated,

            # 관제 UI에서 바로 사용할 수 있는 point 배열이다.
            "points": points
        }

        # =========================
        # [6] JSONL 파일에 한 줄 저장
        # =========================

        self.file.write(json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n")

        if self.frame_index % 30 == 0:
            self.get_logger().info(
                f"recorded frame={self.frame_index}, "
                f"points={len(points)}/{original_point_count}, "
                f"elapsed={elapsed_sec:.3f}s, "
                f"frame_id={msg.header.frame_id}"
            )

        self.frame_index += 1

    def destroy_node(self):
        if hasattr(self, "file"):
            try:
                if not self.file.closed:
                    self.file.close()
            except Exception:
                pass

        super().destroy_node()


def main(args=None):
    rclpy.init(args=args)

    node = None

    try:
        node = PointCloud2JsonlRecorder()
        rclpy.spin(node)
    except KeyboardInterrupt:
        if node is not None:
            node.get_logger().info("recording stopped by Ctrl+C")
    finally:
        if node is not None:
            node.destroy_node()

        rclpy.shutdown()


if __name__ == "__main__":
    main()