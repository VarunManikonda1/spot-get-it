#include "actuator_bridge/packet_codec.hpp"

#include <cmath>
#include <iostream>
#include <vector>

namespace
{

bool almost_equal(float a, float b, float eps = 1e-6F)
{
  return std::fabs(a - b) < eps;
}

int fail(const std::string & message)
{
  std::cerr << "[FAIL] " << message << std::endl;
  return 1;
}

}  // namespace

int main()
{
  using namespace actuator_bridge;

  std::cout << "COMMAND_PACKET_SIZE = " << COMMAND_PACKET_SIZE << " bytes" << std::endl;
  std::cout << "FEEDBACK_PACKET_SIZE = " << FEEDBACK_PACKET_SIZE << " bytes" << std::endl;

  if (COMMAND_PACKET_SIZE != 117) {
    return fail("Command packet size must be 117 bytes");
  }

  if (FEEDBACK_PACKET_SIZE != 261) {
    return fail("Feedback packet size must be 261 bytes");
  }

  CommandPacket cmd;
  cmd.seq = 42;
  cmd.timestamp_us = 12345678;
  cmd.mode = 2;
  cmd.flags = 0x01;
  cmd.motion_state = 3;

  for (std::size_t i = 0; i < NUM_JOINTS; ++i) {
    cmd.target_rad[i] = static_cast<float>(0.1 * static_cast<double>(i));
    cmd.max_delta_rad[i] = 0.03F;
  }

  const std::vector<uint8_t> cmd_bytes = encode_command_packet(cmd);

  if (cmd_bytes.size() != COMMAND_PACKET_SIZE) {
    return fail("encoded command packet size mismatch");
  }

  CommandPacket decoded_cmd;
  DecodeResult cmd_result = decode_command_packet(cmd_bytes, decoded_cmd);

  if (cmd_result != DecodeResult::OK) {
    return fail(std::string("decode command failed: ") + to_string(cmd_result));
  }

  if (decoded_cmd.seq != cmd.seq) {
    return fail("command seq mismatch");
  }

  if (decoded_cmd.timestamp_us != cmd.timestamp_us) {
    return fail("command timestamp mismatch");
  }

  if (decoded_cmd.mode != cmd.mode) {
    return fail("command mode mismatch");
  }

  if (decoded_cmd.flags != cmd.flags) {
    return fail("command flags mismatch");
  }

  if (decoded_cmd.motion_state != cmd.motion_state) {
    return fail("command motion_state mismatch");
  }

  for (std::size_t i = 0; i < NUM_JOINTS; ++i) {
    if (!almost_equal(decoded_cmd.target_rad[i], cmd.target_rad[i])) {
      return fail("command target_rad mismatch");
    }

    if (!almost_equal(decoded_cmd.max_delta_rad[i], cmd.max_delta_rad[i])) {
      return fail("command max_delta_rad mismatch");
    }
  }

  std::vector<uint8_t> corrupted_cmd = cmd_bytes;
  corrupted_cmd[20] ^= 0xAA;

  CommandPacket should_fail_cmd;
  DecodeResult corrupted_cmd_result =
    decode_command_packet(corrupted_cmd, should_fail_cmd);

  if (corrupted_cmd_result != DecodeResult::BAD_CRC) {
    return fail("corrupted command packet must fail with BAD_CRC");
  }

  FeedbackPacket fb;
  fb.seq_echo = 42;
  fb.timestamp_us = 12345999;
  fb.status = 0;
  fb.fault_code = 0;
  fb.bus_voltage = 12.0F;
  fb.gyro_rad_s = {0.01F, 0.02F, 0.03F};
  fb.quat_wxyz = {1.0F, 0.0F, 0.0F, 0.0F};

  for (std::size_t i = 0; i < NUM_JOINTS; ++i) {
    fb.position_rad[i] = static_cast<float>(-0.5 + 0.1 * static_cast<double>(i));
    fb.velocity_rad_s[i] = static_cast<float>(0.01 * static_cast<double>(i));
    fb.load_or_current[i] = static_cast<float>(0.2 * static_cast<double>(i));
    fb.temperature[i] = 25.0F + static_cast<float>(i);
  }

  const std::vector<uint8_t> fb_bytes = encode_feedback_packet(fb);

  if (fb_bytes.size() != FEEDBACK_PACKET_SIZE) {
    return fail("encoded feedback packet size mismatch");
  }

  FeedbackPacket decoded_fb;
  DecodeResult fb_result = decode_feedback_packet(fb_bytes, decoded_fb);

  if (fb_result != DecodeResult::OK) {
    return fail(std::string("decode feedback failed: ") + to_string(fb_result));
  }

  if (decoded_fb.seq_echo != fb.seq_echo) {
    return fail("feedback seq_echo mismatch");
  }

  if (decoded_fb.timestamp_us != fb.timestamp_us) {
    return fail("feedback timestamp mismatch");
  }

  if (decoded_fb.status != fb.status) {
    return fail("feedback status mismatch");
  }

  if (decoded_fb.fault_code != fb.fault_code) {
    return fail("feedback fault_code mismatch");
  }

  if (!almost_equal(decoded_fb.bus_voltage, fb.bus_voltage)) {
    return fail("feedback bus_voltage mismatch");
  }

  for (std::size_t i = 0; i < NUM_JOINTS; ++i) {
    if (!almost_equal(decoded_fb.position_rad[i], fb.position_rad[i])) {
      return fail("feedback position_rad mismatch");
    }

    if (!almost_equal(decoded_fb.velocity_rad_s[i], fb.velocity_rad_s[i])) {
      return fail("feedback velocity_rad_s mismatch");
    }

    if (!almost_equal(decoded_fb.load_or_current[i], fb.load_or_current[i])) {
      return fail("feedback load_or_current mismatch");
    }

    if (!almost_equal(decoded_fb.temperature[i], fb.temperature[i])) {
      return fail("feedback temperature mismatch");
    }
  }

  std::vector<uint8_t> corrupted_fb = fb_bytes;
  corrupted_fb[50] ^= 0x55;

  FeedbackPacket should_fail_fb;
  DecodeResult corrupted_fb_result =
    decode_feedback_packet(corrupted_fb, should_fail_fb);

  if (corrupted_fb_result != DecodeResult::BAD_CRC) {
    return fail("corrupted feedback packet must fail with BAD_CRC");
  }

  std::cout << "[PASS] packet codec encode/decode/crc test passed" << std::endl;
  return 0;
}
