#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace reconclave {

struct DnsResult {
  std::string canonical_name;
  std::vector<std::string> addresses;
};

struct TcpObservation {
  bool connected = false;
  std::string address;
  std::uint16_t port = 0;
  int latency_ms = 0;
  std::string error;
};

struct ServiceObservation {
  bool connected = false;
  std::string address;
  std::uint16_t port = 0;
  std::string service;
  std::string banner;
  std::string transport{"tcp"};
  std::string attention;
  std::string error;
};

bool validHostname(const std::string& hostname);
bool resolveHost(const std::string& hostname, DnsResult& result, std::string& error);

// Numeric IPv4 only. Hostname resolution is deliberately a separate tool so
// signed scopes bind connection jobs to the exact address they authorize.
bool tcpConnect(const std::string& address, std::uint16_t port, int timeout_ms,
                TcpObservation& observation, const std::string& bind_interface = {});

// A single bounded connection. It waits for server-first greetings and sends
// only a fixed HTTP HEAD request on common cleartext HTTP ports. It never
// authenticates, follows links, negotiates TLS, or accepts caller payloads.
bool identifyService(const std::string& address, std::uint16_t port, int timeout_ms,
                     ServiceObservation& observation);
std::string classifyService(std::uint16_t port, const std::string& banner);

// The ports classifyService() knows how to label by number alone. Exposed so
// callers that decide which ports are "worth checking" (discovery_job.cpp's
// host scan) stay in sync with classification instead of maintaining an
// independent list that can drift.
const std::vector<std::uint16_t>& knownServicePorts();

}  // namespace reconclave
