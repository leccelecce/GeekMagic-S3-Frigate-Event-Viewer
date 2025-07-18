#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <HTTPClient.h>

// Expose the TFT display object
extern TFT_eSPI tft;

// Expose display duration (used by Frigate)
extern int displayDuration;

extern int maxImages;

extern String mode;

// Expose setScreen for screen management
void setScreen(const String& newScreen, unsigned long timeoutSec = 0, const char* by = "");

extern HTTPClient http;