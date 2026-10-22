#include "host_knowledge.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>

namespace reconclave {
namespace {
std::string normalMac(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}
}


HostKnowledge::HostKnowledge(std::string path) : path_(std::move(path)) {}

HostRecord& HostKnowledge::touch(const std::string& address, std::uint64_t now_ms) {
  const auto inserted_result = hosts_.emplace(address, HostRecord{});
  const auto position = inserted_result.first;
  const bool inserted = inserted_result.second;
  HostRecord& host = position->second;
  if (inserted) {
    host.address = address;
    host.first_seen_ms = now_ms;
  }
  host.last_seen_ms = std::max(host.last_seen_ms, now_ms);
  ++host.observations;
  return host;
}

unsigned HostKnowledge::observeArp(const std::vector<ArpNeighbour>& neighbours,
                                   std::uint64_t now_ms) {
  unsigned changes = 0;
  for (const auto& neighbour : neighbours) {
    if (!neighbour.complete || neighbour.address.empty()) continue;
    HostRecord& host = touch(neighbour.address, now_ms);
    bool changed = false;
    const auto mac = normalMac(neighbour.mac);
    if (!mac.empty() && normalMac(host.mac) != mac) {
      changed = !host.mac.empty();
      host.mac = mac;
    }
    if (!neighbour.interface.empty() && host.interface != neighbour.interface) {
      changed = changed || !host.interface.empty();
      host.interface = neighbour.interface;
    }
    if (changed) {
      ++host.changes;
      ++changes;
    }
  }
  return changes;
}

unsigned HostKnowledge::observeDiscovery(const std::vector<DiscoveredHost>& discovered,
                                         std::uint64_t now_ms) {
  unsigned changes = 0;
  for (const auto& observation : discovered) {
    if (observation.address.empty()) continue;
    HostRecord& host = touch(observation.address, now_ms);
    auto ports = observation.open_ports;
    std::sort(ports.begin(), ports.end());
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
    if (!host.open_ports.empty() && host.open_ports != ports) {
      ++host.changes;
      ++changes;
    }
    host.open_ports = std::move(ports);
  }
  return changes;
}

unsigned HostKnowledge::observeService(const ServiceObservation& observation,
                                       std::uint64_t now_ms) {
  if (!observation.connected || observation.address.empty() || observation.port == 0) return 0;
  HostRecord& host = touch(observation.address, now_ms);
  auto existing = std::find_if(host.services.begin(), host.services.end(), [&](const KnownService& service) {
    return service.port == observation.port && service.protocol == "tcp";
  });
  if (existing == host.services.end()) {
    host.services.push_back({observation.port, observation.service, observation.banner,
                             observation.attention, now_ms});
    return 0;
  }
  const bool changed = existing->service != observation.service ||
                       existing->banner != observation.banner ||
                       existing->attention != observation.attention;
  existing->service = observation.service;
  existing->banner = observation.banner;
  existing->attention = observation.attention;
  existing->last_seen_ms = now_ms;
  existing->source.clear(); existing->method.clear();
  if (changed) ++host.changes;
  return changed ? 1 : 0;
}

unsigned HostKnowledge::observeNmap(const NmapResult& result) {
  unsigned imported = 0;
  for (const auto& observation : result.hosts) {
    auto& host = hosts_[observation.address];
    if (host.scan_source == result.source && host.scan_observed_ms == result.observed_ms) continue;
    host.address = observation.address;
    if (!host.first_seen_ms || result.observed_ms < host.first_seen_ms) host.first_seen_ms = result.observed_ms;
    host.last_seen_ms = std::max(host.last_seen_ms, result.observed_ms);
    ++host.observations; ++imported;
    if (result.observed_ms >= host.scan_observed_ms) {
      if (!observation.mac.empty()) host.mac = normalMac(observation.mac);
      if (!observation.hostname.empty()) host.hostname = observation.hostname;
      host.scan_source = result.source; host.scan_completion = result.completion;
      host.scan_coverage = result.coverage; host.scan_observed_ms = result.observed_ms;
    }
    for (const auto& port : observation.ports) {
      if (port.state != "open") continue;
      if (port.protocol == "tcp" && std::find(host.open_ports.begin(), host.open_ports.end(), port.port) == host.open_ports.end())
        host.open_ports.push_back(port.port);
      auto it = std::find_if(host.services.begin(), host.services.end(), [&](const auto& service) {
        return service.port == port.port && service.protocol == port.protocol;
      });
      if (it != host.services.end() && it->last_seen_ms > result.observed_ms) continue;
      if (it != host.services.end() && port.method != "probed" && !it->banner.empty()) continue;
      KnownService service;
      service.port = port.port; service.protocol = port.protocol;
      service.service = port.service.empty() ? "unknown" : port.service;
      service.banner = port.product + (port.version.empty() ? "" : " " + port.version);
      service.source = result.source; service.method = port.method;
      service.last_seen_ms = result.observed_ms;
      // An open port/version guess is not automatically a security finding.
      if (it == host.services.end()) host.services.push_back(std::move(service));
      else {
        service.attention = it->attention;
        *it = std::move(service);
      }
    }
    std::sort(host.open_ports.begin(), host.open_ports.end());
  }
  return imported;
}

unsigned HostKnowledge::observePassive(const std::vector<ArpNeighbour>& observations,
                                       std::uint64_t now_ms, const std::string& source) {
  unsigned count = 0;
  for (const auto& observation : observations) {
    if (!observation.complete || observation.address.empty()) continue;
    auto& host = touch(observation.address, now_ms);
    // A passive MAC/IP claim is useful evidence, not enough to overwrite a
    // conflicting identity or create an asset-change finding by itself.
    if (host.mac.empty()) host.mac = normalMac(observation.mac);
    if (host.interface.empty()) host.interface = observation.interface;
    if (now_ms >= host.passive_observed_ms) {
      host.passive_source = source;
      host.passive_observed_ms = now_ms;
    }
    ++count;
  }
  return count;
}

std::vector<HostRecord> HostKnowledge::snapshot() const {
  std::vector<HostRecord> result;
  result.reserve(hosts_.size());
  for (const auto& entry : hosts_) {
    result.push_back(entry.second);
  }
  return result;
}

json::Value HostKnowledge::toJson() const {
  json::Value result = json::Value::makeObject();
  result.set("schema", json::Value::makeString("sightline-assets/v1"));
  result.set("scope", json::Value::makeString("device-history"));
  result.set("coverage", json::Value::makeString("partial-observations"));
  json::Value entries = json::Value::makeArray();
  std::size_t attention_count = 0;
  for (const auto& map_entry : hosts_) {
    const HostRecord& host = map_entry.second;
    json::Value entry = json::Value::makeObject();
    entry.set("address", json::Value::makeString(host.address));
    entry.set("mac", json::Value::makeString(host.mac));
    entry.set("interface", json::Value::makeString(host.interface));
    entry.set("hostname", json::Value::makeString(host.hostname));
    entry.set("scan_source", json::Value::makeString(host.scan_source));
    entry.set("scan_completion", json::Value::makeString(host.scan_completion));
    entry.set("scan_coverage", json::Value::makeString(host.scan_coverage));
    entry.set("scan_observed_ms", json::Value::makeNumber(static_cast<double>(host.scan_observed_ms)));
    entry.set("passive_source", json::Value::makeString(host.passive_source));
    entry.set("passive_observed_ms", json::Value::makeNumber(static_cast<double>(host.passive_observed_ms)));
    json::Value ports = json::Value::makeArray();
    for (const auto port : host.open_ports) ports.push_back(json::Value::makeNumber(port));
    entry.set("open_ports", ports);
    json::Value services = json::Value::makeArray();
    for (const auto& service : host.services) {
      json::Value item = json::Value::makeObject();
      item.set("port", json::Value::makeNumber(service.port));
      item.set("service", json::Value::makeString(service.service));
      item.set("transport", json::Value::makeString(service.protocol));
      item.set("source", json::Value::makeString(service.source));
      item.set("method", json::Value::makeString(service.method));
      item.set("label_basis", json::Value::makeString((service.method == "table" || (service.method.empty() && service.banner.empty())) ? "port-inference" : "response-classification"));
      item.set("validation", json::Value::makeString("not-validated"));
      item.set("banner", json::Value::makeString(service.banner));
      item.set("attention", json::Value::makeString(service.attention));
      item.set("last_seen_ms", json::Value::makeNumber(static_cast<double>(service.last_seen_ms)));
      services.push_back(item);
      if (!service.attention.empty()) ++attention_count;
    }
    entry.set("services", services);
    entry.set("first_seen_ms", json::Value::makeNumber(static_cast<double>(host.first_seen_ms)));
    entry.set("last_seen_ms", json::Value::makeNumber(static_cast<double>(host.last_seen_ms)));
    entry.set("observations", json::Value::makeNumber(static_cast<double>(host.observations)));
    entry.set("changes", json::Value::makeNumber(static_cast<double>(host.changes)));
    entries.push_back(entry);
  }
  result.set("count", json::Value::makeNumber(static_cast<double>(hosts_.size())));
  result.set("attention_count", json::Value::makeNumber(static_cast<double>(attention_count)));
  result.set("hosts", entries);
  return result;
}

std::size_t HostKnowledge::attentionCount() const {
  std::size_t count = 0;
  for (const auto& map_entry : hosts_) {
    for (const auto& service : map_entry.second.services) {
      if (!service.attention.empty()) ++count;
    }
  }
  return count;
}

json::Value HostKnowledge::findingsJson() const {
  json::Value result = json::Value::makeObject();
  json::Value findings = json::Value::makeArray();
  std::size_t count = 0;
  for (const auto& map_entry : hosts_) {
    const HostRecord& host = map_entry.second;
    for (const auto& service : host.services) {
      if (service.attention.empty()) continue;
      json::Value finding = json::Value::makeObject();
      finding.set("schema", json::Value::makeString("reconclave-finding/v1"));
      finding.set("finding_id", json::Value::makeString(
          "service-" + host.address + "-" + std::to_string(service.port)));
      finding.set("severity", json::Value::makeString("review"));
      finding.set("confidence", json::Value::makeString("observed"));
      finding.set("title", json::Value::makeString(service.attention));
      finding.set("address", json::Value::makeString(host.address));
      finding.set("port", json::Value::makeNumber(service.port));
      finding.set("service", json::Value::makeString(service.service));
      finding.set("banner", json::Value::makeString(service.banner));
      finding.set("last_seen_ms", json::Value::makeNumber(static_cast<double>(service.last_seen_ms)));
      finding.set("guidance", json::Value::makeString(
          "Confirm intended exposure and approved transport controls during the assessment."));
      findings.push_back(finding);
      ++count;
    }
    if (host.changes > 0) {
      json::Value finding = json::Value::makeObject();
      finding.set("schema", json::Value::makeString("reconclave-finding/v1"));
      finding.set("finding_id", json::Value::makeString("asset-change-" + host.address));
      finding.set("severity", json::Value::makeString("informational"));
      finding.set("confidence", json::Value::makeString("observed"));
      finding.set("title", json::Value::makeString("Observed asset state changed"));
      finding.set("address", json::Value::makeString(host.address));
      finding.set("change_count", json::Value::makeNumber(static_cast<double>(host.changes)));
      finding.set("guidance", json::Value::makeString(
          "Review the evidence timeline to distinguish expected changes from assessment leads."));
      findings.push_back(finding);
      ++count;
    }
  }
  result.set("count", json::Value::makeNumber(static_cast<double>(count)));
  result.set("findings", findings);
  return result;
}

bool HostKnowledge::save(std::string& error) const {
  const std::string temporary = path_ + ".tmp";
  std::ofstream output(temporary, std::ios::trunc);
  if (!output) {
    error = "unable to open host database temporary file";
    return false;
  }
  output << json::canonicalStringify(toJson()) << '\n';
  output.close();
  if (!output.good()) {
    error = "unable to write host database";
    std::remove(temporary.c_str());
    return false;
  }
  if (std::rename(temporary.c_str(), path_.c_str()) != 0) {
    error = std::strerror(errno);
    std::remove(temporary.c_str());
    return false;
  }
  error.clear();
  return true;
}

bool HostKnowledge::load(std::string& error) {
  std::ifstream input(path_);
  if (!input) {
    error.clear();
    return true;
  }
  std::string wire((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  json::Value root;
  if (!json::parse(wire, root, error) || !root.isObject()) return false;
  const auto* entries = root.find("hosts");
  if (!entries || !entries->isArray()) {
    error = "host database has no hosts array";
    return false;
  }
  std::map<std::string, HostRecord> loaded;
  for (const auto& entry : entries->items()) {
    if (!entry.isObject()) {
      error = "host database contains an invalid record";
      return false;
    }
    HostRecord host;
    const auto* address = entry.find("address");
    if (!address || !address->isString() || address->asString().empty()) {
      error = "host database contains an invalid address";
      return false;
    }
    host.address = address->asString();
    if (const auto* value = entry.find("mac")) host.mac = normalMac(value->asString());
    if (const auto* value = entry.find("interface")) host.interface = value->asString();
    if (const auto* value = entry.find("hostname")) host.hostname = value->asString();
    if (const auto* value = entry.find("scan_source")) host.scan_source = value->asString();
    if (const auto* value = entry.find("scan_completion")) host.scan_completion = value->asString();
    if (const auto* value = entry.find("scan_coverage")) host.scan_coverage = value->asString();
    if (const auto* value = entry.find("scan_observed_ms")) host.scan_observed_ms = value->asUInt64();
    if (const auto* value = entry.find("passive_source")) host.passive_source = value->asString();
    if (const auto* value = entry.find("passive_observed_ms")) host.passive_observed_ms = value->asUInt64();
    if (const auto* value = entry.find("first_seen_ms")) host.first_seen_ms = value->asUInt64();
    if (const auto* value = entry.find("last_seen_ms")) host.last_seen_ms = value->asUInt64();
    if (const auto* value = entry.find("observations")) host.observations = value->asUInt64();
    if (const auto* value = entry.find("changes")) host.changes = value->asUInt64();
    const auto* ports = entry.find("open_ports");
    if (ports && ports->isArray()) {
      for (const auto& port : ports->items()) {
        const auto number = port.asUInt64();
        if (number > 0 && number <= 65535) host.open_ports.push_back(static_cast<std::uint16_t>(number));
      }
    }
    const auto* services = entry.find("services");
    if (services && services->isArray()) {
      for (const auto& item : services->items()) {
        if (!item.isObject()) continue;
        KnownService service;
        if (const auto* value = item.find("port")) {
          const auto number = value->asUInt64();
          if (number > 0 && number <= 65535) service.port = static_cast<std::uint16_t>(number);
        }
        if (service.port == 0) continue;
        if (const auto* value = item.find("service")) service.service = value->asString();
        if (const auto* value = item.find("banner")) service.banner = value->asString();
        if (const auto* value = item.find("attention")) service.attention = value->asString();
        if (const auto* value = item.find("last_seen_ms")) service.last_seen_ms = value->asUInt64();
        if (const auto* value = item.find("transport")) service.protocol = value->asString();
        if (service.protocol != "tcp" && service.protocol != "udp") continue;
        if (const auto* value = item.find("source")) service.source = value->asString();
        if (const auto* value = item.find("method")) service.method = value->asString();
        host.services.push_back(std::move(service));
      }
    }
    loaded[host.address] = std::move(host);
  }
  hosts_ = std::move(loaded);
  error.clear();
  return true;
}

}  // namespace reconclave
