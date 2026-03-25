#include "telemetry.h"
#include "config.h"
#include "version.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

void sendTelemetry() {
  if (strlen(systemConfig.dashboardUrl) == 0) return;
  if (WiFi.status() != WL_CONNECTED) return;

  // Build JSON payload
  StaticJsonDocument<512> doc;

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  doc["device_id"]          = macStr;
  doc["device_name"]        = systemConfig.deviceName;
  doc["firmware_version"]   = FIRMWARE_VERSION;
  doc["setup_complete"]     = systemConfig.setupComplete;
  doc["uptime_seconds"]     = millis() / 1000;
  doc["free_heap"]          = ESP.getFreeHeap();
  doc["wifi_rssi"]          = WiFi.RSSI();
  doc["cpu_temp"]           = temperatureRead();
  doc["assignment_status"]  = assignmentStatus;
  doc["error_code"]         = currentErrorCode;
  doc["consecutive_errors"] = consecutiveErrors;
  doc["time_synced"]        = timeSyncComplete;
  doc["ota_version_seen"]   = otaVersionSeen;  // last version.json version read

  String payload;
  serializeJson(doc, payload);

  // POST with a short timeout — telemetry is fire-and-forget, never block main loop
  String url = String(systemConfig.dashboardUrl);
  if (!url.endsWith("/")) url += "/";
  url += "api/telemetry";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  https.setTimeout(5000);

  if (https.begin(client, url)) {
    https.addHeader("Content-Type", "application/json");
    if (strlen(systemConfig.dashboardApiKey) > 0) {
      https.addHeader("X-API-Key", systemConfig.dashboardApiKey);
    }
    int code = https.POST(payload);
    if (systemConfig.debugMode) {
      Serial.printf("[TELEMETRY] POST %s → %d\n", url.c_str(), code);
    }
    https.end();
  }
}
