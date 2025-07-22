#pragma once
#include <Arduino.h>

extern String weatherApiKey;
extern String weatherCity;

extern String weatherIcon;
extern String lastDrawnWeatherIcon;
extern String weatherTempDay;
extern float weatherHumidity;
extern float weatherTemp;
extern float weatherTempMin;
extern float weatherTempMax;
extern float weatherRainMM;
extern float weatherSnowMM;

void fetchWeather();
void showWeatherIconJPG(String iconCode);
