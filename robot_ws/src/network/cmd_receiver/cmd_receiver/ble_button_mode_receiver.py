#!/usr/bin/env python3
import asyncio

import rclpy
from bleak import BleakClient, BleakScanner
from rclpy.node import Node
from std_msgs.msg import String

DEVICE_NAME = "UNO_R4_Button"
SERVICE_UUID = "19B10000-E8F2-537E-4F6C-D104768A1214"
CHAR_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214"
MODE_TOPIC = "/control/behavior/mode"

# 버튼 문자별 publish data 기본값입니다.
# 필요하면 여기만 바꾸거나, ros2 run 실행 시 파라미터로 덮어쓰면 됩니다.
DEFAULT_BUTTON_MODE_MAP = {
    "A": "CLASSIC",
    "B": "SIT",
    "C": "DETECT",
}


class BleButtonModeReceiver(Node):
    def __init__(self):
        super().__init__("ble_button_mode_receiver")

        self.declare_parameter("device_name", DEVICE_NAME)
        self.declare_parameter("service_uuid", SERVICE_UUID)
        self.declare_parameter("char_uuid", CHAR_UUID)
        self.declare_parameter("mode_topic", MODE_TOPIC)
        for button, mode in DEFAULT_BUTTON_MODE_MAP.items():
            self.declare_parameter(f"mode_for_{button.lower()}", mode)

        self.device_name = self.get_parameter("device_name").value
        self.service_uuid = self.get_parameter("service_uuid").value
        self.char_uuid = self.get_parameter("char_uuid").value
        self.mode_topic = self.get_parameter("mode_topic").value
        self.button_mode_map = {
            button: self.get_parameter(f"mode_for_{button.lower()}").value
            for button in DEFAULT_BUTTON_MODE_MAP
        }

        self.publisher_ = self.create_publisher(String, self.mode_topic, 10)

        mapping_text = ", ".join(
            f"{button}->{mode}" for button, mode in self.button_mode_map.items()
        )
        self.get_logger().info(f"button mode map: {mapping_text}")

    def notification_handler(self, sender, data):
        try:
            value = data.decode("utf-8").strip().upper()
        except UnicodeDecodeError:
            self.get_logger().warn(f"received undecodable BLE data from {sender}: {data!r}")
            return

        self.get_logger().info(f"RX: {value}")

        mode = self.button_mode_map.get(value)
        if not mode:
            self.get_logger().info(f"unknown button value: {value}")
            return

        msg = String()
        msg.data = mode
        self.publisher_.publish(msg)
        self.get_logger().info(f"publish: {mode} -> {self.mode_topic}")

    async def run_ble_loop(self):
        self.get_logger().info(f"Scanning BLE devices for {self.device_name}...")

        device = await BleakScanner.find_device_by_name(
            self.device_name,
            timeout=10.0,
        )

        if device is None:
            self.get_logger().error(f"Device not found: {self.device_name}")
            self.get_logger().error("UNO R4 WiFi가 켜져 있고 BLE advertise 중인지 확인하세요.")
            return

        self.get_logger().info(f"Found device: {device.name}")
        self.get_logger().info(f"Address: {device.address}")

        async with BleakClient(device) as client:
            self.get_logger().info("Connected")

            await client.start_notify(self.char_uuid, self.notification_handler)
            self.get_logger().info("Listening for A/B/C button notifications.")

            try:
                while rclpy.ok():
                    await asyncio.sleep(1)
            finally:
                await client.stop_notify(self.char_uuid)
                self.get_logger().info("Disconnected")


def main():
    rclpy.init()
    node = BleButtonModeReceiver()
    try:
        asyncio.run(node.run_ble_loop())
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        try:
            rclpy.shutdown()
        except Exception:
            pass


if __name__ == "__main__":
    main()
