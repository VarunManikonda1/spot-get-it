#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <gpiod.h>

namespace actuator_bridge
{

class SpiTransport
{
public:
  SpiTransport() = default;
  ~SpiTransport();

  bool open_device(
    const std::string & device,
    uint32_t speed_hz,
    uint8_t mode,
    uint8_t bits_per_word);

  bool configure_data_ready(
    bool enabled,
    const std::string & gpiochip,
    unsigned int line_offset,
    bool active_high,
    int timeout_us,
    int poll_interval_us);

  bool is_open() const;

  bool transfer(
    const std::vector<uint8_t> & tx,
    std::vector<uint8_t> & rx);

  void close_device();
  void close_data_ready();

  std::string last_error() const;

private:
  bool wait_data_ready();

private:
  int fd_{-1};
  uint32_t speed_hz_{1000000};
  uint8_t mode_{0};
  uint8_t bits_per_word_{8};
  std::string last_error_;

  bool use_data_ready_{false};
  bool data_ready_active_high_{true};
  int data_ready_timeout_us_{5000};
  int data_ready_poll_interval_us_{50};

  gpiod_chip * data_ready_chip_{nullptr};
  gpiod_line * data_ready_line_{nullptr};
};

}  // namespace actuator_bridge