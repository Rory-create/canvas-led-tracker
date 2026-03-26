#include "bug_report.h"
#include "config.h"
#include "version.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

bool shouldReportBug() {
  // Check if bug reporting is enabled
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
  // Use ArduinoJson to build diagnostics JSON — avoids heap fragmentation from
  // repeated String concatenation and handles field escaping automatically.
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

void createGitHubIssue() {
  if (!shouldReportBug()) {
    return;
  }

  // Skip GitHub reporting if no token was compiled in (avoids pointless 401 requests)
  if (strlen(GITHUB_TOKEN) == 0) {
    if (systemConfig.debugMode) Serial.println("[BUG] No GITHUB_TOKEN — skipping GitHub issue");
  } else {
    Serial.println("[BUG] Creating GitHub issue...");
  }

  // Always try the dashboard POST (doesn't need a token)
  if (strlen(systemConfig.dashboardUrl) == 0 && strlen(GITHUB_TOKEN) == 0) {
    return;  // nowhere to report
  }

  // Collect diagnostics
  String diagnostics = collectDiagnostics();

  // Get MAC for title
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char last4[5];
  sprintf(last4, "%02X%02X", mac[4], mac[5]);

  // Error name for title — bounds-checked to prevent OOB access on corrupted error code
  const char* errorNames[] = {
    "None", "WiFi Disconnect", "Canvas Auth", "Canvas Server",
    "Time Sync", "Memory Low", "JSON Parse", "Buffer Exhausted"
  };
  int safeCode = (currentErrorCode >= 0 && currentErrorCode < 8) ? currentErrorCode : 0;
  String errorName = errorNames[safeCode];

  String title = "[AUTO] " + errorName + " - Device " + String(last4);
  bool reported = false;

  // ── GitHub Issues (only if token is compiled in) ───────────────────────
  if (strlen(GITHUB_TOKEN) > 0) {
    String body = "### Automatic Bug Report\n\n";
    body += "**Firmware:** " + String(FIRMWARE_VERSION) + "\n";
    body += "**Error Code:** " + String(currentErrorCode) + " (" + errorName + ")\n\n";
    body += "---\n\n### Diagnostics\n\n```json\n";
    body += diagnostics;
    body += "\n```\n\n";
    body += "---\n\n*Note: Device will not report again for 1 hour.*";
    DynamicJsonDocument issueDoc(2048);
    issueDoc["title"] = title;
    issueDoc["body"] = body;
    JsonArray labels = issueDoc.createNestedArray("labels");
    labels.add("auto-bug-report");
    labels.add("critical");
    labels.add(String("firmware-") + FIRMWARE_VERSION);
    String payload;
    serializeJson(issueDoc, payload);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;
    String url = "https://api.github.com/repos/" + String(GITHUB_REPO) + "/issues";
    https.begin(client, url);
    https.addHeader("Content-Type", "application/json");
    https.addHeader("Accept", "application/vnd.github.v3+json");
    https.addHeader("Authorization", "Bearer " + String(GITHUB_TOKEN));
    https.setTimeout(10000);
    int httpCode = https.POST(payload);
    if (httpCode == 201) {
      Serial.println("[BUG] GitHub issue created successfully!");
      reported = true;
    } else if (httpCode > 0) {
      Serial.printf("[BUG] GitHub API returned: %d\n", httpCode);
      if (systemConfig.debugMode) Serial.println(https.getString());
    } else {
      Serial.printf("[BUG] GitHub request failed: %s\n", https.errorToString(httpCode).c_str());
    }
    https.end();
  }

  // ── Dashboard server (only if configured) ─────────────────────────────
  if (strlen(systemConfig.dashboardUrl) > 0) {
    uint8_t macFull[6];
    WiFi.macAddress(macFull);
    char macStr[18];
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
            macFull[0], macFull[1], macFull[2], macFull[3], macFull[4], macFull[5]);

    DynamicJsonDocument dashDoc(2048);
    dashDoc["device_id"]        = macStr;
    dashDoc["device_name"]      = systemConfig.deviceName;
    dashDoc["firmware_version"] = FIRMWARE_VERSION;
    dashDoc["error_code"]       = currentErrorCode;
    dashDoc["error_name"]       = String("ERR_") + errorName;
    dashDoc["title"]            = title;
    // Parse diagnostics JSON string into a nested object (can't deserialize into JsonVariant directly)
    DynamicJsonDocument tempDiag(1024);
    if (deserializeJson(tempDiag, diagnostics) == DeserializationError::Ok) {
      dashDoc["diagnostics"] = tempDiag.as<JsonObject>();
    } else {
      dashDoc["diagnostics"] = diagnostics;  // fallback: embed as raw string
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
      if (dc == 200 || dc == 201) reported = true;
    }
  }

  // Stamp cooldown only if at least one report succeeded
  if (reported) {
    systemConfig.lastBugReport = millis();
    preferences.begin("config", false);
    preferences.putULong("lastReport", systemConfig.lastBugReport);
    preferences.end();
  }
}
