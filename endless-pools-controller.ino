#define NOUDPTEST

#include <Arduino.h>
#include "swim_machine.h"
#include "workout_manager.h"
#include "web_ui.h"
#include "app_network.h"
#include "NetworkSetup.h"

#include <ArduinoOTA.h>
#include "otapassword.h"

void setup()
{
  Serial.begin(115200);
  delay(100);


  WebUI::begin();            // Wi-Fi + HTTP server
  Serial.println("web ui begin done");
  // Initialize Arduino OTA
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA
    .onStart([]() {
      Serial.println("OTA: Start");
    })
    .onEnd([]() {
      Serial.println("OTA: End");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("OTA: Progress: %u%%\n", (progress * 100) / total);
    })
    .onError([](ota_error_t error) {
      Serial.printf("OTA: Error[%u]\n", error);
    });
  ArduinoOTA.begin();
  Serial.println("OTA: Ready (port 3232)");
  
  SwimMachine::begin(WebUI::push_network_event);   // initialise UDP + state machine with function pointer
  Serial.println("swim machine begin done");
  WorkoutManager::begin();   // load saved workouts & prefs
  Serial.println("workout manager begin done");
}

void loop()
{
  g_conn.loop();             // enforce connection policy and SoftAP control
  WebUI::loop();             // handles AsyncEventSource pings
  // OTA handler
  ArduinoOTA.handle();
  WorkoutManager::tick();    // 1 Hz countdown
  SwimMachine::tick();       // drive swim-machine protocol

  /*static uint32_t lastMemLogMs = 0;
  uint32_t now = millis();
  if (now - lastMemLogMs >= 1000) {
    lastMemLogMs = now;
    size_t heapTotal = ESP.getHeapSize();
    size_t heapFree = ESP.getFreeHeap();
    size_t heapUsed = heapTotal - heapFree;
    size_t heapMaxAlloc = ESP.getMaxAllocHeap();
    size_t heapMinFree = ESP.getMinFreeHeap();
    Serial.printf("MEM: heap total=%u bytes, used=%u bytes, free=%u bytes, maxAlloc=%u, minFree=%u\n",
                  (unsigned)heapTotal, (unsigned)heapUsed, (unsigned)heapFree,
                  (unsigned)heapMaxAlloc, (unsigned)heapMinFree);
  }*/

  delay(49);
}
