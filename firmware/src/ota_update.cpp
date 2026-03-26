#include "ota_update.h"
#include "config.h"
#include <WiFi.h>
#include <Update.h>
#include <esp_task_wdt.h>

// Global variables for OTA
static unsigned long lastOTACheck = 0;
static unsigned long lastOTAFailure = 0;   // separate timer for failure retry
static bool otaInProgress = false;

// OTA_CHECK_INTERVAL_MS = 24h (normal); retry failed checks after 1h
static const unsigned long OTA_RETRY_INTERVAL_MS = 60UL * 60UL * 1000UL;

void initOTA() {
  Serial.println("\n🔧 OTA Update System Initialized");
  Serial.printf("📌 Current Version: %s\n", FIRMWARE_VERSION);
  Serial.printf("📌 Build: %s\n", BUILD_TIMESTAMP);
  Serial.printf("⏰ Check Interval: %lu hours\n", OTA_CHECK_INTERVAL_MS / 3600000);
}

bool isNewerVersion(const char* remoteVersion, const char* currentVersion) {
  int remoteMajor, remoteMinor, remotePatch;
  int currentMajor, currentMinor, currentPatch;

  sscanf(remoteVersion, "%d.%d.%d", &remoteMajor, &remoteMinor, &remotePatch);
  sscanf(currentVersion, "%d.%d.%d", &currentMajor, &currentMinor, &currentPatch);

  if (remoteMajor != currentMajor) return remoteMajor > currentMajor;
  if (remoteMinor != currentMinor) return remoteMinor > currentMinor;
  return remotePatch > currentPatch;
}

void checkForOTAUpdate() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (otaInProgress) return;

  unsigned long now = millis();

  // Normal 24h check; if last attempt failed, allow retry after 1h
  bool normalDue  = (now - lastOTACheck   >= OTA_CHECK_INTERVAL_MS);
  bool retryDue   = (lastOTAFailure > 0) && (now - lastOTAFailure >= OTA_RETRY_INTERVAL_MS);
  if (!normalDue && !retryDue) return;

  Serial.println("\n🔍 Checking for firmware updates...");

  // ── Fetch version manifest ─────────────────────────────────────────────

  // Use a dedicated client instance for the manifest request
  WiFiClientSecure manifestClient;
  manifestClient.setInsecure();
  HTTPClient manifestHttp;
  manifestHttp.setTimeout(15000);

  if (!manifestHttp.begin(manifestClient, OTA_UPDATE_URL)) {
    Serial.println("❌ Failed to connect to update server");
    lastOTAFailure = now;
    return;
  }

  int httpCode = manifestHttp.GET();

  if (httpCode != 200) {
    Serial.printf("❌ Update check failed: HTTP %d\n", httpCode);
    manifestHttp.end();
    lastOTAFailure = now;
    return;
  }

  String payload = manifestHttp.getString();
  manifestHttp.end();

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.printf("❌ JSON parse error: %s\n", error.c_str());
    lastOTAFailure = now;
    return;
  }

  const char* remoteVersion = doc["version"];
  const char* firmwareUrl   = doc["firmware_url"];
  const char* releaseNotes  = doc["release_notes"] | "";

  if (!remoteVersion || !firmwareUrl) {
    Serial.println("❌ Invalid version manifest");
    lastOTAFailure = now;
    return;
  }

  // Record the version we just read so telemetry can report it
  strncpy(otaVersionSeen, remoteVersion, sizeof(otaVersionSeen) - 1);
  otaVersionSeen[sizeof(otaVersionSeen) - 1] = '\0';

  // Stamp the normal check timer now — manifest fetch succeeded
  lastOTACheck = now;
  lastOTAFailure = 0;

  Serial.printf("📦 Remote: %s  |  Running: %s\n", remoteVersion, FIRMWARE_VERSION);

  if (!isNewerVersion(remoteVersion, FIRMWARE_VERSION)) {
    Serial.println("✅ Already running latest version");
    return;
  }

  Serial.println("🆕 New version available!");
  if (strlen(releaseNotes) > 0) Serial.printf("📝 %s\n", releaseNotes);

  // ── Download and flash firmware ────────────────────────────────────────

  otaInProgress = true;
  Serial.println("⬇️  Downloading firmware...");

  // Use a fresh client for the firmware download — reusing the manifest client
  // across two separate HTTPS connections to different CDN endpoints is unreliable.
  WiFiClientSecure firmwareClient;
  firmwareClient.setInsecure();
  HTTPClient firmwareHttp;
  firmwareHttp.setTimeout(60000);  // firmware can be large; allow 60s

  if (!firmwareHttp.begin(firmwareClient, firmwareUrl)) {
    Serial.println("❌ Failed to connect to firmware server");
    otaInProgress = false;
    lastOTAFailure = now;
    return;
  }

  int firmwareCode = firmwareHttp.GET();

  if (firmwareCode != 200) {
    Serial.printf("❌ Firmware download failed: HTTP %d\n", firmwareCode);
    firmwareHttp.end();
    otaInProgress = false;
    lastOTAFailure = now;
    return;
  }

  int contentLength = firmwareHttp.getSize();
  Serial.printf("📦 Content-Length: %d\n", contentLength);

  // GitHub CDN sometimes returns -1 (chunked encoding).
  // UPDATE_SIZE_UNKNOWN tells the Update library to accept an unknown size and
  // rely on Update.isFinished() / MD5 verification rather than a byte count.
  size_t updateSize = (contentLength > 0) ? (size_t)contentLength : UPDATE_SIZE_UNKNOWN;

  if (!Update.begin(updateSize)) {
    Serial.printf("❌ OTA begin failed. Free sketch space: %u, Need: %d\n",
                  ESP.getFreeSketchSpace(), contentLength);
    firmwareHttp.end();
    otaInProgress = false;
    lastOTAFailure = now;
    return;
  }

  WiFiClient* stream = firmwareHttp.getStreamPtr();

  // CRITICAL: Disable watchdog during OTA write — large downloads can take 30+ seconds.
  // Always re-add before evaluating the result so WDT is never left permanently disabled.
  esp_task_wdt_delete(NULL);
  size_t written = Update.writeStream(*stream);
  esp_task_wdt_add(NULL);
  esp_task_wdt_reset();

  firmwareHttp.end();

  // Only validate byte count when content-length was known
  if (contentLength > 0 && written != (size_t)contentLength) {
    Serial.printf("❌ Write incomplete. Written: %u, Expected: %d\n", (unsigned)written, contentLength);
    Update.abort();
    otaInProgress = false;
    lastOTAFailure = now;
    return;
  }

  if (!Update.end()) {
    Serial.printf("❌ Update.end() failed: %s\n", Update.errorString());
    otaInProgress = false;
    lastOTAFailure = now;
    return;
  }

  if (!Update.isFinished()) {
    Serial.println("❌ Update not finished — firmware may be corrupt");
    otaInProgress = false;
    lastOTAFailure = now;
    return;
  }

  Serial.println("✅ OTA update successful!");
  Serial.println("🔄 Rebooting in 3 seconds...");
  delay(3000);
  ESP.restart();
}
