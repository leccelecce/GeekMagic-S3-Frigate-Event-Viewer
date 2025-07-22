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
float weatherRainMM = 0.0;
String weatherApiKey = "";
String weatherCity = "";
// Add cached coordinates
float cachedLat = 0.0;
float cachedLon = 0.0;
String cachedCity = "";

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

bool fetchCoordinates() {
  Serial.println("[WEATHER] Fetching coordinates from geocoding API");
  
  HTTPClient http;
  String geocodingUrl = "http://api.openweathermap.org/geo/1.0/direct?q=" + weatherCity + "&limit=1&appid=" + weatherApiKey;
  Serial.print("[WEATHER] Fetching coordinates from: "); Serial.println(geocodingUrl);
  http.begin(geocodingUrl);
  int httpCodeGeo = http.GET();
  
  bool success = false;
  
  if (httpCodeGeo == 200) {
    String geoPayload = http.getString();
    JsonDocument geoDoc;
    DeserializationError error = deserializeJson(geoDoc, geoPayload);
    
    if (!error && geoDoc.size() > 0) {
      cachedLat = geoDoc[0]["lat"] | 0.0;
      cachedLon = geoDoc[0]["lon"] | 0.0;
      cachedCity = weatherCity;
      
      // Save coordinates to preferences
      preferences.begin("weather", false);
      preferences.putFloat("lat", cachedLat);
      preferences.putFloat("lon", cachedLon);
      preferences.putString("geoCity", cachedCity);
      preferences.end();
      
      Serial.printf("[WEATHER] Coordinates cached - lat: %.6f, lon: %.6f\n", cachedLat, cachedLon);
      success = true;
    } else {
      Serial.println("[WEATHER] Error parsing geocoding response or city not found");
    }
  } else {
    Serial.print("[WEATHER] Error fetching coordinates, code: "); Serial.println(httpCodeGeo);
  }
  
  http.end();
  return success;
}

void loadCachedCoordinates() {
  preferences.begin("weather", true);
  cachedLat = preferences.getFloat("lat", 0.0);
  cachedLon = preferences.getFloat("lon", 0.0);
  cachedCity = preferences.getString("geoCity", "");
  preferences.end();
  
  Serial.printf("[WEATHER] Loaded cached coordinates - lat: %.6f, lon: %.6f, city: %s\n", 
                cachedLat, cachedLon, cachedCity.c_str());
}

void fetchWeather() {
  Serial.println("[WEATHER] fetchWeather() started");

  if (weatherApiKey == "" || weatherCity == "") {
    Serial.println("[WEATHER] No weatherApiKey or city set!");
    return;
  }

  // Check if we need to fetch coordinates
  bool needsCoordinates = false;

  loadCachedCoordinates();
  
  if (cachedLat == 0.0 && cachedLon == 0.0) {
    Serial.println("[WEATHER] No cached coordinates found");
    needsCoordinates = true;
  } else if (cachedCity != weatherCity) {
    Serial.println("[WEATHER] City changed, need new coordinates");
    needsCoordinates = true;
  }
  
  if (needsCoordinates) {
    if (!fetchCoordinates()) {
      Serial.println("[WEATHER] Failed to fetch coordinates, aborting weather fetch");
      return;
    }
  }

  // Determine current day as string
  time_t now = time(nullptr);
  struct tm *tm_now = localtime(&now);
  char dateStr[11];
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", tm_now);
  String todayStr(dateStr);
  Serial.print("[WEATHER] Current date (todayStr): "); Serial.println(todayStr);

  // Use cached coordinates for One Call API 3.0
  String oneCallUrl = "https://api.openweathermap.org/data/3.0/onecall?lat=" + String(cachedLat, 6) + 
                      "&lon=" + String(cachedLon, 6) + "&appid=" + weatherApiKey + "&units=metric&exclude=minutely,hourly,alerts";
  Serial.print("[WEATHER] Fetching weather from One Call API: "); Serial.println(oneCallUrl);
  
  HTTPClient http;
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
          weatherRainMM = today["rain"] | 0.0; // Rain in mm, if available
          
          Serial.print("[WEATHER] Minimum temperature today: "); Serial.println(weatherTempMin);
          Serial.print("[WEATHER] Maximum temperature today: "); Serial.println(weatherTempMax);
          Serial.print("[WEATHER] Rain today: "); Serial.println(weatherRainMM);

          // Set the day and save all values
          weatherTempDay = todayStr;
          preferences.putString("day", weatherTempDay);
          preferences.putFloat("min", weatherTempMin);
          preferences.putFloat("max", weatherTempMax);
          preferences.putFloat("rain", weatherRainMM);
        } else {
          Serial.println("[WEATHER] No daily forecast data available");
          weatherTempMin = 0;
          weatherTempMax = 0;
          weatherRainMM = 0;
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