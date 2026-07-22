#include "NetworkSetup.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#define USE_OTA
#ifdef USE_OTA
#include <ArduinoOTA.h>
#include "otapassword.h"
#endif

// Runtime hostname (defaults to the compile-time HOSTNAME, overridden by
// /hostname in LittleFS if present).
static String   s_hostname = HOSTNAME;
static uint32_t s_rebootAtMs = 0;   // 0 = no pending reboot

// Keep only characters valid in a DNS label; map spaces/underscores to '-',
// trim leading/trailing '-', clamp length, and never return empty.
static String sanitizeHostname(const String& in) {
  String out;
  for (size_t i = 0; i < in.length() && out.length() < 32; ++i) {
    char c = in[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-') out += c;
    else if (c == ' ' || c == '_')          out += '-';
  }
  while (out.length() && out[0] == '-') out.remove(0, 1);
  while (out.length() && out[out.length() - 1] == '-') out.remove(out.length() - 1);
  if (out.length() == 0) out = HOSTNAME;
  return out;
}

static void loadHostname() {
  s_hostname = HOSTNAME;
  if (!LittleFS.exists("/hostname")) return;
  File f = LittleFS.open("/hostname", "r");
  if (!f) return;
  String v = f.readString();
  f.close();
  v.trim();
  if (v.length()) s_hostname = sanitizeHostname(v);
}

String NetworkSetup::getHostname() { return s_hostname; }

void NetworkSetup::setHostname(const String& name) {
  String h = sanitizeHostname(name);
  File f = LittleFS.open("/hostname", "w");
  if (f) { f.print(h); f.close(); }
  s_hostname = h;
  // Apply on a clean boot so mDNS/OTA re-register; delay lets the HTTP response flush.
  s_rebootAtMs = millis() + 1200;
  if (s_rebootAtMs == 0) s_rebootAtMs = 1;   // avoid the "disabled" sentinel
  Serial.printf("Hostname set to %s; restarting shortly\n", h.c_str());
}




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
  // Load the saved device name (if any), then start the connection manager
  loadHostname();
  ConnectionManager::configure(s_hostname.c_str(), SOFT_AP_SSID, SOFT_AP_PASS);
  NetworkSetup::applyWifiConfigFromFile();
  ConnectionManager::begin();

#ifdef USE_OTA
  // Initialize Arduino OTA
  ArduinoOTA.setHostname(s_hostname.c_str());
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
  // Deferred restart after a hostname change (once the response has been sent).
  if (s_rebootAtMs && (int32_t)(millis() - s_rebootAtMs) >= 0) {
    Serial.println("Restarting to apply new hostname...");
    delay(50);
    esp_restart();
  }
}
