#pragma once


/* Simple network facade built on ConnectionManager/NetworkSetup.
   Owns AsyncWebServer and AsyncEventSource (SSE) and defines all routes. */
namespace AppNetwork {

  // Initialize networking (Ethernet/WiFi/SoftAP via ConnectionManager) and basic captive portal.
  void setup();

  // True if either Ethernet or WiFi STA has an IP.
  bool connected();

  // Push an SSE event with given name and JSON payload.
  void push_event(const char* , const char* );

  void loop();

} // namespace AppNetwork
