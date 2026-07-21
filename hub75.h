#pragma once
#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>

// Color and line specifications for multi-line rendering
struct ColorRGB {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};


// Initialize HUB75 display and show a default message
void setupHUB75();




uint8_t HUB75_getBrightnessPercent();
void HUB75_setBrightnessPercent(uint8_t percent);

// Screen saver: turn the panel off after this many seconds without a running
// workout. 0 disables the screen saver (display always on). Default 300 (5 min).
uint16_t HUB75_getScreensaverSec();
void HUB75_setScreensaverSec(uint16_t seconds);
// Call frequently from the main loop. Pass whether a workout is currently
// running: while it is, the display is kept awake and the idle timer is reset.
void HUB75_screensaverTick(bool workoutActive);

// Panel layout mode: 0 = classic list, 1 = big countdown + next two swims.
uint8_t HUB75_getPanelMode();
void HUB75_setPanelMode(uint8_t mode);

// hub75.cpp owns /settings.json, so this non-panel setting is persisted here
// too. The value is pushed into SwimMachine when a workout starts.
// Draw note text in capitals (small fonts read better all-caps).
bool AppSettings_getCaps();
void AppSettings_setCaps(bool on);

bool AppSettings_getDryRun();
void AppSettings_setDryRun(bool on);

void printJSon(DynamicJsonDocument doc);
void drawSwimmerAnimationTick();
