#include "actuator_bridge/uart_transport.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <vector>

namespace actuator_bridge {
namespace {

bool baudrate_to_speed(int baudrate, speed_t & speed)
{
  switch (baudrate) {
    case 9600: speed = B9600; return true;
    case 19200: speed = B19200; return true;
    case 38400: speed = B38400; return true;
    case 57600: speed = B57600; return true;
    case 115200: speed = B115200; return true;
    case 230400: speed = B230400; return true;
    case 460800: speed = B460800; return true;
    case 500000: speed = B500000; return true;
    case 576000: speed = B576000; return true;
    case 921600: speed = B921600; return true;
#ifdef B1000000
    case 1000000: speed = B1000000; return true;
#endif
#ifdef B1152000
    case 1152000: speed = B1152000; return true;
#endif
#ifdef B1500000
    case 1500000: speed = B1500000; return true;
#endif
#ifdef B2000000
    case 2000000: speed = B2000000; return true;
#endif
    default: return false;
  }
}

uint16_t read_u16_le_from_buffer(const std::vector<uint8_t> & bytes, std::size_t offset)
{
  return static_cast<uint16_t>(bytes[offset]) |
         (static_cast<uint16_t>(bytes[offset + 1]) << 8U);
}

}  // namespace

UartTransport::~UartTransport()
{
  close_device();
}

bool UartTransport::open_device(
  const std::string & device,
  int baudrate,
  std::size_t max_rx_buffer_size)
{
  close_device();

  max_rx_buffer_size_ = std::max<std::size_t>(max_rx_buffer_size, FEEDBACK_PACKET_SIZE * 2U);

  fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd_ < 0) {
    set_last_error("failed to open " + device + ": " + std::strerror(errno));
    return false;
  }

  if (!configure_port(baudrate)) {
    close_device();
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(rx_buffer_mutex_);
    rx_buffer_.clear();
    rx_buffer_.reserve(max_rx_buffer_size_);
  }

  {
    std::lock_guard<std::mutex> lock(feedback_mutex_);
    have_feedback_ = false;
    latest_feedback_ = FeedbackPacket{};
    latest_feedback_stamp_ = std::chrono::steady_clock::time_point{};
  }

  crc_error_count_ = 0;
  decode_error_count_ = 0;
  rx_overflow_count_ = 0;

  running_ = true;
  rx_thread_ = std::thread(&UartTransport::rx_loop, this);

  return true;
}

bool UartTransport::configure_port(int baudrate)
{
  if (fd_ < 0) {
    set_last_error("UART device is not open");
    return false;
  }

  speed_t speed{};
  if (!baudrate_to_speed(baudrate, speed)) {
    set_last_error("unsupported UART baudrate: " + std::to_string(baudrate));
    return false;
  }

  termios tty{};
  if (tcgetattr(fd_, &tty) != 0) {
    set_last_error("tcgetattr failed: " + std::string(std::strerror(errno)));
    return false;
  }

  cfmakeraw(&tty);

  if (cfsetispeed(&tty, speed) != 0 || cfsetospeed(&tty, speed) != 0) {
    set_last_error("cfsetispeed/cfsetospeed failed: " + std::string(std::strerror(errno)));
    return false;
  }

  tty.c_cflag |= static_cast<tcflag_t>(CLOCAL | CREAD);
  tty.c_cflag &= static_cast<tcflag_t>(~CSIZE);
  tty.c_cflag |= CS8;
  tty.c_cflag &= static_cast<tcflag_t>(~PARENB);
  tty.c_cflag &= static_cast<tcflag_t>(~CSTOPB);
#ifdef CRTSCTS
  tty.c_cflag &= static_cast<tcflag_t>(~CRTSCTS);
#endif

  // Non-blocking fd + poll based RX loop. VMIN/VTIME are still set safely.
  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 1;

  if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
    set_last_error("tcsetattr failed: " + std::string(std::strerror(errno)));
    return false;
  }

  if (tcflush(fd_, TCIOFLUSH) != 0) {
    set_last_error("tcflush failed: " + std::string(std::strerror(errno)));
    return false;
  }

  return true;
}

bool UartTransport::is_open() const
{
  return fd_ >= 0;
}

void UartTransport::close_device()
{
  running_ = false;

  if (rx_thread_.joinable()) {
    rx_thread_.join();
  }

  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }

  {
    std::lock_guard<std::mutex> lock(rx_buffer_mutex_);
    rx_buffer_.clear();
  }

  {
    std::lock_guard<std::mutex> lock(feedback_mutex_);
    have_feedback_ = false;
  }
}

bool UartTransport::write_packet(const std::vector<uint8_t> & bytes)
{
  if (fd_ < 0) {
    set_last_error("UART device is not open");
    return false;
  }

  if (bytes.empty()) {
    set_last_error("UART write packet is empty");
    return false;
  }

  std::lock_guard<std::mutex> lock(tx_mutex_);

  std::size_t written_total = 0;
  while (written_total < bytes.size()) {
    pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLOUT;

    const int poll_ret = ::poll(&pfd, 1, 20);  // ms
    if (poll_ret < 0) {
      if (errno == EINTR) {
        continue;
      }
      set_last_error("UART write poll failed: " + std::string(std::strerror(errno)));
      return false;
    }
    if (poll_ret == 0) {
      set_last_error("UART write timeout");
      return false;
    }

    const ssize_t n = ::write(
      fd_,
      bytes.data() + written_total,
      bytes.size() - written_total);

    if (n < 0) {
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      set_last_error("UART write failed: " + std::string(std::strerror(errno)));
      return false;
    }

    if (n == 0) {
      set_last_error("UART write returned 0 bytes");
      return false;
    }

    written_total += static_cast<std::size_t>(n);
  }

  return true;
}

bool UartTransport::get_latest_feedback(
  FeedbackPacket & feedback,
  std::chrono::steady_clock::time_point & stamp) const
{
  std::lock_guard<std::mutex> lock(feedback_mutex_);
  if (!have_feedback_) {
    return false;
  }

  feedback = latest_feedback_;
  stamp = latest_feedback_stamp_;
  return true;
}

std::string UartTransport::last_error() const
{
  std::lock_guard<std::mutex> lock(error_mutex_);
  return last_error_;
}

uint32_t UartTransport::crc_error_count() const
{
  return crc_error_count_.load();
}

uint32_t UartTransport::decode_error_count() const
{
  return decode_error_count_.load();
}

uint32_t UartTransport::rx_overflow_count() const
{
  return rx_overflow_count_.load();
}

void UartTransport::rx_loop()
{
  std::array<uint8_t, 512> temp{};

  while (running_) {
    if (fd_ < 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN | POLLERR | POLLHUP | POLLNVAL;

    const int ret = ::poll(&pfd, 1, 100);  // ms
    if (!running_) {
      break;
    }

    if (ret < 0) {
      if (errno == EINTR) {
        continue;
      }
      set_last_error("UART read poll failed: " + std::string(std::strerror(errno)));
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    if (ret == 0) {
      continue;
    }

    if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
      set_last_error("UART poll error/hangup");
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    if ((pfd.revents & POLLIN) == 0) {
      continue;
    }

    while (running_) {
      const ssize_t n = ::read(fd_, temp.data(), temp.size());
      if (n > 0) {
        {
          std::lock_guard<std::mutex> lock(rx_buffer_mutex_);
          rx_buffer_.insert(rx_buffer_.end(), temp.begin(), temp.begin() + n);

          if (rx_buffer_.size() > max_rx_buffer_size_) {
            rx_overflow_count_++;
            const std::size_t keep = std::min<std::size_t>(
              rx_buffer_.size(), FEEDBACK_PACKET_SIZE - 1U);
            std::vector<uint8_t> tail(rx_buffer_.end() - keep, rx_buffer_.end());
            rx_buffer_.swap(tail);
          }

          parse_rx_buffer();
        }
        continue;
      }

      if (n == 0) {
        break;
      }

      if (errno == EINTR) {
        continue;
      }

      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }

      set_last_error("UART read failed: " + std::string(std::strerror(errno)));
      break;
    }
  }
}

void UartTransport::parse_rx_buffer()
{
  // Caller must hold rx_buffer_mutex_.
  while (rx_buffer_.size() >= FEEDBACK_PACKET_SIZE) {
    std::size_t magic_pos = rx_buffer_.size();

    for (std::size_t i = 0; i + 1U < rx_buffer_.size(); ++i) {
      if (read_u16_le_from_buffer(rx_buffer_, i) == FEEDBACK_MAGIC) {
        magic_pos = i;
        break;
      }
    }

    if (magic_pos == rx_buffer_.size()) {
      const uint8_t last = rx_buffer_.back();
      rx_buffer_.clear();
      rx_buffer_.push_back(last);
      return;
    }

    if (magic_pos > 0U) {
      rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + static_cast<std::ptrdiff_t>(magic_pos));
    }

    if (rx_buffer_.size() < FEEDBACK_PACKET_SIZE) {
      return;
    }

    const std::vector<uint8_t> candidate(
      rx_buffer_.begin(),
      rx_buffer_.begin() + static_cast<std::ptrdiff_t>(FEEDBACK_PACKET_SIZE));

    FeedbackPacket packet{};
    const DecodeResult result = decode_feedback_packet(candidate, packet);

    if (result == DecodeResult::OK) {
      {
        std::lock_guard<std::mutex> lock(feedback_mutex_);
        latest_feedback_ = packet;
        latest_feedback_stamp_ = std::chrono::steady_clock::now();
        have_feedback_ = true;
      }

      rx_buffer_.erase(
        rx_buffer_.begin(),
        rx_buffer_.begin() + static_cast<std::ptrdiff_t>(FEEDBACK_PACKET_SIZE));
      continue;
    }

    if (result == DecodeResult::BAD_CRC) {
      crc_error_count_++;
    } else {
      decode_error_count_++;
    }

    // Shift one byte and try to resynchronize on the next possible magic.
    rx_buffer_.erase(rx_buffer_.begin());
  }
}

void UartTransport::set_last_error(const std::string & error)
{
  std::lock_guard<std::mutex> lock(error_mutex_);
  last_error_ = error;
}

}  // namespace actuator_bridge