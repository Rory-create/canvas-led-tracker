#include "device_control.h"
#include "settings_handler.h"
#include "canvas_api.h"
#include "config.h"
#include <ArduinoJson.h>

void handleLogs() {
  String logs = "<pre>";
  for (int i = 0; i < LOG_BUFFER_SIZE; i++) {
    logs += serialLog[(logIndex + i) % LOG_BUFFER_SIZE] + "\n";
  }
  logs += "</pre>";
  server.send(200, "text/html", logs);
}

void handleHealth() {
  StaticJsonDocument<256> doc;
  doc["uptime"]            = millis() / 1000;
  doc["wifi_connected"]    = WiFi.status() == WL_CONNECTED;
  doc["wifi_ssid"]         = WiFi.SSID();
  doc["time_synced"]       = timeSyncComplete;
  doc["consecutive_errors"]= consecutiveErrors;
  doc["last_check_ago"]    = (millis() - lastFetch) / 1000;
  doc["last_success_ago"]  = (millis() - lastSuccessfulFetch) / 1000;
  doc["assignment_status"] = assignmentStatus;
  doc["free_heap"]         = ESP.getFreeHeap();
  doc["cpu_temp"]          = temperatureRead();
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleSnooze() {
  if (server.hasArg("cancel") && server.arg("cancel") == "1") {
    snoozeUntil = 0;
    Serial.println("[SNOOZE] Cancelled");
    server.send(200, "text/plain", "Snooze cancelled");
    return;
  }
  int hours = 1;
  if (server.hasArg("hours")) {
    hours = server.arg("hours").toInt();
    if (hours < 1) hours = 1;
    if (hours > 24) hours = 24;
  }
  snoozeUntil = millis() + (unsigned long)hours * 3600000UL;
  Serial.printf("[SNOOZE] Active for %d hour(s)\n", hours);
  server.send(200, "text/plain", ("Snooze active for " + String(hours) + " hour(s)").c_str());
}

void handleSnoozeStatus() {
  unsigned long now = millis();
  if (snoozeUntil == 0 || now >= snoozeUntil) {
    server.send(200, "application/json", "{\"active\":false,\"remaining_minutes\":0}");
  } else {
    unsigned long remainMin = (snoozeUntil - now) / 60000;
    server.send(200, "application/json",
      ("{\"active\":true,\"remaining_minutes\":" + String(remainMin) + "}").c_str());
  }
}

void handleReboot() {
  Serial.println("\nReboot requested");
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "Rebooting...");
  delay(500);
  ESP.restart();
}

void handleFactoryReset() {
  Serial.println("\n========================================");
  Serial.println("  FACTORY RESET INITIATED");
  Serial.println("========================================");
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "Factory reset in progress...");
  delay(500);
  server.stop();
  WiFi.disconnect(true);
  delay(100);
  preferences.begin("config", false);
  preferences.clear();
  preferences.end();
  for (int i = 0; i < 20; i++) {
    int pins[] = LED_PINS;
    for (int p : pins) digitalWrite(p, HIGH);
    delay(100);
    for (int p : pins) digitalWrite(p, LOW);
    delay(100);
  }
  Serial.println("Factory reset complete! Rebooting...\n");
  delay(1000);
  ESP.restart();
}

void handleRefresh() {
  Serial.println("Manual refresh requested");
  int oldStatus = assignmentStatus;
  assignmentStatus = fetchCanvasAssignments();
  lastFetch = millis();
  String statusName = assignmentStatus == 0 ? getErrorMessage() :
                      assignmentStatus == 3 ? "RED (due today)" :
                      assignmentStatus == 2 ? "YELLOW (due soon)" : "GREEN (all clear)";
  server.send(200, "application/json",
    "{\"success\":true,\"status\":" + String(assignmentStatus) +
    ",\"statusName\":\"" + statusName +
    "\",\"changed\":" + String(oldStatus != assignmentStatus ? "true" : "false") + "}");
}
