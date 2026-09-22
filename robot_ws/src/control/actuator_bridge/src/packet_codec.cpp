#include "actuator_bridge/packet_codec.hpp"

#include <cstring>
#include <stdexcept>

namespace actuator_bridge
{
namespace
{

void write_u8(std::vector<uint8_t> & out, uint8_t value)
{
  out.push_back(value);
}

void write_u16_le(std::vector<uint8_t> & out, uint16_t value)
{
  out.push_back(static_cast<uint8_t>(value & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
}

void write_u32_le(std::vector<uint8_t> & out, uint32_t value)
{
  out.push_back(static_cast<uint8_t>(value & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<uint8_t>((value >> 24U) & 0xFFU));
}

void write_f32_le(std::vector<uint8_t> & out, float value)
{
  static_assert(sizeof(float) == 4, "float must be 32-bit");

  uint32_t raw = 0;
  std::memcpy(&raw, &value, sizeof(float));
  write_u32_le(out, raw);
}

uint8_t read_u8(const std::vector<uint8_t> & in, std::size_t & offset)
{
  if (offset + 1 > in.size()) {
    throw std::out_of_range("read_u8 out of range");
  }

  return in[offset++];
}

uint16_t read_u16_le(const std::vector<uint8_t> & in, std::size_t & offset)
{
  if (offset + 2 > in.size()) {
    throw std::out_of_range("read_u16_le out of range");
  }

  const uint16_t value =
    static_cast<uint16_t>(in[offset]) |
    static_cast<uint16_t>(static_cast<uint16_t>(in[offset + 1]) << 8U);

  offset += 2;
  return value;
}

uint32_t read_u32_le(const std::vector<uint8_t> & in, std::size_t & offset)
{
  if (offset + 4 > in.size()) {
    throw std::out_of_range("read_u32_le out of range");
  }

  const uint32_t value =
    static_cast<uint32_t>(in[offset]) |
    (static_cast<uint32_t>(in[offset + 1]) << 8U) |
    (static_cast<uint32_t>(in[offset + 2]) << 16U) |
    (static_cast<uint32_t>(in[offset + 3]) << 24U);

  offset += 4;
  return value;
}

float read_f32_le(const std::vector<uint8_t> & in, std::size_t & offset)
{
  const uint32_t raw = read_u32_le(in, offset);

  float value = 0.0F;
  std::memcpy(&value, &raw, sizeof(float));
  return value;
}

template<std::size_t N>
void write_float_array(
  std::vector<uint8_t> & out,
  const std::array<float, N> & values)
{
  for (const float value : values) {
    write_f32_le(out, value);
  }
}

template<std::size_t N>
void read_float_array(
  const std::vector<uint8_t> & in,
  std::size_t & offset,
  std::array<float, N> & values)
{
  for (float & value : values) {
    value = read_f32_le(in, offset);
  }
}

bool verify_crc(const std::vector<uint8_t> & bytes)
{
  if (bytes.size() < 2) {
    return false;
  }

  const std::size_t payload_size = bytes.size() - 2;
  const uint16_t expected = crc16_ccitt_false(bytes.data(), payload_size);

  std::size_t crc_offset = payload_size;
  const uint16_t received = read_u16_le(bytes, crc_offset);

  return expected == received;
}

}  // namespace

uint16_t crc16_ccitt_false(const uint8_t * data, std::size_t length)
{
  uint16_t crc = 0xFFFFU;

  for (std::size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8U;

    for (int bit = 0; bit < 8; ++bit) {
      if ((crc & 0x8000U) != 0U) {
        crc = static_cast<uint16_t>((crc << 1U) ^ 0x1021U);
      } else {
        crc = static_cast<uint16_t>(crc << 1U);
      }
    }
  }

  return crc;
}

std::vector<uint8_t> encode_command_packet(const CommandPacket & packet)
{
  std::vector<uint8_t> out;
  out.reserve(COMMAND_PACKET_SIZE);

  write_u16_le(out, COMMAND_MAGIC);
  write_u16_le(out, packet.seq);
  write_u32_le(out, packet.timestamp_us);
  write_u8(out, packet.mode);
  write_u8(out, packet.flags);
  write_float_array(out, packet.target_rad);
  write_float_array(out, packet.max_delta_rad);
  write_f32_le(out, packet.gait_phase);
  write_u32_le(out, packet.gait_cycle_count);
  write_u8(out, packet.motion_state);

  const uint16_t crc = crc16_ccitt_false(out.data(), out.size());
  write_u16_le(out, crc);

  return out;
}

std::vector<uint8_t> make_spi_tx_frame(const CommandPacket & packet)
{
  std::vector<uint8_t> frame = encode_command_packet(packet);
  frame.resize(SPI_FRAME_SIZE, 0x00);
  return frame;
}

DecodeResult decode_command_packet(
  const std::vector<uint8_t> & bytes,
  CommandPacket & packet)
{
  if (bytes.size() != COMMAND_PACKET_SIZE) {
    return DecodeResult::SIZE_MISMATCH;
  }

  try {
    std::size_t magic_offset = 0;
    const uint16_t magic = read_u16_le(bytes, magic_offset);

    if (magic != COMMAND_MAGIC) {
      return DecodeResult::BAD_MAGIC;
    }

    if (!verify_crc(bytes)) {
      return DecodeResult::BAD_CRC;
    }

    std::size_t offset = 2;
    packet.seq = read_u16_le(bytes, offset);
    packet.timestamp_us = read_u32_le(bytes, offset);
    packet.mode = read_u8(bytes, offset);
    packet.flags = read_u8(bytes, offset);
    read_float_array(bytes, offset, packet.target_rad);
    read_float_array(bytes, offset, packet.max_delta_rad);
    packet.gait_phase = read_f32_le(bytes, offset);
    packet.gait_cycle_count = read_u32_le(bytes, offset);
    packet.motion_state = read_u8(bytes, offset);

    return DecodeResult::OK;
  } catch (...) {
    return DecodeResult::SIZE_MISMATCH;
  }
}

std::vector<uint8_t> encode_feedback_packet(const FeedbackPacket & packet)
{
  std::vector<uint8_t> out;
  out.reserve(FEEDBACK_PACKET_SIZE);

  write_u16_le(out, FEEDBACK_MAGIC);
  write_u16_le(out, packet.seq_echo);
  write_u32_le(out, packet.timestamp_us);
  write_u8(out, packet.status);
  write_u8(out, packet.fault_code);
  write_u8(out, packet.motion_state);
  write_f32_le(out, packet.gait_phase);
  write_u32_le(out, packet.gait_cycle_count);
  write_f32_le(out, packet.imu_yaw_rad);
  write_float_array(out, packet.position_rad);
  write_float_array(out, packet.velocity_rad_s);
  write_float_array(out, packet.load_or_current);
  write_float_array(out, packet.temperature);
  write_float_array(out, packet.gyro_rad_s);
  write_float_array(out, packet.accel_m_s2);
  write_float_array(out, packet.quat_wxyz);
  write_f32_le(out, packet.bus_voltage);

  const uint16_t crc = crc16_ccitt_false(out.data(), out.size());
  write_u16_le(out, crc);

  return out;
}

DecodeResult decode_feedback_packet(
  const std::vector<uint8_t> & bytes,
  FeedbackPacket & packet)
{
  if (bytes.size() != FEEDBACK_PACKET_SIZE) {
    return DecodeResult::SIZE_MISMATCH;
  }

  try {
    std::size_t magic_offset = 0;
    const uint16_t magic = read_u16_le(bytes, magic_offset);

    if (magic != FEEDBACK_MAGIC) {
      return DecodeResult::BAD_MAGIC;
    }

    if (!verify_crc(bytes)) {
      return DecodeResult::BAD_CRC;
    }

    std::size_t offset = 2;
    packet.seq_echo = read_u16_le(bytes, offset);
    packet.timestamp_us = read_u32_le(bytes, offset);
    packet.status = read_u8(bytes, offset);
    packet.fault_code = read_u8(bytes, offset);
    packet.motion_state = read_u8(bytes, offset);
    packet.gait_phase = read_f32_le(bytes, offset);
    packet.gait_cycle_count = read_u32_le(bytes, offset);
    packet.imu_yaw_rad = read_f32_le(bytes, offset);
    read_float_array(bytes, offset, packet.position_rad);
    read_float_array(bytes, offset, packet.velocity_rad_s);
    read_float_array(bytes, offset, packet.load_or_current);
    read_float_array(bytes, offset, packet.temperature);
    read_float_array(bytes, offset, packet.gyro_rad_s);
    read_float_array(bytes, offset, packet.accel_m_s2);
    read_float_array(bytes, offset, packet.quat_wxyz);
    packet.bus_voltage = read_f32_le(bytes, offset);

    return DecodeResult::OK;
  } catch (...) {
    return DecodeResult::SIZE_MISMATCH;
  }
}

const char * to_string(DecodeResult result)
{
  switch (result) {
    case DecodeResult::OK:
      return "OK";
    case DecodeResult::SIZE_MISMATCH:
      return "SIZE_MISMATCH";
    case DecodeResult::BAD_MAGIC:
      return "BAD_MAGIC";
    case DecodeResult::BAD_CRC:
      return "BAD_CRC";
    default:
      return "UNKNOWN";
  }
}

}  // namespace actuator_bridge
