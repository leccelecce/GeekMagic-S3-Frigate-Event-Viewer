#include "mqtt.h"
#include <ArduinoJson.h>
#include "main.h" // For setScreen, tft, etc.
#include <WiFi.h>
#include "frigate.h"

AsyncMqttClient mqttClient;
String mqttServer = "";
int mqttPort = 1883;
String mqttUser = "";
String mqttPass = "";

const char* MQTT_TOPIC = "frigate/reviews";

void setupMqtt() {
  // Set up MQTT server, credentials, and callbacks
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onMessage(onMqttMessage);

  mqttClient.setServer(mqttServer.c_str(), mqttPort);
  mqttClient.setCredentials(mqttUser.c_str(), mqttPass.c_str());
  
  if (WiFi.status() == WL_CONNECTED) mqttClient.connect();

}


// ------------------------
//  MQTT callbacks
// ------------------------
void onMqttConnect(bool sessionPresent) {
  Serial.println("[MQTT] Connected!");
  mqttClient.subscribe(MQTT_TOPIC, 0);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  static unsigned long lastReconnectAttempt = 0;
  const unsigned long reconnectInterval = 50000;
  Serial.print("[MQTT] Connection lost with reason: "); Serial.println(static_cast<int>(reason));
  unsigned long now = millis();
  if (now - lastReconnectAttempt >= reconnectInterval) {
    setScreen("error", 50, "onMqttDisconnect");
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(0, 0);
    tft.println("MQTT ERROR!");
    Serial.println("[MQTT] Attempting to reconnect...");
    mqttClient.setServer(mqttServer.c_str(), mqttPort);
    mqttClient.setCredentials(mqttUser.c_str(), mqttPass.c_str());
    mqttClient.connect();
    lastReconnectAttempt = now;
  } else {
    Serial.print("[MQTT] Waiting for next reconnect attempt in "); 
    Serial.print((reconnectInterval - (now - lastReconnectAttempt)) / 1000);
    Serial.println(" seconds");
  }
}

// ------------------------
//  MQTT message handler
// ------------------------
void onMqttMessage(
    char* topic,
    char* payload,
    AsyncMqttClientMessageProperties properties,
    size_t len,
    size_t index,
    size_t total
) {
  String payloadStr;
  for (size_t i = 0; i < len; i++) payloadStr += (char)payload[i];
  Serial.println("====[MQTT RECEIVED]====");
  Serial.print("Topic:   "); Serial.println(topic);
  Serial.print("Payload: "); Serial.println(payloadStr);

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, len);
  if (error) {
    Serial.print("[DEBUG] JSON parsing error: "); Serial.println(error.c_str());
    setScreen("error", 30, "onMqttMessage");
    tft.setTextColor(TFT_RED);
    tft.setTextSize(2);
    tft.println("JSON Parse Error");
    return;
  }

  String type = doc["type"] | "";
  JsonObject msg;
  String severity = "";

  if (type == "new" && doc["before"].is<JsonObject>()) {
    msg = doc["before"].as<JsonObject>();
  } else if (type == "update" && doc["after"].is<JsonObject>()) {
    msg = doc["after"].as<JsonObject>();
  } else {
    Serial.println("[DEBUG] Ignoring message of type '" + type + "'");
    return;
  }

  if (msg["severity"].is<String>()) severity = msg["severity"].as<String>();

  String modeClean = mode; modeClean.trim(); modeClean.toLowerCase();
  String severityClean = severity; severityClean.trim(); severityClean.toLowerCase();
  bool show = modeClean.indexOf(severityClean) >= 0;

  if (show && frigateIP.length() > 0 && msg["data"].is<JsonObject>() && msg["data"]["detections"].is<JsonArray>()) {
    JsonArray detections = msg["data"]["detections"].as<JsonArray>();
    JsonArray zonesArray = msg["data"]["zones"].is<JsonArray>() ? msg["data"]["zones"].as<JsonArray>() : JsonArray();
    String zone = "outside-zone";
    if (!zonesArray.isNull() && zonesArray.size() > 0) {
      zone = String(zonesArray[zonesArray.size() - 1].as<const char*>());
    }
  
    if (!detections.isNull() && detections.size() > 0) {
      for (JsonVariant d : detections) {
        String id = d.as<String>();
        int dashIndex = id.indexOf("-");
        String suffix = (dashIndex > 0) ? id.substring(dashIndex + 1) : id;
        String filename = "/events/" + suffix + "-" + zone + ".jpg";
        if (std::find(jpgQueue.begin(), jpgQueue.end(), filename) == jpgQueue.end()) {
          jpgQueue.push_back(filename);
        }
      }
      String url = "http://" + frigateIP + ":" + String(frigatePort) +
                   "/api/events/" + detections[0].as<String>() + "/snapshot.jpg?crop=1&height=240";
      imagePending = true;
      pendingImageUrl = url;
      pendingZone = zone;
    }
  }
}
