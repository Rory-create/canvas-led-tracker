#include "web_server.h"
#include "config.h"
#include "wifi_manager.h"
#include "canvas_api.h"
#include "version.h"
#include "welcome_html.h"
#include "settings_html.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

// ============================================
// ERROR HELPER
// ============================================
String getErrorMessage() {
  switch(currentErrorCode) {
    case ERR_WIFI_DISCONNECT:
      return "WiFi Disconnected";
    case ERR_CANVAS_AUTH:
      return "Canvas Auth Error (401 - Check Token)";
    case ERR_CANVAS_SERVER:
      return "Canvas Server Error (500+)";
    case ERR_TIME_SYNC:
      return "Time Sync Failed";
    case ERR_MEMORY_LOW:
      return "Memory Critical (<15KB)";
    case ERR_JSON_PARSE:
      return "JSON Parse Failed";
    case ERR_BUFFER_EXHAUSTED:
      return "Too Many Assignments (Buffer Full)";
    default:
      return "ERROR - Check Device";
  }
}

// ============================================
// WEB HANDLERS
// ============================================
void handleRoot() {
  if (systemConfig.setupComplete) {
    server.sendHeader("Location", "/settings");
    server.send(302);
  } else {
    String html = FPSTR(WELCOME_HTML);
    html.replace("%FW_VERSION%", FIRMWARE_VERSION);
    server.send(200, "text/html", html);
  }
}

void handleSettings() {
  String html = FPSTR(SETTINGS_HTML);
  html.replace("%DEVICE_NAME%", systemConfig.deviceName);
  html.replace("%FW_VERSION%", FIRMWARE_VERSION);
  if (WiFi.status() == WL_CONNECTED) {
    html.replace("%WIFI_STATUS%", "<span>" + WiFi.SSID() + " (" + WiFi.localIP().toString() + ")</span>");
  } else {
    html.replace("%WIFI_STATUS%", "<span style='color:var(--red)'>Disconnected</span>");
  }

  String statusText = "None";
  if (assignmentStatus == 0) {
    // Provide specific error message based on error code
    switch(currentErrorCode) {
      case ERR_WIFI_DISCONNECT:
        statusText = "WiFi Disconnected";
        break;
      case ERR_CANVAS_AUTH:
        statusText = "Token expired - visit Settings to renew";
        break;
      case ERR_CANVAS_SERVER:
        statusText = "Canvas Server Error";
        break;
      case ERR_TIME_SYNC:
        statusText = "Time Sync Failed";
        break;
      case ERR_MEMORY_LOW:
        statusText = "Memory Critical";
        break;
      case ERR_JSON_PARSE:
        statusText = "JSON Parse Error";
        break;
      case ERR_BUFFER_EXHAUSTED:
        statusText = "Buffer Exhausted (Too Many Assignments)";
        break;
      default:
        statusText = "ERROR - Check Device";
    }
  }
  else if (assignmentStatus == 3) statusText = "Today";
  else if (assignmentStatus == 2) statusText = "Tomorrow";
  else statusText = "All Clear";
  html.replace("%ASSIGNMENT_STATUS%", statusText);

  // Show time since last check and last successful fetch
  String lastCheckText = String((millis() - lastFetch) / 60000) + "m ago";
  if (consecutiveErrors > 0) {
    lastCheckText += " (" + String(consecutiveErrors) + " errors)";
  }
  html.replace("%LAST_CHECK%", lastCheckText);

  // Add error alert if needed
  String errorAlert = "";
  if (currentErrorCode == ERR_CANVAS_AUTH) {
    errorAlert = "<div class='alert alert-error' style='text-align:left;'><b>&#10060; Canvas token expired or invalid.</b><br>"
                 "Follow these steps to fix it:<ol style='margin:8px 0 0 18px;line-height:1.9;'>"
                 "<li>Open Canvas in a browser and log in as the student</li>"
                 "<li>Click <b>Account</b> (top-left) &rarr; <b>Settings</b></li>"
                 "<li>Scroll to <b>Approved Integrations</b> &rarr; click <b>+ New Access Token</b></li>"
                 "<li>Name it <b>DueLight</b>, leave expiry blank (or as far ahead as allowed)</li>"
                 "<li>Click <b>Generate Token</b>, copy it, and paste it in the Canvas API section below</li>"
                 "<li>Click <b>Save Settings</b></li></ol></div>";
  } else if (consecutiveErrors >= 3) {
    errorAlert = "<div class='alert alert-warning'>&#9888; Canvas API experiencing issues (" +
                 String(consecutiveErrors) + " errors). Using last known status.</div>";
  }
  if (!timeSyncComplete) {
    errorAlert += "<div class='alert alert-error'>&#10060; Time sync failed. Assignment checks disabled until time is synced.</div>";
  }
  html.replace("%ERROR_ALERT%", errorAlert);

  // Token age status
  time_t nowTs;
  time(&nowTs);
  String tokenStatus = "";
  String tokenAlert = "";
  if (canvasConfig.tokenLastValidated > 0 && nowTs > 1000000) {
    long tokenAgeDays = ((long)nowTs - (long)canvasConfig.tokenLastValidated) / 86400;
    if (tokenAgeDays < 0) tokenAgeDays = 0;
    if (currentErrorCode != ERR_CANVAS_AUTH) {
      if (tokenAgeDays >= 110) {
        tokenStatus = "<p style='font-size:13px;background:#f8d7da;border:1px solid #f44336;color:#721c24;padding:8px;border-radius:5px;margin:6px 0;'>"
                      "&#128308; Token likely expired (last verified " + String(tokenAgeDays) + " days ago). Generate a new one in Canvas.</p>";
      } else if (tokenAgeDays >= 80) {
        tokenStatus = "<p style='font-size:13px;background:#fff3cd;border:1px solid #ffc107;color:#856404;padding:8px;border-radius:5px;margin:6px 0;'>"
                      "&#9888; Token expires soon (last verified " + String(tokenAgeDays) + " days ago). Consider renewing before it stops working.</p>";
      } else {
        tokenStatus = "<p style='font-size:12px;color:#666;margin:4px 0;'>Last verified " + String(tokenAgeDays) + " day(s) ago &#10003;</p>";
      }
    }
  }
  html.replace("%TOKEN_STATUS%", tokenStatus);
  html.replace("%TOKEN_ALERT%", tokenAlert);

  // Extract school domain from stored API URL
  String canvasSchoolUrl = "";
  String apiUrlStr = String(canvasConfig.apiUrl);
  if (apiUrlStr.startsWith("https://")) {
    String noProto = apiUrlStr.substring(8);
    int slash = noProto.indexOf('/');
    canvasSchoolUrl = (slash > 0) ? noProto.substring(0, slash) : noProto;
  }
  html.replace("%CANVAS_SCHOOL_URL%", canvasSchoolUrl);

  // Build assignments section
  String assignmentsSection = "";
  if (assignmentCount > 0) {
    assignmentsSection = "<div class='section'><h3>Upcoming Assignments</h3>";
    for (int i = 0; i < assignmentCount; i++) {
      Assignment &a = displayedAssignments[i];
      
      // Format due date
      char dueDateStr[50];
      struct tm due_tm;
      localtime_r(&a.dueTimestamp, &due_tm);
      strftime(dueDateStr, sizeof(dueDateStr), "%a %b %d, %I:%M %p", &due_tm);
      
      // Urgency badge
      String urgencyBadge = a.urgency == 2 ? 
        "<span style='background:#f44336;color:white;padding:3px 8px;border-radius:3px;font-size:12px;'>URGENT</span>" :
        "<span style='background:#ff9800;color:white;padding:3px 8px;border-radius:3px;font-size:12px;'>COMING UP</span>";
      
      assignmentsSection += "<div style='border-left:3px solid " + 
        String(a.urgency == 2 ? "#f44336" : "#ff9800") + 
        ";padding:10px;margin:10px 0;background:#f9f9f9;'>";
      assignmentsSection += urgencyBadge + " <strong>" + a.name + "</strong><br>";
      assignmentsSection += "<small style='color:#666;'>Due: " + String(dueDateStr) + "</small><br>";
      
      if (a.description.length() > 0) {
        assignmentsSection += "<small>" + a.description + "</small><br>";
      }
      
      if (a.htmlUrl.length() > 0) {
        assignmentsSection += "<a href='" + a.htmlUrl + "' target='_blank' style='font-size:12px;'>Open in Canvas</a>";
      }
      
      assignmentsSection += "</div>";
    }
    assignmentsSection += "</div>";
  }
  html.replace("%ASSIGNMENTS_SECTION%", assignmentsSection);

  html.replace("%SSID%", wifiConfig.ssid);
  html.replace("%PASSWORD%", wifiConfig.password);
  html.replace("%SSID2%", wifiConfig.ssid2);
  html.replace("%PASSWORD2%", wifiConfig.password2);
  html.replace("%API_TOKEN%", canvasConfig.apiToken);
  html.replace("%TIMEZONE%", timezoneConfig.displayName);
  html.replace("%FLASH_CHECKED%", ledConfig.useFlashing ? "checked" : "");
  html.replace("%FLASH_INT%", String(ledConfig.flashInterval));
  html.replace("%FLASH_STEP%", String(ledConfig.flashStep));
  html.replace("%MAX_BRIGHTNESS%", String(ledConfig.maxBrightness));
  html.replace("%RED_DAYS%", String(ledConfig.redLEDDaysAhead));
  html.replace("%YELLOW_DAYS%", String(ledConfig.yellowLEDDaysAhead));
  html.replace("%QUIET_CHECKED%", ledConfig.quietHoursEnabled ? "checked" : "");
  html.replace("%QUIET_START%", String(ledConfig.quietHourStart));
  html.replace("%QUIET_END%", String(ledConfig.quietHourEnd));
  html.replace("%FETCH_INT%", String(canvasConfig.fetchInterval / 60000));
  html.replace("%AP_PASSWORD%", systemConfig.apPassword);

  html.replace("%INCLUDE_OVERDUE_CHECKED%", canvasConfig.includeOverdue ? "checked" : "");
  html.replace("%BUG_REPORT_CHECKED%", systemConfig.bugReportEnabled ? "checked" : "");
  html.replace("%DEBUG_CHECKED%", systemConfig.debugMode ? "checked" : "");

  server.send(200, "text/html", html);
}

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
  doc["uptime"] = millis() / 1000;
  doc["wifi_connected"] = WiFi.status() == WL_CONNECTED;
  doc["wifi_ssid"] = WiFi.SSID();
  doc["time_synced"] = timeSyncComplete;
  doc["consecutive_errors"] = consecutiveErrors;
  doc["last_check_ago"] = (millis() - lastFetch) / 1000;
  doc["last_success_ago"] = (millis() - lastSuccessfulFetch) / 1000;
  doc["assignment_status"] = assignmentStatus;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["cpu_temp"] = temperatureRead();

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}


void handleTestMode() {
  if (!DEV_MODE) {
    server.send(403, "text/plain", "Test mode disabled");
    return;
  }
  
  String html = R"rawliteral(
<!DOCTYPE html><html><head>
<title>LED Test Mode</title>
<meta name="viewport" content="width=device-width">
<style>
body{font-family:Arial;max-width:600px;margin:20px auto;padding:20px;background:#1a1a1a;color:#fff;}
.container{background:#2a2a2a;padding:30px;border-radius:10px;}
h1{color:#667eea;text-align:center;}
button{width:100%;padding:15px;margin:8px 0;border:none;cursor:pointer;border-radius:5px;font-size:16px;font-weight:bold;color:white;}
.err{background:#f44336;} .norm{background:#4caf50;} .clr{background:#2196f3;}
button:hover{opacity:0.8;}
#status{padding:15px;background:#1a1a1a;border-radius:5px;margin-top:20px;font-family:monospace;}
</style>
</head><body><div class="container">
<h1>LED Test Mode</h1>
<p style="text-align:center;color:#ffa500;">DEV_MODE Active</p>

<h3>Error Patterns</h3>
<button class="err" onclick="test('wifi')">WiFi (Red-Red)</button>
<button class="err" onclick="test('auth')">Auth (Yellow-Red)</button>
<button class="err" onclick="test('server')">Server (Red-Yellow-Red)</button>
<button class="err" onclick="test('time')">Time Sync (Green-Yellow)</button>
<button class="err" onclick="test('memory')">Memory (Red-Red-Red)</button>
<button class="err" onclick="test('json')">JSON Parse (Yellow-Green-Yellow)</button>
<button class="err" onclick="test('buffer')">Buffer Full (All Solid)</button>

<h3>Normal States</h3>
<button class="norm" onclick="test('green')">Green LED</button>
<button class="norm" onclick="test('yellow')">Yellow LED</button>
<button class="norm" onclick="test('red')">Red LED</button>

<button class="clr" onclick="test('clear')">Clear Test</button>

<div id="status">Ready</div>
</div>

<script>
function test(type) {
  fetch('/test-trigger?type=' + type)
    .then(r => r.text())
    .then(msg => {
      document.getElementById('status').innerHTML = msg;
    })
    .catch(() => {
      document.getElementById('status').innerHTML = 'Error';
    });
}
</script>
</body></html>
)rawliteral";
  
  server.send(200, "text/html", html);
}

void handleTestTrigger() {
  if (!DEV_MODE) {
    server.send(403, "text/plain", "Not authorized");
    return;
  }
  
  String type = server.arg("type");
  
  if (type == "wifi") {
    currentErrorCode = ERR_WIFI_DISCONNECT;
    assignmentStatus = 0;
    server.send(200, "text/plain", "[ACTIVE] WiFi error - slow flash");
  } else if (type == "auth") {
    currentErrorCode = ERR_CANVAS_AUTH;
    assignmentStatus = 0;
    server.send(200, "text/plain", "[ACTIVE] Auth error - double flash");
  } else if (type == "server") {
    currentErrorCode = ERR_CANVAS_SERVER;
    assignmentStatus = 0;
    server.send(200, "text/plain", "[ACTIVE] Server error - triple flash");
  } else if (type == "time") {
    currentErrorCode = ERR_TIME_SYNC;
    assignmentStatus = 0;
    server.send(200, "text/plain", "[ACTIVE] Time sync - medium flash");
  } else if (type == "memory") {
    currentErrorCode = ERR_MEMORY_LOW;
    assignmentStatus = 0;
    server.send(200, "text/plain", "[ACTIVE] Memory low - quad flash");
  } else if (type == "json") {
    currentErrorCode = ERR_JSON_PARSE;
    assignmentStatus = 0;
    server.send(200, "text/plain", "[ACTIVE] JSON parse - fast flash");
  } else if (type == "buffer") {
    currentErrorCode = ERR_BUFFER_EXHAUSTED;
    assignmentStatus = 0;
    server.send(200, "text/plain", "[ACTIVE] Buffer full - all solid");
  } else if (type == "green") {
    currentErrorCode = ERR_NONE;
    assignmentStatus = 1;
    server.send(200, "text/plain", "[NORMAL] Green LED");
  } else if (type == "yellow") {
    currentErrorCode = ERR_NONE;
    assignmentStatus = 2;
    server.send(200, "text/plain", "[NORMAL] Yellow LED");
  } else if (type == "red") {
    currentErrorCode = ERR_NONE;
    assignmentStatus = 3;
    server.send(200, "text/plain", "[NORMAL] Red LED");
  } else if (type == "clear") {
    currentErrorCode = ERR_NONE;
    assignmentStatus = 1;
    server.send(200, "text/plain", "[CLEARED] Normal operation");
  } else {
    server.send(400, "text/plain", "Unknown type");
  }
}

void handleSnooze() {
  // POST /snooze?hours=N  activate snooze
  // POST /snooze?cancel=1  cancel snooze
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
    unsigned long remainMs = snoozeUntil - now;
    unsigned long remainMin = remainMs / 60000;
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

  // LED feedback
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

void handleSave() {
  if (server.hasArg("ssid")) server.arg("ssid").toCharArray(wifiConfig.ssid, sizeof(wifiConfig.ssid));
  if (server.hasArg("password")) server.arg("password").toCharArray(wifiConfig.password, sizeof(wifiConfig.password));
  if (server.hasArg("ssid2")) {
    server.arg("ssid2").toCharArray(wifiConfig.ssid2, sizeof(wifiConfig.ssid2));
    wifiConfig.useSecondaryNetwork = strlen(wifiConfig.ssid2) > 0;
  }
  if (server.hasArg("password2")) server.arg("password2").toCharArray(wifiConfig.password2, sizeof(wifiConfig.password2));
  if (server.hasArg("apiToken")) server.arg("apiToken").toCharArray(canvasConfig.apiToken, sizeof(canvasConfig.apiToken));

  // Build Canvas API URL from school domain field
  if (server.hasArg("canvasSchoolUrl")) {
    String school = server.arg("canvasSchoolUrl");
    school.trim();
    if (school.startsWith("https://")) school = school.substring(8);
    if (school.startsWith("http://")) school = school.substring(7);
    int slash = school.indexOf('/');
    if (slash > 0) school = school.substring(0, slash);
    if (school.length() > 0) {
      String fullUrl = "https://" + school + "/api/v1/users/self/todo";
      fullUrl.toCharArray(canvasConfig.apiUrl, sizeof(canvasConfig.apiUrl));
    }
  }

  if (server.hasArg("timezone")) {
    String tz = server.arg("timezone");
    int pipe = tz.indexOf('|');
    if (pipe > 0) {
      tz.substring(0, pipe).toCharArray(timezoneConfig.tzString, sizeof(timezoneConfig.tzString));
      tz.substring(pipe + 1).toCharArray(timezoneConfig.displayName, sizeof(timezoneConfig.displayName));
    }
  }

  ledConfig.useFlashing = server.hasArg("useFlashing");
  if (server.hasArg("flashInterval")) ledConfig.flashInterval = server.arg("flashInterval").toInt();
  if (server.hasArg("flashStep")) ledConfig.flashStep = server.arg("flashStep").toInt();
  if (server.hasArg("maxBrightness")) ledConfig.maxBrightness = constrain(server.arg("maxBrightness").toInt(), 10, 255);

  // Validate and set LED days with constraint
  if (server.hasArg("redDays") && server.hasArg("yellowDays")) {
    int redDays = constrain(server.arg("redDays").toInt(), 0, 7);
    int yellowDays = constrain(server.arg("yellowDays").toInt(), 0, 14);

    // Ensure yellow >= red
    if (yellowDays < redDays) {
      yellowDays = redDays;
    }

    ledConfig.redLEDDaysAhead = redDays;
    ledConfig.yellowLEDDaysAhead = yellowDays;
  }

  ledConfig.quietHoursEnabled = server.hasArg("quietHours");
  if (server.hasArg("quietStart")) ledConfig.quietHourStart = server.arg("quietStart").toInt();
  if (server.hasArg("quietEnd")) ledConfig.quietHourEnd = server.arg("quietEnd").toInt();

  if (server.hasArg("deviceName")) server.arg("deviceName").toCharArray(systemConfig.deviceName, sizeof(systemConfig.deviceName));
  if (server.hasArg("fetchInterval")) canvasConfig.fetchInterval = server.arg("fetchInterval").toInt() * 60UL * 1000UL;
  if (server.hasArg("apPassword")) server.arg("apPassword").toCharArray(systemConfig.apPassword, sizeof(systemConfig.apPassword));
  // dashboardUrl and dashboardApiKey are baked in at build time  not user-configurable

  canvasConfig.includeOverdue = server.hasArg("includeOverdue");
  systemConfig.bugReportEnabled = server.hasArg("bugReport");
  systemConfig.debugMode = server.hasArg("debug");
  systemConfig.setupComplete = true;
  saveConfig();

  // Detect if accessed via AP or local WiFi
  IPAddress clientIP = server.client().remoteIP();
  IPAddress apIP = WiFi.softAPIP();
  bool isFromAP = (clientIP[0] == apIP[0] && clientIP[1] == apIP[1] && clientIP[2] == apIP[2]);

  if (isFromAP) {
    // From AP (initial setup) — earthy theme, show local IP, auto-reboot
    String localIp = WiFi.localIP().toString();
    String confirmHtml = F("<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
    confirmHtml += F("<style>:root{--bg:#ddd6c4;--sf:#faf5eb;--bd:#b5a688;--tx:#1c1408;--mt:#5e4e38;--gr:#3d5a2e;--gw:rgba(61,90,46,.3);}");
    confirmHtml += F("*{box-sizing:border-box;margin:0;padding:0;}");
    confirmHtml += F("body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',system-ui,sans-serif;background:var(--bg);color:var(--tx);font-size:15px;line-height:1.6;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;}");
    confirmHtml += F(".card{background:var(--sf);border:1px solid var(--bd);border-left:3px solid var(--gr);border-radius:14px;padding:32px 28px;max-width:420px;width:100%;text-align:center;}");
    confirmHtml += F(".dot{width:40px;height:40px;border-radius:50%;background:var(--gr);margin:0 auto 18px;display:flex;align-items:center;justify-content:center;}");
    confirmHtml += F(".dot svg{width:22px;height:22px;stroke:#fff;fill:none;stroke-width:2.5;stroke-linecap:round;stroke-linejoin:round;}");
    confirmHtml += F("h2{font-size:20px;font-weight:700;margin-bottom:10px;}");
    confirmHtml += F("p{color:var(--mt);font-size:14px;margin-bottom:14px;}");
    confirmHtml += F(".url-box{background:var(--bg);border:1px solid var(--bd);border-radius:8px;padding:10px 14px;font-size:14px;font-weight:600;color:var(--gr);margin:14px 0;word-break:break-all;}");
    confirmHtml += F(".note{font-size:12px;color:var(--mt);}</style></head><body><div class='card'>");
    confirmHtml += F("<div class='dot'><svg viewBox='0 0 24 24'><polyline points='20 6 9 17 4 12'/></svg></div>");
    confirmHtml += F("<h2>Setup complete!</h2>");
    confirmHtml += F("<p>Due Light is connecting to your home WiFi and will be ready in a moment.</p>");
    confirmHtml += "<div class='url-box'>http://" + localIp + "</div>";
    confirmHtml += F("<p>To adjust settings later, connect to your home WiFi and visit the address above.</p>");
    confirmHtml += F("<p class='note'>This page will close automatically.</p>");
    confirmHtml += F("</div><script>setTimeout(function(){window.close();},4000);</script></body></html>");

    server.sendHeader("Connection", "close");
    server.send(200, "text/html", confirmHtml);
    delay(2000);
    ESP.restart();
  } else {
    // From local WiFi — earthy theme, auto-reboot, spinner, poll until back up, redirect to /settings?saved=1
    String confirmHtml = F("<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
    confirmHtml += F("<style>:root{--bg:#ddd6c4;--sf:#faf5eb;--bd:#b5a688;--tx:#1c1408;--mt:#5e4e38;--gr:#3d5a2e;}");
    confirmHtml += F("*{box-sizing:border-box;margin:0;padding:0;}");
    confirmHtml += F("body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',system-ui,sans-serif;background:var(--bg);color:var(--tx);font-size:15px;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;}");
    confirmHtml += F(".card{background:var(--sf);border:1px solid var(--bd);border-left:3px solid var(--gr);border-radius:14px;padding:40px 28px;max-width:380px;width:100%;text-align:center;}");
    confirmHtml += F("@keyframes spin{to{transform:rotate(360deg)}}");
    confirmHtml += F(".spinner{width:36px;height:36px;border:3px solid var(--bd);border-top-color:var(--gr);border-radius:50%;animation:spin .8s linear infinite;margin:0 auto 20px;}");
    confirmHtml += F("h2{font-size:18px;font-weight:700;margin-bottom:8px;}");
    confirmHtml += F("p{color:var(--mt);font-size:14px;}</style></head><body><div class='card'>");
    confirmHtml += F("<div class='spinner'></div>");
    confirmHtml += F("<h2>Applying settings&hellip;</h2>");
    confirmHtml += F("<p id='msg'>Device is rebooting. This takes about 10 seconds.</p>");
    confirmHtml += F("</div><script>");
    confirmHtml += F("var attempts=0;");
    confirmHtml += F("function poll(){");
    confirmHtml += F("  attempts++;");
    confirmHtml += F("  if(attempts>30){document.getElementById('msg').textContent='Taking longer than expected \u2014 try refreshing.';return;}");
    confirmHtml += F("  fetch('/').then(function(r){");
    confirmHtml += F("    if(r.ok){sessionStorage.removeItem('dlRebooting');window.location.href='/settings?saved=1';}");
    confirmHtml += F("    else setTimeout(poll,2000);");
    confirmHtml += F("  }).catch(function(){setTimeout(poll,2000);});");
    confirmHtml += F("}");
    confirmHtml += F("if(!sessionStorage.getItem('dlRebooting')){");
    confirmHtml += F("  sessionStorage.setItem('dlRebooting','1');");
    confirmHtml += F("  fetch('/reboot',{method:'POST'}).catch(function(){});");
    confirmHtml += F("  setTimeout(poll,5000);");
    confirmHtml += F("} else {");
    confirmHtml += F("  setTimeout(poll,2000);");
    confirmHtml += F("}");
    confirmHtml += F("</script></body></html>");

    server.send(200, "text/html", confirmHtml);
  }
}

// ============================================
// NEW API HANDLERS: WiFi Scan, Test, Refresh
// ============================================
void handleScan() {
  Serial.println(" Scanning WiFi networks...");

  // Clear any previous scan results
  WiFi.scanDelete();
  delay(100);

  // Attempt scan with retries (AP+STA mode can fail intermittently)
  int n = -1;
  for (int attempt = 0; attempt < 3 && n < 0; attempt++) {
    if (attempt > 0) {
      Serial.printf("  Scan retry %d/3...\n", attempt + 1);
      delay(500);
    }
    n = WiFi.scanNetworks(false, false, false, 300);  // sync, no hidden, no passive, 300ms/channel
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
      if (ssid.length() == 0) continue;  // Skip hidden networks

      // Check for duplicate SSID (same network on multiple channels)
      bool isDuplicate = false;
      for (size_t j = 0; j < networks.size(); j++) {
        if (networks[j]["ssid"].as<String>() == ssid) {
          if (WiFi.RSSI(i) > networks[j]["rssi"].as<int>()) {
            networks[j]["rssi"] = WiFi.RSSI(i);
          }
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
  
  // Disconnect current connection temporarily
  WiFi.disconnect();
  delay(100);
  
  WiFi.begin(testSsid.c_str(), testPass.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }
  
  bool success = WiFi.status() == WL_CONNECTED;
  String msg = success ? "Connected successfully! Signal: " + String(WiFi.RSSI()) + " dBm" 
                       : "Connection failed. Check password.";
  
  // On failure clean up; on success leave STA connected so Canvas test can reuse it.
  // Do NOT call WiFi.softAP() — the AP was never disrupted (WIFI_AP_STA keeps AP
  // and STA independent). Restarting the AP kills the browser TCP connection
  // before the response is sent, causing the button to hang forever.
  if (!success) WiFi.disconnect(false);
  
  String response = "{\"success\":" + String(success ? "true" : "false") + ",\"message\":\"" + msg + "\"}";
  server.send(200, "application/json", response);
}

void handleTestCanvas() {
  if (!server.hasArg("token")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing token\"}");
    return;
  }
  
  String testToken = server.arg("token");
  Serial.println(" Testing Canvas token...");
  
  // Need WiFi to test Canvas
  if (WiFi.status() != WL_CONNECTED) {
    // Try credentials from the form (setup page) then fall back to stored config
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
      while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
      }
    }
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    server.send(200, "application/json", "{\"success\":false,\"message\":\"Need WiFi connection first. Test WiFi above.\"}");
    return;
  }
  
  WiFiClientSecure testClient;
  testClient.setInsecure();
  HTTPClient testHttp;
  testHttp.setTimeout(15000);
  
  // Use schoolUrl from form (setup page) or fall back to stored API URL
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
    if (httpCode == 200) {
      msg = "Token valid! Canvas connection working.";
      successStr = "true";
    } else if (httpCode == 401) {
      msg = "Invalid token. Generate a new one in Canvas.";
      successStr = "false";
    } else {
      msg = "Canvas error (HTTP " + String(httpCode) + "). Try again.";
      successStr = "false";
    }
    
    server.send(200, "application/json", "{\"success\":" + successStr + ",\"message\":\"" + msg + "\"}");
  } else {
    server.send(200, "application/json", "{\"success\":false,\"message\":\"Could not connect to Canvas server.\"}");
  }
}

void handleRefresh() {
  Serial.println("Manual refresh requested");
  int oldStatus = assignmentStatus;
  assignmentStatus = fetchCanvasAssignments();
  lastFetch = millis();
  
  String statusName = assignmentStatus == 0 ? getErrorMessage() :
                      assignmentStatus == 3 ? "RED (due today)" : 
                      assignmentStatus == 2 ? "YELLOW (due soon)" : "GREEN (all clear)";
  
  String response = "{\"success\":true,\"status\":" + String(assignmentStatus) + 
                    ",\"statusName\":\"" + statusName + 
                    "\",\"changed\":" + String(oldStatus != assignmentStatus ? "true" : "false") + "}";
  server.send(200, "application/json", response);
}

// ============================================
// SERVER STARTUP
// ============================================
void startWebServer() {
  server.on("/", handleRoot);
  server.on("/settings", handleSettings);
  server.on("/logs", handleLogs);
  server.on("/health", handleHealth);
  server.on("/test", handleTestMode);
  server.on("/test-trigger", handleTestTrigger);
  server.on("/scan", handleScan);
  server.on("/test-wifi", HTTP_POST, handleTestWifi);
  server.on("/test-canvas", HTTP_POST, handleTestCanvas);
  server.on("/refresh", HTTP_POST, handleRefresh);
  server.on("/snooze", HTTP_POST, handleSnooze);
  server.on("/snooze/status", HTTP_GET, handleSnoozeStatus);
  server.on("/reboot", HTTP_POST, handleReboot);
  server.on("/factory-reset", HTTP_POST, handleFactoryReset);
  server.on("/save", HTTP_POST, handleSave);
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

  if (!apStarted) {
    Serial.println(" Failed to start AP!");
    return;
  }

  Serial.println("AP: " + String(systemConfig.deviceName));
  Serial.println(" AP URL: http://" + WiFi.softAPIP().toString());

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  if (!webServerRunning) startWebServer();
  Serial.println("Settings AP ready");
}

void monitorSystem() {
  static unsigned long lastTempCheck = 0, lastPowerCheck = 0;
  unsigned long now = millis();

  // Temperature check every 5 minutes
  if (now - lastTempCheck >= 300000) {
    float temp = temperatureRead();
    Serial.printf(" Temp: %.1fC%s\n", temp, temp > 80 ? "  HIGH" : (temp > 70 ? " (warm)" : ""));
    lastTempCheck = now;
  }

  // Power estimate every 5 minutes
  if (now - lastPowerCheck >= 300000) {
    // Power calculation removed - analogRead() conflicts with WiFi on ADC2 pins (GPIO 25/27)
    lastPowerCheck = now;
  }
}
