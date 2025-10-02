#include "UDPEventSender.h"
#include <string.h>

static inline bool is_multicast_ip(const IPAddress& ip) {
  uint8_t first = ip[0];
  return first >= 224 && first <= 239;
}


void UDPEventSender::begin(IPAddress target, uint16_t port, uint16_t localPort) {

  this->m_target = target; this->m_port = port;
    if (is_multicast_ip(target) && port != 0) {
    m_udp.beginMulticast(target, port);
  } else {
    m_udp.begin(localPort);
  }
}

void UDPEventSender::loop() {
  
  int packetSize = m_udp.parsePacket();
  if (packetSize <= 0) return;

  
  static uint8_t buf[1472];
  int len = m_udp.read(buf, sizeof(buf));
  if (len <= 0) return;

  if (m_onReceive) {
    IPAddress remote = m_udp.remoteIP();
    uint16_t rport = m_udp.remotePort();
    m_onReceive(buf, (size_t)len, remote, rport);
  }
}

bool UDPEventSender::sendBytes(const uint8_t* data, size_t len) {

  if (m_udp.beginPacket(m_target, m_port)) {
    m_udp.write(data, len);
    m_udp.endPacket();
    return true;
  }
  return false;
}
