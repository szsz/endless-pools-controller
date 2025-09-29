#include <Arduino.h>
#if __has_include(<ESP32-HUB75-MatrixPanel-I2S-DMA.h>)
  #include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
  #define HUB75_AVAILABLE 1
#else
  #define HUB75_AVAILABLE 0
#endif
#include "hub75.h"

/*************** HUB75 Panel Config ***************/
static const int PANEL_WIDTH  = 64;
static const int PANEL_HEIGHT = 64;
static const int PANEL_CHAIN  = 1;   // number of chained panels

// User-provided pinout
static const int PIN_R1  = 33;
static const int PIN_G1  = 34;
static const int PIN_B1  = 35;
static const int PIN_R2  = 36;
static const int PIN_G2  = 37;
static const int PIN_B2  = 38;

static const int PIN_A   = 40;
static const int PIN_B   = 41;
static const int PIN_C   = 42;
static const int PIN_D   = 45;
static const int PIN_E   = 39;

static const int PIN_CLK = 46;
static const int PIN_LAT = 47;
static const int PIN_OE  = 48;

/*************** State ***************/
#if HUB75_AVAILABLE
static MatrixPanel_I2S_DMA *dma_display = nullptr;
#endif
static String currentText = "Hello, HUB75!";

/*************** Implementation ***************/
void drawCenteredText(const String &msg) {
#if HUB75_AVAILABLE
  if (!dma_display) return;

  dma_display->fillScreen(0);
  dma_display->setTextWrap(false);
  dma_display->setTextColor(dma_display->color565(128, 128, 128));

  // Using built-in 6x8 font: rough centering
  const int charW = 6;
  const int charH = 8;
  int textW = msg.length() * charW;
  int x = (PANEL_WIDTH - textW) / 2;
  if (x < 0) x = 0;
  int y = (PANEL_HEIGHT - charH) / 2;
  if (y < 0) y = 0;

  dma_display->setCursor(x, y);
  dma_display->print(msg);
#else
  (void)msg;
#endif
}

void drawHorizontalTestLines() {
#if HUB75_AVAILABLE
  if (!dma_display) return;
  dma_display->fillScreen(0);
  for (int y = 0; y < PANEL_HEIGHT; y += 4) {
    uint8_t r = (y * 4) & 0xFF;
    uint8_t g = (255 - (y * 2)) & 0xFF;
    uint8_t b = (y * 3) & 0xFF;
    uint16_t color = dma_display->color565(r, g, b);
    // 1-2px thick lines for visibility
    dma_display->drawFastHLine(0, y, PANEL_WIDTH, color);
    if (y + 1 < PANEL_HEIGHT)
      dma_display->drawFastHLine(0, y + 1, PANEL_WIDTH, color);
  }
#endif
}

void setupHUB75() {
#if HUB75_AVAILABLE
  HUB75_I2S_CFG mxconfig(PANEL_WIDTH, PANEL_HEIGHT, PANEL_CHAIN);
  mxconfig.gpio.r1 = PIN_R1;  mxconfig.gpio.g1 = PIN_G1;  mxconfig.gpio.b1 = PIN_B1;
  mxconfig.gpio.r2 = PIN_R2;  mxconfig.gpio.g2 = PIN_G2;  mxconfig.gpio.b2 = PIN_B2;
  mxconfig.gpio.a = PIN_A;    mxconfig.gpio.b = PIN_B;    mxconfig.gpio.c = PIN_C;
  mxconfig.gpio.d = PIN_D;    mxconfig.gpio.e = PIN_E;
  mxconfig.gpio.clk = PIN_CLK; mxconfig.gpio.lat = PIN_LAT; mxconfig.gpio.oe = PIN_OE;

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(50); // 0..255
  dma_display->clearScreen();
  drawCenteredText(currentText);
#else
  // No hardware library available; nothing to initialize
#endif
}

String getCurrentText() {
  return currentText;
}

void setCurrentText(const String &msg) {
  if (msg == currentText) return; // avoid unnecessary redraws
  currentText = msg;
  drawCenteredText(currentText);
}
