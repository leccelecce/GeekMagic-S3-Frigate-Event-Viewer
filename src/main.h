#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <HTTPClient.h>

extern TFT_eSPI tft;
extern HTTPClient http;

extern int displayDuration;
extern int maxImages;
extern String mode;

void setScreen(const String& newScreen, unsigned long timeoutSec = 0, const char* by = "");
