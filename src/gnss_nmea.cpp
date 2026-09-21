#include "gnss_nmea.h"

#include <cmath>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace reconclave {
namespace {

std::vector<std::string> split(const std::string& value, char delimiter) {
  std::vector<std::string> fields;
  std::istringstream input(value);
  std::string field;
  while (std::getline(input, field, delimiter)) fields.push_back(field);
  return fields;
}

bool checksumValid(const std::string& value) {
  if (value.empty() || value[0] != '$') return false;
  const auto star = value.find('*');
  if (star == std::string::npos || star + 2 >= value.size()) return false;
  unsigned checksum = 0;
  for (std::size_t i = 1; i < star; ++i) checksum ^= static_cast<unsigned char>(value[i]);
  char* end = nullptr;
  const unsigned expected = static_cast<unsigned>(std::strtoul(value.substr(star + 1, 2).c_str(), &end, 16));
  return end != nullptr && *end == '\0' && checksum == expected;
}

bool coordinate(const std::string& raw, const std::string& hemisphere, double& out) {
  if (raw.empty() || hemisphere.empty()) return false;
  char* end = nullptr;
  const double packed = std::strtod(raw.c_str(), &end);
  if (end == raw.c_str() || *end != '\0') return false;
  const double degrees = std::floor(packed / 100.0);
  out = degrees + (packed - degrees * 100.0) / 60.0;
  if (hemisphere == "S" || hemisphere == "W") out = -out;
  return hemisphere == "N" || hemisphere == "S" || hemisphere == "E" || hemisphere == "W";
}

}  // namespace

bool parseNmeaSentence(const std::string& sentence, GnssFix& fix) {
  if (!checksumValid(sentence)) return false;
  const auto star = sentence.find('*');
  const auto fields = split(sentence.substr(1, star - 1), ',');
  if (fields.empty()) return true;
  const std::string type = fields[0].size() >= 3 ? fields[0].substr(fields[0].size() - 3) : fields[0];
  if (type == "GGA" && fields.size() >= 10) {
    double latitude = 0.0, longitude = 0.0;
    const bool positioned = coordinate(fields[2], fields[3], latitude) &&
                            coordinate(fields[4], fields[5], longitude);
    fix.valid = positioned && std::atoi(fields[6].c_str()) > 0;
    if (positioned) { fix.latitude = latitude; fix.longitude = longitude; }
    fix.utc = fields[1];
    fix.satellites = std::atoi(fields[7].c_str());
    fix.altitude_m = std::strtod(fields[9].c_str(), nullptr);
    fix.last_sentence = sentence;
  } else if (type == "RMC" && fields.size() >= 8) {
    double latitude = 0.0, longitude = 0.0;
    const bool positioned = coordinate(fields[3], fields[4], latitude) &&
                            coordinate(fields[5], fields[6], longitude);
    fix.valid = fields[2] == "A" && positioned;
    if (positioned) { fix.latitude = latitude; fix.longitude = longitude; }
    fix.utc = fields[1];
    fix.speed_knots = std::strtod(fields[7].c_str(), nullptr);
    fix.last_sentence = sentence;
  }
  return true;
}

}  // namespace reconclave
