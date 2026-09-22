import cv2
import depthai as dai
import rclpy
from rclpy.node import Node
from pathlib import Path


class VideoCollectorNode(Node):
    def __init__(self):
        super().__init__('video_collector_node')

        # =========================
        # [1] Parameter Declare
        # =========================

        self.declare_parameter('robot_id',          'spot_01')
        self.declare_parameter('output_root',        '/home/jetson/data')
        self.declare_parameter('sensor_subdir',      'video')
        self.declare_parameter('output_filename',    '')
        self.declare_parameter('session_index',      0)
        self.declare_parameter('overwrite_existing', False)
        self.declare_parameter('fps',                15)
        self.declare_parameter('width',              480)
        self.declare_parameter('height',             270)

        # =========================
        # [2] Parameter Read
        # =========================

        self.robot_id          = self.get_parameter('robot_id').value
        self.output_root       = self.get_parameter('output_root').value
        self.sensor_subdir     = self.get_parameter('sensor_subdir').value
        self.output_filename   = self.get_parameter('output_filename').value
        self.session_index     = int(self.get_parameter('session_index').value)
        self.overwrite_existing = bool(self.get_parameter('overwrite_existing').value)
        fps    = self.get_parameter('fps').value
        width  = self.get_parameter('width').value
        height = self.get_parameter('height').value

        if not self.output_filename:
            self.output_filename = f'{self.robot_id}_video.avi'

        # =========================
        # [3] Output Path 생성
        # =========================

        self.output_dir, self.output_file = self._create_output_path()

        # =========================
        # [4] VideoWriter 초기화
        # =========================

        self._writer = cv2.VideoWriter(
            self.output_file,
            cv2.VideoWriter_fourcc(*'MJPG'),
            float(fps),
            (width, height),
        )
        if not self._writer.isOpened():
            self.get_logger().error('VideoWriter 초기화 실패')
            raise RuntimeError('VideoWriter open failed')

        # =========================
        # [5] DepthAI 파이프라인 구성 (v3 API)
        # =========================

        pipeline = dai.Pipeline()

        cam = pipeline.create(dai.node.Camera).build(dai.CameraBoardSocket.CAM_A)
        rgb_out = cam.requestOutput(
            (width, height),
            dai.ImgFrame.Type.BGR888p,
        )
        self._rgb_queue = rgb_out.createOutputQueue(maxSize=1, blocking=False)

        self.pipeline = pipeline
        self.pipeline.start()

        self._frame_n = 0

        self.create_timer(1.0 / fps, self._capture)

        self.get_logger().info('video_collector_node started')
        self.get_logger().info(f'robot_id      : {self.robot_id}')
        self.get_logger().info(f'output_root   : {self.output_root}')
        self.get_logger().info(f'sensor_subdir : {self.sensor_subdir}')
        self.get_logger().info(f'session_index : {self.session_index}')
        self.get_logger().info(f'output_dir    : {self.output_dir}')
        self.get_logger().info(f'output_file   : {self.output_file}')
        self.get_logger().info(f'녹화 시작 — {fps}fps, {width}x{height}')

    def _create_output_path(self):
        root_path = Path(self.output_root).expanduser()
        root_path.mkdir(parents=True, exist_ok=True)

        if self.session_index > 0:
            selected_index = self.session_index
        else:
            selected_index = self._find_next_session_index(root_path)

        output_dir  = root_path / str(selected_index) / self.sensor_subdir
        output_file = output_dir / self.output_filename

        output_dir.mkdir(parents=True, exist_ok=True)

        if output_file.exists() and not self.overwrite_existing:
            raise RuntimeError(
                f'Output file already exists: {output_file}. '
                f'Use -p overwrite_existing:=true to overwrite.'
            )

        return str(output_dir), str(output_file)

    def _find_next_session_index(self, root_path):
        existing_indices = [
            int(child.name)
            for child in root_path.iterdir()
            if child.is_dir() and child.name.isdigit()
        ]
        return max(existing_indices) + 1 if existing_indices else 1

    def _capture(self):
        frame_data = self._rgb_queue.tryGet()
        if frame_data is None:
            return

        frame = frame_data.getCvFrame()
        self._writer.write(frame)
        self._frame_n += 1

    def destroy_node(self):
        self.get_logger().info(f'녹화 종료 — 총 {self._frame_n}프레임 저장')
        self._writer.release()
        self.pipeline.stop()
        super().destroy_node()


def main(args=None):
    rclpy.init(args=args)
    node = VideoCollectorNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
