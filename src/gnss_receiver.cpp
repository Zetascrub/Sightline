#include "gnss_receiver.h"

#include <gpiod.h>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <poll.h>

#include <cstdlib>
#include <cstdio>
#include <sstream>
#include <vector>

namespace reconclave {
namespace {

// The keyboard base's nRF9151 modem has a dedicated enable line - without
// driving it, /dev/ttyS3 is completely silent (every AT command times out
// with zero bytes received). Confirmed on real hardware: this was
// previously misdiagnosed as "the shipped modem firmware doesn't support
// the required GNSS configuration" (see the now-corrected comment at
// ui_app.cpp's kGnssAvailable) - identical AT commands succeed immediately
// once this line is driven high; the modem was simply never powered on.
// Global GPIO2 is gpiochip0 line 2 (line < 32, so chip0 - see
// tca8418_keyboard.h's numbering note for the two-bank K230 GPIO layout
// this project already relies on elsewhere).
constexpr const char* kEnableChipPath = "/dev/gpiochip0";
constexpr unsigned kEnableOffset = 2;

bool setEnableLine(gpiod_chip*& chip, gpiod_line_request*& request, bool enabled) {
  if (request == nullptr) {
    chip = gpiod_chip_open(kEnableChipPath);
    if (chip == nullptr) return false;
    gpiod_line_settings* settings = gpiod_line_settings_new();
    gpiod_line_config* lines = gpiod_line_config_new();
    gpiod_request_config* request_config = gpiod_request_config_new();
    if (settings == nullptr || lines == nullptr || request_config == nullptr) {
      if (settings != nullptr) gpiod_line_settings_free(settings);
      if (lines != nullptr) gpiod_line_config_free(lines);
      if (request_config != nullptr) gpiod_request_config_free(request_config);
      gpiod_chip_close(chip);
      chip = nullptr;
      return false;
    }
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_output_value(
        settings, enabled ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
    gpiod_line_config_add_line_settings(lines, &kEnableOffset, 1, settings);
    gpiod_request_config_set_consumer(request_config, "reconclave-gnss-enable");
    request = gpiod_chip_request_lines(chip, request_config, lines);
    gpiod_line_settings_free(settings);
    gpiod_line_config_free(lines);
    gpiod_request_config_free(request_config);
    if (request == nullptr) {
      gpiod_chip_close(chip);
      chip = nullptr;
      return false;
    }
    return true;
  }
  return gpiod_line_request_set_value(
      request, kEnableOffset,
      enabled ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE) == 0;
}

void releaseEnableLine(gpiod_chip*& chip, gpiod_line_request*& request) {
  if (request != nullptr) {
    gpiod_line_request_set_value(request, kEnableOffset, GPIOD_LINE_VALUE_INACTIVE);
    gpiod_line_request_release(request);
    request = nullptr;
  }
  if (chip != nullptr) {
    gpiod_chip_close(chip);
    chip = nullptr;
  }
}

std::vector<std::string> split(const std::string& value, char delimiter) {
  std::vector<std::string> fields;
  std::istringstream input(value);
  std::string field;
  while (std::getline(input, field, delimiter)) fields.push_back(field);
  return fields;
}

}  // namespace

GnssReceiver::~GnssReceiver() { stop(); }

bool GnssReceiver::sendCommand(const char* command) {
  std::string wire = std::string(command) + "\r\n";
  return write(fd_, wire.data(), wire.size()) == static_cast<ssize_t>(wire.size());
}

void GnssReceiver::processLine(const std::string& raw) {
  std::string line = raw;
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
  if (line.empty()) return;
  last_response_ = line;
  // "#XGPS: " (not "#XGPSPOS:", which this firmware never actually sends -
  // confirmed against Nordic's own documented format and this exact
  // modem's real output) is reused for both the synchronous command
  // acknowledgment (e.g. "#XGPS: 1,1", 2 fields) and the asynchronous fix
  // notification ("#XGPS: <lat>,<lon>,<alt>,<accuracy>,<speed>,<heading>,
  // <datetime>", 7 fields) - the field-count check below is what tells
  // them apart, not the prefix. #XGNSS (the modern API) is unverified
  // against real hardware, since this modem's firmware doesn't support it
  // at all (AT#XGNSS=? returns ERROR) - left as its best-documented guess
  // rather than changed without a way to confirm it.
  const char* position_prefix = line.find("#XGNSSPOS:") != std::string::npos
      ? "#XGNSSPOS:" : line.find("#XGPS:") != std::string::npos ? "#XGPS:" : nullptr;
  const auto fix_position = position_prefix == nullptr ? std::string::npos : line.find(position_prefix);
  if (fix_position != std::string::npos) {
    const auto fields = split(line.substr(fix_position + std::char_traits<char>::length(position_prefix)), ',');
    if (fields.size() >= 7) {
      char* end = nullptr;
      const double latitude = std::strtod(fields[0].c_str(), &end);
      const bool latitude_valid = end != fields[0].c_str();
      const double longitude = std::strtod(fields[1].c_str(), &end);
      const bool longitude_valid = end != fields[1].c_str();
      if (latitude_valid && longitude_valid) {
        fix_.valid = true;
        fix_.latitude = latitude;
        fix_.longitude = longitude;
        fix_.altitude_m = std::strtod(fields[2].c_str(), nullptr);
        fix_.speed_knots = std::strtod(fields[4].c_str(), nullptr) * 1.943844;
        fix_.utc = fields[6];
        while (!fix_.utc.empty() && (fix_.utc.front() == ' ' || fix_.utc.front() == '"'))
          fix_.utc.erase(0, 1);
        while (!fix_.utc.empty() && fix_.utc.back() == '"') fix_.utc.pop_back();
        fix_.last_sentence = line;
        status_ = "GNSS fix acquired";
      }
    }
    return;
  }
  constexpr char prefix[] = "#XGNSSNMEA:";
  const auto position = line.find(prefix);
  if (position == std::string::npos) return;
  auto sentence = line.substr(position + sizeof(prefix) - 1);
  while (!sentence.empty() && sentence.front() == ' ') sentence.erase(0, 1);
  ++nmea_count_;
  if (parseNmeaSentence(sentence, fix_))
    status_ = fix_.valid ? "GNSS fix acquired" : "GNSS active; acquiring fix";
}

bool GnssReceiver::exchange(const char* command, int timeout_ms, std::string& response) {
  response.clear();
  if (!sendCommand(command)) {
    std::fprintf(stderr, "reconclave gnss: %s -> write failed\n", command);
    return false;
  }
  const int slices = timeout_ms / 50 + 1;
  std::string reply;
  for (int i = 0; i < slices; ++i) {
    pollfd descriptor{fd_, POLLIN, 0};
    if (::poll(&descriptor, 1, 50) <= 0) continue;
    char chunk[256];
    const ssize_t count = read(fd_, chunk, sizeof(chunk));
    if (count <= 0) continue;
    reply.append(chunk, static_cast<std::size_t>(count));
    std::size_t newline;
    while ((newline = reply.find('\n')) != std::string::npos) {
      std::string line = reply.substr(0, newline);
      reply.erase(0, newline + 1);
      while (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty() || line == command) continue;
      processLine(line);
      if (!response.empty()) response += " | ";
      response += line;
      if (line == "OK" || line == "ERROR" || line.find("+CME ERROR") != std::string::npos) {
        std::fprintf(stderr, "reconclave gnss: %s -> %s\n", command, line.c_str());
        // A USB/UART read can contain an unsolicited line after the command's
        // terminal response. Keep its partial bytes for poll() instead of
        // dropping the first half of a #XGPS notification.
        buffer_.append(reply);
        return line == "OK";
      }
    }
  }
  if (!reply.empty()) {
    processLine(reply);
    if (!response.empty()) response += " | ";
    response += reply;
  }
  std::fprintf(stderr, "reconclave gnss: %s -> timeout (%zu bytes)\n", command, response.size());
  return false;
}

bool GnssReceiver::start(const std::string& device, std::string& error) {
  stop();
  last_response_.clear();
  modem_info_.clear();
  api_info_.clear();
  if (!setEnableLine(enable_chip_, enable_request_, true)) {
    error = "unable to drive the nRF9151 enable line (gpiochip0 line 2)";
    status_ = "GNSS error: " + error;
    return false;
  }
  // The modem needs time to boot its AT command interface after being
  // enabled - confirmed responsive within ~2s in manual testing; this is
  // cheap insurance before the first exchange() (which itself already
  // retries for 1500ms) rather than a tightly-measured minimum.
  usleep(300000);
  fd_ = open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd_ < 0) {
    error = "unable to open modem UART";
    stop(); status_ = "GNSS error: " + error; return false;
  }
  termios tty{};
  if (tcgetattr(fd_, &tty) != 0) {
    error = "unable to configure modem UART";
    stop(); status_ = "GNSS error: " + error; return false;
  }
  cfmakeraw(&tty);
  cfsetispeed(&tty, B115200);
  cfsetospeed(&tty, B115200);
  tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8 | CLOCAL | CREAD;
  tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
  if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
    error = "unable to apply modem UART settings";
    stop(); status_ = "GNSS error: " + error; return false;
  }
  tcflush(fd_, TCIOFLUSH);
  fix_ = GnssFix{};
  nmea_count_ = 0;
  std::string response;
  bool modem_ready = false;
  for (int attempt = 0; attempt < 5 && !modem_ready; ++attempt)
    modem_ready = exchange("AT", 1200, response);
  if (!modem_ready) {
    error = "nRF9151 did not answer AT" + (response.empty() ? std::string() : ": " + response);
    const std::string diagnosis = error;
    stop(); status_ = "GNSS error: " + diagnosis; return false;
  }
  std::string identity;
  if (exchange("ATI", 2000, identity)) modem_info_ = identity;
  std::string revision;
  if (exchange("AT+CGMR", 2000, revision)) {
    if (!modem_info_.empty()) modem_info_ += " | ";
    modem_info_ += revision;
  }
  std::string modern_test, legacy_test;
  const bool modern_supported = exchange("AT#XGNSS=?", 2000, modern_test);
  const bool legacy_supported = exchange("AT#XGPS=?", 2000, legacy_test);
  api_info_ = "XGNSS=" + modern_test + " | XGPS=" + legacy_test;
  if (!modern_supported && !legacy_supported) {
    error = "installed nRF9151 application has no XGNSS/XGPS service";
    const std::string diagnosis = error;
    stop(); status_ = "GNSS unavailable: " + diagnosis; return false;
  }
  // Recover to offline first, then use the exact smoke-test order required by
  // LilyGO's K230 Serial LTE Modem build. XNMEA must be configured before
  // CFUN=31; that custom command is rejected once this firmware is online.
  exchange("AT#XGNSS=0", 1500, response);
  exchange("AT#XNMEA=0", 1500, response);
  const char* rejected_command = nullptr;
  if (!exchange("AT+CFUN=0", 6000, response))
    rejected_command = "AT+CFUN=0";
  const bool nmea_enabled = exchange("AT#XNMEA=1", 3000, response);
  if (!exchange("AT%XSYSTEMMODE=0,0,1,0", 3000, response))
    rejected_command = "AT%XSYSTEMMODE=0,0,1,0";
  else if (!exchange("AT+CFUN=31", 9000, response))
    rejected_command = "AT+CFUN=31";
  if (rejected_command != nullptr) {
    error = std::string("GNSS setup rejected ") + rejected_command + ": " + response;
    const std::string diagnosis = error;
    stop(); status_ = "GNSS error: " + diagnosis; return false;
  }
  bool legacy_gps = false;
  std::string legacy_failure;
  if ((!modern_supported || !exchange("AT#XGNSS=1,0,1", 3000, response)) &&
      (!modern_supported || !exchange("AT#XGNSS=1,0,0,0", 3000, response))) {
    std::string state;
    const bool modern_active = modern_supported && exchange("AT#XGNSS?", 2000, state) &&
        state.find("#XGNSS: 1") != std::string::npos;
    if (!modern_active) {
      // Nordic Serial LTE Modem releases through NCS 3.1 called this API XGPS.
      legacy_gps = legacy_supported && exchange("AT#XGPS=1,0,0,0", 3000, response);
      legacy_failure = response;
      if (legacy_supported && !legacy_gps) {
        std::string gps_state;
        legacy_gps = exchange("AT#XGPS?", 2000, gps_state) &&
            gps_state.find("#XGPS: 1,") != std::string::npos;
        if (!legacy_gps) {
          usleep(300000);
          legacy_gps = exchange("AT#XGPS=1,0,0,0", 3000, response);
          if (!legacy_gps) legacy_failure = response;
        }
      }
      if (legacy_supported && !legacy_gps) {
        // Some nRF9151/SLM combinations advertise the legacy API but reject
        // its standalone CFUN=31 path. Retry in the documented concurrent
        // LTE-M + GNSS operating mode before declaring the service unusable.
        std::string recovery;
        if (exchange("AT+CFUN=0", 6000, recovery) &&
            exchange("AT%XSYSTEMMODE=1,0,1,0", 3000, recovery) &&
            exchange("AT+CFUN=1", 9000, recovery)) {
          legacy_gps = exchange("AT#XGPS=1,0,0,0", 3000, response);
          if (!legacy_gps) legacy_failure = response;
        }
      }
    }
    if (!modern_active && !legacy_gps) {
      error = "AT#XGPS=1,0,0,0 rejected: " + legacy_failure;
      const std::string diagnosis = error;
      stop(); status_ = "GNSS error: " + diagnosis; return false;
    }
  }
  status_ = nmea_enabled ? "GNSS started; waiting for satellite fix" :
      legacy_gps ? "Legacy GPS active; waiting for position report" :
      "GNSS active; waiting for position report";
  error.clear();
  return true;
}

void GnssReceiver::stop() {
  if (fd_ >= 0) close(fd_);
  fd_ = -1;
  buffer_.clear();
  releaseEnableLine(enable_chip_, enable_request_);
  fix_ = GnssFix{};
  status_ = "GNSS idle";
}

void GnssReceiver::poll() {
  if (fd_ < 0) return;
  char chunk[512];
  ssize_t count = 0;
  while ((count = read(fd_, chunk, sizeof(chunk))) > 0) buffer_.append(chunk, static_cast<std::size_t>(count));
  std::size_t newline = 0;
  while ((newline = buffer_.find('\n')) != std::string::npos) {
    std::string line = buffer_.substr(0, newline);
    buffer_.erase(0, newline + 1);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    processLine(line);
  }
  if (buffer_.size() > 4096) buffer_.erase(0, buffer_.size() - 4096);
}

}  // namespace reconclave
