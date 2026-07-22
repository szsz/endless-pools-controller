#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Fonts/TomThumb.h>
#include "fonts/Panel3x5.h"
#include "hub75.h"

/*************** HUB75 Panel Config ***************/
static const int PANEL_WIDTH  = 64;
static const int PANEL_HEIGHT = 64;
static const int PANEL_CHAIN  = 1;   // number of chained panels

// User-provided pinout
static const int PIN_R1  = 21;
static const int PIN_G1  = 17;
static const int PIN_B1  = 16;
static const int PIN_R2  = 18;
static const int PIN_G2  = 15;
static const int PIN_B2  = 3;

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

static uint8_t s_brightness_percent = 50;

// Screen saver state
static uint16_t s_screensaver_sec = 300;     // 5 minutes; 0 = disabled
static bool s_display_on = true;             // current panel power state
static uint32_t s_last_activity_ms = 0;      // last time a workout was running

// Panel layout: 0 = classic list, 1 = big countdown + next-two.
static uint8_t s_panel_mode = 0;
// Dry run lives here because hub75.cpp owns /settings.json; the value is pushed
// into SwimMachine when a workout starts (see WorkoutManager::run).
static bool s_dry_run = false;
// Draw note text in capitals; the small fonts read better all-caps.
static bool s_caps = true;

static uint8_t percent_to_brightness8(uint8_t p) {
  if (p > 100) p = 100;
  return (uint8_t)((p * 255 + 50) / 100);
}

static void settings_load() {
  if (!LittleFS.exists("/settings.json")) return;
  File f = LittleFS.open("/settings.json", "r");
  if (!f) return;
  StaticJsonDocument<256> d;
  if (deserializeJson(d, f) == DeserializationError::Ok) {
    int b = d["brightness"] | -1;
    if (b >= 0 && b <= 100) {
      s_brightness_percent = (uint8_t)b;
    }
    long ss = d["screensaver_sec"] | -1;
    if (ss >= 0 && ss <= 86400) {
      s_screensaver_sec = (uint16_t)ss;
    }
    int pm = d["panel_mode"] | -1;
    if (pm == 0 || pm == 1) {
      s_panel_mode = (uint8_t)pm;
    }
    s_dry_run = d["dry_run"] | false;
    s_caps = d["caps"] | true;
  }
  f.close();
}

static void settings_store() {
  // Preserve other keys in settings.json, only update the keys we own here
  StaticJsonDocument<512> d;
  if (LittleFS.exists("/settings.json")) {
    File rf = LittleFS.open("/settings.json", "r");
    if (rf) {
      DeserializationError err = deserializeJson(d, rf);
      rf.close();
      if (err) {
        d.clear();
      }
    }
  }
  d["brightness"] = s_brightness_percent;
  d["screensaver_sec"] = s_screensaver_sec;
  d["panel_mode"] = s_panel_mode;
  d["dry_run"] = s_dry_run;
  d["caps"] = s_caps;
  File wf = LittleFS.open("/settings.json", "w");
  if (!wf) return;
  serializeJson(d, wf);
  wf.close();
}

// Power the panel off (clear it) / back on. Drawing functions skip output while
// the display is off, so the cleared frame stays black until we wake.
static void display_sleep() {
  if (!s_display_on) return;
  s_display_on = false;
  if (dma_display) dma_display->clearScreen();
}

static void display_wake() {
  if (s_display_on) return;
  s_display_on = true;
  if (dma_display) {
    dma_display->setBrightness8(percent_to_brightness8(s_brightness_percent));
    dma_display->clearScreen();
  }
}

uint8_t HUB75_getBrightnessPercent() {
  return s_brightness_percent;
}

void HUB75_setBrightnessPercent(uint8_t percent) {
  if (percent > 100) percent = 100;
  s_brightness_percent = percent;
  // Adjusting brightness is user activity: wake the screen and reset the timer.
  display_wake();
  s_last_activity_ms = millis();
  if (dma_display) {
    dma_display->setBrightness8(percent_to_brightness8(s_brightness_percent));
  }
  settings_store();
}

uint16_t HUB75_getScreensaverSec() {
  return s_screensaver_sec;
}

void HUB75_setScreensaverSec(uint16_t seconds) {
  if (seconds > 86400) seconds = 86400;
  s_screensaver_sec = seconds;
  // Changing the setting counts as activity and gives a fresh countdown.
  display_wake();
  s_last_activity_ms = millis();
  settings_store();
}

uint8_t HUB75_getPanelMode() { return s_panel_mode; }

void HUB75_setPanelMode(uint8_t mode) {
  s_panel_mode = (mode == 1) ? 1 : 0;
  settings_store();
}

bool AppSettings_getCaps() { return s_caps; }

void AppSettings_setCaps(bool on) {
  s_caps = on;
  settings_store();
}

bool AppSettings_getDryRun() { return s_dry_run; }

void AppSettings_setDryRun(bool on) {
  s_dry_run = on;
  settings_store();
}

void HUB75_screensaverTick(bool workoutActive) {
  uint32_t now = millis();
  if (workoutActive) {
    // Keep awake and continually push the idle deadline forward.
    s_last_activity_ms = now;
    display_wake();
    return;
  }
  if (s_screensaver_sec == 0) return; // disabled: never turn off
  if (s_display_on &&
      (uint32_t)(now - s_last_activity_ms) >= (uint32_t)s_screensaver_sec * 1000UL) {
    display_sleep();
  }
}


void setupHUB75() {
  HUB75_I2S_CFG mxconfig(PANEL_WIDTH, PANEL_HEIGHT, PANEL_CHAIN);
  mxconfig.gpio.r1 = PIN_R1;  mxconfig.gpio.g1 = PIN_G1;  mxconfig.gpio.b1 = PIN_B1;
  mxconfig.gpio.r2 = PIN_R2;  mxconfig.gpio.g2 = PIN_G2;  mxconfig.gpio.b2 = PIN_B2;
  mxconfig.gpio.a = PIN_A;    mxconfig.gpio.b = PIN_B;    mxconfig.gpio.c = PIN_C;
  mxconfig.gpio.d = PIN_D;    mxconfig.gpio.e = PIN_E;
  mxconfig.gpio.clk = PIN_CLK; mxconfig.gpio.lat = PIN_LAT; mxconfig.gpio.oe = PIN_OE;

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  settings_load();
  dma_display->setBrightness8(percent_to_brightness8(s_brightness_percent)); // 0..255
  dma_display->clearScreen();
  // Start the screen-saver countdown from boot (turns off after the timeout if
  // no workout is ever started).
  s_display_on = true;
  s_last_activity_ms = millis();
}




// Compact pace notation: instead of "1:30" the minutes become a single glyph of
// horizontal bars (1 = top, 2 = top+middle, 3 = top+middle+bottom) followed by
// the two seconds digits, saving a character of width on the panel.
// `top` is the glyph's top row; returns the x advance (bar width + 1px gap).
// The bars are narrower than the cell and sit flush right, so they read as one
// unit with the seconds digits that follow.
static int drawPaceMinuteGlyph(int x, int top, int mins, int gw, int gh, int advance, uint16_t color)
{
  const int x0 = x + advance - gw - 1;      // flush right, 1px clear of the seconds
  auto bar = [&](int yy) {
    for (int i = 0; i < gw; ++i) dma_display->drawPixel(x0 + i, yy, color);
  };
  bar(top);
  if (mins >= 2) bar(top + (gh - 1) / 2);
  if (mins >= 3) bar(top + gh - 1);
  return advance;
}

// Width of a string in whatever font is currently selected.
static uint16_t textWidth(const String &s)
{
  int16_t bx, by; uint16_t bw, bh;
  dma_display->getTextBounds(s.c_str(), 0, 0, &bx, &by, &bw, &bh);
  return bw;
}

static String fmtMMSSs(long secs)
{
  if (secs < 0) secs = 0;
  char buf[12];
  snprintf(buf, sizeof buf, "%ld:%02ld", secs / 60, secs % 60);
  return String(buf);
}

// Right-edge sidebar for one set block, spanning [top, bottom) minus one dark
// pixel at each end so two adjacent sets never merge into a single bar. It is
// Future sets are divided into `reps` segments (1px gaps) to show how many
// repetitions they have, falling back to a solid bar when that many will not
// fit. The set in progress is drawn UNsegmented, because there the bar's job is
// to mark which exercise is running (rows [blueTop, blueBot) in the "current"
// colour) - segmenting it too would put two unrelated meanings on one bar.
static void drawSetSidebar(int top, int bottom, int reps, bool segmented,
                           int blueTop, int blueBot,
                           uint16_t colSet, uint16_t colCurrent)
{
  const int x = PANEL_WIDTH - 1;
  const int lo = top + 1, hi = bottom - 1;      // dark pixel top and bottom
  const int h = hi - lo;
  if (h <= 0) return;
  if (reps < 1) reps = 1;

  auto put = [&](int yy) {
    if (yy < 0 || yy >= PANEL_HEIGHT) return;
    const bool blue = (yy >= blueTop && yy < blueBot);
    dma_display->drawPixel(x, yy, blue ? colCurrent : colSet);
  };

  if (segmented && 2 * reps - 1 <= h) {        // segments fit: one per repetition
    const int g = 1;
    for (int k = 0; k < reps; ++k) {
      int ys = lo + (k * (h + g)) / reps;
      int ye = lo + ((k + 1) * (h + g)) / reps - g - 1;
      if (ye < ys) ye = ys;
      for (int yy = ys; yy <= ye && yy < hi; ++yy) put(yy);
    }
  } else {                                      // too many to show individually
    for (int yy = lo; yy < hi; ++yy) put(yy);
  }
}

// One upcoming-swim line in TomThumb: "<meters>m <paceglyph><SS> <note>", with
// the note truncated to fit maxW. Falls back to plain M:SS when the pace is
// outside the 1..3 minute range the glyphs can express.
// Distances are right-aligned in this fixed pixel field so the pace column
// begins at the same x on every row (padding is blank pixels in front, not
// spaces - the space/'.' glyphs are unchanged). 16px = widest "999m".
static const int DIST_FIELD_PX = 16;

static void drawTomThumbPaceLine(int baseline, long meters, long pace,
                                 const String &note, int maxW,
                                 uint16_t metricCol, uint16_t noteCol)
{
  dma_display->setTextColor(metricCol);
  const int mins = (int)(pace / 60), secs = (int)(pace % 60);
  const bool useGlyph = (mins >= 1 && mins <= 3);

  char mb[8];
  if (meters > 999) snprintf(mb, sizeof mb, "%.1fK", meters / 1000.0);
  else              snprintf(mb, sizeof mb, "%ldm", meters);
  String pre(mb);
  char sb[4]; snprintf(sb, sizeof sb, "%02d", secs);
  const String secsStr(sb);
  String tail = String(" ") + note;

  const int glyphAdv = 4;   // 3px of bars + 1px gap
  const int fixed = DIST_FIELD_PX + (useGlyph ? (glyphAdv + textWidth(secsStr))
                                              : textWidth(fmtMMSSs(pace)));
  while (fixed + (int)textWidth(tail) > maxW && tail.length() > 0) {
    tail.remove(tail.length() - 1);
  }

  // right-align the distance so the pace always starts at DIST_FIELD_PX
  int preX = DIST_FIELD_PX - (int)textWidth(pre);
  if (preX < 0) preX = 0;
  dma_display->setCursor(preX, baseline);
  dma_display->print(pre);
  int cx = DIST_FIELD_PX;

  if (useGlyph) {
    drawPaceMinuteGlyph(cx, baseline - 5, mins, 2, 5, 4, metricCol);  // 2px bars, flush right
    cx += glyphAdv;
    dma_display->setCursor(cx, baseline);
    dma_display->print(secsStr);
  } else {
    dma_display->setCursor(cx, baseline);
    dma_display->print(fmtMMSSs(pace));
  }
  cx = dma_display->getCursorX();
  dma_display->setTextColor(noteCol);
  dma_display->setCursor(cx, baseline);
  dma_display->print(tail);
}


// The upcoming list, shared by both panel modes. The swim in progress is simply
// its first row (marked blue on the right edge) rather than a separate heading,
// which keeps every row the same height and lets the list fill the panel.
// A set is listed once as a block of `set_size` rows in canonical order.
static void drawUpcomingList(JsonArray arr, int startY,
                             int curSetIdx, int curSetPos,
                             uint16_t listCol, uint16_t markSet, uint16_t markCur)
{
  if (arr.isNull() || arr.size() == 0) return;

  dma_display->setFont(&Panel3x5);
  dma_display->setTextSize(1);
  const int LHTT  = 6;
  const int textW = PANEL_WIDTH - 2;      // last column belongs to the sidebar
  int y = startY + 5;                     // TomThumb cursor y is the baseline
  bool setDone[24] = {false};

  auto drawRow = [&](JsonObject o, int baseline, bool isCurrent) {
    long pace = o["pace100s"] | 0;
    long dur  = o["durSec"] | 0;
    const char *note = o["note"] | "";
    // A set's swims are often unnamed; fall back to the set's own name so the
    // row still says what it belongs to.
    if (note[0] == '\0') note = o["set_name"] | "";
    String noteStr(note);
    if (s_caps) noteStr.toUpperCase();
    if (pace > 0) {
      long meters = (dur > 0) ? (long)((dur * 100) / pace) : 0;
      // The swim in progress shows its length and pace in the "current" colour.
      drawTomThumbPaceLine(baseline, meters, pace, noteStr, textW,
                           isCurrent ? markCur : listCol, listCol);
    } else {
      // A rest has no distance: dashes fill the distance field (3 normally, 2
      // once the duration hits 10:00), and the time sits in the pace column so
      // it lines up under a swim's pace.
      const char *dashes = (dur >= 600) ? "--" : "---";
      dma_display->setTextColor(isCurrent ? markCur : listCol);
      dma_display->setCursor(0, baseline);
      dma_display->print(dashes);
      dma_display->setCursor(DIST_FIELD_PX, baseline);
      dma_display->print(fmtMMSSs(dur));
      int cx = dma_display->getCursorX();
      String tail = String(" ") + noteStr;
      while (cx + (int)textWidth(tail) > textW && tail.length() > 0) tail.remove(tail.length() - 1);
      dma_display->setTextColor(listCol);
      dma_display->setCursor(cx, baseline);
      dma_display->print(tail);
    }
  };

  for (size_t i = 0; i < arr.size() && (y - 5) < PANEL_HEIGHT; ++i) {
    JsonObject o = arr[i];
    const int si = o["set_index"].is<int>() ? o["set_index"].as<int>() : -1;

    if (si < 0) {                          // plain swim: one row, no sidebar
      drawRow(o, y, i == 0);               // (the sidebar is a set marker only)
      y += LHTT;
      continue;
    }

    if (si < 24 && setDone[si]) continue;  // this set was already listed
    if (si < 24) setDone[si] = true;

    const int size = o["set_size"] | 1;
    const int reps = o["set_reps"] | 1;
    const int blockTop = y - 5;
    int blueTop = -1, blueBot = -1;

    for (int pos = 0; pos < size && (y - 5) < PANEL_HEIGHT; ++pos) {
      JsonObject pick; bool found = false;
      for (size_t j = i; j < arr.size(); ++j) {
        JsonObject c = arr[j];
        const int cj = c["set_index"].is<int>() ? c["set_index"].as<int>() : -1;
        const int pj = c["set_pos"].is<int>()   ? c["set_pos"].as<int>()   : -1;
        if (cj == si && pj == pos) { pick = c; found = true; break; }
      }
      if (!found) continue;
      const bool isCurrent = (si == curSetIdx && pos == curSetPos);
      drawRow(pick, y, isCurrent);
      if (isCurrent) { blueTop = y - 5; blueBot = y + 1; }
      y += LHTT;
    }
    // y is the next baseline; this block's last visual row ends at y - 5.
    // The active set is unsegmented (its bar marks the current exercise instead).
    const bool isActiveSet = (si == curSetIdx);
    drawSetSidebar(blockTop, y - 5, reps, !isActiveSet, blueTop, blueBot, markSet, markCur);
  }

  dma_display->setFont(nullptr);
  dma_display->setTextSize(1);
}

// The 2px bar under the counters. Inside a set: one segment per repetition of
// that set, the current repetition blue. Otherwise: a fill bar for the current
// swim, easing orange->green as it completes. Returns the bar height drawn (0
// if nothing was drawn) so the caller can place the list below it.
static int drawTopBar(int barY, int curSetReps, int curSetRep,
                      long durSecCurrent, long remTop, bool inSet,
                      uint16_t barIdle, uint16_t markCurrent)
{
  if (inSet) {
    const int gap = (curSetReps > 8) ? 1 : 2;   // tighten spacing past 8 reps
    for (int i = 0; i < curSetReps; ++i) {
      int xs = (i * (PANEL_WIDTH + gap)) / curSetReps;
      int xe = ((i + 1) * (PANEL_WIDTH + gap)) / curSetReps - gap - 1;
      if (xe < xs) xe = xs;                      // at least one pixel per rep
      uint16_t c = (i == (curSetRep - 1)) ? markCurrent : barIdle;
      for (int x = xs; x <= xe && x < PANEL_WIDTH; ++x) {
        dma_display->drawPixel(x, barY, c);
        dma_display->drawPixel(x, barY + 1, c);
      }
    }
    return 2;
  }
  if (durSecCurrent > 0) {
    long done = durSecCurrent - remTop;
    if (done < 0) done = 0;
    if (done > durSecCurrent) done = durSecCurrent;
    const int filled = (int)((done * PANEL_WIDTH) / durSecCurrent);
    const uint8_t r = (uint8_t)(255 + ((25  - 255) * done) / durSecCurrent);
    const uint8_t g = (uint8_t)(140 + ((200 - 140) * done) / durSecCurrent);
    const uint8_t b = (uint8_t)(0   + ((25  - 0)   * done) / durSecCurrent);
    const uint16_t c = dma_display->color565(r, g, b);
    for (int x = 0; x < filled && x < PANEL_WIDTH; ++x) {
      dma_display->drawPixel(x, barY, c);
      dma_display->drawPixel(x, barY + 1, c);
    }
    return 2;
  }
  return 0;
}

void printJSon(DynamicJsonDocument doc)
{
  if (!dma_display || !s_display_on) return;

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

  // Shared by both modes.
  const ColorRGB BAR_IDLE{255, 140, 0};        // progress-bar segment, not current
  const ColorRGB MARK_CURRENT{40, 120, 255};   // blue: the swim in progress
  const ColorRGB MARK_SET{255, 255, 255};      // white: belongs to a set
  const ColorRGB LIST_COLOR{205, 133, 63};     // upcoming rows
  const int curSetIdx = doc["set_index"].is<int>() ? doc["set_index"].as<int>() : -1;
  const int curSetPos = doc["set_pos"].is<int>() ? doc["set_pos"].as<int>() : -1;
  const int curSetReps = doc["set_reps"] | 0;
  const int curSetRep  = doc["set_rep"]  | 0;   // 1-based
  const bool inSet = (curSetIdx >= 0 && curSetReps > 0);

  // ================= Mode 1: big countdown =================
  // Large remaining meters + time for the current swim, then the upcoming list
  // filling the rest of the panel (the current swim is its first row).
  if (s_panel_mode == 1) {
    const uint8_t bigSize  = 2;
    const int bigCharW = 6 * (int)bigSize;   // 12 px per char
    const int bigLineH = 8 * (int)bigSize;   // 16 px per line

    dma_display->setFont(nullptr);
    dma_display->setTextSize(bigSize);
    dma_display->setTextColor(color565(firstColor));

    // Line 1: remaining meters (or REST while resting)
    String bigTop = (pace100s > 0) ? (String(metersVal) + "m") : String("REST");
    int wTop = (int)bigTop.length() * bigCharW;
    if (wTop > PANEL_WIDTH) wTop = PANEL_WIDTH;
    dma_display->setCursor((PANEL_WIDTH - wTop) / 2, 0);
    dma_display->print(bigTop);

    // Line 2: remaining time
    int wTime = (int)timeStr.length() * bigCharW;
    dma_display->setCursor((PANEL_WIDTH - wTime) / 2, bigLineH);
    dma_display->print(timeStr);

    // Progress bar directly under the two big lines (no gap) so the last list
    // row still fits on the 64px panel.
    const int barY = 2 * bigLineH;        // rows 32-33, right below line 2 (ends 31)
    int listStartY = 2 * bigLineH + 1;
    int barH = drawTopBar(barY, curSetReps, curSetRep, durSecCurrent, remTop, inSet,
                          color565(BAR_IDLE), color565(MARK_CURRENT));
    if (barH > 0) listStartY = barY + barH + 1;

    // The list fills the rest of the panel. The swim in progress is simply its
    // first row, so its name appears there instead of as a separate heading.
    drawUpcomingList(arr, listStartY, curSetIdx, curSetPos,
                     color565(LIST_COLOR), color565(MARK_SET), color565(MARK_CURRENT));
    return;
  }

  // ================= Mode 0: classic list =================
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
  

  // Progress bar between the counters and the list (shared with big mode).
  const int barY = counterH + 1;
  int listStartY = counterH + 2;
  int barH = drawTopBar(barY, curSetReps, curSetRep, durSecCurrent, remTop, inSet,
                        color565(BAR_IDLE), color565(MARK_CURRENT));
  if (barH > 0) listStartY = barY + barH + 2;

  // Prepare fonts: built-in for first item; TomThumb will be used for the rest (smaller)
  dma_display->setFont(nullptr);
  dma_display->setTextSize(1);
  const int LH6 = lineHeightBuiltin(1); // 8 px for built-in
  const int LHTT = 6;                   // ~6 px line height for TomThumb

  // Start list under the counters (account for stacked counters if needed)
  drawUpcomingList(arr, listStartY, curSetIdx, curSetPos,
                   color565(LIST_COLOR), color565(MARK_SET), color565(MARK_CURRENT));

  // Restore default font for any later rendering
  dma_display->setFont(nullptr);
  dma_display->setTextSize(1);
}



/* Call every ~50ms to animate a swimmer on the 64x64 panel */
void drawSwimmerAnimationTick() {
  if (!dma_display || !s_display_on) return;

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
