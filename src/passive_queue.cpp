#include "passive_queue.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace reconclave {
namespace {
bool ipv4(const std::string& value, std::uint32_t& address) {
  in_addr parsed{};
  if (inet_pton(AF_INET, value.c_str(), &parsed) != 1) return false;
  address = ntohl(parsed.s_addr);
  return true;
}

bool macAddress(const std::string& mac) {
  if (mac.size() != 17) return false;
  for (std::size_t i = 0; i < mac.size(); ++i) {
    if (i % 3 == 2) { if (mac[i] != ':') return false; }
    else if (!((mac[i] >= '0' && mac[i] <= '9') || (mac[i] >= 'a' && mac[i] <= 'f') ||
               (mac[i] >= 'A' && mac[i] <= 'F'))) return false;
  }
  return true;
}

bool readPassiveFile(const std::string& path, std::vector<ArpNeighbour>& hosts,
                     std::uint64_t& observed_ms, std::string& error) {
  const int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
  struct stat st{};
  if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
      (st.st_mode & 0022) || st.st_size <= 0 || st.st_size > 256 * 1024) {
    if (fd >= 0) close(fd);
    error = "Passive artifact is not a bounded private regular file";
    return false;
  }
  std::string body;
  char buffer[4096]; ssize_t count = 0;
  while ((count = read(fd, buffer, sizeof(buffer))) > 0) {
    body.append(buffer, static_cast<std::size_t>(count));
    if (body.size() > 256 * 1024) break;
  }
  close(fd);
  if (count < 0 || body.size() > 256 * 1024) { error = "Unable to read bounded passive artifact"; return false; }
  json::Value payload;
  if (!json::parse(body, payload, error) || !payload.isObject()) return false;
  const auto* schema = payload.find("schema");
  const auto* scope = payload.find("scope");
  const auto* interface = payload.find("interface");
  const auto* time = payload.find("observed_ms");
  const auto* entries = payload.find("hosts");
  if (!schema || schema->asString() != "sightline-passive-hosts/v1" || !scope || !interface ||
      (interface->asString() != "eth0" && interface->asString() != "wlan0") || !time ||
      !entries || !entries->isArray() || entries->items().size() > 256) {
    error = "Invalid passive observation schema"; return false;
  }
  observed_ms = time->asUInt64();
  if (!observed_ms) { error = "Missing passive observation time"; return false; }
  const auto slash = scope->asString().find('/');
  std::uint32_t network = 0;
  if (slash == std::string::npos || !ipv4(scope->asString().substr(0, slash), network)) {
    error = "Invalid passive scope"; return false;
  }
  const auto prefix_text = scope->asString().substr(slash + 1);
  if (prefix_text.empty() || prefix_text.size() > 2 ||
      prefix_text.find_first_not_of("0123456789") != std::string::npos) {
    error = "Invalid passive prefix"; return false;
  }
  const unsigned prefix = static_cast<unsigned>(std::stoul(prefix_text));
  if (prefix < 24 || prefix > 32) { error = "Passive scope exceeds /24"; return false; }
  const std::uint32_t mask = prefix == 32 ? 0xffffffffu : 0xffffffffu << (32 - prefix);
  if ((network & mask) != network) { error = "Passive scope is not a network address"; return false; }
  std::vector<ArpNeighbour> parsed;
  for (const auto& entry : entries->items()) {
    const auto* address = entry.find("address");
    const auto* mac = entry.find("mac");
    std::uint32_t numeric = 0;
    if (!entry.isObject() || !address || !mac || !ipv4(address->asString(), numeric) ||
        (numeric & mask) != network || !macAddress(mac->asString()) || numeric == 0) {
      error = "Passive host lies outside scope or has invalid identity"; return false;
    }
    parsed.push_back({address->asString(), mac->asString(), interface->asString(), true});
  }
  hosts = std::move(parsed);
  error.clear();
  return true;
}
}

void processPassiveImport(HostKnowledge& knowledge, const std::string& directory) {
  DIR* dir = opendir(directory.c_str());
  if (!dir) return;
  std::string file;
  while (const auto* entry = readdir(dir)) {
    const std::string name = entry->d_name;
    if (name.rfind("passive-", 0) != 0 || name.size() < 20 ||
        name.substr(name.size() - 11) != ".json.ready") continue;
    const auto marker = directory + "/" + name;
    struct stat st{};
    if (lstat(marker.c_str(), &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0022)) continue;
    const auto json_path = marker.substr(0, marker.size() - 6);
    if (access((json_path + ".import.json").c_str(), F_OK) == 0) continue;
    file = json_path; break;
  }
  closedir(dir);
  if (file.empty()) return;
  int marker_status = -1;
  std::ifstream marker(file + ".ready");
  const bool marker_valid = static_cast<bool>(marker >> marker_status) && marker_status == 0;
  std::vector<ArpNeighbour> hosts;
  std::uint64_t observed_ms = 0;
  std::string error;
  bool ok = marker_valid;
  if (ok) ok = readPassiveFile(file, hosts, observed_ms, error);
  else error = "Invalid passive ready marker";
  if (ok) {
    const auto previous = knowledge;
    knowledge.observePassive(hosts, observed_ms, file.substr(file.find_last_of('/') + 1));
    if (!knowledge.save(error)) { knowledge = previous; return; }
  }
  auto receipt = json::Value::makeObject();
  receipt.set("schema", json::Value::makeString("sightline-passive-import/v1"));
  receipt.set("source", json::Value::makeString(file.substr(file.find_last_of('/') + 1)));
  receipt.set("status", json::Value::makeString(ok ? "imported" : "rejected"));
  receipt.set("hosts_imported", json::Value::makeNumber(ok ? hosts.size() : 0));
  receipt.set("error", json::Value::makeString(error));
  const auto dest = file + ".import.json";
  std::ofstream output(dest + ".tmp"); output << json::stringify(receipt) << '\n'; output.close();
  if (output.good()) std::rename((dest + ".tmp").c_str(), dest.c_str());
}
}
