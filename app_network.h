#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "NetworkSetup.h"

// Simple network facade built on ConnectionManager/NetworkSetup.
// Provides a shared AsyncWebServer and AsyncEventSource (SSE).
namespace AppNetwork {

  // Initialize networking (Ethernet/WiFi/SoftAP via ConnectionManager) and basic captive portal.
  void begin();

  // True if either Ethernet or WiFi STA has an IP.
  bool connected();

  // Access the shared AsyncWebServer instance to register routes.
  AsyncWebServer& server();

  // Access the shared AsyncEventSource (SSE) instance for event pushing.
  AsyncEventSource& events();


} // namespace AppNetwork
