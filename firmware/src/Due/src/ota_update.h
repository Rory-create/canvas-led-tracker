#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <Arduino.h>
#include <Update.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "version.h"

// OTA Status codes
enum OTAStatus {
  OTA_SUCCESS = 0,
  OTA_NO_UPDATE = 1,
  OTA_CHECK_FAILED = 2,
  OTA_DOWNLOAD_FAILED = 3,
  OTA_INSTALL_FAILED = 4
};

// Function declarations
void initOTA();
void checkForOTAUpdate();
bool isNewerVersion(const char* remoteVersion, const char* currentVersion);

#endif
