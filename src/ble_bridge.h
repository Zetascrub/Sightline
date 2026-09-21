#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace reconclave {

struct BleDevice {
  int index = -1;
  std::string address;
  int rssi_dbm = 0;
  bool uart_service = false;
  std::string name;
};

class BleBridge {
 public:
  ~BleBridge();
  bool open(const char* device = "/dev/ttyS1");
  void close();
  void poll();
  bool scan();
  bool connect(int index);
  bool disconnect();

  bool available() const { return !version_.empty(); }
  bool scanning() const { return scanning_; }
  bool connecting() const { return connecting_; }
  bool connected() const { return connected_; }
  bool meshAdvertising() const { return mesh_advertising_; }
  const std::string& version() const { return version_; }
  const std::string& status() const { return status_; }
  const std::vector<BleDevice>& devices() const { return devices_; }
  std::uint64_t generation() const { return generation_; }

 private:
  bool send(const std::string& command);
  void processLine(const std::string& line);
  int fd_ = -1;
  std::string buffer_;
  std::string version_;
  std::string selected_name_;
  std::string status_{"Opening BLE radio..."};
  std::vector<BleDevice> devices_;
  std::uint64_t generation_ = 0;
  std::uint64_t scan_started_ms_ = 0;
  std::uint64_t connect_started_ms_ = 0;
  bool scanning_ = false;
  bool connecting_ = false;
  bool connected_ = false;
  bool mesh_advertising_ = false;
};

}  // namespace reconclave
