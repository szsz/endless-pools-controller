#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>

// ---------- Ethernet (W5500 over SPI) defaults (not used in this minimal impl) ----------
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

// ---------- SoftAP defaults ----------
#ifndef SOFT_AP_IP_OCTETS
#define SOFT_AP_IP_OCTETS 192, 168, 4, 1
#endif
#ifndef SOFT_AP_MASK_OCTETS
#define SOFT_AP_MASK_OCTETS 255, 255, 255, 0
#endif

// Minimal, compilable ConnectionManager providing WiFi STA + SoftAP.
// Ethernet is stubbed (ethHasIp() = false).
class ConnectionManager {
public:
  ConnectionManager(const char* hostname,
                    const char* softApSsid,
                    const char* softApPass,
                    bool p_enable_softap = true,
                    bool p_enable_sta    = true,
                    bool p_enable_eth    = false)
  : m_hostname(hostname ? hostname : ""),
    m_softApSsid(softApSsid ? softApSsid : ""),
    m_softApPass(softApPass ? softApPass : ""),
    enable_softap(p_enable_softap),
    enable_sta(p_enable_sta),
    enable_eth(p_enable_eth) {}

  // Provide/update WiFi STA credentials. Starts STA immediately if allowed.
  void setWifiStaCredentials(const char* ssid, const char* pass, uint32_t /*force_STA_time*/ = 0) {
    if (!ssid || !ssid[0]) {
      Serial.println("setWifiStaCredentials: invalid SSID");
      return;
    }
    m_staSsid = String(ssid);
    m_staPass = pass ? String(pass) : String();
    if (enable_sta) {
      startSTA();
    }
  }

  // Bring up SoftAP (always) and STA (if credentials present).
  void begin() {
    // Apply hostname once
#if ARDUINO_USB_CDC_ON_BOOT
    // no-op for certain boards, keep compatibility
#endif
#if defined(ARDUINO_ARCH_ESP32)
    if (m_hostname.length()) WiFi.setHostname(m_hostname.c_str());
#endif

    if (enable_softap && m_softApSsid.length()) {
      startSoftAP();
    }

    if (enable_sta && m_staSsid.length()) {
      startSTA();
    }
  }

 
  // Force SoftAP to be on for at least ms (starts AP if not already).
  void forceSoftAP(uint32_t ms) {
    if (!m_softApActive && enable_softap) {
      startSoftAP();
    }
    m_forceApUntilMs = millis() + ms;
  }

  // Force STA attempt for at least ms (retries begin if we have credentials).
  void forceSTA(uint32_t ms) {
    if (enable_sta && m_staSsid.length()) {
      startSTA();
    }
    m_forceStaUntilMs = millis() + ms;
  }


private:
  void startSoftAP() {
    IPAddress ap_ip(SOFT_AP_IP_OCTETS);
    IPAddress ap_mask(SOFT_AP_MASK_OCTETS);

    // Ensure AP mode is enabled (keep STA if active)
    wifi_mode_t mode = WiFi.getMode();
    if (mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) {
      WiFi.mode(WIFI_MODE_AP);
    }
    if (mode == WIFI_MODE_STA) {
      WiFi.mode(WIFI_MODE_APSTA);
    }

    WiFi.softAPConfig(ap_ip, ap_ip, ap_mask);
    WiFi.softAP(m_softApSsid.c_str(), m_softApPass.c_str());
    m_softApActive = true;

    Serial.printf("SoftAP started: SSID=%s IP=%s\n",
                  m_softApSsid.c_str(),
                  WiFi.softAPIP().toString().c_str());
  }

  void startSTA() {
    wifi_mode_t mode = WiFi.getMode();
    if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) {
      WiFi.mode(WIFI_MODE_STA);
    }
    if (mode == WIFI_MODE_AP) {
      WiFi.mode(WIFI_MODE_APSTA);
    }
    WiFi.begin(m_staSsid.c_str(), m_staPass.c_str());
    Serial.printf("STA start requested: SSID=%s\n", m_staSsid.c_str());
  }

private:
  String m_hostname;
  String m_softApSsid;
  String m_softApPass;
  String m_staSsid;
  String m_staPass;

public:
  // Feature toggles (kept public to mirror original ctor intent)
  bool enable_softap;
  bool enable_sta;
  bool enable_eth;

private:
  bool m_softApActive = false;
  bool m_wifiHasIp    = false;

  uint32_t m_forceApUntilMs  = 0;
  uint32_t m_forceStaUntilMs = 0;
};
