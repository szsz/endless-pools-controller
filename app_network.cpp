#include "app_network.h"
#include "NetworkSetup.h"
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "workout_manager.h"
#include "workout_storage.h"
#include "hub75.h"
#include "otapassword.h"


/* Internal singletons */
static AsyncWebServer g_server(80);
static AsyncEventSource g_sse("/events");


namespace AppNetwork
{

static const uint32_t maxJsonSize = 1024 * 8;

static const size_t kPSKLen = 10; // PSK = first 10 chars of OTA_PASSWORD

// Check PSK from header X-PSK, or query/body param "psk"
static bool check_psk(AsyncWebServerRequest* r) {
  String psk;
  if (r->hasHeader("X-PSK")) {
    auto* h = r->getHeader("X-PSK");
    if (h) psk = h->value();
  }
  if (psk.length() == 0) {
    if (r->hasParam("psk")) {
      psk = r->getParam("psk")->value();
    } else if (r->hasParam("psk", true)) {
      psk = r->getParam("psk", true)->value();
    }
  }
  String expected = String(OTA_PASSWORD).substring(0, kPSKLen);
  if (psk != expected) {
    r->send(401, "text/plain", "unauthorized");
    return false;
  }
  return true;
}

// Create intermediate directories for a given absolute path (/dir/sub/file)
static bool ensure_dirs(const String& absPath) {
  int lastSlash = absPath.lastIndexOf('/');
  if (lastSlash <= 0) return true;
  String dir = absPath.substring(0, lastSlash);
  if (dir.length() == 0) return true;
  String partial;
  int start = 0;
  while (start < (int)dir.length()) {
    int idx = dir.indexOf('/', start);
    String chunk = (idx < 0) ? dir.substring(start) : dir.substring(start, idx);
    if (chunk.length() > 0) {
      partial += "/";
      partial += chunk;
      LittleFS.mkdir(partial);
    }
    if (idx < 0) break;
    start = idx + 1;
  }
  return true;
}

// Normalize a user-provided path into absolute path under LittleFS root
static String sanitize_path(String in) {
  in.replace("\\", "/");
  while (in.startsWith("/")) in = in.substring(1);
  in.replace("..", "");
  String out = "/" + in;
  return out;
}

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
  if (ConnectionManager::softApActive())
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

      NetworkSetup::setNewWifiCredentials(ssid, password);

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

  g_server.on("/settings.html", HTTP_GET, [](AsyncWebServerRequest *req)
              {
                if (LittleFS.exists("/settings.html"))
                {
                  req->send(LittleFS, "/settings.html", "text/html");
                }
                else
                {
                  req->send(404, "text/plain", "Upload settings.html");
                } });



  // Settings API: brightness get/set
  g_server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *r)
              {
                StaticJsonDocument<64> d;
                d["brightness"] = HUB75_getBrightnessPercent();
                String out; serializeJson(d, out);
                send_json(r, out); });

  g_server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *r)
              { r->send(200); },
              nullptr,
              [](AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total)
              {
                static uint8_t g_buffer[maxJsonSize];
                if (total > maxJsonSize) { r->send(413, "text/plain", "Too large"); return; }
                memcpy(&g_buffer[index], data, len);
                if (index + len < total) return;
                StaticJsonDocument<128> d;
                DeserializationError err = deserializeJson(d, g_buffer, total);
                if (err) { r->send(400, "text/plain", "Bad JSON"); return; }
                int b = d["brightness"] | -1;
                if (b < 0 || b > 100) { r->send(400, "text/plain", "brightness 0..100"); return; }
                HUB75_setBrightnessPercent((uint8_t)b);
                StaticJsonDocument<64> resp;
                resp["brightness"] = HUB75_getBrightnessPercent();
                String out; serializeJson(resp, out);
                send_json(r, out);
              });

  // Raw body upload API: POST /api/upload?path=relative/sub/file [&psk=...]
  // Body is the file bytes; supports subfolders.
  g_server.on("/api/upload", HTTP_POST, [](AsyncWebServerRequest *r)
              { /* response sent in body handler */ },
              nullptr,
              [](AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total)
              {
                if (!check_psk(r)) return;
                if (!r->hasParam("path")) { r->send(400, "text/plain", "missing path"); return; }
                String rel = r->getParam("path")->value();
                String abs = sanitize_path(rel);
                if (index == 0) {
                  ensure_dirs(abs);
                  File f = LittleFS.open(abs, "w");
                  if (!f) { r->send(500, "text/plain", "open failed"); return; }
                  size_t w = f.write(data, len);
                  f.close();
                  if (w != len) { r->send(500, "text/plain", "write failed"); return; }
                } else {
                  File f = LittleFS.open(abs, "a");
                  if (!f) { r->send(500, "text/plain", "append open failed"); return; }
                  size_t w = f.write(data, len);
                  f.close();
                  if (w != len) { r->send(500, "text/plain", "append failed"); return; }
                }
                if (index + len >= total) {
                  StaticJsonDocument<128> d;
                  d["path"] = rel;
                  d["size"] = (uint32_t)total;
                  String out; serializeJson(d, out);
                  send_json(r, out);
                }
              });

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



  // Start server
  g_server.begin();
}

bool connected()
{
  return ConnectionManager::ethHasIp() || ConnectionManager::wifiHasIp();
}

void push_event(const char *e, const char *j)
{
  g_sse.send(j, e);
}


} // namespace AppNetwork
