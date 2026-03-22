#include "ota_update.h"
#include <WiFi.h>
#include <esp_task_wdt.h>

// Global variables for OTA
static unsigned long lastOTACheck = 0;
static bool otaInProgress = false;

void initOTA() {
  Serial.println("\n🔧 OTA Update System Initialized");
  Serial.printf("📌 Current Version: %s\n", FIRMWARE_VERSION);
  Serial.printf("📌 Build: %s\n", BUILD_TIMESTAMP);
  Serial.printf("⏰ Check Interval: %lu hours\n", OTA_CHECK_INTERVAL_MS / 3600000);
}

bool isNewerVersion(const char* remoteVersion, const char* currentVersion) {
  // Parse versions (format: "MAJOR.MINOR.PATCH")
  int remoteMajor, remoteMinor, remotePatch;
  int currentMajor, currentMinor, currentPatch;
  
  sscanf(remoteVersion, "%d.%d.%d", &remoteMajor, &remoteMinor, &remotePatch);
  sscanf(currentVersion, "%d.%d.%d", &currentMajor, &currentMinor, &currentPatch);
  
  // Compare versions
  if (remoteMajor > currentMajor) return true;
  if (remoteMajor < currentMajor) return false;
  
  if (remoteMinor > currentMinor) return true;
  if (remoteMinor < currentMinor) return false;
  
  return (remotePatch > currentPatch);
}

void checkForOTAUpdate() {
  // Don't check if not connected to WiFi
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  
  // Rate limiting - only check every OTA_CHECK_INTERVAL_MS
  unsigned long now = millis();
  if (now - lastOTACheck < OTA_CHECK_INTERVAL_MS) {
    return;
  }
  lastOTACheck = now;
  
  // Don't start new check if one is in progress
  if (otaInProgress) {
    return;
  }
  
  Serial.println("\n🔍 Checking for firmware updates...");
  
  WiFiClientSecure client;
  client.setInsecure();
  
  HTTPClient http;
  http.setTimeout(15000);
  
  if (!http.begin(client, OTA_UPDATE_URL)) {
    Serial.println("❌ Failed to connect to update server");
    return;
  }
  
  int httpCode = http.GET();
  
  if (httpCode != 200) {
    Serial.printf("❌ Update check failed: HTTP %d\n", httpCode);
    http.end();
    return;
  }
  
  String payload = http.getString();
  http.end();
  
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, payload);
  
  if (error) {
    Serial.printf("❌ JSON parse error: %s\n", error.c_str());
    return;
  }
  
  const char* remoteVersion = doc["version"];
  const char* firmwareUrl = doc["firmware_url"];
  const char* releaseNotes = doc["release_notes"];
  
  if (!remoteVersion || !firmwareUrl) {
    Serial.println("❌ Invalid version manifest");
    return;
  }
  
  Serial.printf("📦 Remote version: %s\n", remoteVersion);
  Serial.printf("📦 Current version: %s\n", FIRMWARE_VERSION);
  
  if (!isNewerVersion(remoteVersion, FIRMWARE_VERSION)) {
    Serial.println("✅ Already running latest version");
    return;
  }
  
  Serial.println("🆕 New version available!");
  if (releaseNotes) {
    Serial.printf("📝 Release notes: %s\n", releaseNotes);
  }
  
  otaInProgress = true;
  Serial.println("⬇️  Downloading firmware...");
  
  HTTPClient httpFirmware;
  if (!httpFirmware.begin(client, firmwareUrl)) {
    Serial.println("❌ Failed to connect to firmware server");
    otaInProgress = false;
    return;
  }
  
  int firmwareCode = httpFirmware.GET();
  
  if (firmwareCode != 200) {
    Serial.printf("❌ Firmware download failed: HTTP %d\n", firmwareCode);
    httpFirmware.end();
    otaInProgress = false;
    return;
  }
  
  int contentLength = httpFirmware.getSize();
  
  if (contentLength <= 0) {
    Serial.println("❌ Invalid firmware size");
    httpFirmware.end();
    otaInProgress = false;
    return;
  }
  
  Serial.printf("📦 Firmware size: %d bytes\n", contentLength);
  
  if (!Update.begin(contentLength)) {
    Serial.printf("❌ Not enough space. Free: %d, Need: %d\n", 
                  ESP.getFreeSketchSpace(), contentLength);
    httpFirmware.end();
    otaInProgress = false;
    return;
  }
  
  WiFiClient* stream = httpFirmware.getStreamPtr();

  // CRITICAL: Disable watchdog during OTA write - large downloads can take 30+ seconds.
  // Always re-add the task before evaluating results so WDT is never left disabled.
  esp_task_wdt_delete(NULL);
  size_t written = Update.writeStream(*stream);
  esp_task_wdt_add(NULL);   // Re-add unconditionally before any early return
  esp_task_wdt_reset();

  httpFirmware.end();

  if (written != (size_t)contentLength) {
    Serial.printf("❌ Write failed. Written: %u, Expected: %d\n", (unsigned)written, contentLength);
    Update.abort();
    otaInProgress = false;
    return;
  }

  if (!Update.end()) {
    Serial.printf("❌ Update failed: %s\n", Update.errorString());
    otaInProgress = false;
    return;
  }

  if (!Update.isFinished()) {
    Serial.println("❌ Update not finished");
    otaInProgress = false;
    return;
  }
  
  Serial.println("✅ Update successful!");
  Serial.println("🔄 Rebooting in 3 seconds...");
  delay(3000);
  ESP.restart();
}
