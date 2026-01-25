#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "esp_task_wdt.h"

#define FIRMWARE_VERSION "1.2.8"
#define OTA_UPDATE_URL "https://raw.githubusercontent.com/Rory-create/canvas-led-tracker/main/ota/recovery.json"

const int RED_PIN = 18;
const int YELLOW_PIN = 19;
const int GREEN_PIN = 21;

Preferences preferences;

void setLED(bool red, bool yellow, bool green) {
  digitalWrite(RED_PIN, red ? HIGH : LOW);
  digitalWrite(YELLOW_PIN, yellow ? HIGH : LOW);
  digitalWrite(GREEN_PIN, green ? HIGH : LOW);
}

// Try to connect to WiFi with given credentials
bool tryWiFiConnect(const char* ssid, const char* password, int maxAttempts = 30) {
  if (strlen(ssid) == 0) return false;

  Serial.printf("Trying WiFi: %s\n", ssid);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nConnected: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.println("\nFailed");
  WiFi.disconnect();
  delay(100);
  return false;
}

void checkForOTAUpdate() {
  Serial.println("Checking for full firmware...");
  
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  
  if (!http.begin(client, OTA_UPDATE_URL)) {
    Serial.println("Connection failed");
    return;
  }
  
  int httpCode = http.GET();
  if (httpCode != 200) {
    Serial.printf("HTTP %d\n", httpCode);
    http.end();
    return;
  }
  
  String payload = http.getString();
  http.end();
  
  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, payload)) {
    Serial.println("JSON error");
    return;
  }
  
  const char* remoteVersion = doc["version"];
  const char* firmwareUrl = doc["firmware_url"];
  
  if (!remoteVersion || !firmwareUrl) {
    Serial.println("Invalid manifest");
    return;
  }
  
  Serial.printf("Downloading v%s...\n", remoteVersion);
  
  HTTPClient httpFirmware;
  if (!httpFirmware.begin(client, firmwareUrl)) {
    Serial.println("Download failed");
    return;
  }
  
  int firmwareCode = httpFirmware.GET();
  if (firmwareCode != 200) {
    Serial.printf("HTTP %d\n", firmwareCode);
    httpFirmware.end();
    return;
  }
  
  int contentLength = httpFirmware.getSize();
  if (contentLength <= 0) {
    Serial.println("Invalid size");
    httpFirmware.end();
    return;
  }
  
  Serial.printf("Size: %d bytes\n", contentLength);
  
  if (!Update.begin(contentLength)) {
    Serial.println("No space");
    httpFirmware.end();
    return;
  }
  
  WiFiClient* stream = httpFirmware.getStreamPtr();
  
  esp_task_wdt_delete(NULL);
  size_t written = Update.writeStream(*stream);
  esp_task_wdt_add(NULL);
  
  httpFirmware.end();
  
  if (written != contentLength) {
    Serial.printf("Write failed: %d/%d\n", written, contentLength);
    Update.abort();
    return;
  }
  
  if (!Update.end() || !Update.isFinished()) {
    Serial.println("Update failed");
    return;
  }
  
  Serial.println("Success! Rebooting...");
  delay(2000);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  
  Serial.println("\n=== RESCUE MODE ===");
  Serial.printf("v%s\n", FIRMWARE_VERSION);
  
  pinMode(RED_PIN, OUTPUT);
  pinMode(YELLOW_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  setLED(false, true, false);
  
  // CRITICAL FIX: Use "config" namespace (same as main firmware)
  // and "pass" key (not "password") to match main firmware storage
  preferences.begin("config", true);
  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("pass", "");  // Main firmware uses "pass" not "password"
  String ssid2 = preferences.getString("ssid2", "");
  String password2 = preferences.getString("pass2", "");
  preferences.end();

  if (ssid.length() == 0 && ssid2.length() == 0) {
    Serial.println("No WiFi saved");
    setLED(true, false, false);
    return;
  }

  // Try primary network first
  bool connected = tryWiFiConnect(ssid.c_str(), password.c_str());

  // If primary fails and secondary exists, try secondary
  if (!connected && ssid2.length() > 0) {
    Serial.println("Trying secondary network...");
    connected = tryWiFiConnect(ssid2.c_str(), password2.c_str());
  }

  if (!connected) {
    Serial.println("All WiFi networks failed");
    setLED(true, false, false);
    return;
  }

  setLED(false, false, true);  // Green = connected
  delay(1000);
  checkForOTAUpdate();
}

void loop() {
  delay(1000);
}
