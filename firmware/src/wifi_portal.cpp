#include "wifi_portal.h"
#include "config.h"
#include <ArduinoJson.h>

void handleScan() {
  Serial.println(" Scanning WiFi networks...");
  WiFi.scanDelete();
  delay(100);

  int n = -1;
  for (int attempt = 0; attempt < 3 && n < 0; attempt++) {
    if (attempt > 0) { Serial.printf("  Scan retry %d/3...\n", attempt + 1); delay(500); }
    n = WiFi.scanNetworks(false, false, false, 300);
    Serial.printf("  Scan attempt %d result: %d\n", attempt + 1, n);
  }

  StaticJsonDocument<2048> doc;
  JsonObject root = doc.to<JsonObject>();
  JsonArray networks = root.createNestedArray("networks");

  if (n < 0) {
    root["error"] = (n == -1) ? "scan_failed" : "scan_busy";
    root["errorDetail"] = (n == -1)
      ? "WiFi scan returned no results after 3 attempts. Try re-scanning or type your network name manually."
      : "A scan is already in progress. Please wait a moment and try again.";
    Serial.printf("   Scan failed with code: %d after retries\n", n);
  } else if (n == 0) {
    root["error"] = "no_networks";
    root["errorDetail"] = "No networks found. Your network may be hidden or out of range.";
    Serial.println("   No networks found");
  } else {
    Serial.printf("   Found %d raw networks, deduplicating...\n", n);
    for (int i = 0; i < n && i < 20; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;
      bool isDuplicate = false;
      for (size_t j = 0; j < networks.size(); j++) {
        if (networks[j]["ssid"].as<String>() == ssid) {
          if (WiFi.RSSI(i) > networks[j]["rssi"].as<int>()) networks[j]["rssi"] = WiFi.RSSI(i);
          isDuplicate = true;
          break;
        }
      }
      if (!isDuplicate && networks.size() < 15) {
        JsonObject net = networks.createNestedObject();
        net["ssid"] = ssid;
        net["rssi"] = WiFi.RSSI(i);
        net["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
      }
    }
    Serial.printf("   Returning %d unique networks\n", networks.size());
  }

  root["count"] = networks.size();
  WiFi.scanDelete();

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleTestWifi() {
  if (!server.hasArg("ssid") || !server.hasArg("password")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing parameters\"}");
    return;
  }

  String testSsid = server.arg("ssid");
  String testPass = server.arg("password");
  Serial.printf(" Testing WiFi: %s\n", testSsid.c_str());

  WiFi.disconnect();
  delay(100);
  WiFi.begin(testSsid.c_str(), testPass.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) { delay(500); attempts++; }

  bool success = WiFi.status() == WL_CONNECTED;
  String msg = success ? "Connected successfully! Signal: " + String(WiFi.RSSI()) + " dBm"
                       : "Connection failed. Check password.";
  if (!success) WiFi.disconnect(false);

  server.send(200, "application/json",
    "{\"success\":" + String(success ? "true" : "false") + ",\"message\":\"" + msg + "\"}");
}

void handleTestCanvas() {
  if (!server.hasArg("token")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing token\"}");
    return;
  }

  String testToken = server.arg("token");
  testToken.trim(); // strip whitespace/newlines from copy-paste
  Serial.println(" Testing Canvas token...");

  if (WiFi.status() != WL_CONNECTED) {
    const char* connectSsid = nullptr;
    const char* connectPass = nullptr;
    String formSsid, formPass;
    if (server.hasArg("ssid") && server.arg("ssid").length() > 0) {
      formSsid = server.arg("ssid");
      formPass = server.hasArg("wifiPass") ? server.arg("wifiPass") : "";
      connectSsid = formSsid.c_str();
      connectPass = formPass.c_str();
    } else if (strlen(wifiConfig.ssid) > 0) {
      connectSsid = wifiConfig.ssid;
      connectPass = wifiConfig.password;
    }
    if (connectSsid) {
      WiFi.begin(connectSsid, connectPass);
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 20) { delay(500); attempts++; }
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    server.send(200, "application/json", "{\"success\":false,\"message\":\"Need WiFi connection first. Test WiFi above.\"}");
    return;
  }

  WiFiClientSecure testClient; testClient.setInsecure();
  HTTPClient testHttp; testHttp.setTimeout(15000);

  String testUrl;
  if (server.hasArg("schoolUrl")) {
    String school = server.arg("schoolUrl");
    school.trim();
    if (school.startsWith("https://")) school = school.substring(8);
    if (school.startsWith("http://")) school = school.substring(7);
    int slash = school.indexOf('/');
    if (slash > 0) school = school.substring(0, slash);
    testUrl = "https://" + school + "/api/v1/users/self/todo?per_page=1";
  } else {
    testUrl = String(canvasConfig.apiUrl) + "?per_page=1";
  }

  if (testHttp.begin(testClient, testUrl)) {
    testHttp.addHeader("Authorization", "Bearer " + testToken);
    testHttp.addHeader("Accept", "application/json");
    int httpCode = testHttp.GET();
    testHttp.end();

    String msg, successStr;
    if      (httpCode == 200) { msg = "Token valid! Canvas connection working."; successStr = "true"; }
    else if (httpCode == 401) { msg = "Invalid token. Generate a new one in Canvas."; successStr = "false"; }
    else if (httpCode < 0)    { msg = "Connection timed out — check WiFi and try again."; successStr = "false"; }
    else                      { msg = "Canvas error (HTTP " + String(httpCode) + "). Try again."; successStr = "false"; }

    server.send(200, "application/json", "{\"success\":" + successStr + ",\"message\":\"" + msg + "\"}");
  } else {
    server.send(200, "application/json", "{\"success\":false,\"message\":\"Could not connect to Canvas server.\"}");
  }
}
