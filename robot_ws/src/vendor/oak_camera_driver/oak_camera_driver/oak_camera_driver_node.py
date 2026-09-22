import math
import socket
import struct
import time

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Image
from cv_bridge import CvBridge
import depthai as dai


# ─── proto.h 상수 ─────────────────────────────────────────────
PKT_TYPE_IMAGE = 0x01
BRIDGE_PORT    = 9000
PROTO_MTU      = 1400

# PktHeader (24B, little-endian):
#   type(1) robot_id(1) frag_idx(2) frag_total(2)
#   payload_len(2) frame_id(4) payload_offset(4) timestamp_us(8)
_HDR_FMT  = '<BBHHHIIQ'
_HDR_SIZE = struct.calcsize(_HDR_FMT)   # 24


def _now_us() -> int:
    return int(time.monotonic() * 1_000_000)


def _build_header(robot_id: int, frag_idx: int, frag_total: int,
                  payload_len: int, frame_id: int, offset: int) -> bytes:
    return struct.pack(
        _HDR_FMT,
        PKT_TYPE_IMAGE,
        robot_id,
        frag_idx,
        frag_total,
        payload_len,
        frame_id,
        offset,
        _now_us(),
    )


class OakCameraDriverNode(Node):

    def __init__(self):
        super().__init__('oak_camera_driver_node')

        # 파라미터 선언
        self.declare_parameter('fps',            15)
        self.declare_parameter('image_raw_fps',   5)    # perception용 publish fps
        self.declare_parameter('rgb_width',      480)
        self.declare_parameter('rgb_height',     270)
        self.declare_parameter('show_preview',   False)
        self.declare_parameter('preview_width',  1280)
        self.declare_parameter('preview_height', 720)
        self.declare_parameter('rpi_ip',         'REQUIRED')
        self.declare_parameter('robot_id',       0)
        self.declare_parameter('max_size_bytes', 200000)

        self.fps            = self.get_parameter('fps').value
        image_raw_fps       = self.get_parameter('image_raw_fps').value
        self.rgb_width      = self.get_parameter('rgb_width').value
        self.rgb_height     = self.get_parameter('rgb_height').value
        self.show_preview   = self.get_parameter('show_preview').value
        self.preview_width  = self.get_parameter('preview_width').value
        self.preview_height = self.get_parameter('preview_height').value

        rpi_ip = self.get_parameter('rpi_ip').value
        if rpi_ip == 'REQUIRED':
            self.get_logger().fatal('파라미터 rpi_ip 가 설정되지 않았습니다.')
            raise RuntimeError('rpi_ip 파라미터 필수')

        self._robot_id  = int(self.get_parameter('robot_id').value)
        self._max_size  = int(self.get_parameter('max_size_bytes').value)

        if image_raw_fps > self.fps:
            self.get_logger().warn(
                f'image_raw_fps({image_raw_fps}) > fps({self.fps}), fps로 클램프'
            )
            image_raw_fps = self.fps
        self._skip_interval = round(self.fps / image_raw_fps)

        # UDP 소켓
        self._sock     = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._dst      = (rpi_ip, BRIDGE_PORT)
        self._frame_id = 0
        self._fps_count = 0
        self._t_last    = time.monotonic()

        # Publisher (image_raw only)
        self.rgb_pub = self.create_publisher(Image, '/vendor/camera/image_raw', 1)
        self.bridge  = CvBridge()

        self.pipeline = self._build_pipeline()
        self.pipeline.start()
        self.get_logger().info(
            f'OAK 카메라 시작 | {self.rgb_width}x{self.rgb_height} '
            f'@ 캡처 {self.fps}fps | image_raw {image_raw_fps}fps (1/{self._skip_interval}) '
            f'| encoded UDP → {rpi_ip}:{BRIDGE_PORT} robot_id={self._robot_id}'
        )

        self.frame_count = 0
        self.timer = self.create_timer(1.0 / self.fps, self.timer_callback)

    def _build_pipeline(self) -> dai.Pipeline:
        pipeline = dai.Pipeline()

        # RGB 카메라
        cam_rgb = pipeline.create(dai.node.Camera).build(dai.CameraBoardSocket.CAM_A)

        # RAW RGB 출력
        rgb_out = cam_rgb.requestOutput(
            (self.rgb_width, self.rgb_height),
            dai.ImgFrame.Type.BGR888p
        )
        self.rgb_queue = rgb_out.createOutputQueue(maxSize=1, blocking=False)

        # MJPEG 인코더 (width: 32의 배수, height: 8의 배수)
        enc_w = (self.rgb_width  + 31) // 32 * 32
        enc_h = (self.rgb_height +  7) //  8 *  8
        encoder = pipeline.create(dai.node.VideoEncoder)
        encoder.setDefaultProfilePreset(
            self.fps,
            dai.VideoEncoderProperties.Profile.MJPEG
        )
        encoder_input = cam_rgb.requestOutput(
            (enc_w, enc_h),
            dai.ImgFrame.Type.NV12
        )
        encoder_input.link(encoder.input)
        self.encoded_queue = encoder.bitstream.createOutputQueue(maxSize=1, blocking=False)

        return pipeline

    def timer_callback(self):
        now = self.get_clock().now().to_msg()
        self.frame_count += 1

        # RAW RGB publish (image_raw_fps 기준 스킵)
        rgb_data = self.rgb_queue.tryGet()
        if rgb_data is not None and self.frame_count % self._skip_interval == 0:
            frame = rgb_data.getCvFrame()
            msg = self.bridge.cv2_to_imgmsg(frame, encoding='bgr8')
            msg.header.stamp    = now
            msg.header.frame_id = 'oak_camera'
            self.rgb_pub.publish(msg)

        # MJPEG encoded → UDP 직접 송신 (매 프레임 = fps 그대로)
        encoded_data = self.encoded_queue.tryGet()
        if encoded_data is not None:
            self._send_encoded(encoded_data.getData().tobytes())

        # 미리보기
        if self.show_preview and rgb_data is not None:
            import cv2
            preview = cv2.resize(rgb_data.getCvFrame(), (self.preview_width, self.preview_height))
            cv2.imshow('oak_camera_driver preview', preview)
            if cv2.waitKey(1) & 0xFF == ord('q'):
                rclpy.shutdown()

    def _send_encoded(self, payload: bytes) -> None:
        size = len(payload)
        if size == 0:
            return

        if size > self._max_size:
            self.get_logger().warn(f'프레임 너무 큼: {size}B > {self._max_size}B — skip')
            return

        frag_total = math.ceil(size / PROTO_MTU)
        for idx in range(frag_total):
            offset = idx * PROTO_MTU
            chunk  = payload[offset: offset + PROTO_MTU]
            hdr    = _build_header(
                robot_id    = self._robot_id,
                frag_idx    = idx,
                frag_total  = frag_total,
                payload_len = len(chunk),
                frame_id    = self._frame_id,
                offset      = offset,
            )
            try:
                self._sock.sendto(hdr + chunk, self._dst)
            except OSError as e:
                self.get_logger().error(f'UDP 송신 오류: {e}')
                return

        self._frame_id = (self._frame_id + 1) & 0xFFFF_FFFF
        self._fps_count += 1

        # FPS 로그 (1초마다)
        now = time.monotonic()
        if now - self._t_last >= 1.0:
            self.get_logger().info(
                f'encoded FPS={self._fps_count}  size={size}B  '
                f'frags={math.ceil(size / PROTO_MTU)}'
            )
            self._fps_count = 0
            self._t_last    = now

    def destroy_node(self):
        self._sock.close()
        self.pipeline.stop()
        super().destroy_node()


def main(args=None):
    rclpy.init(args=args)
    try:
        node = OakCameraDriverNode()
    except RuntimeError:
        rclpy.shutdown()
        return

    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        if rclpy.ok():
            node.destroy_node()
            rclpy.shutdown()


if __name__ == '__main__':
    main()
