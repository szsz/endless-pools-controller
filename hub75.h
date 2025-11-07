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

void printJSon(DynamicJsonDocument doc);
void drawSwimmerAnimationTick();
