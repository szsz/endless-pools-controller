#pragma once

#include <Arduino.h>
#ifdef ETHERNET
#include <AsyncWebServer_ESP32_W5500.h>
#else
#include <ESPAsyncWebServer.h>
#endif
namespace Network {
  // Initialize Ethernet/WiFi and mDNS. In AP mode, starts a captive portal.
  void begin();

  // True if either Ethernet link is up or WiFi is connected.
  bool connected();

  // Access the shared AsyncWebServer instance to register routes.
  AsyncWebServer& server();

  // Access the shared AsyncEventSource (SSE) instance for event pushing.
  AsyncEventSource& events();

}
