#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace actuator_bridge
{

constexpr uint16_t COMMAND_MAGIC = 0xA55A;
constexpr uint16_t FEEDBACK_MAGIC = 0x5AA5;
constexpr std::size_t NUM_JOINTS = 12;

constexpr std::size_t COMMAND_PACKET_SIZE =
  2 +                         // magic
  2 +                         // seq
  4 +                         // timestamp_us
  1 +                         // mode
  1 +                         // flags
  4 * NUM_JOINTS +            // target_rad[12]
  4 * NUM_JOINTS +            // max_delta_rad[12
  4 +                         // gait_phase
  4 +                         // gait_cycle_count
  1 +                         // motion_state
  2;                          // crc16

constexpr std::size_t FEEDBACK_PACKET_SIZE =
  2 +                         // magic
  2 +                         // seq_echo
  4 +                         // timestamp_us
  1 +                         // status
  1 +                         // fault_code
  1 +                         // motion_state
  4 +                         // gait_phase
  4 +                         // gait_cycle_count
  4 +                         // imu_yaw_rad
  4 * NUM_JOINTS +            // position_rad[12]
  4 * NUM_JOINTS +            // velocity_rad_s[12]
  4 * NUM_JOINTS +            // load_or_current[12]
  4 * NUM_JOINTS +            // temperature[12]
  4 * 3 +                     // gyro_rad_s[3]
  4 * 3 +                     // accel_m_s2[3]
  4 * 4 +                     // quat_wxyz[4]
  4 +                         // bus_voltage
  2;                          // crc16

// SPI full-duplex requires equal tx/rx length.
constexpr std::size_t SPI_FRAME_SIZE = FEEDBACK_PACKET_SIZE;

struct CommandPacket
{
  uint16_t seq{0};
  uint32_t timestamp_us{0};
  uint8_t mode{0};
  uint8_t flags{0};
  std::array<float, NUM_JOINTS> target_rad{};
  std::array<float, NUM_JOINTS> max_delta_rad{};
  float gait_phase{0.0F};
  uint32_t gait_cycle_count{0};
  uint8_t motion_state{0};
};

struct FeedbackPacket
{
  uint16_t seq_echo{0};
  uint32_t timestamp_us{0};
  uint8_t status{0};
  uint8_t fault_code{0};
  uint8_t motion_state{0};
  float gait_phase{0.0F};
  uint32_t gait_cycle_count{0};
  float imu_yaw_rad{0.0F};
  std::array<float, NUM_JOINTS> position_rad{};
  std::array<float, NUM_JOINTS> velocity_rad_s{};
  std::array<float, NUM_JOINTS> load_or_current{};
  std::array<float, NUM_JOINTS> temperature{};
  std::array<float, 3> gyro_rad_s{};
  std::array<float, 3> accel_m_s2{};
  std::array<float, 4> quat_wxyz{1.0F, 0.0F, 0.0F, 0.0F};  // w, x, y, z
  float bus_voltage{0.0F};
};

enum class DecodeResult
{
  OK = 0,
  SIZE_MISMATCH,
  BAD_MAGIC,
  BAD_CRC
};

uint16_t crc16_ccitt_false(const uint8_t * data, std::size_t length);

std::vector<uint8_t> encode_command_packet(const CommandPacket & packet);

DecodeResult decode_command_packet(
  const std::vector<uint8_t> & bytes,
  CommandPacket & packet);

std::vector<uint8_t> make_spi_tx_frame(const CommandPacket & packet);

std::vector<uint8_t> encode_feedback_packet(const FeedbackPacket & packet);

DecodeResult decode_feedback_packet(
  const std::vector<uint8_t> & bytes,
  FeedbackPacket & packet);

const char * to_string(DecodeResult result);

}  // namespace actuator_bridge
