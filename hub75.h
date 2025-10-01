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

struct LineSpec {
  std::vector<String> words; // words composing the line (joined with spaces)
  ColorRGB color;            // RGB color for the entire line
  uint8_t size;              // text size scale (1 => 6x8 base font)
};

// Initialize HUB75 display and show a default message
void setupHUB75();




void printJSon(DynamicJsonDocument doc);
void drawSwimmerAnimationTick();
