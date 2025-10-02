#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <ETH.h>  // ESP32 Arduino Ethernet (supports W5500 over SPI)

// ---------- Ethernet (W5500 over SPI) defaults (can be overridden before include) ----------
#ifndef ETH_TYPE
#define ETH_TYPE ETH_PHY_W5500
#endif
#ifndef ETH_ADDR
#define ETH_ADDR 1
#endif
#ifndef ETH_CS
#define ETH_CS 14
#endif
#ifndef ETH_IRQ
#define ETH_IRQ 10
#endif
#ifndef ETH_RST
#define ETH_RST 9
#endif
#ifndef ETH_SPI_SCK
#define ETH_SPI_SCK 13
#endif
#ifndef ETH_SPI_MISO
#define ETH_SPI_MISO 12
#endif
#ifndef ETH_SPI_MOSI
#define ETH_SPI_MOSI 11
#endif

// ---------- SoftAP defaults (can be overridden before include) ----------
#ifndef SOFT_AP_IP_OCTETS
#define SOFT_AP_IP_OCTETS 192, 168, 4, 1
#endif
#ifndef SOFT_AP_MASK_OCTETS
#define SOFT_AP_MASK_OCTETS 255, 255, 255, 0
#endif

// ---------- Timeouts and watchdogs ----------
#ifndef WIFI_BOOT_TIMEOUT_MS
#define WIFI_BOOT_TIMEOUT_MS 15000UL
#endif
#ifndef ETHERNET_BOOT_TIMEOUT_MS
#define ETHERNET_BOOT_TIMEOUT_MS 8000UL
#endif
// If no interface (ETH, STA, or AP) is active for this long, restart
#ifndef NO_ACTIVE_RESET_MS
#define NO_ACTIVE_RESET_MS 30000UL
#endif
// How often loop() should re-check and print state (ms)
#ifndef NET_LOG_THROTTLE_MS
#define NET_LOG_THROTTLE_MS 5000UL
#endif

// Static-only ConnectionManager providing WiFi STA + SoftAP + (optional) W5500 Ethernet.
class ConnectionManager {
public:
  ConnectionManager() = delete; // static-only
  ConnectionManager(const ConnectionManager&) = delete;
  ConnectionManager& operator=(const ConnectionManager&) = delete;

  // Configure identifiers and feature toggles
  static void configure(const char* hostname,
                        const char* softApSsid,
                        const char* softApPass,
                        bool enable_softap = true,
                        bool enable_sta    = true,
                        bool enable_eth    = false) {
    s_hostname      = hostname   ? String(hostname)   : String();
    s_softApSsid    = softApSsid ? String(softApSsid) : String();
    s_softApPass    = softApPass ? String(softApPass) : String();
    s_enable_softap = enable_softap;
    s_enable_sta    = enable_sta;
    s_enable_eth    = enable_eth;
  }

  // Provide/update WiFi STA credentials. Starts STA immediately if allowed.
  static void setWifiStaCredentials(const char* ssid, const char* pass) {
    if (!ssid || !ssid[0]) {
      Serial.println(F("[WiFi] setWifiStaCredentials: invalid SSID"));
      return;
    }
    s_staSsid = String(ssid);
    s_staPass = pass ? String(pass) : String();

   
  }

  // Begin networking. Try Ethernet (if enabled), start STA (if creds set), and bring up SoftAP (if enabled).
  static void begin() {

    bool eth_ok = false;
    if (s_enable_eth) {
      eth_ok = startEthernet();
    }

    // Start STA only if Ethernet is disabled or failed
    if (s_enable_sta && s_staSsid.length() > 0) {
      if (!s_enable_eth || !eth_ok) {
        if (s_enable_eth && !eth_ok) {
          Serial.println(F("[WiFi] Ethernet not connected; disabling ETH and starting STA"));
          ETH.end(); // ensure ETH is disabled before STA
        }
        startSTA();
      } else {
        Serial.println(F("[WiFi] Ethernet is up; not starting STA"));
      }
    }

    // SoftAP: only start if no Ethernet IP and no STA connection
    if (s_enable_softap) {
      bool eth_ok_now = ethHasIp();
      bool sta_ok_now = wifiHasIp();
      if (!eth_ok_now && !sta_ok_now) {
        startSoftAP();
      } else {
        // Ensure AP is not active when other connectivity is present
        if (s_softApActive) {
          Serial.println(F("[AP] Disabling SoftAP due to active Ethernet or WiFi STA"));
          WiFi.softAPdisconnect(true);
          if (s_enable_sta) {
            WiFi.mode(WIFI_STA);
          } else {
            WiFi.mode(WIFI_OFF);
          }
          s_softApActive = false;
        }
      }
    }

    s_lastStateLogMs = 0;
    s_noActiveSinceMs = 0;
  }


  // State queries used by the rest of the app
  static bool ethHasIp() {
    if (!s_enable_eth) return false;
    IPAddress ip = ETH.localIP();
    return ETH.linkUp() && ip != IPAddress(0,0,0,0);
  }
  static bool wifiHasIp() {
    return (WiFi.getMode() & WIFI_MODE_STA) && WiFi.status() == WL_CONNECTED;
  }
  static bool softApActive() {
    return (WiFi.getMode() & WIFI_MODE_AP);
  }

  // Periodic maintenance. Also enforces reset if no active interface is present.
  static void loop() {

    uint32_t now = millis();

    // Track current interface state
    bool eth = ethHasIp();
    bool sta = wifiHasIp();
    bool ap  = softApActive();
    s_wifiHasIp    = sta;
    s_softApActive = ap;

    // Periodic status logging
    if (now - s_lastStateLogMs >= NET_LOG_THROTTLE_MS) {
      s_lastStateLogMs = now;
      if (eth) {
        Serial.print(F("[NET] Ethernet IP: ")); Serial.println(ETH.localIP());
      } else if (sta) {
        Serial.print(F("[NET] WiFi IP: ")); Serial.println(WiFi.localIP());
      } else if (ap) {
        Serial.print(F("[NET] SoftAP IP: ")); Serial.println(WiFi.softAPIP());
      } else {
        Serial.println(F("[NET] No active network interface"));
      }
    }

    // Reset watchdog: restart if no active interface for NO_ACTIVE_RESET_MS
    bool anyActive = eth || sta || ap;
    if (anyActive) {
      s_noActiveSinceMs = 0;
    } else {
      if (s_noActiveSinceMs == 0) {
        s_noActiveSinceMs = now;
      } else if (now - s_noActiveSinceMs >= NO_ACTIVE_RESET_MS) {
        Serial.println(F("[NET] No active interface for too long. Restarting..."));
        delay(100);
        esp_restart();
      }
    }
  }

private:

  // Start Ethernet (W5500 over SPI). Non-fatal if it fails; returns success.
  static bool startEthernet() {
    Serial.println(F("[ETH] Initializing SPI and W5500..."));
    s_ethAttempted = true;
    s_ethSuccessful = false;
    // SPI.begin should already be called in begin()

      SPI.begin(ETH_SPI_SCK, ETH_SPI_MISO, ETH_SPI_MOSI);
    if (!ETH.begin(ETH_TYPE, ETH_ADDR, ETH_CS, ETH_IRQ, ETH_RST, SPI)) {
      Serial.println(F("[ETH] ETH.begin() failed"));
      ETH.end();
      return false;
    }

    if (s_hostname.length() > 0) {
      ETH.setHostname(s_hostname.c_str());
    }

    // Wait briefly for link + DHCP
    uint32_t start = millis();
    while (millis() - start < ETHERNET_BOOT_TIMEOUT_MS) {
      if (ETH.linkUp() && (ETH.localIP() != IPAddress(0,0,0,0))) {
        Serial.print(F("[ETH] Up. IP: ")); Serial.println(ETH.localIP());
        s_ethSuccessful = true;
        return true;
      }
      delay(100);
    }

    Serial.println(F("[ETH] Timeout waiting for link/DHCP"));
    s_ethSuccessful = false;
    ETH.end();
    return false;
  }

  // Start WiFi STA connection attempt (with short timeout). Returns whether connected.
  static bool startSTA() {
    Serial.print(F("[WiFi] Connecting to SSID: "));
    Serial.println(s_staSsid);

    if (s_hostname.length() > 0) {
      WiFi.setHostname(s_hostname.c_str());
    }
    WiFi.begin(s_staSsid.c_str(), s_staPass.c_str());

    uint32_t start = millis();
    wl_status_t last = (wl_status_t)0xff;
    while (millis() - start < WIFI_BOOT_TIMEOUT_MS) {
      wl_status_t s = WiFi.status();
      if (s != last) {
        last = s;
        Serial.printf("[WiFi] Status: %d\n", s);
      }
      if (s == WL_CONNECTED) {
        Serial.print(F("[WiFi] Connected. IP: "));
        Serial.println(WiFi.localIP());
        s_wifiHasIp = true;
        return true;
      }
      delay(200);
    }

    Serial.println(F("[WiFi] Timeout; will keep trying in background"));
    // Leave STA enabled; background reconnection will proceed
    s_wifiHasIp = false;
    return false;
  }

  // Start/ensure SoftAP
  static void startSoftAP() {
    if (!s_enable_softap) return;

    Serial.println(F("[AP] Starting SoftAP..."));

    IPAddress apIP(SOFT_AP_IP_OCTETS);
    IPAddress apMask(SOFT_AP_MASK_OCTETS);



    // Set AP IP before starting AP
    if (!WiFi.softAPConfig(apIP, apIP, apMask)) {
      Serial.println(F("[AP] softAPConfig failed (continuing with default)"));
    }

    const char* ssid = s_softApSsid.length() ? s_softApSsid.c_str() : nullptr;
    const char* pass = s_softApPass.length() ? s_softApPass.c_str() : nullptr;

    bool ok = WiFi.softAP(ssid ? ssid : "esp32-ap", pass && strlen(pass) >= 8 ? pass : nullptr);
    if (!ok) {
      Serial.println(F("[AP] softAP start FAILED"));
      s_softApActive = false;
      return;
    }

    s_softApActive = true;
    Serial.print(F("[AP] SSID: "));
    Serial.println(ssid ? ssid : "esp32-ap");
    Serial.print(F("[AP] IP: "));
    Serial.println(WiFi.softAPIP());
  }

private:
  // Configuration and state (header-only via inline static)
  inline static String s_hostname;
  inline static String s_softApSsid;
  inline static String s_softApPass;
  inline static String s_staSsid;
  inline static String s_staPass;

  inline static bool s_enable_softap = true;
  inline static bool s_enable_sta    = true;
  inline static bool s_enable_eth    = false;

  inline static bool s_ethAttempted  = false;
  inline static bool s_ethSuccessful = false;

  inline static bool s_softApActive  = false;
  inline static bool s_wifiHasIp     = false;

  inline static bool s_begun         = false;


  inline static uint32_t s_noActiveSinceMs = 0;
  inline static uint32_t s_lastStateLogMs  = 0;
};
