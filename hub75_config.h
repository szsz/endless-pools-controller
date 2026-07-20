#pragma once
// Master switch for the HUB75 LED matrix panel.
//
// Default is ON to preserve behavior for the panel-equipped board. Build with
// -DHUB75_ENABLE=0 to produce firmware for a board that has no HUB75 panel:
// the ESP32-HUB75-MatrixPanel-I2S-DMA library is not required and all panel
// code is compiled out to no-op stubs (brightness/screensaver values are still
// stored so the web API keeps working).
#ifndef HUB75_ENABLE
#define HUB75_ENABLE 1
#endif
