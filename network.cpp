#include "network.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

 #include <ESPmDNS.h>

// Internal singletons
static AsyncWebServer g_server(80);
static AsyncEventSource g_sse("/events");

namespace Network
{

static void setupMDNS()
{
  if (MDNS.begin("swimmachine"))
  {
    Serial.println("mDNS responder started: http://swimmachine.local");
  }
  else
  {
    Serial.println("Error setting up mDNS responder!");
  }
}

#ifdef ETHERNET
// Track Ethernet readiness
static bool s_ethReady = false;

/*************** W5500 (Ethernet) Pins & MAC ***************/
#define W5500_CS 14  // Chip Select
#define W5500_RST 9  // Reset (optional but recommended)
#define W5500_INT 10 // Interrupt (unused by this sketch)
#define W5500_MISO 12
#define W5500_MOSI 11
#define W5500_SCK 13

static byte s_mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};


static void makeEthMac(uint8_t mac[6])
{
  uint64_t base = ESP.getEfuseMac();
  mac[0] = 0x02; // locally administered
  mac[1] = (base >> 32) & 0xFF;
  mac[2] = (base >> 24) & 0xFF;
  mac[3] = (base >> 16) & 0xFF;
  mac[4] = (base >> 8) & 0xFF;
  mac[5] = base & 0xFF;
}

static bool startEthernet()
{
  // Bring up SPI and W5500
  SPI.begin(W5500_SCK, W5500_MISO, W5500_MOSI);
  Ethernet.init(W5500_CS);

  if (W5500_RST >= 0)
  {
    pinMode(W5500_RST, OUTPUT);
    digitalWrite(W5500_RST, LOW);
    delay(10);
    digitalWrite(W5500_RST, HIGH);
    delay(50);
  }

  // DHCP first, fallback to static if it fails
  if (Ethernet.begin(s_mac) == 0)
  {
    return false;
  }

  Serial.print("W5500 up. IP: ");
  Serial.println(Ethernet.localIP());
  return true;
}

#else // !ETHERNET



static void startCaptivePortal()
{
  // Start Access Point for configuration portal with custom IP only if captive portal is necessary
  WiFi.mode(WIFI_AP);

  IPAddress local_IP(192, 168, 0, 3);
  IPAddress gateway(192, 168, 0, 3);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);

  WiFi.softAP("SwimMachine_Config");

  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);

  setupMDNS();

  // Serve a simple portal to set WiFi credentials
  g_server.on("/", HTTP_GET, [](AsyncWebServerRequest *req)
              {
    String html = "<html><body><h1>Configure WiFi</h1><form action=\"/save\" method=\"POST\">"
                  "SSID: <input type=\"text\" name=\"ssid\"><br>"
                  "Password: <input type=\"password\" name=\"password\"><br>"
                  "<input type=\"submit\" value=\"Save\">"
                  "</form></body></html>";
    req->send(200, "text/html", html); });

  g_server.on("/save", HTTP_POST, [](AsyncWebServerRequest *req)
              {
    if (req->hasParam("ssid", true) && req->hasParam("password", true)) {
      String ssid = req->getParam("ssid", true)->value();
      String password = req->getParam("password", true)->value();

      StaticJsonDocument<256> doc;
      doc["ssid"] = ssid;
      doc["password"] = password;

      File configFile = LittleFS.open("/wifi_config.json", "w");
      if (configFile) {
        serializeJson(doc, configFile);
        configFile.close();
        req->send(200, "text/html", "<html><body><h1>Saved. Rebooting...</h1></body></html>");
        delay(1000);
        ESP.restart();
        return;
      } else {
        req->send(500, "text/plain", "Failed to save config");
        return;
      }
    } else {
      req->send(400, "text/plain", "Missing SSID or password");
      return;
    } });

  // Redirect all other requests to the captive portal root
  g_server.onNotFound([](AsyncWebServerRequest *req)
                      { req->redirect("/"); });

  // Start server now in AP mode (WebUI can still add more handlers later if needed)
  g_server.begin();
}

static void setupWiFiOrAP()
{
  // Read stored WiFi credentials from LittleFS file
  String storedSSID;
  String storedPassword;
  if (LittleFS.exists("/wifi_config.json"))
  {
    File configFile = LittleFS.open("/wifi_config.json", "r");
    if (configFile)
    {
      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, configFile);
      if (!error)
      {
        storedSSID = doc["ssid"].as<String>();
        storedPassword = doc["password"].as<String>();
      }
      configFile.close();
    }
  }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("swimmachine"); // Set the device hostname

  // Try to connect to stored WiFi credentials if available
  if (storedSSID.length() > 0)
  {
    Serial.printf("Connecting to stored WiFi SSID: %s\n", storedSSID.c_str());
    WiFi.begin(storedSSID.c_str(), storedPassword.c_str());
  }

  uint32_t startAttemptTime = millis();
  const uint32_t wifiTimeout = 15000; // 15 seconds timeout

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < wifiTimeout)
  {
    delay(250);
    Serial.print('.');
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.printf("\nConnected, IP = %s\n", WiFi.localIP().toString().c_str());
    setupMDNS();
    // Note: Do not start the server here; WebUI will add its routes then call begin().
  }
  else
  {
    Serial.println("\nFailed to connect to WiFi, starting AP mode for configuration");
    startCaptivePortal();
  }
}

#endif // ETHERNET

void begin()
{
#ifdef ETHERNET
  // Start Ethernet; if it fails, there is no WiFi fallback when ETHERNET is defined.
  // If you want fallback, build without ETHERNET.
  delay(1000);
  // makeEthMac(s_mac); // optional, using fixed s_mac for now
  s_ethReady = startEthernet();

  if (s_ethReady)
  {
    setupMDNS(); // Start mDNS responder for hostname.local
  }
  else
  {
    Serial.println("Ethernet failed to start");
  }
#else
  // WiFi path
  setupWiFiOrAP();
#endif
}

bool connected()
{
#ifdef ETHERNET
  return s_ethReady;
#else
  return WiFi.status() == WL_CONNECTED;
#endif
}

AsyncWebServer &server()
{
  return g_server;
}

AsyncEventSource &events()
{
  return g_sse;
}



} // namespace Network
