#pragma once
#include <Arduino.h>

// Initialize HUB75 display and show a default message
void setupHUB75();

// Draw centered text on the HUB75 panel
void drawCenteredText(const String &msg);

// Draw test pattern: multiple horizontal lines
void drawHorizontalTestLines();
String getCurrentText();
void setCurrentText(const String &msg);
