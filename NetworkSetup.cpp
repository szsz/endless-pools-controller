#include "NetworkSetup.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
//#define USE_OTA
#ifdef USE_OTA
#include <ArduinoOTA.h>
#include "otapassword.h"
#endif




// Apply WiFi config from LittleFS wifi_config.json if present
void NetworkSetup::applyWifiConfigFromFile() {
  if (!LittleFS.exists("/wifi_config.json")) {
    return;
  }
  File configFile = LittleFS.open("/wifi_config.json", "r");
  if (!configFile) {
    Serial.println("wifi_config.json open failed");
    return;
  }
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();
  if (error) {
    Serial.println("wifi_config.json parse failed");
    return;
  }
  String ssid = doc["ssid"].as<String>();
  String password = doc["password"].as<String>();
  if (ssid.length() == 0) {
    Serial.println("wifi_config.json missing ssid");
    return;
  }
  Serial.printf("Applying Wi-Fi from file: ssid=%s\n", ssid.c_str());
  ConnectionManager::setWifiStaCredentials(ssid.c_str(), password.c_str());
}




/* Save Wi-Fi credentials to LittleFS (/wifi_config.json) */
void NetworkSetup::saveWifiConfigToFile(const String& ssid, const String& password) {
  StaticJsonDocument<256> doc;
  doc["ssid"] = ssid;
  doc["password"] = password;
  File f = LittleFS.open("/wifi_config.json", "w");
  if (!f) {
    Serial.println("Failed to open /wifi_config.json for writing");
    return;
  }
  if (serializeJson(doc, f) == 0) {
    Serial.println("Failed to write /wifi_config.json");
    f.close();
    return;
  }
  f.close();
  Serial.println("Saved /wifi_config.json");
}


// Set new WiFi credentials, delete config file if present
void NetworkSetup::setNewWifiCredentials(const String& ssid, const String& password) {
  // Save immediately; connection result will arrive via async events
  NetworkSetup::saveWifiConfigToFile(ssid, password);

  // restart
  esp_restart(); 
}

void NetworkSetup::begin() {
  // Load saved Wi‑Fi credentials (if any) and start connection manager
  ConnectionManager::configure(HOSTNAME, SOFT_AP_SSID, SOFT_AP_PASS);
  NetworkSetup::applyWifiConfigFromFile();
  ConnectionManager::begin();
  
#ifdef USE_OTA
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
#endif
}
void NetworkSetup::loop() {
  ConnectionManager::loop();
#ifdef USE_OTA
  // OTA handler
  ArduinoOTA.handle();
#endif
}
