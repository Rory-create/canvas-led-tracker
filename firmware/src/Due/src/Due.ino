#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include "version.h"
#include "ota_update.h"

// ============================================
// CONFIG STRUCTURES & GLOBAL VARIABLES
// ============================================
struct LEDConfig {
  bool useFlashing = true;
  int flashInterval = 1, flashStep = 1, solidBrightness = 100, maxBrightness = 100;
  bool quietHoursEnabled = false;
  int quietHourStart = 22, quietHourEnd = 7;
  int redLEDDaysAhead = 0, yellowLEDDaysAhead = 1;
} ledConfig;

struct WiFiConfig {
  char ssid[64] = "", password[64] = "", ssid2[64] = "", password2[64] = "";
  bool useSecondaryNetwork = false;
} wifiConfig;

struct CanvasConfig {
  char apiUrl[256] = "https://ojrsd.instructure.com/api/v1/users/self/todo";
  char apiToken[128] = "";
  int itemsPerPage = 6;
  unsigned long fetchInterval = 10UL * 60UL * 1000UL;
} canvasConfig;

struct TimezoneConfig {
  char tzString[64] = "EST5EDT,M3.2.0/2,M11.1.0/2";
  char displayName[32] = "US Eastern";
} timezoneConfig;

struct SystemConfig {
  bool setupComplete = false;
  char deviceName[32] = "Canvas_LED_Tracker";
  bool debugMode = true;
  char apPassword[64] = "canvas123";
} systemConfig;

const int greenLED = 12, yellowLED = 15, redLED = 32;
const int MAX_RETRIES = 3, HTTPS_TIMEOUT = 60000, DNS_PORT = 53;
const byte LOG_BUFFER_SIZE = 50;

WiFiClientSecure client;
HTTPClient http;
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

String serialLog[LOG_BUFFER_SIZE];
int logIndex = 0, assignmentStatus = 0;
unsigned long lastFetch = 0, lastPulseTime = 0, lastSuccessfulFetch = 0;
int pulseBrightness = 0, fadeDirection = 1, consecutiveErrors = 0;
bool webServerRunning = false;
bool timeSyncComplete = false;

// ============================================
// MACROS & HELPERS
// ============================================
#define PREFS_GET_STR(key, var, def) preferences.getString(key, var, sizeof(var)); if (strlen(var) == 0) strcpy(var, def)
#define PREFS_SET_STR(key, var) preferences.putString(key, var)
#define PREFS_GET_INT(key, def) preferences.getInt(key, def)
#define PREFS_GET_BOOL(key, def) preferences.getBool(key, def)
#define LED_PINS {greenLED, yellowLED, redLED}

void addToLog(String msg) {
  serialLog[logIndex++] = msg;
  logIndex %= LOG_BUFFER_SIZE;
  Serial.println(msg);
}

// ============================================
// STORAGE FUNCTIONS
// ============================================
void saveConfig() {
  preferences.begin("config", false);
  
  PREFS_SET_STR("ssid", wifiConfig.ssid);
  PREFS_SET_STR("pass", wifiConfig.password);
  PREFS_SET_STR("ssid2", wifiConfig.ssid2);
  PREFS_SET_STR("pass2", wifiConfig.password2);
  preferences.putBool("use2nd", wifiConfig.useSecondaryNetwork);
  
  PREFS_SET_STR("apiUrl", canvasConfig.apiUrl);
  PREFS_SET_STR("apiToken", canvasConfig.apiToken);
  preferences.putInt("itemsPer", canvasConfig.itemsPerPage);
  preferences.putULong("fetchInt", canvasConfig.fetchInterval);
  
  preferences.putBool("useFlash", ledConfig.useFlashing);
  preferences.putInt("flashInt", ledConfig.flashInterval);
  preferences.putInt("flashStep", ledConfig.flashStep);
  preferences.putInt("brightness", ledConfig.solidBrightness);
  preferences.putInt("maxBright", ledConfig.maxBrightness);
  preferences.putBool("quietEn", ledConfig.quietHoursEnabled);
  preferences.putInt("quietStart", ledConfig.quietHourStart);
  preferences.putInt("quietEnd", ledConfig.quietHourEnd);
  preferences.putInt("redDays", ledConfig.redLEDDaysAhead);
  preferences.putInt("yellowDays", ledConfig.yellowLEDDaysAhead);
  
  PREFS_SET_STR("tzString", timezoneConfig.tzString);
  PREFS_SET_STR("tzName", timezoneConfig.displayName);
  
  preferences.putBool("setupDone", systemConfig.setupComplete);
  PREFS_SET_STR("devName", systemConfig.deviceName);
  preferences.putBool("debug", systemConfig.debugMode);
  PREFS_SET_STR("apPass", systemConfig.apPassword);
  
  preferences.end();
  Serial.println("✅ Configuration saved!");
}

void loadConfig() {
  preferences.begin("config", true);
  systemConfig.setupComplete = PREFS_GET_BOOL("setupDone", false);
  
  if (systemConfig.setupComplete) {
    PREFS_GET_STR("ssid", wifiConfig.ssid, "");
    PREFS_GET_STR("pass", wifiConfig.password, "");
    PREFS_GET_STR("ssid2", wifiConfig.ssid2, "");
    PREFS_GET_STR("pass2", wifiConfig.password2, "");
    wifiConfig.useSecondaryNetwork = PREFS_GET_BOOL("use2nd", false);
    
    PREFS_GET_STR("apiUrl", canvasConfig.apiUrl, "https://ojrsd.instructure.com/api/v1/users/self/todo");
    PREFS_GET_STR("apiToken", canvasConfig.apiToken, "");
    canvasConfig.itemsPerPage = PREFS_GET_INT("itemsPer", 6);
    canvasConfig.fetchInterval = preferences.getULong("fetchInt", 10UL * 60UL * 1000UL);
    
    ledConfig.useFlashing = PREFS_GET_BOOL("useFlash", true);
    ledConfig.flashInterval = PREFS_GET_INT("flashInt", 1);
    ledConfig.flashStep = PREFS_GET_INT("flashStep", 1);
    ledConfig.solidBrightness = PREFS_GET_INT("brightness", 100);
    ledConfig.maxBrightness = PREFS_GET_INT("maxBright", 100);
    ledConfig.quietHoursEnabled = PREFS_GET_BOOL("quietEn", false);
    ledConfig.quietHourStart = PREFS_GET_INT("quietStart", 22);
    ledConfig.quietHourEnd = PREFS_GET_INT("quietEnd", 7);
    ledConfig.redLEDDaysAhead = PREFS_GET_INT("redDays", 0);
    ledConfig.yellowLEDDaysAhead = PREFS_GET_INT("yellowDays", 1);
    
    PREFS_GET_STR("tzString", timezoneConfig.tzString, "EST5EDT,M3.2.0/2,M11.1.0/2");
    PREFS_GET_STR("tzName", timezoneConfig.displayName, "US Eastern");
    PREFS_GET_STR("devName", systemConfig.deviceName, "Canvas_LED_Tracker");
    systemConfig.debugMode = PREFS_GET_BOOL("debug", true);
    PREFS_GET_STR("apPass", systemConfig.apPassword, "canvas123");
    
    Serial.println("✅ Configuration loaded");
  } else {
    Serial.println("⚠️ First time setup required");
  }
  preferences.end();
}

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
  // Don't interrupt if already connected or connecting
  if (WiFi.status() == WL_CONNECTED) return;
  
  // Check if connection is in progress
  wl_status_t status = WiFi.status();
  if (status == WL_IDLE_STATUS || status == WL_DISCONNECTED) {
    // Safe to attempt connection
  } else {
    // Connection in progress, wait
    return;
  }
  
  Serial.println("📡 Connecting to WiFi...");
  
  // Only start AP if we're in first-time setup
  if (!systemConfig.setupComplete) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_STA);
  }
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
    Serial.printf("📍 IP: %s\n", WiFi.localIP().toString().c_str());
    
    String hostname = String(systemConfig.deviceName);
    hostname.replace(" ", "");
    hostname.replace("_", "");
    hostname.toLowerCase();
    
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

int fetchCanvasAssignments() {
  connectWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi not connected - keeping last status");
    consecutiveErrors++;
    return assignmentStatus;
  }
  
  // Don't fetch if time isn't synced
  if (!timeSyncComplete) {
    Serial.println("⚠️ Time not synced - retrying sync...");
    initTime();
    if (!timeSyncComplete) {
      Serial.println("❌ Cannot fetch without valid time - keeping last status");
      return assignmentStatus;
    }
  }
  
  client.setInsecure();
  http.setTimeout(HTTPS_TIMEOUT);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  
  int newStatus = 0;
  char fullUrl[512];
  snprintf(fullUrl, sizeof(fullUrl), "%s?per_page=%d", canvasConfig.apiUrl, canvasConfig.itemsPerPage);
  
  for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
    if (systemConfig.debugMode) Serial.printf("🔄 Attempt %d/%d\n", attempt + 1, MAX_RETRIES);
    
    if (http.begin(client, fullUrl)) {
      http.addHeader("Authorization", String("Bearer ") + canvasConfig.apiToken);
      http.addHeader("Accept", "application/json");
      
      int httpCode = http.GET();
      if (systemConfig.debugMode) Serial.printf("📥 HTTP: %d\n", httpCode);
      
      if (httpCode == 200) {
        DynamicJsonDocument doc(8192);
        DeserializationError error = deserializeJson(doc, http.getString());
        
        if (!error) {
          time_t now, redDeadline = 0, yellowDeadline = 0;
          time(&now);
          redDeadline = now + (ledConfig.redLEDDaysAhead * 86400);
          yellowDeadline = now + (ledConfig.yellowLEDDaysAhead * 86400);
          
          if (systemConfig.debugMode) {
            Serial.printf("📅 Checking: Red=%d days, Yellow=%d days\n", 
                         ledConfig.redLEDDaysAhead, ledConfig.yellowLEDDaysAhead);
          }
          
          for (JsonObject item : doc.as<JsonArray>()) {
            const char* dueDate = item["assignment"]["due_at"];
            if (dueDate && !item["completed"].as<bool>()) {
              struct tm due_tm = {0};
              if (strptime(dueDate, "%Y-%m-%dT%H:%M:%SZ", &due_tm)) {
                time_t due_time = esp_timegm(&due_tm);
                
                // Validate date is reasonable (between 2020 and 2050)
                if (due_time < 1577836800 || due_time > 2524608000) {
                  Serial.printf("⚠️ Invalid date detected: %s\n", dueDate);
                  continue;
                }
                
                if (due_time <= redDeadline) {
                  newStatus = 1;
                  if (systemConfig.debugMode) Serial.println("🔴 RED");
                  break;
                } else if (due_time <= yellowDeadline) {
                  newStatus = 2;
                  if (systemConfig.debugMode) Serial.println("🟡 YELLOW");
                }
              }
            }
          }
          http.end();
          
          // Success!
          consecutiveErrors = 0;
          lastSuccessfulFetch = millis();
          Serial.println("✅ Canvas fetch successful");
          return newStatus;
        } else {
          Serial.printf("⚠️ JSON parse error: %s\n", error.c_str());
        }
      } else if (httpCode == 401) {
        Serial.println("❌ Canvas API: Invalid token (401)");
        http.end();
        consecutiveErrors++;
        return assignmentStatus; // Keep current status
      } else if (httpCode == 500 || httpCode == 502 || httpCode == 503 || httpCode == 504) {
        Serial.printf("⚠️ Canvas server error (%d) - likely outage\n", httpCode);
        http.end();
        consecutiveErrors++;
        // Don't retry on server errors - save time
        break;
      } else if (httpCode == 429) {
        Serial.println("⚠️ Canvas API: Rate limited (429)");
        http.end();
        consecutiveErrors++;
        return assignmentStatus;
      } else if (httpCode < 0) {
        Serial.printf("❌ HTTP request failed: %s\n", http.errorToString(httpCode).c_str());
      } else {
        Serial.printf("⚠️ Unexpected HTTP code: %d\n", httpCode);
      }
      http.end();
    } else {
      Serial.println("❌ Could not connect to Canvas");
    }
    
    if (attempt < MAX_RETRIES - 1) {
      Serial.println("⏳ Retrying in 2 seconds...");
      delay(2000);
    }
  }
  
  consecutiveErrors++;
  
  // If we've had many consecutive errors, warn the user
  if (consecutiveErrors >= 3) {
    Serial.printf("⚠️ %d consecutive Canvas errors - keeping last known status\n", consecutiveErrors);
    if (consecutiveErrors == 5) {
      Serial.println("💡 TIP: Canvas may be experiencing an outage. Device will keep trying.");
    }
  }
  
  return assignmentStatus; // Keep last known status on error
}

// ============================================
// LED CONTROL
// ============================================
void setAllLEDsOff() {
  int pins[] = LED_PINS;
  for (int p : pins) analogWrite(p, 0);
}

void setLED(int ledPin, int brightness) {
  setAllLEDsOff();
  analogWrite(ledPin, constrain(brightness, 0, ledConfig.maxBrightness));
}

void pulseLED(int ledPin, unsigned long &lastTime, int &brightness, int &direction) {
  unsigned long now = millis();
  if (now - lastTime >= ledConfig.flashInterval) {
    brightness += direction * ledConfig.flashStep;
    if (brightness >= ledConfig.maxBrightness) {
      brightness = ledConfig.maxBrightness;
      direction = -1;
    } else if (brightness <= 0) {
      brightness = 0;
      direction = 1;
    }
    analogWrite(ledPin, brightness);
    lastTime = now;
  }
}

void updateLEDs() {
  if (isQuietHours()) {
    setAllLEDsOff();
    return;
  }
  
  int targetLED = greenLED;
  if (assignmentStatus == 1) targetLED = redLED;
  else if (assignmentStatus == 2) targetLED = yellowLED;
  
  if (ledConfig.useFlashing && assignmentStatus > 0) {
    setAllLEDsOff();
    pulseLED(targetLED, lastPulseTime, pulseBrightness, fadeDirection);
  } else {
    setLED(targetLED, ledConfig.solidBrightness);
  }
}

// ============================================
// HTML TEMPLATES (must be before web handlers)
// ============================================
const char WELCOME_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>Canvas LED Setup</title>
<style>
body{font-family:Arial;max-width:600px;margin:20px auto;padding:20px;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;}
.container{background:white;padding:30px;border-radius:10px;box-shadow:0 10px 40px rgba(0,0,0,0.2);}
h1{color:#667eea;text-align:center;margin-bottom:10px;}
input,textarea{width:100%;padding:10px;margin:5px 0;box-sizing:border-box;border:1px solid #ddd;border-radius:5px;}
.pass-wrap{position:relative;}
.pass-wrap input:hover{-webkit-text-security:none !important;}
.show-pass{position:absolute;right:10px;top:15px;cursor:pointer;display:none;}
button{background:linear-gradient(135deg,#667eea,#764ba2);color:white;padding:12px;border:none;cursor:pointer;width:100%;margin:10px 0;border-radius:5px;font-size:16px;}
button:hover{opacity:0.9;}
.section{border:1px solid #e0e0e0;padding:15px;margin:15px 0;border-radius:8px;background:#f9f9f9;}
.section h3{margin-top:0;color:#555;}
.error{color:#d32f2f;font-size:13px;margin-top:5px;display:none;}
</style></head><body><div class="container">
<h1>Canvas LED Tracker</h1>
<p style="text-align:center;color:#666;">Initial Setup</p>
<p><b>Get Canvas Token:</b> Open Canvas in browser → Account → Settings → Approved Integrations → + New Access Token</p>
<form method="POST" action="/save" onsubmit="return validateSetup()">
<div class="section">
<h3>WiFi Settings</h3>
<label>Network Name (SSID)<input type="text" name="ssid" required></label>
<label>WiFi Password<div class="pass-wrap"><input type="password" name="password" class="pwd"><span class="show-pass" onclick="togglePass(this)">SHOW</span></div></label>
<label>Backup SSID<input type="text" name="ssid2"></label>
<label>Backup Password<div class="pass-wrap"><input type="password" name="password2" class="pwd"><span class="show-pass" onclick="togglePass(this)">SHOW</span></div></label>
</div>
<div class="section">
<h3>Canvas API Token</h3>
<textarea name="apiToken" rows="3" required placeholder="Paste your Canvas API token here"></textarea>
</div>
<div class="section">
<h3>LED Settings</h3>
<label>Red LED (days ahead)<input type="number" name="redDays" id="redDays" value="0" min="0" max="7"></label>
<label>Yellow LED (days ahead)<input type="number" name="yellowDays" id="yellowDays" value="1" min="0" max="14"></label>
<div class="error" id="daysError">Yellow days must be greater than or equal to red days</div>
<label>Brightness (10-255)<input type="number" name="maxBrightness" value="100" min="10" max="255"></label>
</div>
<div class="section">
<h3>Access Point Password (optional)</h3>
<label>AP Password (leave blank for no password)<input type="text" name="apPassword" placeholder="canvas123"></label>
</div>
<button type="submit">Save & Continue</button>
</form></div>
<script>
if(/Mobi|Android/i.test(navigator.userAgent)){document.querySelectorAll('.show-pass').forEach(e=>e.style.display='block');}
function togglePass(btn){let inp=btn.previousElementSibling;inp.type=inp.type==='password'?'text':'password';btn.textContent=inp.type==='password'?'SHOW':'HIDE';}
function validateSetup(){
  let red=parseInt(document.getElementById('redDays').value);
  let yellow=parseInt(document.getElementById('yellowDays').value);
  let err=document.getElementById('daysError');
  if(yellow<red){err.style.display='block';return false;}
  err.style.display='none';return true;
}
</script></body></html>
)rawliteral";

const char SETTINGS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>Canvas LED Settings</title>
<style>
body{font-family:Arial;max-width:600px;margin:20px auto;padding:20px;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;}
.container{background:white;padding:30px;border-radius:10px;box-shadow:0 10px 40px rgba(0,0,0,0.2);}
h1{color:#667eea;text-align:center;margin-bottom:5px;}
.status{text-align:center;padding:10px;background:#f0f0f0;border-radius:5px;margin:15px 0;font-size:14px;}
.status span{font-weight:bold;}
.alert{padding:10px;margin:10px 0;border-radius:5px;text-align:center;}
.alert-warning{background:#fff3cd;border:1px solid #ffc107;color:#856404;}
.alert-error{background:#f8d7da;border:1px solid #f44336;color:#721c24;}
input,textarea,select{width:100%;padding:10px;margin:5px 0;box-sizing:border-box;border:1px solid #ddd;border-radius:5px;}
.pass-wrap{position:relative;}
.pass-wrap input:hover{-webkit-text-security:none !important;}
.show-pass{position:absolute;right:10px;top:15px;cursor:pointer;display:none;}
button{color:white;padding:12px;border:none;cursor:pointer;width:100%;margin:5px 0;border-radius:5px;font-size:15px;}
.btn-save{background:linear-gradient(135deg,#667eea,#764ba2);}
.btn-reboot{background:#ff9800;}
.btn-reset{background:#f44336;}
button:hover{opacity:0.9;}
.section{border:1px solid #e0e0e0;padding:15px;margin:15px 0;border-radius:8px;background:#f9f9f9;}
.section h3{margin-top:0;color:#555;}
label{display:block;margin:8px 0;font-size:14px;}
.error{color:#d32f2f;font-size:13px;margin-top:5px;display:none;}
</style></head><body><div class="container">
<h1>Canvas LED Settings</h1>
<div class="status">
Device: <span>%DEVICE_NAME%</span> | WiFi: %WIFI_STATUS% | Assignment: <span>%ASSIGNMENT_STATUS%</span> | Checked: <span>%LAST_CHECK%</span>
</div>
%ERROR_ALERT%
<form method="POST" action="/save" onsubmit="return validateSettings()">
<div class="section"><h3>WiFi</h3>
<label>Primary SSID<input type="text" name="ssid" value="%SSID%"></label>
<label>Password<div class="pass-wrap"><input type="password" name="password" value="%PASSWORD%" class="pwd"><span class="show-pass" onclick="togglePass(this)">SHOW</span></div></label>
<label>Backup SSID<input type="text" name="ssid2" value="%SSID2%"></label>
<label>Backup Password<div class="pass-wrap"><input type="password" name="password2" value="%PASSWORD2%" class="pwd"><span class="show-pass" onclick="togglePass(this)">SHOW</span></div></label>
</div>
<div class="section"><h3>Canvas API</h3>
<label>API Token<textarea name="apiToken" rows="3">%API_TOKEN%</textarea></label>
</div>
<div class="section"><h3>LED Settings</h3>
<label><input type="checkbox" name="useFlashing" %FLASH_CHECKED%> Use Pulsing Effect</label>
<label>Brightness (10-255)<input type="number" name="maxBrightness" value="%MAX_BRIGHTNESS%" min="10" max="255"></label>
<label>Flash Interval (ms)<input type="number" name="flashInterval" value="%FLASH_INT%" min="1"></label>
<label>Flash Step<input type="number" name="flashStep" value="%FLASH_STEP%" min="1"></label>
<label>Red LED (days ahead)<input type="number" name="redDays" id="redDays" value="%RED_DAYS%" min="0" max="7"></label>
<label>Yellow LED (days ahead)<input type="number" name="yellowDays" id="yellowDays" value="%YELLOW_DAYS%" min="0" max="14"></label>
<div class="error" id="daysError">Yellow days must be greater than or equal to red days</div>
</div>
<div class="section"><h3>Quiet Hours</h3>
<label><input type="checkbox" name="quietHours" %QUIET_CHECKED%> Enable Quiet Hours</label>
<label>Start Hour (0-23)<input type="number" name="quietStart" value="%QUIET_START%" min="0" max="23"></label>
<label>End Hour (0-23)<input type="number" name="quietEnd" value="%QUIET_END%" min="0" max="23"></label>
</div>
<div class="section"><h3>System</h3>
<label>Device Name<input type="text" name="deviceName" value="%DEVICE_NAME%"></label>
<label>Check Interval (minutes)<input type="number" name="fetchInterval" value="%FETCH_INT%" min="1"></label>
<label>AP Password<input type="text" name="apPassword" value="%AP_PASSWORD%"></label>
<label><input type="checkbox" name="debug" %DEBUG_CHECKED%> Debug Mode</label>
</div>
<button type="submit" class="btn-save">Save Settings</button>
<button type="button" class="btn-reboot" onclick="if(confirm('Reboot device?'))fetch('/reboot',{method:'POST'});">Reboot</button>
<button type="button" class="btn-reset" onclick="if(confirm('FACTORY RESET? All settings erased!'))fetch('/factory-reset',{method:'POST'});">Factory Reset</button>
</form></div>
<script>
if(/Mobi|Android/i.test(navigator.userAgent)){document.querySelectorAll('.show-pass').forEach(e=>e.style.display='block');}
function togglePass(btn){let inp=btn.previousElementSibling;inp.type=inp.type==='password'?'text':'password';btn.textContent=inp.type==='password'?'SHOW':'HIDE';}
function validateSettings(){
  let red=parseInt(document.getElementById('redDays').value);
  let yellow=parseInt(document.getElementById('yellowDays').value);
  let err=document.getElementById('daysError');
  if(yellow<red){err.style.display='block';return false;}
  err.style.display='none';return true;
}
</script></body></html>
)rawliteral";

// ============================================
// WEB SERVER HANDLERS
// ============================================
void handleRoot() {
  if (systemConfig.setupComplete) {
    server.sendHeader("Location", "/settings");
    server.send(302);
  } else {
    server.send(200, "text/html", FPSTR(WELCOME_HTML));
  }
}

void handleSettings() {
  String html = FPSTR(SETTINGS_HTML);
  html.replace("%DEVICE_NAME%", systemConfig.deviceName);
  html.replace("%WIFI_STATUS%", WiFi.status() == WL_CONNECTED ? "●" : "○");
  
  String statusText = "None";
  if (assignmentStatus == 1) statusText = "Today";
  else if (assignmentStatus == 2) statusText = "Tomorrow";
  html.replace("%ASSIGNMENT_STATUS%", statusText);
  
  // Show time since last check and last successful fetch
  String lastCheckText = String((millis() - lastFetch) / 60000) + "m ago";
  if (consecutiveErrors > 0) {
    lastCheckText += " (" + String(consecutiveErrors) + " errors)";
  }
  html.replace("%LAST_CHECK%", lastCheckText);
  
  // Add error alert if needed
  String errorAlert = "";
  if (consecutiveErrors >= 3) {
    errorAlert = "<div class='alert alert-warning'>⚠️ Canvas API experiencing issues (" + 
                 String(consecutiveErrors) + " errors). Using last known status.</div>";
  }
  if (!timeSyncComplete) {
    errorAlert += "<div class='alert alert-error'>❌ Time sync failed. Assignment checks disabled until time is synced.</div>";
  }
  html.replace("%ERROR_ALERT%", errorAlert);
  
  html.replace("%SSID%", wifiConfig.ssid);
  html.replace("%PASSWORD%", wifiConfig.password);
  html.replace("%SSID2%", wifiConfig.ssid2);
  html.replace("%PASSWORD2%", wifiConfig.password2);
  html.replace("%API_TOKEN%", canvasConfig.apiToken);
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

void handleReboot() {
  Serial.println("\n🔄 Reboot requested");
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "Rebooting...");
  delay(500);
  ESP.restart();
}

void handleFactoryReset() {
  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println("║ 🏭 FACTORY RESET INITIATED ║");
  Serial.println("╚══════════════════════════════════════╝");
  
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
  
  systemConfig.debugMode = server.hasArg("debug");
  systemConfig.setupComplete = true;
  saveConfig();
  
  // Detect if accessed via AP or local WiFi
  IPAddress clientIP = server.client().remoteIP();
  IPAddress apIP = WiFi.softAPIP();
  bool isFromAP = (clientIP[0] == apIP[0] && clientIP[1] == apIP[1] && clientIP[2] == apIP[2]);
  
  if (isFromAP) {
    // From AP - show simple success message that auto-closes
    String confirmHtml = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width'>";
    confirmHtml += "<style>body{font-family:Arial;max-width:600px;margin:50px auto;padding:20px;background:linear-gradient(135deg,#667eea,#764ba2);min-height:100vh;text-align:center;}";
    confirmHtml += ".container{background:white;padding:40px;border-radius:10px;box-shadow:0 10px 40px rgba(0,0,0,0.2);}";
    confirmHtml += "h1{color:#4CAF50;font-size:48px;margin:0;}p{font-size:18px;color:#666;}</style></head><body><div class='container'>";
    confirmHtml += "<h1>✓</h1><h2>Settings Saved!</h2><p>Device is rebooting...<br>You can close this window.</p>";
    confirmHtml += "<p>Access settings at:<br><b>http://" + WiFi.localIP().toString() + "</b></p>";
    confirmHtml += "</div><script>setTimeout(function(){window.close();},3000);</script></body></html>";
    
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", confirmHtml);
    delay(2000);
    ESP.restart();
  } else {
    // From local WiFi - show confirmation page with reboot button
    String confirmHtml = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width'>";
    confirmHtml += "<style>body{font-family:Arial;max-width:600px;margin:20px auto;padding:20px;background:linear-gradient(135deg,#667eea,#764ba2);min-height:100vh;}";
    confirmHtml += ".container{background:white;padding:30px;border-radius:10px;box-shadow:0 10px 40px rgba(0,0,0,0.2);text-align:center;}";
    confirmHtml += "h1{color:#4CAF50;}input{width:100%;padding:10px;margin:10px 0;font-size:16px;border:2px solid #667eea;border-radius:5px;text-align:center;}";
    confirmHtml += "button{background:linear-gradient(135deg,#667eea,#764ba2);color:white;padding:12px 30px;border:none;cursor:pointer;border-radius:5px;font-size:16px;margin:10px;}";
    confirmHtml += ".note{background:#d4edda;padding:15px;border-radius:5px;margin:20px 0;border:2px solid #4CAF50;color:#155724;}</style></head><body><div class='container'>";
    confirmHtml += "<h1>Settings Saved Successfully!</h1>";
    confirmHtml += "<div class='note'>Your changes have been saved.<br>Reboot the device to apply all changes.</div>";
    
    String localUrl = "http://" + WiFi.localIP().toString();
    confirmHtml += "<p>Bookmark this URL:<br><input type='text' id='url' value='" + localUrl + "' readonly onclick='this.select();document.execCommand(\"copy\");alert(\"Copied!\");'></p>";
    confirmHtml += "<button onclick='doReboot()'>Reboot Now</button>";
    confirmHtml += "<button onclick='location.href=\"/settings\"'>Back to Settings</button>";
    confirmHtml += "<script>function doReboot(){if(confirm('Reboot device now?')){fetch('/reboot',{method:'POST'}).then(()=>{document.body.innerHTML='<div class=\"container\"><h2>Rebooting...</h2><p>Please wait 10 seconds then refresh the page.</p></div>';});}}</script>";
    confirmHtml += "</div></body></html>";
    
    server.send(200, "text/html", confirmHtml);
  }
}

void startWebServer() {
  server.on("/", handleRoot);
  server.on("/settings", handleSettings);
  server.on("/logs", handleLogs);
  server.on("/health", handleHealth);
  server.on("/reboot", HTTP_POST, handleReboot);
  server.on("/factory-reset", HTTP_POST, handleFactoryReset);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleRoot);
  
  server.begin();
  webServerRunning = true;
  Serial.println("✅ Web server started");
}

void startSettingsAP() {
  Serial.println("🔧 Starting Settings AP");
  WiFi.mode(WIFI_AP_STA);
  
  bool apStarted = systemConfig.setupComplete 
    ? WiFi.softAP(systemConfig.deviceName, systemConfig.apPassword)
    : WiFi.softAP(systemConfig.deviceName);
  
  if (!apStarted) {
    Serial.println("❌ Failed to start AP!");
    return;
  }
  
  Serial.println("📡 AP: " + String(systemConfig.deviceName));
  Serial.println("🌐 AP URL: http://" + WiFi.softAPIP().toString());
  
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  if (!webServerRunning) startWebServer();
  Serial.println("✓ Settings AP ready");
}

void monitorSystem() {
  static unsigned long lastTempCheck = 0, lastPowerCheck = 0;
  unsigned long now = millis();
  
  // Temperature check every 5 minutes
  if (now - lastTempCheck >= 300000) {
    float temp = temperatureRead();
    Serial.printf("🌡️ Temp: %.1f°C%s\n", temp, temp > 80 ? " ⚠️ HIGH" : (temp > 70 ? " (warm)" : ""));
    lastTempCheck = now;
  }
  
  // Power estimate every 5 minutes
  if (now - lastPowerCheck >= 300000) {
    float currentMA = 80 + (WiFi.getMode() & WIFI_AP ? 100 : 0);
    int pins[] = LED_PINS;
    for (int p : pins) if (analogRead(p) > 0) currentMA += 20;
    
    float watts = (3.3 * currentMA) / 1000.0;
    float wattHoursPerDay = watts * 24;
    Serial.printf("⚡ Power: %.2fW | %.2f Wh/day | %.3f kWh/month\n", 
                  watts, wattHoursPerDay, (wattHoursPerDay * 30) / 1000.0);
    lastPowerCheck = now;
  }
}

// ============================================
// SETUP & LOOP
// ============================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║ Canvas LED Tracker - Optimized ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  // Initialize watchdog timer (30 second timeout)
  // Watchdog configuration for ESP-IDF 5.x
esp_task_wdt_config_t wdt_config = {
  .timeout_ms = 30000,
  .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
  .trigger_panic = true
};
esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);
  
  int pins[] = LED_PINS;
  for (int p : pins) pinMode(p, OUTPUT);
  setAllLEDsOff();
  
  Serial.printf("🌡️ CPU Temp: %.1f°C\n\n", temperatureRead());
  
  loadConfig();
  startSettingsAP();
  
  if (!systemConfig.setupComplete) {
    Serial.println("⚠️ FIRST TIME SETUP REQUIRED\n");
    while (!systemConfig.setupComplete) {
      static unsigned long lastBlink = 0;
      static int brightness = 0, direction = 5;
      
      if (millis() - lastBlink > 10) {
        brightness += direction;
        int maxBright = min(ledConfig.maxBrightness, 100);
        if (brightness >= maxBright || brightness <= 0) {
          brightness = constrain(brightness, 0, maxBright);
          direction = -direction;
        }
        int pins[] = LED_PINS;
        for (int p : pins) analogWrite(p, brightness);
        lastBlink = millis();
      }
      server.handleClient();
      dnsServer.processNextRequest();
      esp_task_wdt_reset();
      delay(10);
    }
  }
  
  connectWiFi();
  initTime();
  
  Serial.println("📋 Configuration:");
  Serial.printf(" • LED Mode: %s\n", ledConfig.useFlashing ? "Flashing" : "Solid");
  Serial.printf(" • Red LED: %d days, Yellow LED: %d days\n", ledConfig.redLEDDaysAhead, ledConfig.yellowLEDDaysAhead);
  Serial.printf(" • Check Interval: %lu min\n", canvasConfig.fetchInterval / 60000);
  Serial.printf(" • Timezone: %s\n\n", timezoneConfig.displayName);
  
  if (timeSyncComplete) {
    Serial.println("📡 Fetching initial status...");
    assignmentStatus = fetchCanvasAssignments();
    lastFetch = millis();
    if (consecutiveErrors == 0) {
      lastSuccessfulFetch = lastFetch;
    }
  } else {
    Serial.println("⚠️ Skipping initial fetch until time is synced\n");
  }

  initOTA();
  
  Serial.println("\n✅ Running!\n");
}

void loop() {
  unsigned long now = millis();
  
  // Reset watchdog
  esp_task_wdt_reset();

  checkForOTAUpdate();
  
  monitorSystem();
  dnsServer.processNextRequest();
  server.handleClient();
  
  if (systemConfig.setupComplete) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("⚠️ WiFi disconnected, reconnecting...");
      connectWiFi();
    }
    
    if (now - lastFetch >= canvasConfig.fetchInterval) {
      Serial.println("\n--- Canvas Check Cycle ---");
      assignmentStatus = fetchCanvasAssignments();
      lastFetch = now;
      pulseBrightness = 0;
      fadeDirection = 1;
      
      // Show status summary
      if (consecutiveErrors == 0) {
        Serial.println("✅ Status updated successfully");
      } else {
        Serial.printf("⚠️ Using cached status (last success: %lu min ago)\n", 
                     (now - lastSuccessfulFetch) / 60000);
      }
      Serial.println("--- End Check ---\n");
    }
  }
  
  updateLEDs();
  delay(10);
}
