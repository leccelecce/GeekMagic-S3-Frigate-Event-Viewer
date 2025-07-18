#pragma once

#include <Arduino.h>
#include <vector>

extern String frigateIP;
extern int frigatePort;

extern unsigned long lastFrigateRequest;

extern std::vector<String> jpgQueue;
extern bool imagePending;
extern String pendingImageUrl;
extern String pendingZone;

void displayImageFromAPI(String url, String zone);
void frigateKeepAlive();