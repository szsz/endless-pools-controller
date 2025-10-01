#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Fonts/TomThumb.h>
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
static const int PIN_G2  = 15;
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
static MatrixPanel_I2S_DMA *dma_display = nullptr;


void setupHUB75() {
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
}




void printJSon(DynamicJsonDocument doc)
{
  if (!dma_display) return;

  // Colors
  const ColorRGB DARK_ORANGE{255, 140, 0};
  const ColorRGB RED{255, 0, 0};
  const ColorRGB GREEN{25, 200, 25};

  auto color565 = [&](const ColorRGB &c) -> uint16_t {
    return dma_display->color565(c.r, c.g, c.b);
  };
  auto dimColor = [&](const ColorRGB &c, uint8_t num, uint8_t den) -> uint16_t {
    uint8_t r = (uint8_t)(((uint16_t)c.r * num) / den);
    uint8_t g = (uint8_t)(((uint16_t)c.g * num) / den);
    uint8_t b = (uint8_t)(((uint16_t)c.b * num) / den);
    return dma_display->color565(r, g, b);
  };
  auto fmtMMSS = [](long secs) -> String {
    if (secs < 0) secs = 0;
    long m = secs / 60;
    long s = secs % 60;
    char buf[8];
    snprintf(buf, sizeof(buf), "%ld:%02ld", m, s);
    return String(buf);
  };
  auto lineHeightBuiltin = [](uint8_t size) -> int { return 8 * (int)size; };

  bool paused = doc["paused"] | false;
  const ColorRGB headerRGB = paused ? RED : DARK_ORANGE;

  // Extract current step note and remaining seconds
  String curNote = doc["current_step_note"].is<const char*>() ? String(doc["current_step_note"].as<const char*>()) : String("");
  long remSec = -1;
  if (doc["remaining_sec_current"].is<long>()) remSec = doc["remaining_sec_current"].as<long>();

  JsonArray arr;
  if (doc["remaining_swims"].is<JsonArray>()) {
    arr = doc["remaining_swims"].as<JsonArray>();
    if (remSec < 0 && !arr.isNull() && arr.size() > 0) {
      JsonObject obj0 = arr[0];
      if (obj0.containsKey("durSec")) remSec = obj0["durSec"].as<long>();
      if (curNote.length() == 0 && obj0.containsKey("note")) curNote = obj0["note"].as<const char*>();
    }
  }
  if (remSec < 0) remSec = 0;
  if (curNote.length() == 0) curNote = "Step";

  // Prepare screen
  dma_display->fillScreen(0);
  dma_display->setTextWrap(false);

  // Top counters: remaining time and remaining meters (meters = remTime / pace100s * 100)
  dma_display->setFont(nullptr); // built-in 6x8
  long elapsedMs = doc["elapsed_ms"] | 0;
  long elapsedSec = elapsedMs / 1000;
  long durSecCurrent = 0;
  long pace100s = 0;
  if (!arr.isNull() && arr.size() > 0) {
    JsonObject obj0 = arr[0];
    if (obj0.containsKey("durSec")) {
      durSecCurrent = obj0["durSec"].as<long>();
    }
    if (obj0.containsKey("pace100s")) {
      pace100s = obj0["pace100s"].as<long>();
    }
  }
  long remTop = durSecCurrent - elapsedSec;
  if (remTop < 0) remTop = 0;
  String timeStr = fmtMMSS(remTop);

  // Compute meters; 
  long metersVal = 0;
  if (pace100s > 0) {
    metersVal = (long)((remTop * 100) / pace100s);
  }
  String metersStr = String(metersVal) + "m";
  if(metersVal==0)
      metersStr ="";
  // First item determines the color of the counters:
  // - Swim (pace > 0): GREEN
  // - Rest (pace == 0): DARK_ORANGE if not paused, RED if paused
  ColorRGB firstColor = (pace100s > 0) ? GREEN : (paused ? RED : DARK_ORANGE);

  // Draw counters ~30% smaller (use size 1 as nearest integer approximation)
  const uint8_t counterSize = 1;
  int charW = 6 * (int)counterSize;
  int counterH = lineHeightBuiltin(counterSize);
  int timeW = (int)timeStr.length() * charW;
  int metersW = (int)metersStr.length() * charW;

  // Center counters: try to center "time  meters" on one row, else stack centered
  int gapSpaces = 2;
  int gapW = charW * gapSpaces;  
  if(metersVal==0)
      gapW =0;
  int combinedW = timeW + gapW + metersW;

  dma_display->setTextSize(counterSize);
  dma_display->setTextColor(color565(firstColor));

  int x0 = (PANEL_WIDTH - combinedW) / 2;
  int y0 = 0;
  dma_display->setCursor(x0, y0);  
  dma_display->print(timeStr);
  dma_display->setCursor(x0 + timeW + gapW, y0);
  dma_display->print(metersStr);
  

  // Determine where list should start
  int listStartY = (counterH) + 2;

  // Prepare fonts: built-in for first item; TomThumb will be used for the rest (smaller)
  dma_display->setFont(nullptr);
  dma_display->setTextSize(1);
  const int LH6 = lineHeightBuiltin(1); // 8 px for built-in
  const int LHTT = 6;                   // ~6 px line height for TomThumb

  // Start list under the counters (account for stacked counters if needed)
  int y = listStartY;

  // Remaining list: up to first 5 items, each item uses two lines
  // start list under top margin
  // Two brown shades for later items
  const ColorRGB BROWN1{139, 69, 19};
  const ColorRGB BROWN2{205, 133, 63};

  if (!arr.isNull()) {
    size_t shown = 0;
    for (size_t i = 0; i < arr.size() && shown < 9; ++i) { // include current item
      JsonObject obj = arr[i];
      long pace = obj["pace100s"] | 0;
      long dur = obj["durSec"] | 0;
      const char* note = obj["note"] | "";

      // If the very first item is rest (pace == 0), skip drawing it (only show the counter)
      if (i == 0 && pace == 0) {
        y+=5; //other font is centered idfferetnly
        continue;
      }

      if (i == 0) {
        // First visible item (swim): color matches counters; two lines
        dma_display->setFont(nullptr);
        dma_display->setTextSize(1);
        dma_display->setTextColor(color565(firstColor));

        long meters = (dur > 0) ? (long)((dur * 100) / pace) : 0;
        String line1 = String(meters) + "m " + fmtMMSS(pace);
        String line2 = String(note);

        int16_t bx, by; uint16_t bw, bh;
        /*dma_display->getTextBounds(line1.c_str(), 0, y, &bx, &by, &bw, &bh);
        while (bw > PANEL_WIDTH && line1.length() > 1) {
          line1.remove(line1.length() - 1);
          dma_display->getTextBounds(line1.c_str(), 0, y, &bx, &by, &bw, &bh);
        }*/
        dma_display->setCursor(0, y);
        dma_display->print(line1);
        y += LH6;

       /*dma_display->getTextBounds(line2.c_str(), 0, y, &bx, &by, &bw, &bh);
        while (bw > PANEL_WIDTH && line2.length() > 1) {
          line2.remove(line2.length() - 1);
          dma_display->getTextBounds(line2.c_str(), 0, y, &bx, &by, &bw, &bh);
        }*/
        dma_display->setCursor(0, y);
        dma_display->print(line2);
        y += (LH6);
        y+=5; //other font is centered idfferetnly
      } else {
        // Rest of items: smaller (TomThumb) and in one line
        dma_display->setFont(&TomThumb);
        dma_display->setTextSize(1);
        dma_display->setTextColor(color565((shown % 2) ? BROWN2 : BROWN1));

        String line;
        if (pace > 0) {
          long meters = (dur > 0) ? (long)((dur * 100) / pace) : 0;
          line = String(meters) + "M" + fmtMMSS(pace) + " " + String(note);
        } else {
          line = String("rest ") + fmtMMSS(dur) + " " + String(note);
        }

        int16_t bx, by; uint16_t bw, bh;
        dma_display->getTextBounds(line.c_str(), 0, y, &bx, &by, &bw, &bh);
        while (bw > PANEL_WIDTH && line.length() > 1) {
          line.remove(line.length() - 1);
          dma_display->getTextBounds(line.c_str(), 0, y, &bx, &by, &bw, &bh);
        }
        dma_display->setCursor(0, y);
        dma_display->print(line);
        y += (LHTT);
      }

      if (y >= PANEL_HEIGHT) break;
      shown++;
    }
  }

  // Restore default font for any later rendering
  dma_display->setFont(nullptr);
  dma_display->setTextSize(1);
}



/* Call every ~50ms to animate a swimmer on the 64x64 panel */
void drawSwimmerAnimationTick() {
  if (!dma_display) return;

  static uint32_t tick = 0;
  tick++;

  const int W = PANEL_WIDTH;
  const int H = PANEL_HEIGHT;

  // Time in seconds (assuming ~50ms per call)
  float t = tick * 0.05f;

  // Colors
  const uint16_t sky     = dma_display->color565(20, 30, 60);
  const uint16_t water   = dma_display->color565(0, 50, 90);
  const uint16_t waveHi  = dma_display->color565(120, 200, 255);
  const uint16_t waveLo  = dma_display->color565(60, 120, 200);
  const uint16_t skin    = dma_display->color565(255, 220, 180);
  const uint16_t white   = dma_display->color565(220, 240, 255);
  const uint16_t bodyCol = skin;

  // Water surface (slightly oscillating)
  float ySurfaceF = 30.0f + 2.0f * sinf(t * 1.1f);
  int ySurface = (int)roundf(ySurfaceF);
  if (ySurface < 4) ySurface = 4;
  if (ySurface > H - 10) ySurface = H - 10;

  int yTorso = ySurface - 1;

  // Background: water everywhere, then sky above the surface
  dma_display->fillScreen(water);
  if (ySurface > 0) {
    dma_display->fillRect(0, 0, W, ySurface, sky);
  }

  // Swimmer translation across the lane (compute positions early)
  float speedPxPerSec = 12.0f;
  float xHeadF = fmodf(t * speedPxPerSec, (float)(W + 30)) - 15.0f; // wrap across screen
  int xHead = (int)roundf(xHeadF);

  // Head aligned with body line
  int yHead = yTorso;
  int rHead = 3;

  // Shoulder anchor point (slightly behind the head)
  int xShoulder = xHead - 2;
  int yShoulder = yTorso;

  // Circular arm path: hands go around the shoulder; half cycle above water, half under
  const float strokeHz = 0.8f;
  const float Rhand = 10.0f;
  const float upperL = 6.0f; // upper arm length; the rest is forearm

  auto handPos = [&](float phaseOffset) {
    float ang = 2.0f * 3.14159f * (strokeHz * t + phaseOffset);
    int hx = (int)roundf(xShoulder + Rhand * cosf(ang));
    int hy = (int)roundf(yShoulder + Rhand * sinf(ang));
    return std::pair<int,int>(hx, hy);
  };

  auto elbowFor = [&](int hx, int hy) {
    float vx = (float)hx - (float)xShoulder;
    float vy = (float)hy - (float)yShoulder;
    float len = sqrtf(vx*vx + vy*vy);
    if (len < 0.001f) len = 0.001f;
    int ex = (int)roundf(xShoulder + upperL * (vx / len));
    int ey = (int)roundf(yShoulder + upperL * (vy / len));
    return std::pair<int,int>(ex, ey);
  };

  auto h1 = handPos(0.0f);
  auto h2 = handPos(0.5f);
  int hx1 = h1.first, hy1 = h1.second;
  int hx2 = h2.first, hy2 = h2.second;
  auto e1 = elbowFor(hx1, hy1);
  auto e2 = elbowFor(hx2, hy2);
  int ex1 = e1.first, ey1 = e1.second;
  int ex2 = e2.first, ey2 = e2.second;

  // Legs (flutter kick), anchored at hips
  int xHip = xHead - 16;
  int yHip = ySurface + 2; // lower legs by an additional 1px
  const float thighL = 7.0f;
  const float shinL  = 6.0f;
  const float legHz  = 2.2f;
  float kickPhase = legHz * t * 2.0f * 3.14159f;
  float baseAngle = 3.14159f; // pointing left
  float amp = 0.35f;

  float aThigh1 = baseAngle + amp * sinf(kickPhase);
  float aThigh2 = baseAngle + amp * sinf(kickPhase + 3.14159f);
  float aShinOff = 0.6f; // knee bend

  int k1x = (int)roundf(xHip + thighL * cosf(aThigh1));
  int k1y = (int)roundf(yHip + thighL * sinf(aThigh1));
  int f1x = (int)roundf(k1x + shinL * cosf(aThigh1 + aShinOff));
  int f1y = (int)roundf(k1y + shinL * sinf(aThigh1 + aShinOff));

  int k2x = (int)roundf(xHip + thighL * cosf(aThigh2));
  int k2y = (int)roundf(yHip + thighL * sinf(aThigh2));
  int f2x = (int)roundf(k2x + shinL * cosf(aThigh2 + aShinOff));
  int f2y = (int)roundf(k2y + shinL * sinf(aThigh2 + aShinOff));

  // Ensure the visually top foot is always 2px lower (downwards on screen)
  if (f1y < f2y) {
    f1y = min(f1y + 2, H - 1);
  } else if (f2y < f1y) {
    f2y = min(f2y + 2, H - 1);
  } else {
    // If equal, arbitrarily lower one
    f1y = min(f1y + 2, H - 1);
  }

  // Draw water surface highlights BEFORE swimmer (to ensure swimmer drawn last)
  for (int x = 0; x < W; ++x) {
    float yy = (float)ySurface + 1.5f * sinf(x * 0.25f + t * 2.2f);
    int y = (int)roundf(yy);
    dma_display->drawPixel(x, y, waveHi);
    if ((x + tick) % 11 == 0 && y + 1 < H) {
      dma_display->drawPixel(x, y + 1, waveLo);
    }
  }
  // Subtle darker line just below surface for depth
  if (ySurface + 1 < H) {
    for (int x = 0; x < W; ++x) {
      if ((x + tick) % 5 == 0) {
        dma_display->drawPixel(x, ySurface + 1, dma_display->color565(0, 40, 80));
      }
    }
  }
  // Splashes (water effect) BEFORE swimmer
  auto drawSplash = [&](int hx, int hy) {
    if (hy < ySurface - 1) {
      for (int i = 0; i < 4; ++i) {
        int dx = ((hx * 17 + (int)tick * 13 + i * 23) % 7) - 3;
        int dy = -((hx * 29 + (int)tick * 19 + i * 11) % 3);
        int px = hx + dx;
        int py = hy + dy;
        if (px >= 0 && px < W && py >= 0 && py < H) {
          dma_display->drawPixel(px, py, white);
        }
      }
    }
  };
  drawSplash(hx1, hy1);
  drawSplash(hx2, hy2);

  // Helper: draw line but only the pixels above the water surface (y < ySurface)
  auto drawClippedLineAbove = [&](int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int steps = max(abs(dx), abs(dy));
    if (steps == 0) {
      if (y0 < ySurface && x0 >= 0 && x0 < W && y0 >= 0 && y0 < H) dma_display->drawPixel(x0, y0, color);
      return;
    }
    float fx = x0;
    float fy = y0;
    float stepx = (float)dx / (float)steps;
    float stepy = (float)dy / (float)steps;
    for (int i = 0; i <= steps; ++i) {
      int xi = (int)roundf(fx);
      int yi = (int)roundf(fy);
      if (yi < ySurface && xi >= 0 && xi < W && yi >= 0 && yi < H) {
        dma_display->drawPixel(xi, yi, color);
      }
      fx += stepx;
      fy += stepy;
    }
  };

  // Helper: draw filled circle but only above the water surface
  auto fillCircleClippedAbove = [&](int cx, int cy, int r, uint16_t color) {
    for (int yy = -r; yy <= r; ++yy) {
      int y = cy + yy;
      if (y >= ySurface || y < 0 || y >= H) continue;
      int xx = (int)floorf(sqrtf((float)(r*r - yy*yy)));
      int xL = cx - xx;
      int xR = cx + xx;
      if (xR < 0 || xL >= W) continue;
      if (xL < 0) xL = 0;
      if (xR >= W) xR = W - 1;
      dma_display->drawFastHLine(xL, y, xR - xL + 1, color);
    }
  };

  // Draw swimmer LAST (to avoid flicker) - full silhouette for recognizability

  // Body: split into two segments; left half lowered by 1px
  int xStart = xHead - 2;
  int xEnd = xHip;
  int xR = max(xStart, xEnd);
  int xL = min(xStart, xEnd);
  int xMid = (xL + xR) / 2;
  // right half (near head) at yTorso
  if (xR >= xMid + 1) {
    dma_display->drawFastHLine(xMid + 1, yTorso, xR - (xMid + 1) + 1, skin);
  }
  // left half at yTorso + 1
  if (xMid >= xL) {
    dma_display->drawFastHLine(xL, yTorso + 1, xMid - xL + 1, skin);
  }

  // Legs (thigh + shin) with flutter kick (draw full limbs for clarity)
  dma_display->drawLine(xHip, yHip, k1x, k1y, skin);
  dma_display->drawLine(k1x, k1y, f1x, f1y, skin);
  dma_display->drawLine(xHip, yHip, k2x, k2y, skin);
  dma_display->drawLine(k2x, k2y, f2x, f2y, skin);
  // Feet accents (2x2 squares)
  auto drawSquare2 = [&](int cx, int cy, uint16_t col) {
    if (cx >= 0 && cx + 1 < W && cy >= 0 && cy + 1 < H) {
      dma_display->drawPixel(cx, cy, col);
      dma_display->drawPixel(cx + 1, cy, col);
      dma_display->drawPixel(cx, cy + 1, col);
      dma_display->drawPixel(cx + 1, cy + 1, col);
    } else if (cx >= 0 && cx < W && cy >= 0 && cy < H) {
      dma_display->drawPixel(cx, cy, col);
    }
  };
  drawSquare2(f1x, f1y, white);
  drawSquare2(f2x, f2y, white);

  // Arms: only the portion above the water surface is visible
  drawClippedLineAbove(xShoulder, yShoulder, ex1, ey1, skin);
  drawClippedLineAbove(ex1, ey1, hx1, hy1, skin);
  drawClippedLineAbove(xShoulder, yShoulder, ex2, ey2, skin);
  drawClippedLineAbove(ex2, ey2, hx2, hy2, skin);
  // Hands: show only when above water, use skin color for consistency
  if (hy1 < ySurface) drawSquare2(hx1, hy1, skin);
  if (hy2 < ySurface) drawSquare2(hx2, hy2, skin);

  // Head (stick-man): unfilled circle
  dma_display->drawCircle(xHead, yHead, rHead, skin);
}
