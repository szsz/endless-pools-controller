#include "web_ui.h"
#include "workout_manager.h"
#include "workout_storage.h"
#include "network.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

using namespace WebUI;
using namespace WorkoutStorage;

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

    // Bring up networking (WiFi/Ethernet/mDNS/AP portal handled inside Network)
    Network::begin();

    // Register routes for normal operation only if connected in STA/Ethernet mode.
    if (Network::connected())
    {
        auto &server = Network::server();

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

        // run only the specified workout
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
        server.addHandler(&Network::events());
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
    }
}

void WebUI::push_event(const char *e, const char *j)
{
    Network::events().send(j, e);
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

    Network::events().send(json.c_str(), "network");
}
