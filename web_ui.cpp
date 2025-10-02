#include "web_ui.h"
#include "app_network.h"
#include "NetworkSetup.h"
#include <LittleFS.h>

using namespace WebUI;

void WebUI::begin()
{
    // Initialize LittleFS (format if mount fails), but do not abort networking
    bool fsOk = LittleFS.begin(true, "/littlefs", 10, "spiffs");
    if (!fsOk)
    {
        Serial.println("Failed to mount/format LittleFS, continuing without FS");
    }
#ifdef DEBUGCRASH
HEAP_CHECK_HARD();
#endif
    // Bring up networking; all routes and SSE are owned by AppNetwork
    NetworkSetup::begin();
#ifdef DEBUGCRASH
Serial.println("webuibegin");
HEAP_CHECK_HARD();
#endif
}

void WebUI::loop()
{    
  #ifdef DEBUGCRASH
HEAP_CHECK_HARD();
#endif
    NetworkSetup::loop();
  #ifdef DEBUGCRASH
HEAP_CHECK_HARD();
#endif
    //AppNetwork::loop(); async
  #ifdef DEBUGCRASH
HEAP_CHECK_HARD();
#endif
    static uint32_t t0 = millis();
    if (millis() - t0 > 2000)
    {
        t0 = millis();
        push_event("ping", "{}");
    }
}

void WebUI::push_event(const char *e, const char *j)
{
    AppNetwork::push_event(e, j);
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
    AppNetwork::push_event("network", json.c_str());
}
