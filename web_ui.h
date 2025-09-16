#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

namespace WebUI {
  /** Initialise Wi-Fi, LittleFS and register routes. */
  void begin();

  /** Call in loop() to drive SSE heartbeats. */
  void loop();

  /** Push a Server-Sent Event. */
  void push_event(const char *event, const char *json);

  /** Push a network event as hex string. */
  void push_network_event(const uint8_t *data, size_t len);
}
