#include "nmap_import.h"
#include "../third_party/tinyxml2/tinyxml2.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <memory>

namespace reconclave {
namespace {
constexpr std::size_t max_bytes = 8 * 1024 * 1024;
std::string attr(const tinyxml2::XMLElement* e, const char* key) {
  const auto* p = e ? e->Attribute(key) : nullptr;
  std::string value = p ? p : "";
  if (value.size() > 4096) value = value.substr(0,4096) + " [truncated]";
  for (auto& c : value) if (static_cast<unsigned char>(c) < 32) c = ' ';
  return value;
}
}
bool parseNmapXml(const std::string& xml, int status, NmapResult& result, std::string& error) {
  if (xml.size() > max_bytes || xml.find('\0') != std::string::npos) { error = "XML exceeds limits or contains NUL"; return false; }
  // TinyXML-2 treats stylesheet processing instructions as declarations and
  // rejects Nmap's normal placement after DOCTYPE. Ignore only this presentation
  // hint in the prolog; never load the stylesheet or modify host/service data.
  std::string input = xml;
  const auto root_start = input.find("<nmaprun");
  std::size_t offset = 0;
  while ((offset = input.find("<?xml-stylesheet ", offset)) != std::string::npos && offset < root_start) {
    const auto end = input.find("?>", offset);
    if (end == std::string::npos || end > root_start) { error = "Invalid stylesheet instruction"; return false; }
    std::fill(input.begin() + offset, input.begin() + end + 2, ' ');
    offset = end + 2;
  }
  tinyxml2::XMLDocument doc;
  if (doc.Parse(input.data(), input.size()) != tinyxml2::XML_SUCCESS) {
    error = "Incomplete or invalid Nmap XML; raw evidence retained"; return false;
  }
  const auto* root = doc.RootElement();
  if (!root || std::string(root->Name()) != "nmaprun" || root->NextSiblingElement()) {
    error = "Expected one nmaprun document"; return false;
  }
  NmapResult parsed;
  const auto* stats = root->FirstChildElement("runstats");
  const auto* finished = stats ? stats->FirstChildElement("finished") : nullptr;
  parsed.completion = status == 130 ? "cancelled" : status == 124 ? "timed-out" :
      status == 0 && attr(finished, "exit") == "success" ? "complete" : "partial-or-failed";
  std::uint64_t seconds = 0;
  if (finished) finished->QueryUnsigned64Attribute("time", &seconds);
  if (!seconds) root->QueryUnsigned64Attribute("start", &seconds);
  if (!seconds || seconds > UINT64_MAX / 1000) { error = "Missing or invalid scan time"; return false; }
  parsed.observed_ms = seconds * 1000;
  for (auto* info = root->FirstChildElement("scaninfo"); info; info = info->NextSiblingElement("scaninfo")) {
    if (parsed.coverage.size() > 12000) { error = "Excessive coverage metadata"; return false; }
    if (!parsed.coverage.empty()) parsed.coverage += "; ";
    parsed.coverage += attr(info, "protocol") + " " + attr(info, "numservices") + " ports [" + attr(info, "services") + "]";
  }
  if (parsed.coverage.empty()) parsed.coverage = "Host discovery only; no port coverage";
  parsed.coverage += "; per-host completion not guaranteed; historical open observations retained";
  std::size_t port_count = 0;
  for (auto* host = root->FirstChildElement("host"); host; host = host->NextSiblingElement("host")) {
    if (parsed.hosts.size() >= 4096) { error = "Too many hosts"; return false; }
    if (attr(host->FirstChildElement("status"), "state") != "up") continue;
    NmapHost h;
    for (auto* address = host->FirstChildElement("address"); address; address = address->NextSiblingElement("address")) {
      if (attr(address,"addrtype") == "ipv4") h.address = attr(address,"addr");
      if (attr(address,"addrtype") == "mac") h.mac = attr(address,"addr");
    }
    in_addr numeric{};
    if (h.address.empty()) continue; // IPv6 is not silently put in the IPv4 inventory.
    if (inet_pton(AF_INET,h.address.c_str(), &numeric) != 1) { error = "Invalid IPv4 address"; return false; }
    const auto* names = host->FirstChildElement("hostnames");
    if (names) h.hostname = attr(names->FirstChildElement("hostname"), "name");
    const auto* ports = host->FirstChildElement("ports");
    if (ports) for (auto* p = ports->FirstChildElement("port"); p; p = p->NextSiblingElement("port")) {
      if (++port_count > 65536) { error = "Too many port observations"; return false; }
      unsigned number = 0;
      if (p->QueryUnsignedAttribute("portid", &number) != tinyxml2::XML_SUCCESS || !number || number > 65535) {
        error = "Invalid port number"; return false;
      }
      NmapPort port;
      port.port = static_cast<std::uint16_t>(number);
      port.protocol = attr(p,"protocol");
      if (port.protocol != "tcp" && port.protocol != "udp") continue;
      port.state = attr(p->FirstChildElement("state"),"state");
      const auto* service = p->FirstChildElement("service");
      port.service = attr(service,"name"); port.product = attr(service,"product");
      port.version = attr(service,"version"); port.method = attr(service,"method");
      h.ports.push_back(std::move(port));
    }
    parsed.hosts.push_back(std::move(h));
  }
  result = std::move(parsed); error.clear(); return true;
}
bool readNmapFile(const std::string& path, int status, NmapResult& result, std::string& error) {
  const int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0) { error = "Unable to open XML evidence"; return false; }
  struct stat st{};
  if (fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size < 0 || static_cast<std::uint64_t>(st.st_size) > max_bytes) {
    close(fd); error = "XML evidence is not a bounded regular file"; return false;
  }
  std::string xml;
  char buffer[8192]; ssize_t n;
  while ((n = read(fd,buffer,sizeof(buffer))) > 0) {
    xml.append(buffer,static_cast<std::size_t>(n));
    if (xml.size() > max_bytes) break;
  }
  close(fd);
  if (n < 0) { error = "Unable to read XML evidence"; return false; }
  if (!parseNmapXml(xml,status,result,error)) return false;
  result.source = path.substr(path.find_last_of('/') + 1);
  return true;
}
}
