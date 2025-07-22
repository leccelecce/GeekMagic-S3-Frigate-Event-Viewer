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

  HTTPClient http;
  
  // First, get coordinates from geocoding API
  // WeatherCity should be in the format "City,CountryCode" (e.g., "Berlin,DE") with ISO 3166-1 alpha-2 country code
  String geocodingUrl = "http://api.openweathermap.org/geo/1.0/direct?q=" + weatherCity + "&limit=1&appid=" + weatherApiKey;
  Serial.print("[WEATHER] Fetching coordinates from: "); Serial.println(geocodingUrl);
  http.begin(geocodingUrl);
  int httpCodeGeo = http.GET();
  
  float lat = 0.0;
  float lon = 0.0;
  
  if (httpCodeGeo == 200) {
    String geoPayload = http.getString();
    JsonDocument geoDoc;
    DeserializationError error = deserializeJson(geoDoc, geoPayload);
    
    if (!error && geoDoc.size() > 0) {
      lat = geoDoc[0]["lat"] | 0.0;
      lon = geoDoc[0]["lon"] | 0.0;
      Serial.printf("[WEATHER] Coordinates - lat: %.6f, lon: %.6f\n", lat, lon);
    } else {
      Serial.println("[WEATHER] Error parsing geocoding response or city not found");
      http.end();
      return;
    }
  } else {
    Serial.print("[WEATHER] Error fetching coordinates, code: "); Serial.println(httpCodeGeo);
    http.end();
    return;
  }
  http.end();

  // Use One Call API 3.0 for weather data
  String oneCallUrl = "https://api.openweathermap.org/data/3.0/onecall?lat=" + String(lat, 6) + 
                      "&lon=" + String(lon, 6) + "&appid=" + weatherApiKey + "&units=metric&exclude=minutely,hourly,alerts";
  Serial.print("[WEATHER] Fetching weather from One Call API: "); Serial.println(oneCallUrl);
  
  http.begin(oneCallUrl);
  int httpCodeOneCall = http.GET();

  if (httpCodeOneCall == 200) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      // Current weather data
      weatherTemp = doc["current"]["temp"] | 0.0;
      weatherHumidity = doc["current"]["humidity"] | 0.0;
      weatherIcon = doc["current"]["weather"][0]["icon"].as<String>();

      Serial.print("[WEATHER] Current temperature: "); Serial.println(weatherTemp);
      Serial.print("[WEATHER] Current humidity: "); Serial.println(weatherHumidity);
      Serial.print("[WEATHER] Icon: "); Serial.println(weatherIcon);

      // Only update min/max on a new day
      if (todayStr != weatherTempDay) {
        // Get today's min/max from daily forecast (first entry is today)
        if (doc["daily"].size() > 0) {
          JsonObject today = doc["daily"][0];
          weatherTempMin = today["temp"]["min"] | 0.0;
          weatherTempMax = today["temp"]["max"] | 0.0;
          
          Serial.print("[WEATHER] Minimum temperature today: "); Serial.println(weatherTempMin);
          Serial.print("[WEATHER] Maximum temperature today: "); Serial.println(weatherTempMax);
          
          // Set the day and save all values
          weatherTempDay = todayStr;
          preferences.putString("day", weatherTempDay);
          preferences.putFloat("min", weatherTempMin);
          preferences.putFloat("max", weatherTempMax);
        } else {
          Serial.println("[WEATHER] No daily forecast data available");
          weatherTempMin = 0;
          weatherTempMax = 0;
        }
      } else {
        Serial.println("[WEATHER] Min/max already fetched for this day, no update needed.");
      }
      
      preferences.putFloat("humidity", weatherHumidity);
    } else {
      Serial.print("[WEATHER] JSON parse error: "); Serial.println(error.c_str());
    }
  } else {
    Serial.print("[WEATHER] Error fetching weather data, code: "); Serial.println(httpCodeOneCall);
  }

  http.end();
}