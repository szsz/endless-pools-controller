#pragma once


/* Simple network facade built on ConnectionManager/NetworkSetup.
   Owns AsyncWebServer and AsyncEventSource (SSE) and defines all routes. */
namespace AppNetwork {

  // Initialize networking (Ethernet/WiFi/SoftAP via ConnectionManager) and basic captive portal.
  void begin();

  // Returns true if network is connected (Ethernet or WiFi STA).
  bool connected();

  // Push an SSE event with given name and JSON payload.
  void push_event(const char* , const char* );

  // For compatibility; no-op for Async server.
  void loop();

} // namespace AppNetwork
