#include "web_server.h"
#include "config.h"
#include "wifi_manager.h"
#include "settings_handler.h"
#include "wifi_portal.h"
#include "device_control.h"
#include "test_mode.h"

void startWebServer() {
  server.on("/",                  handleRoot);
  server.on("/settings",          handleSettings);
  server.on("/logs",              handleLogs);
  server.on("/health",            handleHealth);
  server.on("/test",              handleTestMode);
  server.on("/test-trigger",      handleTestTrigger);
  server.on("/scan",              handleScan);
  server.on("/test-wifi",  HTTP_POST, handleTestWifi);
  server.on("/test-canvas", HTTP_POST, handleTestCanvas);
  server.on("/refresh",    HTTP_POST, handleRefresh);
  server.on("/snooze",     HTTP_POST, handleSnooze);
  server.on("/snooze/status", HTTP_GET, handleSnoozeStatus);
  server.on("/reboot",     HTTP_POST, handleReboot);
  server.on("/factory-reset", HTTP_POST, handleFactoryReset);
  server.on("/save",       HTTP_POST, handleSave);
  server.onNotFound(handleRoot);
  server.begin();
  webServerRunning = true;
  Serial.println(" Web server started");
}

void startSettingsAP() {
  Serial.println("Starting Settings AP");
  WiFi.mode(WIFI_AP_STA);
  bool apStarted = systemConfig.setupComplete
    ? WiFi.softAP(systemConfig.deviceName, systemConfig.apPassword)
    : WiFi.softAP(systemConfig.deviceName);
  if (!apStarted) { Serial.println(" Failed to start AP!"); return; }
  Serial.println("AP: " + String(systemConfig.deviceName));
  Serial.println(" AP URL: http://" + WiFi.softAPIP().toString());
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  if (!webServerRunning) startWebServer();
  Serial.println("Settings AP ready");
}

void monitorSystem() {
  static unsigned long lastTempCheck = 0, lastPowerCheck = 0;
  unsigned long now = millis();
  if (now - lastTempCheck >= 300000) {
    float temp = temperatureRead();
    Serial.printf(" Temp: %.1fC%s\n", temp, temp > 80 ? "  HIGH" : (temp > 70 ? " (warm)" : ""));
    lastTempCheck = now;
  }
  if (now - lastPowerCheck >= 300000) {
    lastPowerCheck = now;
  }
}
