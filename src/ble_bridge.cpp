#include "ble_bridge.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>

namespace reconclave {
namespace {

std::uint64_t monotonicMs() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

}  // namespace

BleBridge::~BleBridge() { close(); }

bool BleBridge::open(const char* device) {
  close();
  fd_ = ::open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd_ < 0) {
    status_ = "BLE UART unavailable";
    ++generation_;
    return false;
  }
  termios tty{};
  if (tcgetattr(fd_, &tty) != 0) {
    close();
    status_ = "Unable to configure BLE UART";
    ++generation_;
    return false;
  }
  cfmakeraw(&tty);
  cfsetispeed(&tty, B115200);
  cfsetospeed(&tty, B115200);
  tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8 | CLOCAL | CREAD;
  tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
  if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
    close();
    status_ = "Unable to configure BLE UART";
    ++generation_;
    return false;
  }
  tcflush(fd_, TCIFLUSH);
  status_ = "Checking nRF52840...";
  ++generation_;
  if (send("AT+VER?") && send("AT+STATUS?")) return true;
  close();
  status_ = "Unable to query BLE radio";
  ++generation_;
  return false;
}

void BleBridge::close() {
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
  buffer_.clear();
  version_.clear();
  selected_name_.clear();
  devices_.clear();
  scanning_ = connecting_ = connected_ = mesh_advertising_ = false;
}

bool BleBridge::send(const std::string& command) {
  if (fd_ < 0) return false;
  const std::string wire = command + '\n';
  return write(fd_, wire.data(), wire.size()) == static_cast<ssize_t>(wire.size());
}

bool BleBridge::scan() {
  if (!available() || scanning_ || connecting_ || connected_) return false;
  devices_.clear();
  scanning_ = true;
  scan_started_ms_ = monotonicMs();
  status_ = "Scanning for BLE devices...";
  ++generation_;
  if (send("AT+SCAN=5")) return true;
  scanning_ = false;
  status_ = "Unable to start BLE scan";
  ++generation_;
  return false;
}

bool BleBridge::connect(int index) {
  if (!available() || scanning_ || connecting_ || connected_) return false;
  bool found = false;
  for (const auto& device : devices_) {
    if (device.index == index) {
      found = true;
      selected_name_ = device.name.empty() || device.name == "Unknown"
          ? device.address : device.name;
      break;
    }
  }
  if (!found) return false;
  connecting_ = true;
  connect_started_ms_ = monotonicMs();
  status_ = "Connecting to " + selected_name_ + "...";
  ++generation_;
  if (send("AT+CONN=" + std::to_string(index))) return true;
  connecting_ = false;
  status_ = "Unable to send BLE connect command";
  ++generation_;
  return false;
}

bool BleBridge::disconnect() {
  if (!connected_ && !connecting_) return false;
  if (!send("AT+DISC")) return false;
  status_ = "Disconnecting BLE device...";
  ++generation_;
  return true;
}

void BleBridge::processLine(const std::string& line) {
  if (line.rfind("+VER:", 0) == 0) {
    version_ = line.substr(5);
    status_ = "BLE ready";
    ++generation_;
  } else if (line.rfind("+STATUS:", 0) == 0) {
    connected_ = line.rfind("+STATUS:CONNECTED", 0) == 0;
    const auto pos = line.find("MESH_ADV=");
    if (pos != std::string::npos) mesh_advertising_ = line[pos + 9] == '1';
    ++generation_;
  } else if (line.rfind("+SCAN:DONE", 0) == 0) {
    scanning_ = false;
    status_ = "Found " + std::to_string(devices_.size()) + " BLE devices";
    ++generation_;
  } else if (line.rfind("+SCAN:", 0) == 0) {
    const std::string body = line.substr(6);
    const auto first = body.find(',');
    const auto second = first == std::string::npos ? first : body.find(',', first + 1);
    const auto third = second == std::string::npos ? second : body.find(',', second + 1);
    const auto fourth = third == std::string::npos ? third : body.find(',', third + 1);
    if (fourth == std::string::npos) return;
    BleDevice device;
    device.index = std::atoi(body.substr(0, first).c_str());
    device.address = body.substr(first + 1, second - first - 1);
    device.rssi_dbm = std::atoi(body.substr(second + 1, third - second - 1).c_str());
    device.uart_service = body.substr(third + 1, fourth - third - 1) == "1";
    device.name = body.substr(fourth + 1);
    for (auto& existing : devices_) {
      if (existing.index == device.index) {
        existing = device;
        ++generation_;
        return;
      }
    }
    if (devices_.size() < 40) {
      devices_.push_back(device);
      ++generation_;
    }
  } else if (line.rfind("+CONNECTED:", 0) == 0) {
    connecting_ = false;
    connected_ = true;
    status_ = "Connected to " + selected_name_;
    ++generation_;
  } else if (line.rfind("+DISCONNECTED:", 0) == 0) {
    connecting_ = connected_ = false;
    status_ = "BLE device disconnected";
    ++generation_;
  } else if (line.rfind("ERR:", 0) == 0) {
    scanning_ = connecting_ = false;
    status_ = "BLE error: " + line.substr(4);
    ++generation_;
  }
}

void BleBridge::poll() {
  if (fd_ < 0) return;
  char chunk[512];
  ssize_t count;
  while ((count = read(fd_, chunk, sizeof(chunk))) > 0)
    buffer_.append(chunk, static_cast<std::size_t>(count));
  std::size_t newline;
  while ((newline = buffer_.find('\n')) != std::string::npos) {
    std::string line = buffer_.substr(0, newline);
    buffer_.erase(0, newline + 1);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) processLine(line);
  }
  if (buffer_.size() > 4096) buffer_.clear();
  if (scanning_ && monotonicMs() - scan_started_ms_ > 12000) {
    scanning_ = false;
    status_ = "BLE scan timed out";
    ++generation_;
  }
  if (connecting_ && monotonicMs() - connect_started_ms_ > 15000) {
    connecting_ = false;
    status_ = "BLE connection timed out";
    ++generation_;
  }
}

}  // namespace reconclave
