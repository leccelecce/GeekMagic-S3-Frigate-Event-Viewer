#pragma once

#include <Arduino.h>
#include <AsyncMqttClient.h>

extern AsyncMqttClient mqttClient;
extern String mqttServer;
extern int mqttPort;
extern String mqttUser;
extern String mqttPass;

void setupMqtt();
void onMqttConnect(bool sessionPresent);
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
void onMqttMessage(
    char* topic,
    char* payload,
    AsyncMqttClientMessageProperties properties,
    size_t len,
    size_t index,
    size_t total
);