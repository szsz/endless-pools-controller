#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <ETH.h>
#include <SPI.h>
#include <vector>
#include <functional>
#include <ESPmDNS.h>

// ---------- Ethernet (W5500 over SPI) defaults ----------
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

// Event callback signature: notifies whenever ETH/WIFI connection status changes.
// Parameters are: (ethHasIp, wifiHasIp).
using ConnectionChangeCallback = std::function<void(bool, bool, bool)>; // (ethHasIp, wifiHasIp, softApActive)

class ConnectionManager
{
public:
  ConnectionManager(const char *hostname,
                    const char *softApSsid,
                    const char *softApPass,
                    bool p_enable_softap = false,
                    bool p_enable_sta = false,
                    bool p_enable_eth = true)
      : m_hostname(hostname),
        m_softApSsid(softApSsid),
        m_softApPass(softApPass),
        m_staSsid(""),
        m_staPass(""),
        enable_softap(p_enable_softap),
        enable_sta(p_enable_sta),
        enable_eth(p_enable_eth) {}

  // Bring up SoftAP, attempt WiFi STA, and start Ethernet (W5500).
  // Also attaches to Network.onEvent to track link/IP changes and notify subscribers.
  void begin()
  {
    s_instance = this;

    // Subscribe to Arduino network events first, so we don't miss early events
    Network.onEvent(ConnectionManager::onArduinoEvent);
    // Apply hostname once for all interfaces
    setHostname(m_hostname.c_str());

    // Start Ethernet (DHCP by default)
    if (enable_eth)
      startEthernet();

    // Initialize mDNS once (no dynamic rebinding). Only if hostname provided.
    /*if (m_hostname.length())
    {
      if (MDNS.begin(m_hostname.c_str()))
      {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("mDNS active at %s.local\n", m_hostname.c_str());
      }
      else
      {
        Serial.println("mDNS begin failed");
      }
    }*/
  }

  // Subscribe to connection change events.
  void subscribe(ConnectionChangeCallback cb)
  {
    m_subscribers.push_back(cb);
  }
 // Force SoftAP even if STA is connected. After durationMs, attempt to reconnect STA,
  // and revert to normal policy (SoftAP off when STA has IP).
  void forceSoftAP(uint32_t durationMs)
  {
    m_forceApUntil = millis() + durationMs;
  }

  void forceSTA(uint32_t durationMs)
  {
    m_forceSTAUntil = millis() + durationMs;
  }

  // Set Wi-Fi STA credentials and optionally attempt immediate connection.
  // Simplified: non-blocking, rely on WIFI_STA_GOT_IP/DISCONNECTED events to update state.
  void setWifiStaCredentials(const char *ssid, const char *pass, uint32_t timeoutMs = 8000)
  {
    if (!ssid || !ssid[0])
    {
      Serial.println("setWifiStaCredentials: invalid SSID");
      return;
    }
    m_staSsid = String(ssid);
    m_staPass = pass ? String(pass) : String();
    // Temporarily suspend Wi-Fi disable policy so it attempts to connect with new credentials for 30s
    forceSTA(30000);
  }

 


  // Periodic processing: enforce SoftAP policy and handle force timeout.
  void loop()
  {
    // Enforce policy on every loop iteration
    ensureApState();
  }

  // Status properties
  bool ethHasIp() const { return m_ethHasIp; }
  bool wifiHasIp() const { return m_wifiHasIp; }
  bool softApActive() const { return m_softApActive; }

  const String &hostname() const { return m_hostname; }
  // Set global hostname and apply to all network interfaces (STA, SoftAP, ETH).
  void setHostname(const char *hostname)
  {
    if (hostname && hostname[0])
    {
      m_hostname = String(hostname);
    }
    else
    {
      m_hostname = String();
      return;
    }
    if (m_hostname.length())
    {
      WiFi.setHostname(m_hostname.c_str());
      WiFi.softAPsetHostname(m_hostname.c_str());
      ETH.setHostname(m_hostname.c_str());
    }
  }
  void printWifiMode() const
  {
    wifi_mode_t m = WiFi.getMode();
    const char *name = "UNKNOWN";
    switch (m)
    {
    case WIFI_MODE_NULL:
      name = "WIFI_MODE_NULL (Radio Off)";
      break;
    case WIFI_MODE_STA:
      name = "WIFI_MODE_STA (Station)";
      break;
    case WIFI_MODE_AP:
      name = "WIFI_MODE_AP (SoftAP)";
      break;
    case WIFI_MODE_APSTA:
      name = "WIFI_MODE_APSTA (Station+AP)";
      break;
    default:
      break;
    }
    Serial.printf("WiFi Mode: %s (%d)\n", name, (int)m);
  }

private:
  // Singleton pointer to forward Network events
  static ConnectionManager *s_instance;

  // Internal state
  String m_hostname;
  String m_softApSsid;
  String m_softApPass;
  bool m_ethHasIp = false;
  bool m_wifiHasIp = false;
  bool m_softApActive = false;

  // Optional explicit STA credentials provided by user
  String m_staSsid;
  String m_staPass;

  std::vector<ConnectionChangeCallback> m_subscribers;

  uint32_t m_forceApUntil = 0;
  uint32_t m_forceSTAUntil = 0;

  bool enable_softap = false;
  bool enable_sta = false;
  bool enable_eth = false;

  bool need_eth() { return enable_eth; }
  bool need_sta()
  {
    if (enable_eth && millis() < 5000)
      return false;
      
    bool m_forceSTA = m_forceSTAUntil && (m_forceSTAUntil > millis());
    if (!m_forceSTA)
      m_forceSTAUntil = 0;
    return enable_sta && (!m_ethHasIp || m_forceSTA);
  }
  bool need_softap()
  {
    bool m_forceAp = m_forceApUntil && (m_forceApUntil > millis());
    if (!m_forceAp)
      m_forceApUntil = 0;

    return enable_softap && ((!m_ethHasIp && !m_wifiHasIp) || m_forceAp);
  }

  // Helpers
  bool isStaEnabled() const
  {
    wifi_mode_t m = WiFi.getMode();
    return (m == WIFI_MODE_STA || m == WIFI_MODE_APSTA);
  }
  bool isApEnabled() const
  {
    wifi_mode_t m = WiFi.getMode();
    return (m == WIFI_MODE_AP || m == WIFI_MODE_APSTA);
  }



  void notify()
  {
    Serial.printf("Notify: ETH=%s, WIFI_STA=%s, SoftAP=%s\n",
                  m_ethHasIp ? "IP" : "NO-IP",
                  m_wifiHasIp ? "IP" : "NO-IP",
                  m_softApActive ? "ON" : "OFF");
    for (auto &cb : m_subscribers)
    {
      cb(m_ethHasIp, m_wifiHasIp, m_softApActive);
    }
    Serial.printf("test");
  }

  void setEthHasIp(bool v)
  {
    if (m_ethHasIp != v)
    {
      m_ethHasIp = v;
      notify();
    }
  }

  void setWifiHasIp(bool v)
  {
    if (m_wifiHasIp != v)
    {
      m_wifiHasIp = v;
      notify();
    }
  }

  void setSoftApActive(bool v)
  {
    if (m_softApActive != v)
    {
      m_softApActive = v;
      notify();
    }
  }

  void startSoftAP()
  {
    if (!(WiFi.getMode() & WIFI_AP))
      {
        IPAddress ap_ip(SOFT_AP_IP_OCTETS);
        IPAddress ap_mask(SOFT_AP_MASK_OCTETS);

        WiFi.enableAP(true);
        WiFi.softAPConfig(ap_ip, ap_ip, ap_mask);
        WiFi.softAP(m_softApSsid.c_str(), m_softApPass.c_str());
        setSoftApActive(true);
        Serial.printf("SoftAP up: SSID=%s IP=%s\n",
                      m_softApSsid.c_str(), WiFi.softAPIP().toString().c_str());
        printWifiMode();
      }
  }

  void stopSoftAP()
  {
    if (WiFi.getMode() & WIFI_AP)
      {
        Serial.println("Disabling SoftAP...");
        WiFi.softAPdisconnect(false);
        WiFi.enableAP(false);  
        Serial.println("SoftAP disabled!");
        printWifiMode();
      }
  }

    void startSTA()
  {
    if (!(WiFi.getMode() & WIFI_STA))
      {
        WiFi.enableSTA(true);
        WiFi.begin(m_staSsid.c_str(), m_staPass.c_str());
        Serial.printf("SoftAP up: SSID=%s IP=%s\n",
                      m_softApSsid.c_str(), WiFi.softAPIP().toString().c_str());
        printWifiMode();
      }
  }

  void stopSTA()
  {
    if (WiFi.getMode() & WIFI_STA)
      {
        Serial.println("Disabling STA...");
        WiFi.disconnect(false); 
        WiFi.enableSTA(false); 
        Serial.println("STA disabled!");
        printWifiMode();
      }
  }

 

  void startEthernet()
  {
    SPI.begin(ETH_SPI_SCK, ETH_SPI_MISO, ETH_SPI_MOSI);
    ETH.begin(ETH_TYPE, ETH_ADDR, ETH_CS, ETH_IRQ, ETH_RST, SPI);
  }


  void handleEthDown(const char *msg)
  {
    setEthHasIp(false);
    Serial.println(msg);
    // Re-enable Wi-Fi and attempt STA using credentials from /wifi_config.json
  }


  // Enforce Wi-Fi policy:
  // - If ETH has IP: disable Wi-Fi entirely (WIFI_MODE_NULL),
  //   unless SoftAP is explicitly forced via forceSoftAP(), in which case run AP-only.
  // - If ETH does not have IP: normal policy (SoftAP only when STA is not connected).
  void ensureApState()
  {
    if (need_sta())
    {
      startSTA();
    }
    else
    {
      stopSTA();
    }
    if (need_softap())
    {
      startSoftAP();
    }
    else
    {
      stopSoftAP();
    }
  }


  // Static callback adapter
  static void onArduinoEvent(arduino_event_id_t event, arduino_event_info_t info)
  {
    if (!s_instance)
      return;

    switch (event)
    {
    // -------- Ethernet events --------
    case ARDUINO_EVENT_ETH_START:
      Serial.println("ETH Started");
      break;

    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;

    case ARDUINO_EVENT_ETH_GOT_IP:
      s_instance->setEthHasIp(true);
      Serial.printf("ETH Got IP: %s\n", ETH.localIP().toString().c_str());
      // With Ethernet up, enforce Wi-Fi disable (unless forced AP)
      break;

    case ARDUINO_EVENT_ETH_LOST_IP:
      s_instance->handleEthDown("ETH Lost IP");
      break;

    case ARDUINO_EVENT_ETH_DISCONNECTED:
      s_instance->handleEthDown("ETH Disconnected");
      break;

    case ARDUINO_EVENT_ETH_STOP:
      s_instance->handleEthDown("ETH Stopped");
      break;

    // -------- Wi-Fi AP events --------
    case ARDUINO_EVENT_WIFI_AP_START:
      Serial.println("AP Started");
      s_instance->setSoftApActive(true);
      break;

    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      Serial.println("AP STA Connected");
      break;

    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      Serial.println("AP STA Disconnected");
      break;

    case ARDUINO_EVENT_WIFI_AP_STOP:
      Serial.println("AP Stopped");
      s_instance->setSoftApActive(false);
      break;

    // -------- Wi-Fi STA events --------
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      s_instance->setWifiHasIp(true);
      Serial.printf("Wi-Fi STA GOT IP: %s\n", WiFi.localIP().toString().c_str());
      // After a successful STA connection, resume normal policy:
      // allow Ethernet to disable Wi-Fi again if applicable.
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      s_instance->setWifiHasIp(false);
      Serial.println("Wi-Fi STA Disconnected");
      break;

    default:
      break;
    }
  }
};

// Define the static member
inline ConnectionManager *ConnectionManager::s_instance = nullptr;
