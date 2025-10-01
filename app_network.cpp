// AppNetwork.cpp — synchronous WebServer + manual SSE
// Works on ESP32 (WebServer.h) and ESP8266 (ESP8266WebServer.h).

#include "app_network.h"
#include "NetworkSetup.h"
#include "workout_manager.h"
#include "workout_storage.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#if defined(ESP32)
#include <WiFi.h>
#include <WebServer.h>
static WebServer g_server(80);
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
static ESP8266WebServer g_server(80);
#else
#error "This code targets ESP32/ESP8266 Arduino cores."
#endif

#include <vector>

#define EVENTSTREAM

namespace AppNetwork
{

  // ---------- Config ----------
  static const uint32_t kMaxJsonSize = 8 * 1024;
  static bool g_initialized = false;

  // ---------- SSE support (manual) ----------
  struct SseClient
  {
    WiFiClient client;
    unsigned long lastFlushMs = 0;
    unsigned long lastKeepAliveMs = 0;
  };
  static std::vector<SseClient> g_sseClients;

  // Pending events (made synchronous: no locks, drained in loop)
  struct PendingEvent
  {
    String evt;
    String data;
  };
  static std::vector<PendingEvent> g_pendingEvents;

  // ---------- Helpers ----------
  static bool connected()
  {
    return NetworkSetup::conn().ethHasIp() || NetworkSetup::conn().wifiHasIp();
  }

  static void send_json(const String &js)
  {
    g_server.send(200, "application/json", js);
  }

  static bool require_id(String &id)
  {
    if (!g_server.hasArg("id"))
    {
      g_server.send(400, "text/plain", "missing id");
      return false;
    }
    id = g_server.arg("id");
    return true;
  }

  static String mimeFor(const String &path)
  {
    if (path.endsWith(".html"))
      return "text/html";
    if (path.endsWith(".htm"))
      return "text/html";
    if (path.endsWith(".js"))
      return "application/javascript";
    if (path.endsWith(".css"))
      return "text/css";
    if (path.endsWith(".svg"))
      return "image/svg+xml";
    if (path.endsWith(".png"))
      return "image/png";
    if (path.endsWith(".jpg") || path.endsWith(".jpeg"))
      return "image/jpeg";
    if (path.endsWith(".gif"))
      return "image/gif";
    if (path.endsWith(".json"))
      return "application/json";
    if (path.endsWith(".wasm"))
      return "application/wasm";
    return "text/plain";
  }

  static void streamFileOr404(const char *path, const char *mime = nullptr)
  {
    if (!LittleFS.exists(path))
    {
      g_server.send(404, "text/plain", String("Upload ") + path);
      return;
    }
    File f = LittleFS.open(path, "r");
    g_server.streamFile(f, mime ? mime : mimeFor(path));
    f.close();
  }

  static void serve_index()
  {
    if (NetworkSetup::conn().softApActive() && !connected())
    {
      g_server.sendHeader("Location", String("/wifi"), true);
      g_server.send(302, "text/plain", "");
      return;
    }
    if (LittleFS.exists("/index.html"))
      streamFileOr404("/index.html", "text/html");
    else
      g_server.send(200, "text/plain", "Upload index.html");
  }

  static void addCaptivePortalRoutes()
  {
    g_server.on("/wifi", HTTP_GET, []()
                {
    String html =
      "<html><body><h1>Configure WiFi</h1>"
      "<form action=\"/save\" method=\"POST\">"
      "SSID: <input type=\"text\" name=\"ssid\"><br>"
      "Password: <input type=\"password\" name=\"password\"><br>"
      "<input type=\"submit\" value=\"Save\">"
      "</form>"
      "<p><a href=\"/index.html\">Open main interface</a></p>"
      "</body></html>";
    g_server.send(200, "text/html", html); });

    g_server.on("/save", HTTP_POST, []()
                {
    if (g_server.hasArg("ssid") && g_server.hasArg("password")) {
      String ssid = g_server.arg("ssid");
      String password = g_server.arg("password");
      NetworkSetup::setNewWifiCredentials(ssid, password);
      g_server.send(200, "text/html", "<html><body><h1>Saved</h1></body></html>");
    } else {
      g_server.send(400, "text/plain", "Missing SSID or password");
    } });
  }

  // ---------- SSE: register client ----------
  static void handle_sse()
  {
    // Manually write the full HTTP response to ensure the connection stays open for SSE.
    WiFiClient client = g_server.client();
    if (!client)
      return;

    // Write minimal SSE-compliant headers and keep the connection alive.
    client.print(
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n");

    // Send an initial comment to kickstart the SSE parser on the client
    client.print(": ok\n\n");
    client.flush();

    // Record client
    SseClient sc;
    sc.client = client;
    sc.lastFlushMs = millis();
    sc.lastKeepAliveMs = millis();
    g_sseClients.push_back(std::move(sc));
  }

  // ---------- SSE: broadcast pending events & keepalives ----------
  static void sse_send_event(SseClient &c, const String &evt, const String &data)
  {
    if (!c.client.connected())
      return;
    if (evt.length())
    {
      c.client.print("event: ");
      c.client.println(evt);
    }
    c.client.print("data: ");
    c.client.println(data);
    c.client.print("\n"); // blank line ends the SSE message
  }

  static void sse_tick()
  {
    const unsigned long now = millis();

    // Drop dead clients
    for (int i = (int)g_sseClients.size() - 1; i >= 0; --i)
    {
      if (!g_sseClients[i].client.connected())
      {
        g_sseClients.erase(g_sseClients.begin() + i);
      }
    }

    // Drain pending events
    if (!g_pendingEvents.empty() && !g_sseClients.empty())
    {
      for (const auto &e : g_pendingEvents)
      {
        for (auto &c : g_sseClients)
        {
          if (!c.client.connected())
            continue;
          sse_send_event(c, e.evt, e.data);
          // Light flush pacing
          if (now - c.lastFlushMs > 50)
          {
            c.client.flush();
            c.lastFlushMs = now;
          }
          yield(); // avoid long blocking
        }
      }
      g_pendingEvents.clear();
    }

    // Send keepalive comments every ~15s so proxies don’t time out
    for (auto &c : g_sseClients)
    {
      if (!c.client.connected())
        continue;
      if (now - c.lastKeepAliveMs > 15000UL)
      {
        c.client.print(": keepalive\n\n");
        c.client.flush();
        c.lastKeepAliveMs = now;
      }
    }
  }

  // ---------- Public API (matching your original endpoints) ----------
  void setup()
  {
    // NOTE: Ensure LittleFS.begin() was called earlier in your program.

    // Captive portal routes always available
    addCaptivePortalRoutes();

    // Core pages
    g_server.on("/", HTTP_GET, serve_index);
    g_server.on("/run.html", HTTP_GET, []()
                { streamFileOr404("/run.html", "text/html"); });
    g_server.on("/status.html", HTTP_GET, []()
                { streamFileOr404("/status.html", "text/html"); });
    g_server.on("/force-ap.html", HTTP_GET, []()
                { streamFileOr404("/force-ap.html", "text/html"); });
    g_server.on("/force-sta.html", HTTP_GET, []()
                { streamFileOr404("/force-sta.html", "text/html"); });

    // Static files under /static/*
    g_server.onNotFound([]()
                        {
    String uri = g_server.uri();        // e.g. /static/app.js
    if (uri.startsWith("/static/")) {
      if (LittleFS.exists(uri)) {
        File f = LittleFS.open(uri, "r");
        g_server.streamFile(f, mimeFor(uri));
        f.close();
      } else {
        g_server.send(404, "text/plain", "Not found");
      }
    } else {
      g_server.send(404, "text/plain", "Not found");
    } });

    // ---- JSON APIs ----

    // list IDs
    g_server.on("/api/workouts", HTTP_GET, []()
                {
    StaticJsonDocument<256> d;
    auto arr = d.to<JsonArray>();
    for (auto id : WorkoutStorage::list_ids()) arr.add(id);
    String out; serializeJson(d, out);
    send_json(out); });

    // get one
    g_server.on("/api/workout", HTTP_GET, []()
                {
    String id;
    if (!require_id(id)) return;
    Workout w;
    if (!WorkoutStorage::load(id, w)) { g_server.send(404); return; }
    send_json(WorkoutStorage::to_json(w)); });

    // save one (body is synchronous)
    g_server.on("/api/workout", HTTP_POST, []()
                {
    String body = g_server.arg("plain"); // raw POST body
    if (body.isEmpty() || body.length() > kMaxJsonSize) {
      g_server.send(413, "text/plain", "Too large or empty");
      return;
    }
    Workout w;
    if (!WorkoutStorage::from_json((const uint8_t*)body.c_str(), body.length(), w)) {
      g_server.send(400);
      return;
    }
    WorkoutStorage::save(w);
    send_json(WorkoutStorage::to_json(w)); });

    // delete one
    g_server.on("/api/workout", HTTP_DELETE, []()
                {
    String id;
    if (!require_id(id)) return;
    if (!WorkoutStorage::erase(id)) { g_server.send(404); return; }
    g_server.send(200); });

    // run / pause / stop
    g_server.on("/api/run", HTTP_POST, []()
                {
    String id;
    if (!require_id(id)) return;
    if (!WorkoutManager::run(id)) {
      g_server.send(404, "text/plain", "could not start workout");
      return;
    }
    g_server.send(200, "text/plain", "OK"); });

    g_server.on("/api/pause", HTTP_POST, []()
                { WorkoutManager::pause(); g_server.send(200); });
    g_server.on("/api/stop", HTTP_POST, []()
                { WorkoutManager::stop();  g_server.send(200); });

#ifdef EVENTSTREAM
    // ---- SSE endpoint ----
    g_server.on("/events", HTTP_GET, handle_sse);
#endif
    g_server.on("/api/force_ap", HTTP_POST, []()
                {
    if (!g_server.hasArg("seconds")) {
      g_server.send(400, "text/plain", "missing seconds");
      return;
    }
    uint32_t sec = (uint32_t) g_server.arg("seconds").toInt();
    uint64_t ms = (uint64_t)sec * 1000ULL;
    NetworkSetup::conn().forceSoftAP(ms);
    g_server.send(200, "text/plain", "OK"); });

    g_server.on("/api/force_sta", HTTP_POST, []()
                {
    if (!g_server.hasArg("seconds")) {
      g_server.send(400, "text/plain", "missing seconds");
      return;
    }
    uint32_t sec = (uint32_t) g_server.arg("seconds").toInt();
    uint64_t ms = (uint64_t)sec * 1000ULL;
    NetworkSetup::conn().forceSTA(ms);
    g_server.send(200, "text/plain", "OK"); });

    g_server.begin();
    g_initialized = true;
  }

  bool connectedWrapper()
  { // optional, matches your previous symbol
    return connected();
  }

  // Thread-safe style not needed anymore; this is called from your app code.
  // We buffer the event and drain in loop() to keep everything synchronous.
  void push_event(const char *evt, const char *json)
  {
#ifdef EVENTSTREAM
    PendingEvent e;
    e.evt = evt ? evt : "";
    e.data = json ? json : "";
    g_pendingEvents.push_back(std::move(e));
#endif
  }

  void loop()
  {
    if (!g_initialized)
    {
      if (!connected())
        return; // wait until we have an IP before starting server
      setup();
    }

    // Synchronous pump
    g_server.handleClient();

    // Broadcast any pending events and keep connections alive
    sse_tick();
  }

} // namespace AppNetwork
