#include "wifi_manager.h"
#include "config.h"

// ============================================
// HELPER FUNCTIONS
// ============================================
time_t esp_timegm(struct tm* t) {
  time_t year = t->tm_year + 1900;
  time_t days = 0;

  for (int y = 1970; y < year; y++) {
    days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
  }

  int monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) monthDays[1] = 29;

  for (int m = 1; m < t->tm_mon + 1; m++) {
    days += monthDays[m - 1];
  }

  days += t->tm_mday - 1;
  return days * 86400 + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
}

bool isQuietHours() {
  if (!ledConfig.quietHoursEnabled) return false;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return false;

  int h = timeinfo.tm_hour;
  return (ledConfig.quietHourStart < ledConfig.quietHourEnd)
    ? (h >= ledConfig.quietHourStart && h < ledConfig.quietHourEnd)
    : (h >= ledConfig.quietHourStart || h < ledConfig.quietHourEnd);
}

void initTime() {
  configTzTime(timezoneConfig.tzString, "pool.ntp.org");
  Serial.println("⏳ Waiting for NTP time sync...");
  struct tm timeinfo;
  int attempts = 0;

  while (!getLocalTime(&timeinfo) && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (attempts >= 20) {
    Serial.println("\n⚠️ Time sync timeout - will retry later");
    timeSyncComplete = false;
    return;
  }

  timeSyncComplete = true;
  Serial.println("\n✅ Time synced!");
  char buffer[64];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", &timeinfo);
  Serial.printf("📅 %s: %s\n", timezoneConfig.displayName, buffer);
}

void connectWiFi() {
  wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) return;
  // WL_IDLE_STATUS and WL_DISCONNECTED are safe to attempt a new connection.
  // Any other status (WL_NO_SSID_AVAIL, WL_CONNECT_FAILED, WL_CONNECTION_LOST)
  // also needs a fresh connect attempt, so only bail on states that mean
  // a connection is actively being established.
  if (status == WL_NO_SHIELD) return;  // no hardware

  Serial.println("📡 Connecting to WiFi...");

  // Don't change WiFi mode here - startSettingsAP() already set it correctly
  // This prevents mode conflicts that kill the AP broadcast during setup
  // if (!systemConfig.setupComplete) {
  //   WiFi.mode(WIFI_AP_STA);
  // } else {
  //   WiFi.mode(WIFI_STA);
  // }
  delay(100);

  // Try primary network
  Serial.printf("Trying: %s\n", wifiConfig.ssid);
  WiFi.begin(wifiConfig.ssid, wifiConfig.password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(100);
    Serial.print(".");
  }

  // Try secondary if available and primary failed
  if (WiFi.status() != WL_CONNECTED && wifiConfig.useSecondaryNetwork && strlen(wifiConfig.ssid2) > 0) {
    Serial.printf("\nTrying: %s\n", wifiConfig.ssid2);
    WiFi.begin(wifiConfig.ssid2, wifiConfig.password2);
    start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
      delay(100);
      Serial.print(".");
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n✅ Connected: %s\n", WiFi.SSID().c_str());

    // Wait a moment for IP to stabilize
    delay(500);
    Serial.printf("🔍 IP: %s\n", WiFi.localIP().toString().c_str());

    String hostname = "due-light";

    for (int i = 0; i < 3; i++) {
      if (MDNS.begin(hostname.c_str())) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("🌐 mDNS: http://%s.local\n", hostname.c_str());

        // Try time sync if not already complete
        if (!timeSyncComplete) {
          initTime();
        }
        return;
      }
      delay(500);
    }
    Serial.println("⚠️ mDNS failed");
  } else {
    Serial.println("\n❌ WiFi connection failed");
  }
}
