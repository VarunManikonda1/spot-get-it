#include "actuator_bridge/spi_transport.hpp"

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace actuator_bridge
{

SpiTransport::~SpiTransport()
{
  close_device();
  close_data_ready();
}

bool SpiTransport::open_device(
  const std::string & device,
  uint32_t speed_hz,
  uint8_t mode,
  uint8_t bits_per_word)
{
  close_device();

  fd_ = ::open(device.c_str(), O_RDWR);
  if (fd_ < 0) {
    last_error_ = "failed to open " + device + ": " + std::strerror(errno);
    return false;
  }

  speed_hz_ = speed_hz;
  mode_ = mode;
  bits_per_word_ = bits_per_word;

  if (ioctl(fd_, SPI_IOC_WR_MODE, &mode_) < 0) {
    last_error_ = "SPI_IOC_WR_MODE failed: " + std::string(std::strerror(errno));
    close_device();
    return false;
  }

  if (ioctl(fd_, SPI_IOC_RD_MODE, &mode_) < 0) {
    last_error_ = "SPI_IOC_RD_MODE failed: " + std::string(std::strerror(errno));
    close_device();
    return false;
  }

  if (ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word_) < 0) {
    last_error_ = "SPI_IOC_WR_BITS_PER_WORD failed: " + std::string(std::strerror(errno));
    close_device();
    return false;
  }

  if (ioctl(fd_, SPI_IOC_RD_BITS_PER_WORD, &bits_per_word_) < 0) {
    last_error_ = "SPI_IOC_RD_BITS_PER_WORD failed: " + std::string(std::strerror(errno));
    close_device();
    return false;
  }

  if (ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz_) < 0) {
    last_error_ = "SPI_IOC_WR_MAX_SPEED_HZ failed: " + std::string(std::strerror(errno));
    close_device();
    return false;
  }

  if (ioctl(fd_, SPI_IOC_RD_MAX_SPEED_HZ, &speed_hz_) < 0) {
    last_error_ = "SPI_IOC_RD_MAX_SPEED_HZ failed: " + std::string(std::strerror(errno));
    close_device();
    return false;
  }

  return true;
}

bool SpiTransport::configure_data_ready(
  bool enabled,
  const std::string & gpiochip,
  unsigned int line_offset,
  bool active_high,
  int timeout_us,
  int poll_interval_us)
{
  close_data_ready();

  use_data_ready_ = enabled;
  data_ready_active_high_ = active_high;
  data_ready_timeout_us_ = timeout_us;
  data_ready_poll_interval_us_ = poll_interval_us;

  if (!use_data_ready_) {
    return true;
  }

  data_ready_chip_ = gpiod_chip_open(gpiochip.c_str());
  if (data_ready_chip_ == nullptr) {
    last_error_ = "failed to open DATA_READY gpiochip " + gpiochip + ": " +
      std::strerror(errno);
    use_data_ready_ = false;
    return false;
  }

  data_ready_line_ = gpiod_chip_get_line(data_ready_chip_, line_offset);
  if (data_ready_line_ == nullptr) {
    last_error_ = "failed to get DATA_READY line " + std::to_string(line_offset) +
      ": " + std::strerror(errno);
    close_data_ready();
    use_data_ready_ = false;
    return false;
  }

  if (gpiod_line_request_input(data_ready_line_, "actuator_bridge_data_ready") < 0) {
    last_error_ = "failed to request DATA_READY input line: " +
      std::string(std::strerror(errno));
    close_data_ready();
    use_data_ready_ = false;
    return false;
  }

  return true;
}

bool SpiTransport::is_open() const
{
  return fd_ >= 0;
}

bool SpiTransport::wait_data_ready()
{
  if (!use_data_ready_) {
    return true;
  }

  if (data_ready_line_ == nullptr) {
    last_error_ = "DATA_READY enabled but GPIO line is not initialized";
    return false;
  }

  const auto start = std::chrono::steady_clock::now();

  while (true) {
    const int value = gpiod_line_get_value(data_ready_line_);
    if (value < 0) {
      last_error_ = "failed to read DATA_READY line: " + std::string(std::strerror(errno));
      return false;
    }

    const bool ready = data_ready_active_high_ ? (value == 1) : (value == 0);
    if (ready) {
      return true;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed_us =
      std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();

    if (elapsed_us >= data_ready_timeout_us_) {
      last_error_ = "DATA_READY timeout";
      return false;
    }

    std::this_thread::sleep_for(
      std::chrono::microseconds(data_ready_poll_interval_us_));
  }
}

bool SpiTransport::transfer(
  const std::vector<uint8_t> & tx,
  std::vector<uint8_t> & rx)
{
  if (fd_ < 0) {
    last_error_ = "SPI device is not open";
    return false;
  }

  if (tx.empty()) {
    last_error_ = "tx buffer is empty";
    return false;
  }

  if (!wait_data_ready()) {
    return false;
  }

  rx.assign(tx.size(), 0x00);

  spi_ioc_transfer tr{};
  tr.tx_buf = reinterpret_cast<unsigned long>(tx.data());
  tr.rx_buf = reinterpret_cast<unsigned long>(rx.data());
  tr.len = static_cast<uint32_t>(tx.size());
  tr.speed_hz = speed_hz_;
  tr.bits_per_word = bits_per_word_;
  tr.delay_usecs = 0;
  tr.cs_change = 0;

  const int ret = ioctl(fd_, SPI_IOC_MESSAGE(1), &tr);
  if (ret < 1) {
    last_error_ = "SPI_IOC_MESSAGE failed: " + std::string(std::strerror(errno));
    return false;
  }

  return true;
}

void SpiTransport::close_device()
{
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

void SpiTransport::close_data_ready()
{
  if (data_ready_line_ != nullptr) {
    gpiod_line_release(data_ready_line_);
    data_ready_line_ = nullptr;
  }

  if (data_ready_chip_ != nullptr) {
    gpiod_chip_close(data_ready_chip_);
    data_ready_chip_ = nullptr;
  }
}

std::string SpiTransport::last_error() const
{
  return last_error_;
}

}  // namespace actuator_bridge