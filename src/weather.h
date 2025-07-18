#pragma once
#include <Arduino.h>

extern String weatherIcon;
extern String lastDrawnWeatherIcon;
extern String weatherTempDay;
extern float weatherHumidity;
extern float weatherTemp;
extern float weatherTempMin;
extern float weatherTempMax;
extern String weatherApiKey;
extern String weatherCity;

void fetchWeather();
void showWeatherIconJPG(String iconCode);