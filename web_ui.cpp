#include "web_ui.h"
#include "app_network.h"
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

    // Bring up networking; all routes and SSE are owned by AppNetwork
    AppNetwork::begin();
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
    AppNetwork::push_event(e, j);
}

void WebUI::push_network_event(const uint8_t *data, size_t len)
{
    // Convert data bytes to hex string safely (no repeated sprintf, no overflow)
    static char hexStr[256]; // enough for 128 bytes * 2 + 1
    static const char HEX[] = "0123456789ABCDEF";
    const size_t maxBytes = (sizeof(hexStr) - 1) / 2; // number of bytes we can encode
    const size_t n = (len < maxBytes) ? len : maxBytes;

    for (size_t i = 0; i < n; ++i)
    {
        uint8_t b = data[i];
        hexStr[2 * i]     = HEX[b >> 4];
        hexStr[2 * i + 1] = HEX[b & 0x0F];
    }
    hexStr[2 * n] = '\0';

    // Send hex string as JSON string
    String json = String("{\"packet\":\"") + hexStr + "\"}";
    AppNetwork::push_event("network", json.c_str());
}
