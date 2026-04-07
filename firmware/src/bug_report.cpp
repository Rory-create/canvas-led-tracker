#include "bug_report.h"
#include "config.h"
#include "version.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

bool shouldReportBug() {
  if (!systemConfig.bugReportEnabled) {
    return false;
  }

  // Check cooldown (1 hour = 3600000ms)
  unsigned long now = millis();
  if (now - systemConfig.lastBugReport < 3600000) {
    if (systemConfig.debugMode) {
      unsigned long remaining = (3600000 - (now - systemConfig.lastBugReport)) / 60000;
      Serial.printf("[BUG] Cooldown active: %lu minutes remaining\n", remaining);
    }
    return false;
  }

  return true;
}

String collectDiagnostics() {
  StaticJsonDocument<512> doc;

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  const char* errorNames[] = {
    "NONE", "WIFI_DISCONNECT", "CANVAS_AUTH", "CANVAS_SERVER",
    "TIME_SYNC", "MEMORY_LOW", "JSON_PARSE", "BUFFER_EXHAUSTED"
  };
  int safeCode = (currentErrorCode >= 0 && currentErrorCode < 8) ? currentErrorCode : 0;

  time_t now;
  time(&now);
  char timestamp[32];
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

  doc["device_id"] = macStr;
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["error_code"] = currentErrorCode;
  doc["error_name"] = String("ERR_") + errorNames[safeCode];
  doc["timestamp"] = timestamp;
  doc["uptime_seconds"] = millis() / 1000;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["max_heap_used"] = ESP.getMaxAllocHeap();
  doc["consecutive_errors"] = consecutiveErrors;
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["cpu_temp"] = temperatureRead();
  doc["assignment_count"] = assignmentCount;

  JsonObject cfg = doc.createNestedObject("config");
  cfg["timezone"] = timezoneConfig.displayName;
  cfg["fetch_interval"] = canvasConfig.fetchInterval / 60000;
  cfg["red_days"] = ledConfig.redLEDDaysAhead;
  cfg["yellow_days"] = ledConfig.yellowLEDDaysAhead;

  String out;
  serializeJson(doc, out);
  return out;
}

void reportBug() {
  if (!shouldReportBug()) return;
  if (strlen(systemConfig.dashboardUrl) == 0) return;

  Serial.println("[BUG] Reporting to dashboard...");

  String diagnostics = collectDiagnostics();

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  char last4[5];
  sprintf(last4, "%02X%02X", mac[4], mac[5]);

  const char* errorNames[] = {
    "None", "WiFi Disconnect", "Canvas Auth", "Canvas Server",
    "Time Sync", "Memory Low", "JSON Parse", "Buffer Exhausted"
  };
  int safeCode = (currentErrorCode >= 0 && currentErrorCode < 8) ? currentErrorCode : 0;
  String errorName = errorNames[safeCode];
  String title = "[AUTO] " + errorName + " - Device " + String(last4);

  DynamicJsonDocument dashDoc(2048);
  dashDoc["device_id"]        = macStr;
  dashDoc["device_name"]      = systemConfig.deviceName;
  dashDoc["firmware_version"] = FIRMWARE_VERSION;
  dashDoc["error_code"]       = currentErrorCode;
  dashDoc["error_name"]       = String("ERR_") + errorName;
  dashDoc["title"]            = title;
  DynamicJsonDocument tempDiag(1024);
  if (deserializeJson(tempDiag, diagnostics) == DeserializationError::Ok) {
    dashDoc["diagnostics"] = tempDiag.as<JsonObject>();
  } else {
    dashDoc["diagnostics"] = diagnostics;
  }
  String dashPayload;
  serializeJson(dashDoc, dashPayload);

  String dashUrl = String(systemConfig.dashboardUrl);
  if (!dashUrl.endsWith("/")) dashUrl += "/";
  dashUrl += "api/bug";

  WiFiClientSecure dashClient;
  dashClient.setInsecure();
  HTTPClient dashHttp;
  dashHttp.setTimeout(5000);
  if (dashHttp.begin(dashClient, dashUrl)) {
    dashHttp.addHeader("Content-Type", "application/json");
    if (strlen(systemConfig.dashboardApiKey) > 0) {
      dashHttp.addHeader("X-API-Key", systemConfig.dashboardApiKey);
    }
    int dc = dashHttp.POST(dashPayload);
    Serial.printf("[BUG] Dashboard POST → %d\n", dc);
    dashHttp.end();
    if (dc == 200 || dc == 201) {
      systemConfig.lastBugReport = millis();
      preferences.begin("config", false);
      preferences.putULong("lastReport", systemConfig.lastBugReport);
      preferences.end();
    }
  }
}
