#include "network_tools.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>

namespace reconclave {
namespace {

std::int64_t monotonicMs() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<std::int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

int connectSocket(const std::string& address, std::uint16_t port, int timeout_ms,
                  int& latency_ms, std::string& error) {
  if (port == 0 || timeout_ms < 50 || timeout_ms > 10000) {
    error = "invalid port or timeout";
    return -1;
  }
  sockaddr_in peer{};
  peer.sin_family = AF_INET;
  peer.sin_port = htons(port);
  if (inet_pton(AF_INET, address.c_str(), &peer.sin_addr) != 1) {
    error = "numeric IPv4 address required";
    return -1;
  }
  const int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    error = std::strerror(errno);
    return -1;
  }
  const int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  const auto started = monotonicMs();
  int status = connect(fd, reinterpret_cast<sockaddr*>(&peer), sizeof(peer));
  if (status != 0 && errno == EINPROGRESS) {
    pollfd event{fd, POLLOUT, 0};
    status = poll(&event, 1, timeout_ms);
    if (status > 0) {
      int socket_error = 0;
      socklen_t length = sizeof(socket_error);
      getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &length);
      errno = socket_error;
      status = socket_error == 0 ? 0 : -1;
    } else if (status == 0) {
      errno = ETIMEDOUT;
      status = -1;
    }
  }
  latency_ms = static_cast<int>(monotonicMs() - started);
  if (status == 0) return fd;
  error = std::strerror(errno);
  close(fd);
  return -1;
}

// Single source of truth for "which port maps to which service name" -
// classifyService()'s port fallback, knownServicePorts() (what
// discovery_job.cpp scans) and the cleartext-attention lookup below all
// derive from this one table instead of three independently maintained
// lists.
struct KnownPort {
  std::uint16_t port;
  const char* service;
};
constexpr KnownPort kKnownPorts[] = {
    {21, "ftp"},   {22, "ssh"},    {23, "telnet"}, {25, "smtp"},
    {53, "dns"},   {80, "http"},   {110, "pop3"},  {143, "imap"},
    {443, "https"}, {445, "smb"},  {554, "rtsp"},  {3389, "rdp"},
    {8000, "http"}, {8080, "http"}, {8443, "https"},
};

const char* portService(std::uint16_t port) {
  for (const auto& entry : kKnownPorts) {
    if (entry.port == port) return entry.service;
  }
  return "unknown";
}

// Services this tool actively flags as worth an operator's attention because
// they carry credentials or content in the clear. Keyed by the same service
// names classifyService() already produces, so a service recognised there is
// never silently missing from here.
struct CleartextAttention {
  const char* service;
  const char* message;
};
constexpr CleartextAttention kCleartextAttention[] = {
    {"telnet", "cleartext remote administration"},
    {"ftp", "cleartext file-transfer service"},
    {"http", "cleartext web service"},
    {"smtp", "cleartext mail-transfer service"},
    {"pop3", "cleartext mail-retrieval service"},
    {"imap", "cleartext mail-retrieval service"},
};

std::string cleartextAttention(const std::string& service) {
  for (const auto& entry : kCleartextAttention) {
    if (service == entry.service) return entry.message;
  }
  return "";
}

std::string printableBanner(const char* data, std::size_t length) {
  std::string result;
  result.reserve(std::min<std::size_t>(length, 256));
  for (std::size_t i = 0; i < length && result.size() < 256; ++i) {
    const unsigned char c = static_cast<unsigned char>(data[i]);
    if (c == '\r' || c == '\n' || c == '\t') result.push_back(' ');
    else if (c >= 0x20 && c <= 0x7e) result.push_back(static_cast<char>(c));
  }
  while (!result.empty() && result.back() == ' ') result.pop_back();
  return result;
}

}  // namespace

bool validHostname(const std::string& hostname) {
  if (hostname.empty() || hostname.size() > 253 || hostname.front() == '.' || hostname.back() == '.') return false;
  std::size_t label_length = 0;
  for (unsigned char c : hostname) {
    if (c == '.') {
      if (label_length == 0 || label_length > 63) return false;
      label_length = 0;
    } else {
      if (!(std::isalnum(c) || c == '-')) return false;
      ++label_length;
    }
  }
  return label_length > 0 && label_length <= 63;
}

bool resolveHost(const std::string& hostname, DnsResult& result, std::string& error) {
  result = DnsResult{};
  if (!validHostname(hostname)) { error = "invalid hostname"; return false; }
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_CANONNAME;
  addrinfo* addresses = nullptr;
  const int status = getaddrinfo(hostname.c_str(), nullptr, &hints, &addresses);
  if (status != 0) { error = gai_strerror(status); return false; }
  for (addrinfo* item = addresses; item != nullptr && result.addresses.size() < 16; item = item->ai_next) {
    char text[INET6_ADDRSTRLEN]{};
    const void* raw = item->ai_family == AF_INET
                          ? static_cast<const void*>(&reinterpret_cast<sockaddr_in*>(item->ai_addr)->sin_addr)
                          : item->ai_family == AF_INET6
                                ? static_cast<const void*>(&reinterpret_cast<sockaddr_in6*>(item->ai_addr)->sin6_addr)
                                : nullptr;
    if (raw == nullptr || inet_ntop(item->ai_family, raw, text, sizeof(text)) == nullptr) continue;
    if (std::find(result.addresses.begin(), result.addresses.end(), text) == result.addresses.end()) {
      result.addresses.emplace_back(text);
    }
    if (result.canonical_name.empty() && item->ai_canonname != nullptr) result.canonical_name = item->ai_canonname;
  }
  freeaddrinfo(addresses);
  error.clear();
  return true;
}

bool tcpConnect(const std::string& address, std::uint16_t port, int timeout_ms,
                TcpObservation& observation) {
  observation = TcpObservation{};
  observation.address = address;
  observation.port = port;
  const int fd = connectSocket(address, port, timeout_ms, observation.latency_ms,
                               observation.error);
  observation.connected = fd >= 0;
  if (fd < 0) return false;
  close(fd);
  return true;
}

std::string classifyService(std::uint16_t port, const std::string& banner) {
  std::string lower = banner;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (lower.find("ssh-") != std::string::npos) return "ssh";
  if (lower.find("http/") != std::string::npos) return "http";
  if (lower.find("smtp") != std::string::npos) return "smtp";
  if (lower.find("ftp") != std::string::npos) return "ftp";
  return portService(port);
}

const std::vector<std::uint16_t>& knownServicePorts() {
  static const std::vector<std::uint16_t> ports = [] {
    std::vector<std::uint16_t> result;
    for (const auto& entry : kKnownPorts) result.push_back(entry.port);
    return result;
  }();
  return ports;
}

bool identifyService(const std::string& address, std::uint16_t port, int timeout_ms,
                     ServiceObservation& observation) {
  observation = ServiceObservation{};
  observation.address = address;
  observation.port = port;
  int latency_ms = 0;
  const int fd = connectSocket(address, port, timeout_ms, latency_ms, observation.error);
  (void)latency_ms;
  if (fd < 0) return false;
  observation.connected = true;

  if (portService(port) == std::string("http")) {
    const std::string request = "HEAD / HTTP/1.0\r\nHost: " + address +
                                "\r\nConnection: close\r\n\r\n";
    (void)send(fd, request.data(), request.size(), MSG_NOSIGNAL);
  }
  pollfd event{fd, POLLIN, 0};
  if (poll(&event, 1, std::min(timeout_ms, 1000)) > 0 && (event.revents & POLLIN)) {
    char buffer[512];
    const ssize_t count = recv(fd, buffer, sizeof(buffer), 0);
    if (count > 0) observation.banner = printableBanner(buffer, static_cast<std::size_t>(count));
  }
  close(fd);
  observation.service = classifyService(port, observation.banner);
  observation.attention = cleartextAttention(observation.service);
  return true;
}

}  // namespace reconclave
