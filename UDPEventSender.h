#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ETH.h>
#include <functional>

// Simple UDP event sender that prefers Ethernet when available.
// Target can be overridden by constructor or begin(target, port).
class UDPEventSender {
public:
  UDPEventSender(IPAddress target, uint16_t port)
  : m_target(target), m_port(port) {}

  // Optionally bind a local UDP port (0 means default 45654)
  bool begin(uint16_t localPort = 0);

  // Poll incoming packets; must be called in loop()
  void loop();

  // Register data receive handler (optional)
  using ReceiveHandler = std::function<void(const uint8_t*, size_t, const IPAddress&, uint16_t)>;
  void onReceive(ReceiveHandler cb) { m_onReceive = cb; }

  // ConnectionManager integration: update interface preference
  void handleConnectionChange(bool ethHasIp, bool wifiHasIp, bool softApActive);

  bool begin(IPAddress target, uint16_t port, uint16_t localPort = 0);

  // High-level event helpers
  bool sendConnectionEvent(bool ethHasIp, bool wifiHasIp, bool softApActive);
  bool sendWifiStaLost(int reason);


  // Raw bytes sender (binary payload)
  bool sendBytes(const uint8_t* data, size_t len);

private:
  IPAddress m_target;
  uint16_t  m_port = 0;
  uint16_t  m_localPort = 0;

  WiFiUDP   m_udp;

  // Receive callback
  ReceiveHandler m_onReceive = nullptr;

  // Snapshot of connection state
  bool m_ethHasIp = false;
  bool m_wifiHasIp = false;
  bool m_softApActive = false;

  // Internal state
  bool m_begun = false;      // begin() called by user
  bool m_udpReady = false;   // WiFiUDP actually begun (only when network has IP)

  enum class PreferredIf { NONE, ETH, WIFI_STA, SOFTAP };
  PreferredIf m_currentIf = PreferredIf::NONE;
  bool rebindUdp();
};
