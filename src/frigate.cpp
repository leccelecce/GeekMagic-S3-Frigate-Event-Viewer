#include "frigate.h"
#include <HTTPClient.h>
#include <SD_MMC.h>
#include "main.h" // For setScreen, tft, etc.

String frigateIP = "";
int frigatePort = 5000;
unsigned long lastFrigateRequest = 0;
std::vector<String> jpgQueue;
bool imagePending = false;
String pendingImageUrl = "";
String pendingZone = "";


// ------------------------
//  Display image from API
// ------------------------
void displayImageFromAPI(String url, String zone) {
  const int maxTries = 5;
  int tries = 0;
  bool success = false;
  const size_t MAX_FILE_SIZE = 40 * 1024;

  // Construct detectionId from URL
  String detectionId = url.substring(url.lastIndexOf("/events/") + 8, url.indexOf("/snapshot.jpg"));
  int dashIndex = detectionId.indexOf("-");
  String suffix = (dashIndex > 0) ? detectionId.substring(dashIndex + 1) : detectionId;

  // New filename: <suffix>-<zone>.jpg
  String filename = "/events/" + suffix + "-" + zone + ".jpg";
  if (filename.length() >= 32) {
    filename = "/events/default.jpg";
  }

  // Skip if image already exists
  if (SD_MMC.exists(filename)) {
    Serial.print("[DEBUG] Image already exists: "); Serial.println(filename);
    if (std::find(jpgQueue.begin(), jpgQueue.end(), filename) == jpgQueue.end()) {
      jpgQueue.push_back(filename);
    }
    setScreen("event", displayDuration, "displayImageFromAPI");
    return;
  }

  while (tries < maxTries && !success) {
    Serial.print("[DEBUG] Attempt "); Serial.print(tries + 1); Serial.print("/"); Serial.println(url);
    
  unsigned long timeAgoMillis = millis() - lastFrigateRequest;

    if (timeAgoMillis < 10000) {
      Serial.printf("[FRIGATE] Last Frigate Request: %lums ago\n", timeAgoMillis);
    } else {
      Serial.printf("[FRIGATE] Last Frigate Request: %lus ago\n", timeAgoMillis / 1000);
    }

    lastFrigateRequest = millis();

    http.end(); // Ensure previous connection is closed
    http.setTimeout(10000);
    http.begin(url);

    unsigned long start = millis();
    int httpCode = http.GET();
    unsigned long end = millis();

    Serial.printf("[FRIGATE] Elapsed GET time: %lu ms\n", end - start);

    if (httpCode == 200) {
      uint32_t len = http.getSize();
      if (len > MAX_FILE_SIZE) {
        Serial.println("[ERROR] Image too large: " + String(len) + " bytes");
        setScreen("error", 10, "displayImageFromAPI");
        tft.setCursor(10, 30);
        tft.setTextColor(TFT_RED);
        tft.setTextSize(2);
        tft.println("Image too large");
        http.end();
        return;
      }
      Serial.println("[DEBUG] Image size: " + String(len) + " bytes");

      // Remove oldest image if maxImages is reached
      int jpgCount = 0;
      String oldestFile = "";
      unsigned long oldestTime = ULONG_MAX;
      File root = SD_MMC.open("/events");
      if (root && root.isDirectory()) {
        File file = root.openNextFile();
        while (file) {
          String fname = file.name();
          if (fname.endsWith(".jpg")) {
            jpgCount++;
            if (!fname.startsWith("/")) {
              fname = "/events/" + fname;
            }
            unsigned long mtime = file.getLastWrite();
            if (mtime < oldestTime) {
              oldestTime = mtime;
              oldestFile = fname;
            }
          }
          file = root.openNextFile();
        }
        root.close();
        if (jpgCount >= maxImages && !oldestFile.isEmpty()) {
          SD_MMC.remove(oldestFile);
          Serial.println("[DEBUG] Removed: " + oldestFile);
        }
      }

      WiFiClient *stream = http.getStreamPtr();
      uint8_t *jpgData = (uint8_t*)malloc(len);
      if (!jpgData) {
        Serial.println("[ERROR] Memory allocation failed!");
        http.end();
        return;
      }

      if (stream->available()) {
        size_t bytesRead = stream->readBytes((char*)jpgData, len);
        File file = SD_MMC.open(filename, FILE_WRITE);
        if (file) {
          size_t written = file.write(jpgData, len);
          file.close();
          if (written == len) {
            Serial.println("[DEBUG] Image saved: " + filename);
            success = true;
            if (std::find(jpgQueue.begin(), jpgQueue.end(), filename) == jpgQueue.end()) {
              jpgQueue.push_back(filename);
            }
            setScreen("event", displayDuration, "displayImageFromAPI");
          }
        }
        free(jpgData);
      }
      http.end();
    } else {
      Serial.println("[WARNING] HTTP GET failed: " + String(httpCode) + " - " + http.getString());
      http.end();
      tries++;
      delay(2000);
    }
  }

  if (!success) {
    Serial.println("[ERROR] Failed to load image after " + String(maxTries) + " attempts");
    setScreen("error", 10, "displayImageFromAPI");
    tft.setCursor(10, 30);
    tft.setTextColor(TFT_RED);
    tft.setTextSize(2);
    tft.println("Loading failed");
  }
}



void frigateKeepAlive() { 
   if (WiFi.status() == WL_CONNECTED) {

    http.end(); // Ensure any previous instance is closed

    String healthCheckUrl = "http://" + frigateIP + ":" + String(frigatePort) + "/api/version";
    http.setTimeout(1000);// short timeout for keep-alive
    
    http.begin(healthCheckUrl);
    
    //Serial.println("[FRIGATE] Sending GET: " + healthCheckUrl);

    unsigned long start = millis();
    int httpCode = http.GET();
    unsigned long end = millis();

    if (end - start > 50) {
      Serial.printf("[FRIGATE] Keep-alive took too long: %lu ms\n", end - start);
    }

    if (httpCode != 200) {
      Serial.printf("[FRIGATE] Keep-alive failed with code: %d\n", httpCode);
      Serial.println("[FRIGATE] HTTP error message: " + http.errorToString(httpCode));
    }

    http.end();
  }
}