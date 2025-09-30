#include "app_network.h"
#include "NetworkSetup.h"
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "workout_manager.h"
#include "workout_storage.h"


/* Internal singletons */
static AsyncWebServer g_server(80);
static AsyncEventSource g_sse("/events");


namespace AppNetwork
{

static const uint32_t maxJsonSize = 1024 * 8;

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
  // When only SoftAP is active (no STA/Ethernet), redirect to Wi-Fi setup page
  if (g_conn.softApActive() && !connected())
  {
    req->redirect("/wifi");
    return;
  }

  if (LittleFS.exists("/index.html"))
    req->send(LittleFS, "/index.html", "text/html");
  else
    req->send(200, "text/plain", "Upload index.html");
}

static void addCaptivePortalRoutes()
{
  // Simple portal to set WiFi credentials while in SoftAP mode
  g_server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *req)
              {
    String html =
      "<html><body><h1>Configure WiFi</h1>"
      "<form action=\"/save\" method=\"POST\">"
      "SSID: <input type=\"text\" name=\"ssid\"><br>"
      "Password: <input type=\"password\" name=\"password\"><br>"
      "<input type=\"submit\" value=\"Save\">"
      "</form>"
      "<p><a href=\"/index.html\">Open main interface</a></p><p>get w</p>"
      "</body></html>";
    req->send(200, "text/html", html); });

  g_server.on("/save", HTTP_POST, [](AsyncWebServerRequest *req)
              {
    if (req->hasParam("ssid", true) && req->hasParam("password", true)) {
      String ssid = req->getParam("ssid", true)->value();
      String password = req->getParam("password", true)->value();

      setNewWifiCredentials(ssid, password);

      req->send(200, "text/html",
                "<html><body><h1>Saved</h1></body></html>");
      //delay(1000);
      //ESP.restart();
    } else {
      req->send(400, "text/plain", "Missing SSID or password");
    } });

}

void begin()
{
  // Load saved Wi‑Fi credentials (if any) and start connection manager
  applyWifiConfigFromFile();
  g_conn.begin();

  // Ensure SSE handler is present
  g_server.addHandler(&g_sse);

  // Always provide captive portal endpoints
  addCaptivePortalRoutes();

  // Routes owned here (LittleFS should already be mounted by WebUI::begin before calling this)
  g_server.on("/run.html", HTTP_GET, [](AsyncWebServerRequest *req)
              {
                if (LittleFS.exists("/run.html"))
                {
                  req->send(LittleFS, "/run.html", "text/html");
                }
                else
                {
                  req->send(404, "text/plain", "Upload run.html");
                } });

  g_server.on("/status.html", HTTP_GET, [](AsyncWebServerRequest *req)
              {
                if (LittleFS.exists("/status.html"))
                {
                  req->send(LittleFS, "/status.html", "text/html");
                }
                else
                {
                  req->send(404, "text/plain", "Upload status.html");
                } });

  g_server.on("/force-ap.html", HTTP_GET, [](AsyncWebServerRequest *req)
              {
                if (LittleFS.exists("/force-ap.html"))
                {
                  req->send(LittleFS, "/force-ap.html", "text/html");
                }
                else
                {
                  req->send(404, "text/plain", "Upload force-ap.html");
                } });

  g_server.on("/force-sta.html", HTTP_GET, [](AsyncWebServerRequest *req)
              {
                if (LittleFS.exists("/force-sta.html"))
                {
                  req->send(LittleFS, "/force-sta.html", "text/html");
                }
                else
                {
                  req->send(404, "text/plain", "Upload force-sta.html");
                } });

  g_server.on("/", HTTP_GET, serve_index);
  g_server.serveStatic("/static", LittleFS, "/static/");

  // API: list IDs
  g_server.on("/api/workouts", HTTP_GET, [](AsyncWebServerRequest *r)
              {
                StaticJsonDocument<256> d;
                auto arr = d.to<JsonArray>();
                for (auto id : WorkoutStorage::list_ids())
                  arr.add(id);
                String out;
                serializeJson(d, out);
                send_json(r, out); });

  // API: get one
  g_server.on("/api/workout", HTTP_GET, [](AsyncWebServerRequest *r)
              {
                String id;
                if (!require_id(r, id))
                  return;
                Workout w;
                if (!WorkoutStorage::load(id, w))
                {
                  r->send(404);
                  return;
                }
                send_json(r, WorkoutStorage::to_json(w)); });

  // API: save one
  g_server.on("/api/workout", HTTP_POST, [](AsyncWebServerRequest *r)
              { r->send(200); },
              nullptr,
              [](AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total)
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
                if (!WorkoutStorage::from_json(g_buffer, total, w))
                {
                  r->send(400);
                  return;
                }
                WorkoutStorage::save(w);
                send_json(r, WorkoutStorage::to_json(w));
              });

  // API: delete one
  g_server.on("/api/workout", HTTP_DELETE, [](AsyncWebServerRequest *r)
              {
                String id;
                if (!require_id(r, id))
                  return;
                if (!WorkoutStorage::erase(id))
                {
                  r->send(404);
                  return;
                }
                r->send(200); });

  // API: run only the specified workout
  g_server.on("/api/run", HTTP_POST, [](AsyncWebServerRequest *r)
              {
                String id;
                if (!require_id(r, id))
                  return;
                if (!WorkoutManager::run(id))
                {
                  r->send(404, "text/plain", "could not start workout");
                  return;
                }
                r->send(200, "text/plain", "OK"); });

  // API: pause & stop
  g_server.on("/api/pause", HTTP_POST, [](AsyncWebServerRequest *r)
              {
                WorkoutManager::pause();
                r->send(200); });

  g_server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest *r)
              {
                WorkoutManager::stop();
                r->send(200); });

  // API: force SoftAP or STA for N seconds
  g_server.on("/api/force_ap", HTTP_POST, [](AsyncWebServerRequest *r)
              {
                if (!r->hasParam("seconds"))
                {
                  r->send(400, "text/plain", "missing seconds");
                  return;
                }
                uint32_t sec = r->getParam("seconds")->value().toInt();
                uint32_t ms = sec * 1000u;
                g_conn.forceSoftAP(ms);
                r->send(200, "text/plain", "OK"); });

  g_server.on("/api/force_sta", HTTP_POST, [](AsyncWebServerRequest *r)
              {
                if (!r->hasParam("seconds"))
                {
                  r->send(400, "text/plain", "missing seconds");
                  return;
                }
                uint32_t sec = r->getParam("seconds")->value().toInt();
                uint32_t ms = sec * 1000ull;
                g_conn.forceSTA(ms);
                r->send(200, "text/plain", "OK"); });

  // Start server
  g_server.begin();
}

bool connected()
{
  return g_conn.ethHasIp() || g_conn.wifiHasIp();
}

void push_event(const char *e, const char *j)
{
  g_sse.send(j, e);
}


} // namespace AppNetwork
