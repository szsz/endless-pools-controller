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



  // Poll incoming packets; must be called in loop()
  void loop();

  // Register data receive handler (optional)
  using ReceiveHandler = std::function<void(const uint8_t*, size_t, const IPAddress&, uint16_t)>;
  void onReceive(ReceiveHandler cb) { m_onReceive = cb; }


  void begin(IPAddress target, uint16_t port, uint16_t localPort = 0);




  // Raw bytes sender (binary payload)
  bool sendBytes(const uint8_t* data, size_t len);


private:
  WiFiUDP   m_udp;
  IPAddress m_target;
  uint16_t  m_port = 0;
  
  // Receive callback
  ReceiveHandler m_onReceive = nullptr;
};
