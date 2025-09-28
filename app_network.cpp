#include "app_network.h"
#include "NetworkSetup.h"


// Internal singletons
static AsyncWebServer g_server(80);
static AsyncEventSource g_sse("/events");

namespace AppNetwork
{

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
      "<p><a href=\"/index.html\">Open main interface</a></p>"
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

  // If not connected yet, provide a minimal captive portal while SoftAP is active
  if (!(g_conn.ethHasIp() || g_conn.wifiHasIp()))
  {
    addCaptivePortalRoutes();
  }
}

bool connected()
{
  return g_conn.ethHasIp() || g_conn.wifiHasIp();
}

AsyncWebServer &server()
{
  return g_server;
}

AsyncEventSource &events()
{
  return g_sse;
}

} // namespace AppNetwork
