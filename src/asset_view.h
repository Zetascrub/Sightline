#pragma once

#include "host_knowledge.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace reconclave {
enum class AssetFilter { All, Review, Services, Unidentified };
enum class AssetSort { Address, Recent, Review };

inline bool assetNeedsReview(const HostRecord& host) {
  return host.changes > 0 || std::any_of(host.services.begin(), host.services.end(),
      [](const KnownService& service) { return !service.attention.empty(); });
}

inline std::vector<std::uint16_t> assetPorts(const HostRecord& host) {
  std::set<std::uint16_t> ports(host.open_ports.begin(), host.open_ports.end());
  for (const auto& service : host.services) if (service.port && service.protocol == "tcp") ports.insert(service.port);
  return {ports.begin(), ports.end()};
}

inline std::string assetLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

inline bool assetAddressLess(const std::string& a, const std::string& b) {
  // Zero-padded IPv4 keys give natural ordering without DNS or network access.
  const auto key = [](const std::string& address) {
    std::istringstream input(address); std::string part, result;
    while (std::getline(input, part, '.')) {
      if (part.size() > 3 || part.empty() || part.find_first_not_of("0123456789") != std::string::npos)
        return address;
      result += std::string(3 - part.size(), '0') + part;
    }
    return result;
  };
  return key(a) < key(b);
}

inline std::vector<HostRecord> selectAssets(const std::vector<HostRecord>& hosts,
    std::string query, AssetFilter filter, AssetSort sort) {
  query = assetLower(query);
  std::vector<HostRecord> result;
  for (const auto& host : hosts) {
    if (filter == AssetFilter::Review && !assetNeedsReview(host)) continue;
    if (filter == AssetFilter::Services && assetPorts(host).empty() && host.services.empty()) continue;
    if (filter == AssetFilter::Unidentified && !host.services.empty()) continue;
    std::string haystack = host.address + " " + host.mac + " " + host.interface + " " + host.hostname;
    for (auto port : assetPorts(host)) haystack += " " + std::to_string(port);
    for (const auto& service : host.services)
      haystack += " " + std::to_string(service.port) + " " + service.protocol + " " + service.service + " " + service.banner + " " + service.attention;
    if (!query.empty() && assetLower(haystack).find(query) == std::string::npos) continue;
    result.push_back(host);
  }
  std::sort(result.begin(), result.end(), [sort](const HostRecord& a, const HostRecord& b) {
    if (sort == AssetSort::Recent && a.last_seen_ms != b.last_seen_ms) return a.last_seen_ms > b.last_seen_ms;
    if (sort == AssetSort::Review && assetNeedsReview(a) != assetNeedsReview(b)) return assetNeedsReview(a);
    return assetAddressLess(a.address, b.address);
  });
  return result;
}
} // namespace reconclave
