#include "web_ui.h"
#include "workout_manager.h"
#include "workout_storage.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ETH.h>
#include <SPI.h>
#include "driver/spi_master.h"


using namespace WebUI;
using namespace WorkoutStorage;

static AsyncWebServer server(80);
static AsyncEventSource sse("/events");

/*************** W5500 (Ethernet) Pins & MAC ***************/
#define W5500_CS    14  // Chip Select
#define W5500_RST    9  // Reset (optional but recommended)
#define W5500_INT   10  // Interrupt (unused by this sketch)
#define W5500_MISO  12
#define W5500_MOSI  11
#define W5500_SCK   13
#define ETH_SPI_HOST SPI2_HOST

static void makeEthMac(uint8_t mac[6]) {
    uint64_t base = ESP.getEfuseMac();
    mac[0] = 0x02; // locally administered
    mac[1] = (base >> 32) & 0xFF;
    mac[2] = (base >> 24) & 0xFF;
    mac[3] = (base >> 16) & 0xFF;
    mac[4] = (base >> 8) & 0xFF;
    mac[5] = base & 0xFF;
}

static bool startEthernet() {
    // Reset W5500
    pinMode(W5500_RST, OUTPUT);
    digitalWrite(W5500_RST, LOW);
    delay(10);
    digitalWrite(W5500_RST, HIGH);
    delay(100);

    // Start SPI with provided pins
    SPI.begin(W5500_SCK, W5500_MISO, W5500_MOSI);

    // Bring up W5500 via Arduino-ESP32 ETH driver (SPI W5500)
    ETH.setHostname("swimmachine");
    bool ok = ETH.begin(ETH_PHY_W5500, W5500_CS, W5500_INT, W5500_RST, ETH_SPI_HOST, W5500_SCK, W5500_MISO, W5500_MOSI);
    if (!ok) {
        Serial.println("ETH.begin(W5500) failed");
        return false;
    }


    // Wait for link + DHCP
    uint32_t t0 = millis();
    while (millis() - t0 < 10000) { // 10s
        if (ETH.linkUp()) {
            IPAddress ip = ETH.localIP();
            if (ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0) {
                return true;
            }
        }
        delay(100);
    }
    Serial.println("Ethernet link/DHCP timeout");
    return false;
}


String storedSSID;
String storedPassword;

const uint32_t maxJsonSize = 1024 * 8;

// helper: send JSON
static void send_json(AsyncWebServerRequest *r, const String &js)
{
    r->send(200, "application/json", js);
}

// helper: require ?id=
static bool require_id(AsyncWebServerRequest *r, String &id)
{
    if (!r->hasParam("id"))
    {
        r->send(400, "text/plain", "missing id");
        return false;
    }
    id = r->getParam("id")->value();
    return true;
}

static void serve_index(AsyncWebServerRequest *req)
{
    if (LittleFS.exists("/index.html"))
        req->send(LittleFS, "/index.html", "text/html");
    else
        req->send(200, "text/plain", "Upload index.html");
}

void WebUI::begin()
{
    // Initialize LittleFS (format if mount fails), but do not abort networking
    bool fsOk = LittleFS.begin(true, "/littlefs", 10, "spiffs");
    if (!fsOk)
    {
        Serial.println("Failed to mount/format LittleFS, continuing without FS");
    }

    // Read stored WiFi credentials from LittleFS file
    storedSSID = "";
    storedPassword = "";
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

    bool ethReady = startEthernet();

    if (ethReady) {
        Serial.printf("\nEthernet connected, IP = %s\n", ETH.localIP().toString().c_str());
        // Start mDNS responder for hostname.local
        if (MDNS.begin("swimmachine")) {
            Serial.println("mDNS responder started: http://swimmachine.local");
        } else {
            Serial.println("Error setting up mDNS responder!");
        }
        // Disable WiFi to route all traffic through Ethernet
        WiFi.mode(WIFI_OFF);
    }

    if (!ethReady) {
        WiFi.mode(WIFI_STA);
    WiFi.setHostname("swimmachine"); // Set the device hostname

    // Try to connect to stored WiFi credentials if available, else default from file
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

        // Start mDNS responder for hostname.local
        if (MDNS.begin("swimmachine"))
        {
            Serial.println("mDNS responder started: http://swimmachine.local");
        }
        else
        {
            Serial.println("Error setting up mDNS responder!");
        }
    }
    else
    {
        Serial.println("\nFailed to connect to WiFi, starting AP mode for configuration");

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

        // Serve a simple portal to set WiFi credentials
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *req)
                  {
            String html = "<html><body><h1>Configure WiFi</h1><form action=\"/save\" method=\"POST\">"
                          "SSID: <input type=\"text\" name=\"ssid\"><br>"
                          "Password: <input type=\"password\" name=\"password\"><br>"
                          "<input type=\"submit\" value=\"Save\">"
                          "</form></body></html>";
            req->send(200, "text/html", html);
        });

        server.on("/save", HTTP_POST, [](AsyncWebServerRequest *req)
                  {
            if (req->hasParam("ssid", true) && req->hasParam("password", true))
            {
                String ssid = req->getParam("ssid", true)->value();
                String password = req->getParam("password", true)->value();

                StaticJsonDocument<256> doc;
                doc["ssid"] = ssid;
                doc["password"] = password;

                File configFile = LittleFS.open("/wifi_config.json", "w");
                if (configFile)
                {
                    serializeJson(doc, configFile);
                    configFile.close();
                    req->send(200, "text/html", "<html><body><h1>Saved. Rebooting...</h1></body></html>");
                    delay(1000);
                    ESP.restart();
                    return;
                }
                else
                {
                    req->send(500, "text/plain", "Failed to save config");
                    return;
                }
            }
            else
            {
                req->send(400, "text/plain", "Missing SSID or password");
                return;
            }
        });

        // Redirect all other requests to the captive portal root
        server.onNotFound([](AsyncWebServerRequest *req)
                         {
            req->redirect("/");
        });

        server.begin();
    }

    }
    
    // routes for normal operation (only if connected)
    if (ethReady || WiFi.status() == WL_CONNECTED)
    {
        server.on("/run.html", HTTP_GET, [](AsyncWebServerRequest *req)
                  {
            if (LittleFS.exists("/run.html"))
            {
                req->send(LittleFS, "/run.html", "text/html");
            }
            else
            {
                req->send(404, "text/plain", "Upload run.html");
            }
        });

        server.on("/status.html", HTTP_GET, [](AsyncWebServerRequest *req)
                  {
            if (LittleFS.exists("/status.html"))
            {
                req->send(LittleFS, "/status.html", "text/html");
            }
            else
            {
                req->send(404, "text/plain", "Upload status.html");
            }
        });

        server.on("/", HTTP_GET, serve_index);
        server.serveStatic("/static", LittleFS, "/static/");

        // list IDs
        server.on("/api/workouts", HTTP_GET, [](AsyncWebServerRequest *r)
                  {
            StaticJsonDocument<256> d;
            auto arr = d.to<JsonArray>();
            for (auto id : list_ids())
                arr.add(id);
            String out;
            serializeJson(d, out);
            send_json(r, out);
        });

        // get one
        server.on("/api/workout", HTTP_GET, [](AsyncWebServerRequest *r)
                  {
            String id;
            if (!require_id(r, id))
                return;
            Workout w;
            if (!load(id, w))
            {
                r->send(404);
                return;
            }
            send_json(r, to_json(w));
        });

        // save one
        server.on("/api/workout", HTTP_POST, [](AsyncWebServerRequest *r)
                  { r->send(200); }, nullptr, [](AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total)
                  {
            static uint8_t g_buffer[maxJsonSize]; // for reading Json objects
            if (total > maxJsonSize)
            {
                r->send(413, "text/plain", "Too large");
                return;
            } // 413 Payload Too Large
            memcpy(&g_buffer[index], data, len);
            if (index + len < total) // more coming – return now
                return;
            Workout w;
            if (!from_json(g_buffer, total, w))
            {
                r->send(400);
                return;
            }
            save(w);
            send_json(r, to_json(w));
        });

        // delete one
        server.on("/api/workout", HTTP_DELETE, [](AsyncWebServerRequest *r)
                  {
            String id;
            if (!require_id(r, id))
                return;
            if (!erase(id))
            {
                r->send(404);
                return;
            }
            r->send(200);
        });

        // **UPDATED**: run only the specified workout
        server.on("/api/run", HTTP_POST, [](AsyncWebServerRequest *r)
                  {
            String id;
            if (!require_id(r, id))
                return;
            if (!WorkoutManager::run(id))
            {
                r->send(404, "text/plain", "could not start workout");
                return;
            }
            r->send(200, "text/plain", "OK");
        });

        // pause & stop unchanged
        server.on("/api/pause", HTTP_POST, [](AsyncWebServerRequest *r)
                  {
            WorkoutManager::pause();
            r->send(200);
        });
        server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest *r)
                  {
            WorkoutManager::stop();
            r->send(200);
        });

        // SSE
        server.addHandler(&sse);
        server.begin();
    }
}


void WebUI::loop()
{
    static uint32_t t0 = millis();
    if (millis() - t0 > 2000)
    {
        t0 = millis();
        push_event("ping", "{}");
        push_event("ping", "{}");
    }
}

void WebUI::push_event(const char *e, const char *j)
{
    sse.send(j, e);
}

void WebUI::push_network_event(const uint8_t *data, size_t len)
{
    // Convert data bytes to hex string
    static char hexStr[256]; // enough for 128 bytes * 2 + 1
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 2 < sizeof(hexStr); i++)
    {
        sprintf(&hexStr[pos], "%02X", data[i]);
        pos += 2;
    }
    hexStr[pos] = '\0';

    // Send hex string as JSON string
    String json = String("{\"packet\":\"") + hexStr + "\"}";

    sse.send(json.c_str(), "network");
}
