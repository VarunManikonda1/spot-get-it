#pragma once

#include<atomic>
#include<chrono>
#include<cstddef>
#include<cstdint>
#include<mutex>
#include<string>
#include<thread>
#include<vector>

#include"actuator_bridge/packet_codec.hpp"

namespace actuator_bridge {

class UartTransport
{
public:
  UartTransport() = default;
  ~UartTransport();

  UartTransport(const UartTransport &) = delete;
  UartTransport & operator=(const UartTransport &) = delete;

  bool open_device(
    const std::string & device,
    int baudrate,
    std::size_t max_rx_buffer_size = 4096);

  bool is_open() const;
  void close_device();

  bool write_packet(const std::vector<uint8_t> & bytes);

  bool get_latest_feedback(
    FeedbackPacket & feedback,
    std::chrono::steady_clock::time_point & stamp) const;

  std::string last_error() const;

  uint32_t crc_error_count() const;
  uint32_t decode_error_count() const;
  uint32_t rx_overflow_count() const;

private:
  bool configure_port(int baudrate);
  void rx_loop();
  void parse_rx_buffer();
  void set_last_error(const std::string & error);

private:
  int fd_{-1};
  std::atomic<bool> running_{false};
  std::thread rx_thread_;

  std::size_t max_rx_buffer_size_{4096};

  mutable std::mutex tx_mutex_;
  mutable std::mutex feedback_mutex_;
  mutable std::mutex error_mutex_;
  mutable std::mutex rx_buffer_mutex_;

  std::vector<uint8_t> rx_buffer_;

  FeedbackPacket latest_feedback_{};
  std::chrono::steady_clock::time_point latest_feedback_stamp_{};
  bool have_feedback_{false};

  std::string last_error_;

  std::atomic<uint32_t> crc_error_count_{0};
  std::atomic<uint32_t> decode_error_count_{0};
  std::atomic<uint32_t> rx_overflow_count_{0};
};

}  // namespace actuator_bridge