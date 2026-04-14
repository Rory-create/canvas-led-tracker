#include "settings_handler.h"
#include "config.h"
#include "canvas_api.h"
#include "version.h"
#include "welcome_html.h"
#include "settings_html.h"

String getErrorMessage() {
  switch(currentErrorCode) {
    case ERR_WIFI_DISCONNECT:   return "WiFi Disconnected";
    case ERR_CANVAS_AUTH:       return "Canvas Auth Error (401 - Check Token)";
    case ERR_CANVAS_SERVER:     return "Canvas Server Error (500+)";
    case ERR_TIME_SYNC:         return "Time Sync Failed";
    case ERR_MEMORY_LOW:        return "Memory Critical (<15KB)";
    case ERR_JSON_PARSE:        return "JSON Parse Failed";
    case ERR_BUFFER_EXHAUSTED:  return "Too Many Assignments (Buffer Full)";
    default:                    return "ERROR - Check Device";
  }
}

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
    switch(currentErrorCode) {
      case ERR_WIFI_DISCONNECT:  statusText = "WiFi Disconnected"; break;
      case ERR_CANVAS_AUTH:      statusText = "Token expired - visit Settings to renew"; break;
      case ERR_CANVAS_SERVER:    statusText = "Canvas Server Error"; break;
      case ERR_TIME_SYNC:        statusText = "Time Sync Failed"; break;
      case ERR_MEMORY_LOW:       statusText = "Memory Critical"; break;
      case ERR_JSON_PARSE:       statusText = "JSON Parse Error"; break;
      case ERR_BUFFER_EXHAUSTED: statusText = "Buffer Exhausted (Too Many Assignments)"; break;
      default:                   statusText = "ERROR - Check Device";
    }
  } else if (assignmentStatus == 3) statusText = "Today";
  else if (assignmentStatus == 2) statusText = "Tomorrow";
  else statusText = "All Clear";
  html.replace("%ASSIGNMENT_STATUS%", statusText);

  String lastCheckText = String((millis() - lastFetch) / 60000) + "m ago";
  if (consecutiveErrors > 0) lastCheckText += " (" + String(consecutiveErrors) + " errors)";
  html.replace("%LAST_CHECK%", lastCheckText);

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

  time_t nowTs; time(&nowTs);
  String tokenStatus = "", tokenAlert = "";
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

  String canvasSchoolUrl = "";
  String apiUrlStr = String(canvasConfig.apiUrl);
  if (apiUrlStr.startsWith("https://")) {
    String noProto = apiUrlStr.substring(8);
    int slash = noProto.indexOf('/');
    canvasSchoolUrl = (slash > 0) ? noProto.substring(0, slash) : noProto;
  }
  html.replace("%CANVAS_SCHOOL_URL%", canvasSchoolUrl);

  String assignmentsSection = "";
  if (assignmentCount > 0) {
    assignmentsSection = "<div class='section'><h3>Upcoming Assignments</h3>";
    for (int i = 0; i < assignmentCount; i++) {
      Assignment &a = displayedAssignments[i];
      char dueDateStr[50];
      struct tm due_tm;
      localtime_r(&a.dueTimestamp, &due_tm);
      strftime(dueDateStr, sizeof(dueDateStr), "%a %b %d, %I:%M %p", &due_tm);
      String urgencyBadge = a.urgency == 2 ?
        "<span style='background:#f44336;color:white;padding:3px 8px;border-radius:3px;font-size:12px;'>URGENT</span>" :
        "<span style='background:#ff9800;color:white;padding:3px 8px;border-radius:3px;font-size:12px;'>COMING UP</span>";
      assignmentsSection += "<div style='border-left:3px solid " + String(a.urgency == 2 ? "#f44336" : "#ff9800") + ";padding:10px;margin:10px 0;background:#f9f9f9;'>";
      assignmentsSection += urgencyBadge + " <strong>" + a.name + "</strong><br>";
      assignmentsSection += "<small style='color:#666;'>Due: " + String(dueDateStr) + "</small><br>";
      if (a.description.length() > 0) assignmentsSection += "<small>" + a.description + "</small><br>";
      if (a.htmlUrl.length() > 0) assignmentsSection += "<a href='" + a.htmlUrl + "' target='_blank' style='font-size:12px;'>Open in Canvas</a>";
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

void handleSave() {
  if (server.hasArg("ssid")) server.arg("ssid").toCharArray(wifiConfig.ssid, sizeof(wifiConfig.ssid));
  if (server.hasArg("password")) server.arg("password").toCharArray(wifiConfig.password, sizeof(wifiConfig.password));
  if (server.hasArg("ssid2")) {
    server.arg("ssid2").toCharArray(wifiConfig.ssid2, sizeof(wifiConfig.ssid2));
    wifiConfig.useSecondaryNetwork = strlen(wifiConfig.ssid2) > 0;
  }
  if (server.hasArg("password2")) server.arg("password2").toCharArray(wifiConfig.password2, sizeof(wifiConfig.password2));
  if (server.hasArg("apiToken")) server.arg("apiToken").toCharArray(canvasConfig.apiToken, sizeof(canvasConfig.apiToken));

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

  if (server.hasArg("redDays") && server.hasArg("yellowDays")) {
    int redDays = constrain(server.arg("redDays").toInt(), 0, 7);
    int yellowDays = constrain(server.arg("yellowDays").toInt(), 0, 14);
    if (yellowDays < redDays) yellowDays = redDays;
    ledConfig.redLEDDaysAhead = redDays;
    ledConfig.yellowLEDDaysAhead = yellowDays;
  }

  ledConfig.quietHoursEnabled = server.hasArg("quietHours");
  if (server.hasArg("quietStart")) ledConfig.quietHourStart = server.arg("quietStart").toInt();
  if (server.hasArg("quietEnd")) ledConfig.quietHourEnd = server.arg("quietEnd").toInt();

  if (server.hasArg("deviceName")) server.arg("deviceName").toCharArray(systemConfig.deviceName, sizeof(systemConfig.deviceName));
  if (server.hasArg("fetchInterval")) canvasConfig.fetchInterval = server.arg("fetchInterval").toInt() * 60UL * 1000UL;
  if (server.hasArg("apPassword")) server.arg("apPassword").toCharArray(systemConfig.apPassword, sizeof(systemConfig.apPassword));

  canvasConfig.includeOverdue = server.hasArg("includeOverdue");
  systemConfig.bugReportEnabled = server.hasArg("bugReport");
  systemConfig.debugMode = server.hasArg("debug");
  systemConfig.setupComplete = true;
  saveConfig();

  IPAddress clientIP = server.client().remoteIP();
  IPAddress apIP = WiFi.softAPIP();
  bool isFromAP = (clientIP[0] == apIP[0] && clientIP[1] == apIP[1] && clientIP[2] == apIP[2]);

  if (isFromAP) {
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
    confirmHtml += F("<div class='url-box'>http://due-light.local</div>");
    confirmHtml += F("<p>To adjust settings later, connect to your home WiFi and visit the address above.</p>");
    confirmHtml += F("<p class='note'>This page will close automatically.</p>");
    confirmHtml += F("</div><script>setTimeout(function(){window.close();},4000);</script></body></html>");
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", confirmHtml);
    delay(2000);
    ESP.restart();
  } else {
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
