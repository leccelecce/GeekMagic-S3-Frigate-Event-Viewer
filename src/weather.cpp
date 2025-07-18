#include "weather.h"
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <SPIFFS.h>
#include <TJpg_Decoder.h>

String weatherIcon = "";
String lastDrawnWeatherIcon = "";
String weatherTempDay = "";
float weatherHumidity = 0.0;
float weatherTemp = 0.0;
float weatherTempMin = 0.0;
float weatherTempMax = 0.0;
String weatherApiKey = "";
String weatherCity = "";

extern TFT_eSPI tft;
extern Preferences preferences;

void showWeatherIconJPG(String iconCode) {
  String path = "/icons/" + iconCode + ".jpg";
  Serial.print("Search icon: "); Serial.println(path);
  int iconWidth = 90;
  int iconHeight = 90;
  int x = 240 - iconWidth - 8;
  int y = 240 - iconHeight - 8;
  if (SPIFFS.exists(path)) {
    TJpgDec.drawJpg(x, y, path.c_str());
    Serial.print("[WEATHER] Icon drawn: "); Serial.println(path);
  } else {
    Serial.print("[WEATHER] Icon NOT found: "); Serial.println(path);
    int pad = 10;
    tft.drawLine(x + pad, y + pad, x + iconWidth - pad, y + iconHeight - pad, TFT_RED);
    tft.drawLine(x + iconWidth - pad, y + pad, x + pad, y + iconHeight - pad, TFT_RED);
    tft.drawRect(x, y, iconWidth, iconHeight, TFT_RED);
  }
}

void fetchWeather() {
  Serial.println("[WEATHER] fetchWeather() started");

  if (weatherApiKey == "" || weatherCity == "") {
    Serial.println("[WEATHER] No weatherApiKey or city set!");
    return;
  }

  // Determine current day as string
  time_t now = time(nullptr);
  struct tm *tm_now = localtime(&now);
  char dateStr[11];
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", tm_now);
  String todayStr(dateStr);
  Serial.print("[WEATHER] Current date (todayStr): "); Serial.println(todayStr);

  // Fetch current weather data
  HTTPClient http;
  String urlNow = "http://api.openweathermap.org/data/2.5/weather?q=" + weatherCity + "&appid=" + weatherApiKey + "&units=metric";
  Serial.print("[WEATHER] Fetching current weather from: "); Serial.println(urlNow);
  http.begin(urlNow);
  int httpCodeNow = http.GET();

  if (httpCodeNow == 200) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      weatherTemp = doc["main"]["temp"] | 0.0;
      weatherHumidity = doc["main"]["humidity"] | 0.0;
      weatherIcon = doc["weather"][0]["icon"].as<String>();

      Serial.print("[WEATHER] Current temperature: "); Serial.println(weatherTemp);
      Serial.print("[WEATHER] Current humidity: "); Serial.println(weatherHumidity);
      Serial.print("[WEATHER] Icon: "); Serial.println(weatherIcon);
    } else {
      Serial.print("[WEATHER] JSON parse error (current): "); Serial.println(error.c_str());
    }
  } else {
    Serial.print("[WEATHER] Error fetching current weather, code: "); Serial.println(httpCodeNow);
  }
  http.end();

  // Only update min/max on a new day
  if (todayStr == weatherTempDay) {
    Serial.println("[WEATHER] Min/max already fetched for this day, no update needed.");
    return;
  }

  // Fetch min/max for today from forecast
  String urlForecast = "http://api.openweathermap.org/data/2.5/forecast?q=" + weatherCity + "&appid=" + weatherApiKey + "&units=metric&lang=en";
  Serial.print("[WEATHER] Fetching forecast from: "); Serial.println(urlForecast);
  http.begin(urlForecast);
  int httpCodeForecast = http.GET();

  if (httpCodeForecast == 200) {
    String forecastPayload = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, forecastPayload);

    if (!error) {
      float minTemp = 99.0;
      float maxTemp = -99.0;
      int matched = 0;

      JsonArray list = doc["list"];
      for (JsonObject entry : list) {
        String dt_txt = entry["dt_txt"].as<String>();
        float tempMin = entry["main"]["temp_min"] | 0.0;
        float tempMax = entry["main"]["temp_max"] | 0.0;

        if (dt_txt.startsWith(todayStr)) {
          matched++;
          Serial.print("[WEATHER] Match found for timestamp: "); Serial.print(dt_txt);
          Serial.print(", temp_min: "); Serial.print(tempMin);
          Serial.print(", temp_max: "); Serial.println(tempMax);
          if (tempMin < minTemp) minTemp = tempMin;
          if (tempMax > maxTemp) maxTemp = tempMax;
        }
      }

      if (matched > 0) {
        weatherTempMin = minTemp;
        weatherTempMax = maxTemp;
        Serial.print("[WEATHER] Minimum temperature today: "); Serial.println(weatherTempMin);
        Serial.print("[WEATHER] Maximum temperature today: "); Serial.println(weatherTempMax);
        if (matched == 1) {
          Serial.println("[WEATHER] Warning: Only one timestamp found for today, min/max based on single data point.");
        }
      } else {
        Serial.println("[WEATHER] No forecasts found for today (no date match).");
        weatherTempMin = 0;
        weatherTempMax = 0;
        Serial.print("[WEATHER] Minimum temperature today: "); Serial.println(weatherTempMin);
        Serial.print("[WEATHER] Maximum temperature today: "); Serial.println(weatherTempMax);
      }
    } else {
      Serial.print("[WEATHER] JSON parse error (forecast): "); Serial.println(error.c_str());
      weatherTempMin = 0;
      weatherTempMax = 0;
      Serial.print("[WEATHER] Minimum temperature today: "); Serial.println(weatherTempMin);
      Serial.print("[WEATHER] Maximum temperature today: "); Serial.println(weatherTempMax);
    }
  } else {
    Serial.print("[WEATHER] Error fetching forecast, code: "); Serial.println(httpCodeForecast);
    weatherTempMin = 0;
    weatherTempMax = 0;
    Serial.print("[WEATHER] Minimum temperature today: "); Serial.println(weatherTempMin);
    Serial.print("[WEATHER] Maximum temperature today: "); Serial.println(weatherTempMax);
  }

  // Set the day and save all values
  weatherTempDay = todayStr;
  preferences.putString("day", weatherTempDay);
  preferences.putFloat("min", weatherTempMin);
  preferences.putFloat("max", weatherTempMax);
  preferences.putFloat("humidity", weatherHumidity);

  http.end();
}