#include "UDPEventSender.h"
#include <string.h>

namespace {
static inline bool is_valid_ip(const IPAddress& ip) {
  return !(ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0);
}
static inline bool is_multicast_ip(const IPAddress& ip) {
  uint8_t first = ip[0];
  return first >= 224 && first <= 239;
}
} // namespace

bool UDPEventSender::rebindUdp() {
  // Decide preferred interface by priority: Ethernet > WiFi STA > SoftAP
  m_requestrebind = false;
  IPAddress ifIp;
  PreferredIf newIf = PreferredIf::NONE;

  if (m_ethHasIp && is_valid_ip(ETH.localIP())) {
    ifIp = ETH.localIP();
    newIf = PreferredIf::ETH;
  } else if (m_wifiHasIp && is_valid_ip(WiFi.localIP())) {
    ifIp = WiFi.localIP();
    newIf = PreferredIf::WIFI_STA;
  } else if (m_softApActive && is_valid_ip(WiFi.softAPIP())) {
    ifIp = WiFi.softAPIP();
    newIf = PreferredIf::SOFTAP;
  } else {
    // As a fallback, try actual IPs even if flags haven't propagated yet
    if (is_valid_ip(ETH.localIP())) {
      ifIp = ETH.localIP();
      newIf = PreferredIf::ETH;
    } else if (is_valid_ip(WiFi.localIP())) {
      ifIp = WiFi.localIP();
      newIf = PreferredIf::WIFI_STA;
    } else if (is_valid_ip(WiFi.softAPIP())) {
      ifIp = WiFi.softAPIP();
      newIf = PreferredIf::SOFTAP;
    } else {
      if (m_udpReady) {
        m_udp.stop();
        m_udpReady = false;
      }
      m_currentIf = PreferredIf::NONE;
      return false;
    }
  }

  // (Re)initialize UDP socket according to target type
  m_udp.stop();
  bool ok = false;
  if (is_multicast_ip(m_target) && m_port != 0) {
    ok = m_udp.beginMulticast(m_target, m_port);
  } else {
    ok = m_udp.begin(m_localPort);
  }

  m_udpReady = ok;
  m_currentIf = ok ? newIf : PreferredIf::NONE;
  return m_udpReady;
}

void UDPEventSender::begin(uint16_t localPort) {
  #ifdef NOUDPTEST
  return;
  #endif
  m_localPort = (localPort == 0) ? 45654 : localPort;
  m_begun = true;
  m_requestrebind=true;
}

void UDPEventSender::begin(IPAddress target, uint16_t port, uint16_t localPort) {
  #ifdef NOUDPTEST
  return;
  #endif
  m_target = target;
  m_port = port;
  m_localPort = (localPort == 0) ? 45654 : localPort;
  m_begun = true;
  m_requestrebind=true;
}

void UDPEventSender::loop() {
  #ifdef NOUDPTEST
  return;
  #endif
  // Ensure socket is up before attempting any receive operations
  if (m_begun && (!m_udpReady|| m_requestrebind)) {
    rebindUdp();
    return;
  }
  if (!m_udpReady) return;

  int packetSize = m_udp.parsePacket();
  if (packetSize <= 0) return;

  // Check again before read in case socket dropped between parse and read
  if (!m_udpReady) return;

  static uint8_t buf[1472];
  int len = m_udp.read(buf, sizeof(buf));
  if (len < 0) return;

  IPAddress remote = m_udp.remoteIP();
  uint16_t rport = m_udp.remotePort();

  if (m_onReceive) {
    m_onReceive(buf, (size_t)len, remote, rport);
  }
}

bool UDPEventSender::sendBytes(const uint8_t* data, size_t len) {
  #ifdef NOUDPTEST
  return true;
  #endif
  // Ensure socket is up before any send
  if (m_begun && !m_udpReady) {
    
    m_requestrebind=true;
    return false;
  }
  if (!m_udpReady) return false;

  // Do not attempt to send until any interface has IP (ETH > STA > SoftAP)
  if (!m_ethHasIp && !m_wifiHasIp && !m_softApActive) return false;
  if (!is_valid_ip(m_target) || m_port == 0) return false;

  if (m_udp.beginPacket(m_target, m_port)) {
    m_udp.write(data, len);
    m_udp.endPacket();
    return true;
  }
  return false;
}

void UDPEventSender::handleConnectionChange(bool ethHasIp, bool wifiHasIp, bool softApActive) {
  m_ethHasIp = ethHasIp;
  m_wifiHasIp = wifiHasIp;
  m_softApActive = softApActive;

  // Requirement: whenever connection changes, re-initialize UDP so it continues working
  if (m_begun) {
  m_requestrebind=true;
  }
}
