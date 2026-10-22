#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace reconclave {
struct NmapPort {
  std::uint16_t port = 0;
  std::string protocol, state, service, product, version, method;
};
struct NmapHost {
  std::string address, mac, hostname;
  std::vector<NmapPort> ports;
};
struct NmapResult {
  std::string source, completion, coverage;
  std::uint64_t observed_ms = 0;
  std::vector<NmapHost> hosts;
};
// Bounded XML ingestion; does not resolve names, load DTDs or transmit traffic.
// Incomplete XML is rejected transactionally, with the raw evidence retained.
bool parseNmapXml(const std::string& xml, int exit_status, NmapResult& result, std::string& error);
bool readNmapFile(const std::string& path, int exit_status, NmapResult& result, std::string& error);
}
