#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>

// Function declarations
time_t esp_timegm(struct tm* t);
bool isQuietHours();
void initTime();
void connectWiFi();
