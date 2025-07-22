#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncMqttClient.h>
#include <TJpg_Decoder.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <FS.h>
#include <SD_MMC.h>
#include <SPIFFS.h>
#include <time.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <vector>
#include <algorithm>

#include "frigate.h"
#include "mqtt.h"
#include "weather.h"

//
// Hardware Settings
//
//Micro SD Card PIN
#define SD_SCLK_PIN 21
#define SD_MOSI_PIN 18
#define SD_MISO_PIN 16

// ------------------------
//  Globals & Config
// ------------------------
Preferences preferences;

// Constants
const char* CLIENT_ID = "ESP32Client";
const char* DEFAULT_SSID = "ESP32_AP";
const char* DEFAULT_PASSWORD = "admin1234";
const unsigned long WIFI_TIMEOUT = 10000;
const unsigned long MQTT_RECONNECT_INTERVAL = 5000;
const unsigned long FRIGATE_KEEPALIVE_INTERVAL = 25UL * 1000UL; // 25 seconds

// Clock consts
const unsigned long CLOCK_REFRESH_INTERVAL = 1000UL; // 1 second
const char* daysShort[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// Variables
String lastDate = "";
String mode = "alert"; // Default to "alert"
String currentScreen = "clock"; // ["clock", "event", "status", "error"]
TFT_eSPI tft = TFT_eSPI();
AsyncWebServer server(80);
HTTPClient http;


int displayDuration = 30;
int maxImages = 30;
unsigned long lastClockUpdate = 0;
unsigned long lastKeyTime = 0;
unsigned long screenTimeout = 0;
unsigned long screenSince = 0;

// --- WEATHER ---
unsigned long lastWeatherFetch = 0;
const unsigned long WEATHER_REFRESH_INTERVAL = 15UL * 60UL * 1000UL; // 10 minutes

// --- Slideshow ---
bool slideshowActive = false;
unsigned long slideshowStart = 0;
int currentSlideshowIdx = 0;
unsigned long slideshowInterval = 3000; // Interval in ms
std::vector<unsigned long> eventCallTimes;
unsigned long lastEventCall = 0;

// ------------------------
//  Forward Declarations
// ------------------------
void setScreen(const String& newScreen, unsigned long timeoutSec = 0, const char* by = "");

// ------------------------
//  JPEG render callback
// ------------------------
bool jpgRenderCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  tft.pushImage(x, y, w, h, bitmap);
  return true;
}

// ------------------------
//  Slideshow handler
// ------------------------
void handleSlideshow() {
  unsigned long now = millis();

  // Check if slideshow is active and jpgQueue is not empty
  if (!slideshowActive || jpgQueue.empty()) {
    slideshowActive = false;
    return;
  }

  // Check if displayDuration has elapsed
  if (now - slideshowStart >= displayDuration * 1000UL) {
    Serial.println("[SLIDESHOW] Display duration expired, stopping slideshow");
    slideshowActive = false;
    jpgQueue.clear();
    eventCallTimes.clear();
    setScreen("clock", 0, "slideshow timeout");
    return;
  }

  // Check if it's time for the next image
  if (now - slideshowStart >= currentSlideshowIdx * slideshowInterval) {
    String filename = jpgQueue[currentSlideshowIdx % jpgQueue.size()];
    if (SD_MMC.exists(filename)) {
      File file = SD_MMC.open(filename, FILE_READ);
      if (file) {
        uint32_t fileSize = file.size();
        uint8_t* jpgData = (uint8_t*)malloc(fileSize);
        if (jpgData) {
          size_t bytesRead = file.readBytes((char*)jpgData, fileSize);
          file.close();
          if (bytesRead == fileSize) {
            tft.fillScreen(TFT_BLACK);
            TJpgDec.drawJpg(0, 0, jpgData, fileSize);
            Serial.println("[SLIDESHOW] Displayed: " + filename);
          }
          free(jpgData);
        } else {
          file.close(); // Ensure file is closed even if malloc fails
          Serial.println("[SLIDESHOW] Memory allocation failed for: " + filename);
        }
      } else {
        Serial.println("[SLIDESHOW] Cannot open file: " + filename);
      }
    } else {
      Serial.println("[SLIDESHOW] Image not found: " + filename);
    }

    currentSlideshowIdx++;
  }
}

// ------------------------
//  STATE-based screen management
// ------------------------
void setScreen(const String& newScreen, unsigned long timeoutSec, const char* by) {
  Serial.printf("setScreen: from %s to %s (timeout: %lu sec) by: %s\n", currentScreen.c_str(), newScreen.c_str(), timeoutSec, by);

  // Remove old event calls outside displayDuration
  unsigned long now = millis();
  eventCallTimes.erase(
    std::remove_if(eventCallTimes.begin(), eventCallTimes.end(),
      [now](unsigned long t) { return now - t > displayDuration * 1000UL; }),
    eventCallTimes.end()
  );

  // Handle "event" screen
  if (newScreen == "event") {
    // Add current time to eventCallTimes
    eventCallTimes.push_back(now);
    lastEventCall = now;

    // Check if slideshow should be started (more than 1 call within displayDuration)
    if (eventCallTimes.size() > 1 && !slideshowActive) {
      Serial.println("[SLIDESHOW] Starting slideshow due to multiple event calls");
      slideshowActive = true;
      slideshowStart = now;
      currentSlideshowIdx = 0;
    }

    // If slideshow is active, delegate to handleSlideshow
    if (slideshowActive && !jpgQueue.empty()) {
      handleSlideshow();
    } else if (!slideshowActive && !jpgQueue.empty()) {
      // Display single image
      String filename = jpgQueue[0];
      if (SD_MMC.exists(filename)) {
        File file = SD_MMC.open(filename, FILE_READ);
        if (file) {
          uint32_t fileSize = file.size();
          uint8_t* jpgData = (uint8_t*)malloc(fileSize);
          if (jpgData) {
            size_t bytesRead = file.readBytes((char*)jpgData, fileSize);
            file.close();
            if (bytesRead == fileSize) {
              tft.fillScreen(TFT_BLACK);
              TJpgDec.drawJpg(0, 0, jpgData, fileSize);
              Serial.println("[DEBUG] Displayed single image: " + filename);
            }
            free(jpgData);
          } else {
            file.close(); // Ensure file is closed even if malloc fails
            Serial.println("[DEBUG] Memory allocation failed for: " + filename);
          }
        } else {
          Serial.println("[DEBUG] Cannot open file: " + filename);
        }
      } else {
        Serial.println("[DEBUG] Image not found: " + filename);
      }
    }

    currentScreen = "event";
    screenTimeout = (timeoutSec == 0) ? 0 : timeoutSec * 1000UL;
    screenSince = now;
    return;
  }

  // Handle other screens (clock, error, etc.)
  if (currentScreen != newScreen) {
    currentScreen = newScreen;
    tft.fillScreen(TFT_BLACK);

    if (newScreen == "clock") {
      lastDrawnWeatherIcon = "";
      lastDate = "";
      slideshowActive = false;
      jpgQueue.clear();
      eventCallTimes.clear();
    }
  }
  screenTimeout = (timeoutSec == 0) ? 0 : timeoutSec * 1000UL;
  screenSince = now;
}

// ------------------------
//  Clock display
// ------------------------
void showClock() {
  bool isScreenTransition = (currentScreen != "clock");
  if (isScreenTransition) {
    tft.fillScreen(TFT_BLACK);
  }

  time_t now = time(nullptr);
  if (now < 100000) {
    Serial.println("[showClock] Time not yet synchronized.");
  }
  struct tm *tm_info = localtime(&now);
  if (!tm_info) return;

  // Date
  String enDate = String(daysShort[tm_info->tm_wday]) + " " +
                  String(tm_info->tm_mday) + " " +
                  String(months[tm_info->tm_mon]);
  if (isScreenTransition || enDate != lastDate) {
    tft.fillRect(0, 0, 240, 25, TFT_BLACK);
    tft.setTextSize(3);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    int dateWidth = tft.textWidth(enDate);
    int dateX = (240 - dateWidth) / 2;
    tft.setCursor(dateX, 10);
    tft.println(enDate);
    lastDate = enDate;
  }

  // Time
  if (millis() - lastClockUpdate > 1000 || isScreenTransition) {
    lastClockUpdate = millis();
    char timeBuffer[20];
    strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", tm_info);
    tft.setTextSize(5);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    int timeWidth = tft.textWidth(timeBuffer);
    int timeX = (240 - timeWidth) / 2;
    tft.setCursor(timeX, 45);
    tft.println(timeBuffer);
  }

  // Temperature
  String tempValue = String(weatherTemp, 1);
  String tempUnit = "÷c";
  String humidityValue = String((int)weatherHumidity);
  String humidityUnit = "%";
  String tempMinValue = String(weatherTempMin, 1);
  String tempMinUnit = "÷c";
  String tempMaxValue = String(weatherTempMax, 1);
  String tempMaxUnit = "÷c";
  String weatherRainMMValue = String(weatherRainMM, 1);

  tft.setTextSize(4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 105);
  tft.print(tempValue);
  int tempValueWidth = tft.textWidth(tempValue);
  tft.setTextSize(2);
  tft.setCursor(10 + tempValueWidth, 105);
  tft.print(tempUnit);
  int tempUnitWidth = tft.textWidth(tempUnit);
  tft.setTextSize(4);
  tft.setCursor(10 + tempValueWidth + tempUnitWidth + 5, 105);
  tft.print(humidityValue);
  int humidityValueWidth = tft.textWidth(humidityValue);
  tft.setTextSize(3);
  tft.setCursor(10 + tempValueWidth + tempUnitWidth + 5 + humidityValueWidth, 105);
  tft.print(humidityUnit);

  // Min label
  tft.setTextSize(2);
  tft.setCursor(2, 160);
  tft.print("Min ");
  int minLabelWidth = tft.textWidth("Min ");
  tft.setTextSize(3);
  tft.setCursor(2 + minLabelWidth, 155);
  tft.print(tempMinValue);
  int tempMinValueWidth = tft.textWidth(tempMinValue);
  tft.setTextSize(2);
  tft.setCursor(2 + minLabelWidth + tempMinValueWidth, 160);
  tft.print(tempMinUnit);

  // Max label
  tft.setTextSize(2);
  tft.setCursor(2, 190);
  tft.print("Max ");
  int maxLabelWidth = tft.textWidth("Max ");
  tft.setTextSize(3);
  tft.setCursor(2 + maxLabelWidth, 185);
  tft.print(tempMaxValue);
  int tempMaxValueWidth = tft.textWidth(tempMaxValue);
  tft.setTextSize(2);
  tft.setCursor(2 + maxLabelWidth + tempMaxValueWidth, 190);
  tft.print(tempMaxUnit);

  // Rain MM
  tft.setTextSize(2);
  tft.setCursor(2, 220);
  tft.print("Rain ");
  int rainLabelWidth = tft.textWidth("Rain ");
  tft.setTextSize(3);
  tft.setCursor(2 + rainLabelWidth, 215);
  tft.print(weatherRainMMValue);
  int rainValueWidth = tft.textWidth(weatherRainMMValue);
  tft.setTextSize(2);
  tft.setCursor(2 + rainLabelWidth + rainValueWidth, 220);
  tft.print("mm");

  // Weather icon
  if (isScreenTransition || weatherIcon != lastDrawnWeatherIcon) {
    showWeatherIconJPG(weatherIcon);
    lastDrawnWeatherIcon = weatherIcon;
  }
  currentScreen = "clock";
}



// ------------------------
//  Preferences Config Struct Helper
// ------------------------
struct Config {
  String ssid;
  String pwd;
  String mqtt;
  int mqttPort;
  String mqttUser;
  String mqttPass;
  String fip;
  int fport;
  int sec;
  String mode;
  String weatherApiKey;
  String weatherCity;
  int maxImages;
  int timezone;
};

Config getConfig() {
  Config config;
  preferences.begin("config", true);
  config.ssid = preferences.getString("ssid", "");
  config.pwd = preferences.getString("pwd", "");
  config.mqtt = preferences.getString("mqtt", "");
  config.mqttPort = preferences.getInt("mqttport", 1883);
  config.mqttUser = preferences.getString("mqttuser", "");
  config.mqttPass = preferences.getString("mqttpass", "");
  config.fip = preferences.getString("fip", "");
  config.fport = preferences.getInt("fport", 5000);
  config.sec = preferences.getInt("sec", 30);
  config.mode = preferences.getString("mode", "alert");
  config.weatherApiKey = preferences.getString("weatherApiKey", "");
  config.weatherCity = preferences.getString("weatherCity", "");
  config.maxImages = preferences.getInt("maxImages", 10);
  config.timezone = preferences.getInt("timezone", 0);
  preferences.end();
  return config;
}

String formatTimestamp(unsigned long mtime) {
  char buf[20];
  time_t t = (time_t)mtime;
  struct tm *tm_info = localtime(&t);
  if (!tm_info) return "";
  strftime(buf, sizeof(buf), "%d/%m/%y %H:%M:%S", tm_info);
  return String(buf);
}

// ------------------------
//  SD_MMC images list helper
// ------------------------
String getImagesList() {
  String html = "<ul class='image-list'>";
  File root = SD_MMC.open("/events");
  
  if (!root || !root.isDirectory()) {
    if (root) root.close(); // Close if opened but not directory
    html += "<li>Could not open SD_MMC</li>";
    html += "</ul>";
    return html;
  }

  struct FileInfo {
    String name;
    String displayName;
    unsigned long size;
    unsigned long mtime;
  };
  std::vector<FileInfo> files;

  File file = root.openNextFile();
  while (file) {
    String fname = file.name();
    if (fname.endsWith(".jpg")) {
      String displayName = fname;
      if (!fname.startsWith("/")) {
        fname = "/events/" + fname;
      }
      FileInfo info;
      info.name = fname;
      info.displayName = displayName;
      info.size = (unsigned long)file.size();
      info.mtime = file.getLastWrite();
      files.push_back(info);
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();

  std::sort(files.begin(), files.end(), [](const FileInfo& a, const FileInfo& b) {
    return a.mtime > b.mtime;
  });

  for (const FileInfo& info : files) {
    html += "<li>";
    html += "<img src='" + info.name + "' alt='Event image'>";
    html += "<a href='" + info.name + "'>" + info.displayName + "</a>";
    html += "<span>" + String(info.size) + " bytes</span>";
    html += " <span>" + formatTimestamp(info.mtime) + "</span></li>";
  }

  html += "</ul>";
  return html;
}

// ------------------------
//  Helper for checked attr
// ------------------------
String getCheckedAttribute(bool isChecked) {
  return isChecked ? "checked" : "";
}

// ------------------------
//  Webinterface
// ------------------------
void setupWebInterface() {
  server.serveStatic("/styles.css", SPIFFS, "/styles.css", "no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/scripts.js", SPIFFS, "/scripts.js", "no-store, no-cache, must-revalidate, max-age=0");
  server.serveStatic("/icons", SPIFFS, "/icons");

  // event images stored on SD instead of SPIFFS for longevity / capacity
  server.serveStatic("/events", SD_MMC, "/events", "max-age=3600");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    Config config = getConfig();
    String modeLower = config.mode;
    modeLower.toLowerCase();
    bool isAlertChecked = modeLower.indexOf("alert") >= 0;
    bool isDetectionChecked = modeLower.indexOf("detection") >= 0;

    String alertCheckbox = "<input type='checkbox' id='mode_alert' name='mode_alert' value='alert' " + getCheckedAttribute(isAlertChecked) + ">";
    String detectionCheckbox = "<input type='checkbox' id='mode_detection' name='mode_detection' value='detection' " + getCheckedAttribute(isDetectionChecked) + ">";

    File file = SPIFFS.open("/index.html", "r");
    if (!file) {
      request->send(500, "text/plain", "Could not open index.html");
      return;
    }
    String html = file.readString();
    file.close();

    // Fill HTML placeholders
    html.replace("{{ssid}}", config.ssid);
    html.replace("{{pwd}}", config.pwd != "" ? "******" : "");
    html.replace("{{pwd_exists}}", config.pwd != "" ? "1" : "0");
    html.replace("{{mqtt}}", config.mqtt);
    html.replace("{{mqttport}}", String(config.mqttPort));
    html.replace("{{mqttuser}}", config.mqttUser);
    html.replace("{{mqttpass}}", config.mqttPass != "" ? "******" : "");
    html.replace("{{mqttpass_exists}}", config.mqttPass != "" ? "1" : "0");
    html.replace("{{fip}}", config.fip);
    html.replace("{{fport}}", String(config.fport));
    html.replace("{{sec}}", String(config.sec));
    html.replace("{{maxImages}}", String(config.maxImages));
    html.replace("{{slideshowInterval}}", String(slideshowInterval));
    html.replace("{{alertCheckbox}}", alertCheckbox);
    html.replace("{{detectionCheckbox}}", detectionCheckbox);
    html.replace("{{weatherApiKey}}", config.weatherApiKey != "" ? "******" : "");
    html.replace("{{weatherApiKey_exists}}", config.weatherApiKey != "" ? "1" : "0");
    html.replace("{{weatherCity}}", config.weatherCity);
    html.replace("{{timezone}}", String(config.timezone));
    html.replace("{{totalBytes}}", String(SD_MMC.totalBytes() / 1024));
    html.replace("{{usedBytes}}", String(SD_MMC.usedBytes() / 1024));
    html.replace("{{freeBytes}}", String((SD_MMC.totalBytes() - SD_MMC.usedBytes()) / 1024));
    html.replace("{{imagesList}}", getImagesList());

    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", html);
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "-1");
    request->send(response);
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Safe integer fetch helper
    auto getIntParam = [](AsyncWebServerRequest* req, const char* name, int def) -> int {
      if (req->getParam(name, true)) {
        String v = req->getParam(name, true)->value();
        if (v.length() > 0) return v.toInt();
      }
      return def;
    };
  
    preferences.begin("config", false);
    String currentPwd = preferences.getString("pwd", "");
    String currentMqttPass = preferences.getString("mqttpass", "");
    String currentApiKey = preferences.getString("weatherApiKey", "");
  
    String newSSID = request->getParam("ssid", true)->value();
    preferences.putString("ssid", newSSID);
  
    String newPwd = request->getParam("pwd", true)->value();
    bool pwdExists = request->getParam("pwd_exists", true)->value() == "1";
    if (!pwdExists || newPwd != "******") {
      preferences.putString("pwd", newPwd);
    }
  
    String newMqtt = request->getParam("mqtt", true)->value();
    preferences.putString("mqtt", newMqtt);
  
    int newMqttPort = getIntParam(request, "mqttport", 0);
    if (newMqttPort < 1 || newMqttPort > 65535) newMqttPort = 1883;
    preferences.putInt("mqttport", newMqttPort);
  
    String newMqttUser = request->getParam("mqttuser", true)->value();
    preferences.putString("mqttuser", newMqttUser);
  
    String newMqttPass = request->getParam("mqttpass", true)->value();
    bool mqttPassExists = request->getParam("mqttpass_exists", true)->value() == "1";
    if (!mqttPassExists || newMqttPass != "******") {
      preferences.putString("mqttpass", newMqttPass);
    }
  
    String newFip = request->getParam("fip", true)->value();
    preferences.putString("fip", newFip);
  
    int newFport = getIntParam(request, "fport", 0);
    if (newFport < 1 || newFport > 65535) newFport = 5000;
    preferences.putInt("fport", newFport);
  
    int newSec = getIntParam(request, "sec", 0);
    if (newSec < 1) newSec = 30;
    preferences.putInt("sec", newSec);
    displayDuration = newSec;
  
    int newMaxImages = getIntParam(request, "maxImages", 0);
    if (newMaxImages < 1) newMaxImages = 1;
    if (newMaxImages > 60) newMaxImages = 60;
    preferences.putInt("maxImages", newMaxImages);
    maxImages = newMaxImages;
  
    int newSlideshowInterval = getIntParam(request, "slideshowInterval", 0);
    if (newSlideshowInterval < 500) newSlideshowInterval = 3000;
    if (newSlideshowInterval > 20000) newSlideshowInterval = 20000;
    slideshowInterval = newSlideshowInterval;
    preferences.putInt("slideInterval", slideshowInterval);
  
    // Modes
    String modeValue = "";
    if (request->hasParam("mode_alert", true)) {
      if (request->getParam("mode_alert", true)->value() == "alert") {
        modeValue += "alert";
      }
    }
    if (request->hasParam("mode_detection", true)) {
      if (request->getParam("mode_detection", true)->value() == "detection") {
        if (modeValue != "") modeValue += ",";
        modeValue += "detection";
      }
    }
    preferences.putString("mode", modeValue);
    mode = modeValue;
  
    // Weather
    String newApiKey = request->getParam("weatherApiKey", true)->value();
    bool apiKeyExists = request->getParam("weatherApiKey_exists", true)->value() == "1";
    if (!apiKeyExists || newApiKey != "******") {
      preferences.putString("weatherApiKey", newApiKey);
      fetchWeather();
    }
    String newCity = request->getParam("weatherCity", true)->value();
    preferences.putString("weatherCity", newCity);
  
    int newTimezone = getIntParam(request, "timezone", 0);
    preferences.putInt("timezone", newTimezone);
  
    preferences.end();
  
    bool mqttConfigChanged = (newMqtt != mqttServer || newMqttPort != mqttPort || newMqttUser != mqttUser || (newMqttPass != "******" && newMqttPass != mqttPass));
    mqttServer = newMqtt;
    mqttPort = newMqttPort;
    mqttUser = newMqttUser;
    mqttPass = newMqttPass;
    frigatePort = newFport;
  
    if (mqttConfigChanged) {
      if (mqttClient.connected()) mqttClient.disconnect();
      mqttClient.setServer(mqttServer.c_str(), mqttPort);
      mqttClient.setCredentials(mqttUser.c_str(), mqttPass.c_str());
      mqttClient.connect();
    }

    String cacheBuster = "/?v=" + String(millis());
    request->redirect(cacheBuster);
  });

  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    File file = SPIFFS.open("/update.html", "r");
    if (!file) {
      request->send(500, "text/plain", "Could not open update.html");
      return;
    }
    String html = file.readString();
    file.close();
    request->send(200, "text/html", html);
  });

  server.on("/update", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      bool ok = !Update.hasError();
      request->send(200, "text/plain", ok ? "Update completed. Restarting..." : "Update failed!");
      if (ok) {
        delay(1000);
        ESP.restart();
      }
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if (!index) {
        Update.begin();
      }
      if (!Update.hasError()) {
        Update.write(data, len);
      }
      if (final) {
        Update.end(true);
      }
    });

  server.on("/delete_all", HTTP_GET, [](AsyncWebServerRequest *request) {
    int deleted = 0;
    File root = SD_MMC.open("/events");
    if (!root || !root.isDirectory()) {
      if (root) root.close(); // Close if opened but not directory
      request->send(500, "text/plain", "Could not open /events");
      return;
    }
    File file = root.openNextFile();
    while (file) {
      String fname = file.name();
      if (fname.endsWith(".jpg")) {
        if (!fname.startsWith("/")) {
          fname = "/events/" + fname;
        }
        if (SD_MMC.exists(fname) && SD_MMC.remove(fname)) {
          // remove() automatically closes the file
          deleted++;
        }
      }
      file = root.openNextFile();
    }
    root.close();
    String response = "Deleted: " + String(deleted) + " images";
    request->send(200, "text/plain", response);
    String cacheBuster = "/?v=" + String(millis());
    request->redirect(cacheBuster);
  });

  server.on("/show_image", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("url")) {
      String url = request->getParam("url")->value();
      pendingImageUrl = url;
      imagePending = true;
      request->send(200, "text/plain", "Image will be shown on display!");
    } else {
      request->send(400, "text/plain", "Missing url parameter");
    }
  });

  server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
    Serial.println("[WEB] Reboot requested via /reboot");
    request->send(200, "text/plain", "Rebooting ESP32...");
    delay(500);
    ESP.restart();
});

  server.begin();
}

// ------------------------
// WiFi connection
// ------------------------
void setupWiFi() {
  preferences.begin("config", false);
  String ssid = preferences.getString("ssid", "");
  String pwd = preferences.getString("pwd", "");
  preferences.end();

  bool fallbackAP = false;

  if (ssid == "") {
    Serial.println("No saved SSID, switching to AP mode...");
    fallbackAP = true;
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pwd.c_str());
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < WIFI_TIMEOUT) {
      delay(500);
      Serial.println("Connecting to WiFi...");
    }
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi failed, fallback to AP mode...");
      fallbackAP = true;
    } else {
      Serial.println("WiFi connected to: " + ssid);
      Serial.println("BSSID: " + WiFi.BSSIDstr());
      Serial.println("IP address: " + WiFi.localIP().toString());
      Serial.println("MAC: " + WiFi.macAddress());
      setScreen("statusWiFi", 10, "show_wifi_status");
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.setTextSize(2);
      tft.setCursor(10, 40);
      tft.println("Connected to:");
      tft.setCursor(10, 70);
      tft.println(ssid);
      tft.setCursor(10, 100);
      tft.println(WiFi.BSSIDstr());
      tft.setCursor(10, 140);
      tft.println("IP / MAC:");
      tft.setCursor(10, 170);
      tft.println(WiFi.localIP());
      tft.setCursor(10, 200);
      tft.println(WiFi.macAddress());
    }
  }

  if (fallbackAP) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(DEFAULT_SSID, DEFAULT_PASSWORD);
    setScreen("apmode", 86400, "fallbackAP");
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 30);
    tft.println("WiFi not connected");
    tft.setTextSize(3);
    tft.setCursor(20, 60);
    tft.println("**AP MODE**");
    tft.setTextSize(2);
    tft.setCursor(10, 110);
    tft.println("SSID: " + String(DEFAULT_SSID));
    tft.setCursor(10, 140);
    tft.println("PWD: " + String(DEFAULT_PASSWORD));
    tft.setCursor(10, 170);
    tft.println("IP: 192.168.4.1");
  }

  setupWebInterface();
}

void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
 File root = fs.open(dirname,"r");
 root.rewindDirectory();
      
  while (true)
  {
    File entry =  root.openNextFile();
    if (!entry)
    {break;}

    Serial.printf("[SD_MMC] File: %s [size: %luB]\n", entry.name(), (unsigned long)entry.size());
    entry.close();
  }

}

void setup_SPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    setScreen("error", 30, "setup_error");
    tft.setTextColor(TFT_RED);
    tft.setTextSize(2);
    tft.println("SPIFFS failed");
    while (true) delay(1000);
  }
}

void setupSD_MMC() {

  SD_MMC.setPins(SD_SCLK_PIN, SD_MOSI_PIN, SD_MISO_PIN);
  if (!SD_MMC.begin("/sdcard", true, true, 4000000)) {
    Serial.println("[SD_MMC] Card Mount Failed");
    return;
  }

  uint8_t cardType = SD_MMC.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("[SD_MMC] No SD_MMC card attached");
    return;
  }

  Serial.print("[SD_MMC] SD Card Type: ");
  if(cardType == CARD_MMC){
    Serial.println("MMC");
  } else if(cardType == CARD_SD){
    Serial.println("SDSC");
  } else if(cardType == CARD_SDHC){
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }
    
  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);

  Serial.printf("[SD_MMC] Card Size: %lluMB\n", cardSize);

  if (!SD_MMC.exists("/events")) {
    SD_MMC.mkdir("/events");
  }

  Serial.println("[SD_MMC] Listing contents");
  listDir(SD_MMC, "/events", 0);
  
}



// ------------------------
// Arduino setup()
// ------------------------
void setup() {
  Serial.begin(115200);

  setup_SPIFFS();

  tft.begin();
  tft.setRotation(0);

  tft.fillScreen(TFT_BLACK);

  TJpgDec.setCallback(jpgRenderCallback);
  TJpgDec.setSwapBytes(true);

  preferences.begin("config", false);
  mqttServer = preferences.getString("mqtt", "");
  mqttPort = preferences.getInt("mqttport", 1883);
  mqttUser = preferences.getString("mqttuser", "");
  mqttPass = preferences.getString("mqttpass", "");
  frigateIP = preferences.getString("fip", "");
  frigatePort = preferences.getInt("fport", 5000);
  displayDuration = preferences.getInt("sec", 30);
  mode = preferences.getString("mode", "alert");
  weatherApiKey = preferences.getString("weatherApiKey", "");
  weatherCity = preferences.getString("weatherCity", "");
  weatherHumidity = preferences.getFloat("humidity", 0.0);
  weatherTempDay = preferences.getString("day", "");
  weatherTempMin = preferences.getFloat("min", 0.0);
  weatherTempMax = preferences.getFloat("max", 0.0);
  int timezoneVal = preferences.getInt("timezone", 0);
  maxImages = preferences.getInt("maxImages", 30);
  slideshowInterval = preferences.getInt("slideInterval", 3000);
  preferences.end();

  long gmtOffset_sec = timezoneVal * 3600L;
  Serial.printf("configTime: gmtOffset_sec = %ld\n", gmtOffset_sec);
  configTime(gmtOffset_sec, 0, "pool.ntp.org");

  setupSD_MMC();

  setupWiFi();

  setupMqtt();


  if (WiFi.status() == WL_CONNECTED) {

    http.end(); // Ensure any previous instance is closed

    String healthCheckUrl = "http://" + frigateIP + ":" + String(frigatePort) + "/api/version";
    http.setTimeout(20000);// allow plenty of time to establish the initial connection which can be 8-14 seconds
    
    http.begin(healthCheckUrl);
    
    Serial.println("[FRIGATE] Sending GET: " + healthCheckUrl);

    unsigned long start = millis();
    int httpCode = http.GET();
    unsigned long end = millis();

    Serial.printf("[FRIGATE] Elapsed time: %lu ms\n", end - start);

    // Expected response: "Frigate is running. Alive and healthy!"
    if (httpCode == 200) {
      Serial.println("[FRIGATE] Successfully connected to Frigate API at: " + healthCheckUrl);
      Serial.println("[FRIGATE] Frigate API v" + http.getString());
    } else {
      Serial.println("[ERROR] failed connecting to Frigate API at: " + healthCheckUrl + " with code: " + String(httpCode));
      Serial.println("[ERROR] HTTP err message: " + http.errorToString(httpCode));
    }

    http.end();

  } 

  fetchWeather();
  lastWeatherFetch = millis();
}



// ------------------------
// Arduino loop()
// ------------------------
void loop() {
  if (imagePending) {
    imagePending = false;
    displayImageFromAPI(pendingImageUrl, pendingZone);
  }

  static wl_status_t lastStatus = WL_CONNECTED;
  static unsigned long lastReconnectAttempt = 0;

  if (WiFi.status() == WL_CONNECTED && lastStatus != WL_CONNECTED && millis() - lastReconnectAttempt > MQTT_RECONNECT_INTERVAL) {
    mqttClient.connect();
    lastReconnectAttempt = millis();
  }
  lastStatus = WiFi.status();

  if (slideshowActive) {
    handleSlideshow();
  }

  if (currentScreen != "clock" && screenTimeout > 0 && millis() - screenSince > screenTimeout) {
    setScreen("clock", 0, "timeout");
  }

  if (millis() - lastFrigateRequest > FRIGATE_KEEPALIVE_INTERVAL) {
    lastFrigateRequest = millis();
    frigateKeepAlive();
  }  

  if (millis() - lastWeatherFetch > WEATHER_REFRESH_INTERVAL) {
    lastWeatherFetch = millis();
    fetchWeather();
    if (currentScreen == "clock") showClock();
  }

  if (currentScreen == "clock" && millis() - lastClockUpdate > CLOCK_REFRESH_INTERVAL) {
    showClock();
  }
}