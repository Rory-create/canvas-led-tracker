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

// Forward declarations
void createGitHubIssue();

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
  int itemsPerPage = 5;  // Balanced coverage with 80KB buffer
  unsigned long fetchInterval = 10UL * 60UL * 1000UL;
  size_t jsonBufferSize = 81920;  // 80KB starting buffer
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
  bool bugReportEnabled = true;
  unsigned long lastBugReport = 0;
} systemConfig;

const int greenLED = 32, yellowLED = 25, redLED = 27;
const int MAX_RETRIES = 3, HTTPS_TIMEOUT = 60000, DNS_PORT = 53;
const byte LOG_BUFFER_SIZE = 50;

// Error codes for diagnostic LED patterns
enum ErrorCode {
  ERR_NONE = 0,
  ERR_WIFI_DISCONNECT = 1,
  ERR_CANVAS_AUTH = 2,
  ERR_CANVAS_SERVER = 3,
  ERR_TIME_SYNC = 4,
  ERR_MEMORY_LOW = 5,
  ERR_JSON_PARSE = 6,
  ERR_BUFFER_EXHAUSTED = 7
};

int currentErrorCode = ERR_NONE;

// Assignment storage for web UI display
struct Assignment {
  String name;
  String dueAt;
  String description;
  String htmlUrl;
  time_t dueTimestamp;
  int urgency; // 0=none, 1=coming up (yellow), 2=urgent (red)
};

const int MAX_DISPLAYED_ASSIGNMENTS = 5;
Assignment displayedAssignments[MAX_DISPLAYED_ASSIGNMENTS];
int assignmentCount = 0;

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

  preferences.putString("ssid", wifiConfig.ssid);
  preferences.putString("pass", wifiConfig.password);
  preferences.putString("ssid2", wifiConfig.ssid2);
  preferences.putString("pass2", wifiConfig.password2);
  preferences.putBool("use2nd", wifiConfig.useSecondaryNetwork);

  preferences.putString("apiUrl", canvasConfig.apiUrl);
  preferences.putString("apiToken", canvasConfig.apiToken);
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

  preferences.putString("tzString", timezoneConfig.tzString);
  preferences.putString("tzName", timezoneConfig.displayName);

  preferences.putBool("setupDone", systemConfig.setupComplete);
  preferences.putString("devName", systemConfig.deviceName);
  preferences.putBool("debug", systemConfig.debugMode);
  preferences.putString("apPass", systemConfig.apPassword);
  preferences.putBool("bugReport", systemConfig.bugReportEnabled);
  preferences.putULong("lastReport", systemConfig.lastBugReport);

  preferences.end();
  Serial.println("Configuration saved!");
}

void loadConfig() {
  preferences.begin("config", true);
  systemConfig.setupComplete = preferences.getBool("setupDone", false);

  if (systemConfig.setupComplete) {
    preferences.getString("ssid", wifiConfig.ssid, sizeof(wifiConfig.ssid));
    if (strlen(wifiConfig.ssid) == 0) strcpy(wifiConfig.ssid, "");

    preferences.getString("pass", wifiConfig.password, sizeof(wifiConfig.password));
    if (strlen(wifiConfig.password) == 0) strcpy(wifiConfig.password, "");

    preferences.getString("ssid2", wifiConfig.ssid2, sizeof(wifiConfig.ssid2));
    if (strlen(wifiConfig.ssid2) == 0) strcpy(wifiConfig.ssid2, "");

    preferences.getString("pass2", wifiConfig.password2, sizeof(wifiConfig.password2));
    if (strlen(wifiConfig.password2) == 0) strcpy(wifiConfig.password2, "");

    wifiConfig.useSecondaryNetwork = preferences.getBool("use2nd", false);

    preferences.getString("apiUrl", canvasConfig.apiUrl, sizeof(canvasConfig.apiUrl));
    if (strlen(canvasConfig.apiUrl) == 0) strcpy(canvasConfig.apiUrl, "https://ojrsd.instructure.com/api/v1/users/self/todo");

    preferences.getString("apiToken", canvasConfig.apiToken, sizeof(canvasConfig.apiToken));
    if (strlen(canvasConfig.apiToken) == 0) strcpy(canvasConfig.apiToken, "");

    canvasConfig.itemsPerPage = preferences.getInt("itemsPer", 2);  // Default 2 - Canvas responses are huge
    canvasConfig.fetchInterval = preferences.getULong("fetchInt", 10UL * 60UL * 1000UL);

    ledConfig.useFlashing = preferences.getBool("useFlash", true);
    ledConfig.flashInterval = preferences.getInt("flashInt", 1);
    ledConfig.flashStep = preferences.getInt("flashStep", 1);
    ledConfig.solidBrightness = preferences.getInt("brightness", 100);
    ledConfig.maxBrightness = preferences.getInt("maxBright", 100);
    ledConfig.quietHoursEnabled = preferences.getBool("quietEn", false);
    ledConfig.quietHourStart = preferences.getInt("quietStart", 22);
    ledConfig.quietHourEnd = preferences.getInt("quietEnd", 7);
    ledConfig.redLEDDaysAhead = preferences.getInt("redDays", 0);
    ledConfig.yellowLEDDaysAhead = preferences.getInt("yellowDays", 1);

    preferences.getString("tzString", timezoneConfig.tzString, sizeof(timezoneConfig.tzString));
    if (strlen(timezoneConfig.tzString) == 0) strcpy(timezoneConfig.tzString, "EST5EDT,M3.2.0/2,M11.1.0/2");

    preferences.getString("tzName", timezoneConfig.displayName, sizeof(timezoneConfig.displayName));
    if (strlen(timezoneConfig.displayName) == 0) strcpy(timezoneConfig.displayName, "US Eastern");

    preferences.getString("devName", systemConfig.deviceName, sizeof(systemConfig.deviceName));
    if (strlen(systemConfig.deviceName) == 0) strcpy(systemConfig.deviceName, "Canvas_LED_Tracker");

    systemConfig.debugMode = preferences.getBool("debug", true);

    preferences.getString("apPass", systemConfig.apPassword, sizeof(systemConfig.apPassword));
    if (strlen(systemConfig.apPassword) == 0) strcpy(systemConfig.apPassword, "canvas123");
    
    systemConfig.bugReportEnabled = preferences.getBool("bugReport", true);
    systemConfig.lastBugReport = preferences.getULong("lastReport", 0);

    Serial.println("Configuration loaded");
  } else {
    Serial.println("âš ï¸ First time setup required");
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
  Serial.println("â³ Waiting for NTP time sync...");
  struct tm timeinfo;
  int attempts = 0;

  while (!getLocalTime(&timeinfo) && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (attempts >= 20) {
    Serial.println("\nâš ï¸ Time sync timeout - will retry later");
    timeSyncComplete = false;
    return;
  }

  timeSyncComplete = true;
  Serial.println("\nâœ… Time synced!");
  char buffer[64];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", &timeinfo);
  Serial.printf("ðŸ“… %s: %s\n", timezoneConfig.displayName, buffer);
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

  Serial.println("ðŸ“¡ Connecting to WiFi...");

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
    Serial.printf("\nâœ… Connected: %s\n", WiFi.SSID().c_str());

    // Wait a moment for IP to stabilize
    delay(500);
    Serial.printf("ðŸ“ IP: %s\n", WiFi.localIP().toString().c_str());

    String hostname = String(systemConfig.deviceName);
    hostname.replace(" ", "");
    hostname.replace("_", "");
    hostname.toLowerCase();

    for (int i = 0; i < 3; i++) {
      if (MDNS.begin(hostname.c_str())) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("ðŸŒ mDNS: http://%s.local\n", hostname.c_str());

        // Try time sync if not already complete
        if (!timeSyncComplete) {
          initTime();
        }
        return;
      }
      delay(500);
    }
    Serial.println("âš ï¸ mDNS failed");
  } else {
    Serial.println("\nâŒ WiFi connection failed");
  }
}

int fetchCanvasAssignments() {
  connectWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected - returning error state");
    consecutiveErrors++;
    
    // Report critical errors to GitHub
    if (consecutiveErrors >= 10) {
      createGitHubIssue();
    }
    
    return 0;  // Return 0 = ERROR state (all LEDs flash)
  }

  // Don't fetch if time isn't synced
  if (!timeSyncComplete) {
    Serial.println("Time not synced - retrying sync...");
    initTime();
    if (!timeSyncComplete) {
      Serial.println("Cannot fetch without valid time - returning error state");
      return 0;  // Return 0 = ERROR state
    }
  }

  client.setInsecure();
  http.setTimeout(HTTPS_TIMEOUT);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  int newStatus = 1;  // Default: GREEN (1) = all clear
  char fullUrl[512];
  snprintf(fullUrl, sizeof(fullUrl), "%s?per_page=%d", canvasConfig.apiUrl, canvasConfig.itemsPerPage);
  
  for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
    if (systemConfig.debugMode) Serial.printf("Attempt %d/%d\n", attempt + 1, MAX_RETRIES);

    if (http.begin(client, fullUrl)) {
      http.addHeader("Authorization", String("Bearer ") + canvasConfig.apiToken);
      http.addHeader("Accept", "application/json");

      int httpCode = http.GET();
      if (systemConfig.debugMode) Serial.printf("HTTP: %d\n", httpCode);

      if (httpCode == 200) {
        String response = http.getString();
        size_t bufferSize = canvasConfig.jsonBufferSize;
        size_t freeHeap = ESP.getFreeHeap();
        size_t maxBuffer = (freeHeap * 90) / 100;  // 90% of free heap
        
        // Check for critical memory condition
        if (freeHeap < 15000) {
          currentErrorCode = ERR_MEMORY_LOW;
          Serial.println("[CRITICAL] Memory below 15KB threshold!");
          createGitHubIssue();
        }

        // Memory analysis
        Serial.printf("\nCanvas Response Analysis:\n");
        Serial.printf("   Response size: %d bytes\n", response.length());
        Serial.printf("   JSON buffer:   %d bytes\n", bufferSize);
        Serial.printf("   Buffer usage:  %.1f%%\n", (response.length() / (float)bufferSize) * 100);
        Serial.printf("   Free heap:     %d bytes\n", freeHeap);
        Serial.printf("   Max buffer:    %d bytes (90%% of heap)\n", maxBuffer);

        DynamicJsonDocument doc(bufferSize);
        
        // Temporarily remove loop task from watchdog during slow JSON parsing
        // (HTML-heavy Canvas responses can take 10+ seconds to parse)
        esp_task_wdt_delete(NULL);  // Remove current task
        Serial.println("   Parsing JSON (watchdog disabled for this task)...");
        
        DeserializationError error = deserializeJson(doc, response);
        
        // Re-add loop task to watchdog
        esp_task_wdt_add(NULL);  // Add current task back
        esp_task_wdt_reset();    // Reset timer
        Serial.println("   Parse complete (watchdog re-enabled)");

        if (!error) {
          time_t now, redDeadline = 0, yellowDeadline = 0;
          time(&now);
          
          // Debug: Check if time(&now) is UTC or local
          struct tm now_tm_local, now_tm_utc;
          localtime_r(&now, &now_tm_local);  // Convert to local
          gmtime_r(&now, &now_tm_utc);       // Convert to UTC
          
          if (systemConfig.debugMode) {
            char buf_local[32], buf_utc[32];
            strftime(buf_local, sizeof(buf_local), "%H:%M:%S", &now_tm_local);
            strftime(buf_utc, sizeof(buf_utc), "%H:%M:%S", &now_tm_utc);
            Serial.printf("time(&now) = %ld\n", now);
            Serial.printf("  As local: %s\n", buf_local);
            Serial.printf("  As UTC:   %s\n", buf_utc);
          }
          
          redDeadline = now + (ledConfig.redLEDDaysAhead * 86400);
          yellowDeadline = now + (ledConfig.yellowLEDDaysAhead * 86400);

          if (systemConfig.debugMode) {
            Serial.printf("Checking: Red=%d days, Yellow=%d days\n",
                         ledConfig.redLEDDaysAhead, ledConfig.yellowLEDDaysAhead);
            Serial.printf("Current time (now): %ld\n", now);
            Serial.printf("Red deadline: %ld\n", redDeadline);
            Serial.printf("Yellow deadline: %ld\n", yellowDeadline);
          }

          assignmentCount = 0; // Reset assignment list
          
          for (JsonObject item : doc.as<JsonArray>()) {
            const char* dueDate = item["assignment"]["due_at"];
            if (dueDate && !item["completed"].as<bool>()) {
              struct tm due_tm = {0};
              if (strptime(dueDate, "%Y-%m-%dT%H:%M:%SZ", &due_tm)) {
                // Parse Canvas UTC timestamp
                time_t due_time_utc = esp_timegm(&due_tm);
                
                // Calculate timezone offset from current time
                struct tm now_tm_local, now_tm_utc;
                localtime_r(&now, &now_tm_local);
                gmtime_r(&now, &now_tm_utc);
                
                // Calculate offset in seconds
                time_t now_as_local = mktime(&now_tm_local);
                time_t now_as_utc = mktime(&now_tm_utc);
                int offset_seconds = now_as_local - now_as_utc;
                
                // Apply offset to due time
                time_t due_time_local = due_time_utc + offset_seconds;
                
                if (systemConfig.debugMode) {
                  Serial.printf("Assignment due_at: %s\n", dueDate);
                  Serial.printf("  UTC: %ld, Offset: %d sec, Local: %ld\n", 
                               due_time_utc, offset_seconds, due_time_local);
                }

                // Validate date is reasonable (between 2020 and 2050)
                if (due_time_local < 1577836800 || due_time_local > 2524608000) {
                  Serial.printf("Invalid date detected: %s\n", dueDate);
                  continue;
                }

                // Store assignment for web UI display (up to 5)
                if (assignmentCount < MAX_DISPLAYED_ASSIGNMENTS && due_time_local <= yellowDeadline) {
                  Assignment &a = displayedAssignments[assignmentCount++];
                  a.name = item["assignment"]["name"].as<String>();
                  a.dueAt = String(dueDate);
                  a.dueTimestamp = due_time_local;
                  a.htmlUrl = item["assignment"]["html_url"].as<String>();
                  
                  // Get description (limit to 200 chars)
                  String desc = item["assignment"]["description"].as<String>();
                  if (desc.length() > 200) {
                    desc = desc.substring(0, 197) + "...";
                  }
                  a.description = desc;
                  
                  // Determine urgency based on user thresholds
                  if (due_time_local <= redDeadline) {
                    a.urgency = 2; // Urgent (red threshold)
                  } else {
                    a.urgency = 1; // Coming up (yellow threshold)
                  }
                }

                if (due_time_local <= redDeadline) {
                  newStatus = 3;  // RED (3) = due today
                  if (systemConfig.debugMode) Serial.println("RED");
                  break;
                } else if (due_time_local <= yellowDeadline) {
                  newStatus = 2;  // YELLOW (2) = due soon
                  if (systemConfig.debugMode) Serial.println("YELLOW");
                }
              }
            }
          }
          http.end();

          // Success!
          consecutiveErrors = 0;
          lastSuccessfulFetch = millis();
          Serial.println("Canvas fetch successful");
          return newStatus;
        } else {
          // ============ DYNAMIC BUFFER INCREASE ============
          Serial.printf("\nJSON Parse Error: %s\n", error.c_str());
          Serial.printf("   Error code: %d\n", error.code());

          if (error.code() == DeserializationError::NoMemory) {
            Serial.println("\nNoMemory Error - Attempting Dynamic Buffer Increase");
            Serial.printf("   Current buffer: %d bytes\n", bufferSize);
            Serial.printf("   Response size:  %d bytes\n", response.length());
            Serial.printf("   Free heap:      %d bytes\n", freeHeap);
            
            // Smart buffer increase: scale up intelligently or go to max
            size_t newBufferSize;
            if (bufferSize < maxBuffer * 0.7) {
              // We have room to grow - try response size + 40KB headroom for JSON overhead
              size_t targetSize = response.length() + 40960;
              size_t scaledSize = bufferSize + (bufferSize / 2); // 1.5x current
              newBufferSize = max(targetSize, scaledSize);
            } else {
              // Near max already - try going to absolute maximum
              newBufferSize = maxBuffer;
            }
            
            // Cap at maxBuffer
            if (newBufferSize > maxBuffer) {
              newBufferSize = maxBuffer;
            }
            
            // Only increase if we can actually grow
            if (newBufferSize > bufferSize) {
              Serial.printf("   Increasing buffer: %d -> %d bytes\n", bufferSize, newBufferSize);
              canvasConfig.jsonBufferSize = newBufferSize;
              
              // Retry with new buffer size
              Serial.println("   Retrying with increased buffer...\n");
              continue;  // Retry this attempt with new buffer
            } else {
              Serial.println("\nCRITICAL: Cannot increase buffer further!");
              Serial.printf("   Already at maximum: %d bytes\n", bufferSize);
              Serial.printf("   Max allowed (90%% heap): %d bytes\n", maxBuffer);
              Serial.println("   Returning ERROR state - all LEDs will flash");
              currentErrorCode = ERR_BUFFER_EXHAUSTED;
              createGitHubIssue();
              http.end();
              consecutiveErrors++;
              return 0;  // Return ERROR state
            }
          } else {
            Serial.println("   (Non-memory error)\n");
          }
        }
      } else if (httpCode == 401) {
        Serial.println("Canvas API: Invalid token (401)");
        http.end();
        consecutiveErrors++;
        return 0;  // Return ERROR state
      } else if (httpCode == 500 || httpCode == 502 || httpCode == 503 || httpCode == 504) {
        Serial.printf("Canvas server error (%d) - likely outage\n", httpCode);
        http.end();
        consecutiveErrors++;
        break;  // Don't retry on server errors
      } else if (httpCode == 429) {
        Serial.println("Canvas API: Rate limited (429)");
        http.end();
        consecutiveErrors++;
        return assignmentStatus;  // Keep last status on rate limit
      } else if (httpCode < 0) {
        Serial.printf("HTTP request failed: %s\n", http.errorToString(httpCode).c_str());
      } else {
        Serial.printf("Unexpected HTTP code: %d\n", httpCode);
      }
      http.end();
    } else {
      Serial.println("Could not connect to Canvas");
    }

    if (attempt < MAX_RETRIES - 1) {
      Serial.println("Retrying in 2 seconds...");
      delay(2000);
    }
  }

  consecutiveErrors++;

  // If we've had many consecutive errors, warn the user
  if (consecutiveErrors >= 3) {
    Serial.printf("%d consecutive Canvas errors - returning ERROR state\n", consecutiveErrors);
    if (consecutiveErrors == 5) {
      Serial.println("TIP: Canvas may be experiencing an outage. All LEDs will flash.");
    }
  }

  return 0;  // Return ERROR state after all retries failed
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

void displayErrorPattern(int errorCode) {
  unsigned long now = millis();
  static unsigned long lastFlash = 0;
  static int stepIndex = 0;
  static bool isInPause = false;
  static unsigned long pauseStart = 0;
  
  // Define patterns: array of LED pins to light in sequence
  // 0 = all off, greenLED/yellowLED/redLED = specific LED
  int pattern[7][6] = {
    {redLED, 0, redLED, 0, 0, 0},           // WiFi: Red blink twice
    {yellowLED, redLED, 0, 0, 0, 0},        // Auth: Yellow-Red
    {redLED, yellowLED, redLED, 0, 0, 0},   // Server: Red-Yellow-Red
    {greenLED, yellowLED, 0, 0, 0, 0},      // Time: Green-Yellow
    {redLED, redLED, redLED, 0, 0, 0},      // Memory: Red-Red-Red
    {yellowLED, greenLED, yellowLED, 0, 0, 0}, // JSON: Yellow-Green-Yellow
    {-1, -1, -1, -1, -1, -1}                // Buffer: all flash together (handled separately)
  };
  
  // Buffer exhausted: all three LEDs flash together rapidly
  if (errorCode == ERR_BUFFER_EXHAUSTED) {
    static bool allLEDState = false;
    if (now - lastFlash >= 300) { // Fast flash: 300ms on/off
      lastFlash = now;
      allLEDState = !allLEDState;
      int brightness = allLEDState ? ledConfig.maxBrightness : 0;
      analogWrite(greenLED, brightness);
      analogWrite(yellowLED, brightness);
      analogWrite(redLED, brightness);
    }
    return;
  }
  
  int patternIndex = errorCode - 1; // ERR_WIFI_DISCONNECT=1 → pattern[0]
  if (patternIndex < 0 || patternIndex >= 6) patternIndex = 0;
  
  // Handle 3-second pause between pattern loops
  if (isInPause) {
    if (now - pauseStart >= 3000) {
      isInPause = false;
      stepIndex = 0;
    }
    setAllLEDsOff();
    return;
  }
  
  // Flash each LED in pattern for 500ms
  if (now - lastFlash >= 500) {
    lastFlash = now;
    
    int ledPin = pattern[patternIndex][stepIndex];
    
    if (ledPin == 0 || ledPin == -1) {
      // End of pattern, start pause
      setAllLEDsOff();
      isInPause = true;
      pauseStart = now;
      stepIndex = 0;
    } else {
      // Light up the LED
      setAllLEDsOff();
      analogWrite(ledPin, ledConfig.maxBrightness);
      stepIndex++;
    }
  }
}

void updateLEDs() {
  if (isQuietHours()) {
    setAllLEDsOff();
    return;
  }

  // ERROR state (0) = all LEDs flash rapidly
  // ERROR state (0) = display diagnostic pattern based on error code
  if (assignmentStatus == 0) {
    displayErrorPattern(currentErrorCode);
    return;
  }

  int targetLED = greenLED;  // Default: GREEN (1)
  if (assignmentStatus == 3) targetLED = redLED;    // RED (3)
  else if (assignmentStatus == 2) targetLED = yellowLED;  // YELLOW (2)

  if (ledConfig.useFlashing && assignmentStatus > 1) {  // Flash for YELLOW/RED only
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
<p><b>Get Canvas Token:</b> Open Canvas in browser â†’ Account â†’ Settings â†’ Approved Integrations â†’ + New Access Token</p>
<form method="POST" action="/save" onsubmit="return validateSetup()">
<div class="section">
<h3>WiFi Settings</h3>
<label>Network Name<select name="ssid" id="ssidSelect" style="width:100%;padding:10px;"><option value="">-- Loading... --</option></select></label>
<label>WiFi Password<div class="pass-wrap"><input type="password" name="password" id="wifiPass" class="pwd"><span class="show-pass" onclick="togglePass(this)">SHOW</span></div></label>
<button type="button" onclick="testWifi()" style="background:#28a745;margin:5px 0;">Test WiFi</button>
<div id="wifiResult" style="padding:8px;margin:5px 0;border-radius:5px;display:none;"></div>
<label>Backup SSID<select name="ssid2" id="ssid2Select" style="width:100%;padding:10px;"><option value="">-- None --</option></select></label>
<label>Backup Password<div class="pass-wrap"><input type="password" name="password2" class="pwd"><span class="show-pass" onclick="togglePass(this)">SHOW</span></div></label>
</div>
<div class="section">
<h3>Canvas API Token</h3>
<textarea name="apiToken" id="apiToken" rows="3" required placeholder="Paste your Canvas API token here"></textarea>
<button type="button" onclick="testCanvas()" style="background:#28a745;margin:5px 0;">Test Canvas Token</button>
<div id="canvasResult" style="padding:8px;margin:5px 0;border-radius:5px;display:none;"></div>
</div>
<div class="section">
<h3>Timezone</h3>
<label>Your Timezone
<select name="timezone" style="width:100%;padding:10px;">
<option value="EST5EDT,M3.2.0/2,M11.1.0/2|US Eastern">US Eastern (EST/EDT)</option>
<option value="CST6CDT,M3.2.0/2,M11.1.0/2|US Central">US Central (CST/CDT)</option>
<option value="MST7MDT,M3.2.0/2,M11.1.0/2|US Mountain">US Mountain (MST/MDT)</option>
<option value="PST8PDT,M3.2.0/2,M11.1.0/2|US Pacific">US Pacific (PST/PDT)</option>
<option value="AKST9AKDT,M3.2.0/2,M11.1.0/2|Alaska">Alaska (AKST/AKDT)</option>
<option value="HST10|Hawaii">Hawaii (HST)</option>
<option value="AST4ADT,M3.2.0/2,M11.1.0/2|Atlantic">Atlantic (AST/ADT)</option>
<option value="GMT0BST,M3.5.0/1,M10.5.0/2|UK">UK (GMT/BST)</option>
<option value="CET-1CEST,M3.5.0/2,M10.5.0/3|Central Europe">Central Europe (CET/CEST)</option>
<option value="EET-2EEST,M3.5.0/3,M10.5.0/4|Eastern Europe">Eastern Europe (EET/EEST)</option>
<option value="MSK-3|Moscow">Moscow (MSK)</option>
<option value="GST-4|Gulf">Gulf (GST)</option>
<option value="PKT-5|Pakistan">Pakistan (PKT)</option>
<option value="IST-5:30|India">India (IST)</option>
<option value="BST-6|Bangladesh">Bangladesh (BST)</option>
<option value="ICT-7|Indochina">Indochina (ICT)</option>
<option value="CST-8|China">China (CST)</option>
<option value="JST-9|Japan">Japan (JST)</option>
<option value="KST-9|Korea">Korea (KST)</option>
<option value="AEST-10AEDT,M10.1.0/2,M4.1.0/3|Australia East">Australia East (AEST/AEDT)</option>
<option value="ACST-9:30ACDT,M10.1.0/2,M4.1.0/3|Australia Central">Australia Central (ACST/ACDT)</option>
<option value="AWST-8|Australia West">Australia West (AWST)</option>
<option value="NZST-12NZDT,M9.5.0/2,M4.1.0/3|New Zealand">New Zealand (NZST/NZDT)</option>
<option value="UTC0|UTC">UTC (Universal)</option>
</select>
</label>
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
function scanWifi(){
  let s=document.getElementById('ssidSelect'),s2=document.getElementById('ssid2Select');
  s.innerHTML='<option>-- Scanning... --</option>';
  fetch('/scan').then(r=>r.json()).then(nets=>{
    s.innerHTML='<option value="">-- Select Network --</option>';
    s2.innerHTML='<option value="">-- None --</option>';
    nets.forEach(n=>{
      let sig=n.rssi>-50?'▓▓▓▓':n.rssi>-60?'▓▓▓░':n.rssi>-70?'▓▓░░':'▓░░░';
      let o='<option value="'+n.ssid+'">'+n.ssid+' '+sig+(n.secure?' 🔒':'')+'</option>';
      s.innerHTML+=o;s2.innerHTML+=o;
    });
  }).catch(()=>{s.innerHTML='<option value="">Scan failed</option>';});
}
function testWifi(){
  let ssid=document.getElementById('ssidSelect').value;
  let pass=document.getElementById('wifiPass').value;
  let r=document.getElementById('wifiResult');
  r.style.display='block';r.style.background='#f0f0f0';r.textContent='Testing...';
  fetch('/test-wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'ssid='+encodeURIComponent(ssid)+'&password='+encodeURIComponent(pass)})
  .then(x=>x.json()).then(d=>{r.style.background=d.success?'#d4edda':'#f8d7da';r.textContent=d.message;})
  .catch(()=>{r.style.background='#f8d7da';r.textContent='Test failed';});
}
function testCanvas(){
  let token=document.getElementById('apiToken').value;
  let r=document.getElementById('canvasResult');
  r.style.display='block';r.style.background='#f0f0f0';r.textContent='Testing...';
  fetch('/test-canvas',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'token='+encodeURIComponent(token)})
  .then(x=>x.json()).then(d=>{r.style.background=d.success?'#d4edda':'#f8d7da';r.textContent=d.message;})
  .catch(()=>{r.style.background='#f8d7da';r.textContent='Test failed';});
}
document.addEventListener('DOMContentLoaded',scanWifi);
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
%ASSIGNMENTS_SECTION%
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
<div class="section"><h3>Timezone</h3>
<label>Your Timezone
<select name="timezone">
<option value="EST5EDT,M3.2.0/2,M11.1.0/2|US Eastern">US Eastern (EST/EDT)</option>
<option value="CST6CDT,M3.2.0/2,M11.1.0/2|US Central">US Central (CST/CDT)</option>
<option value="MST7MDT,M3.2.0/2,M11.1.0/2|US Mountain">US Mountain (MST/MDT)</option>
<option value="PST8PDT,M3.2.0/2,M11.1.0/2|US Pacific">US Pacific (PST/PDT)</option>
<option value="AKST9AKDT,M3.2.0/2,M11.1.0/2|Alaska">Alaska (AKST/AKDT)</option>
<option value="HST10|Hawaii">Hawaii (HST)</option>
<option value="AST4ADT,M3.2.0/2,M11.1.0/2|Atlantic">Atlantic (AST/ADT)</option>
<option value="GMT0BST,M3.5.0/1,M10.5.0/2|UK">UK (GMT/BST)</option>
<option value="CET-1CEST,M3.5.0/2,M10.5.0/3|Central Europe">Central Europe (CET/CEST)</option>
<option value="EET-2EEST,M3.5.0/3,M10.5.0/4|Eastern Europe">Eastern Europe (EET/EEST)</option>
<option value="MSK-3|Moscow">Moscow (MSK)</option>
<option value="GST-4|Gulf">Gulf (GST)</option>
<option value="PKT-5|Pakistan">Pakistan (PKT)</option>
<option value="IST-5:30|India">India (IST)</option>
<option value="BST-6|Bangladesh">Bangladesh (BST)</option>
<option value="ICT-7|Indochina">Indochina (ICT)</option>
<option value="CST-8|China">China (CST)</option>
<option value="JST-9|Japan">Japan (JST)</option>
<option value="KST-9|Korea">Korea (KST)</option>
<option value="AEST-10AEDT,M10.1.0/2,M4.1.0/3|Australia East">Australia East (AEST/AEDT)</option>
<option value="ACST-9:30ACDT,M10.1.0/2,M4.1.0/3|Australia Central">Australia Central (ACST/ACDT)</option>
<option value="AWST-8|Australia West">Australia West (AWST)</option>
<option value="NZST-12NZDT,M9.5.0/2,M4.1.0/3|New Zealand">New Zealand (NZST/NZDT)</option>
<option value="UTC0|UTC">UTC (Universal)</option>
</select>
</label>
<small style="color:#666;">Current: %TIMEZONE%</small>
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
<label><input type="checkbox" name="bugReport" %BUG_REPORT_CHECKED%> Enable Auto Bug Reports</label>
<small style="color:#666;">Automatically report critical errors to GitHub for faster support</small>
<label><input type="checkbox" name="debug" %DEBUG_CHECKED%> Debug Mode</label>
</div>
<button type="submit" class="btn-save">Save Settings</button>
<button type="button" class="btn-refresh" onclick="manualRefresh()" style="background:#17a2b8;">Check Canvas Now</button>
<div id="refreshResult" style="padding:8px;margin:5px 0;border-radius:5px;display:none;"></div>
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
function manualRefresh(){
  let r=document.getElementById('refreshResult');
  r.style.display='block';r.style.background='#f0f0f0';r.textContent='Checking Canvas...';
  fetch('/refresh',{method:'POST'}).then(x=>x.json()).then(d=>{
    r.style.background=d.success?'#d4edda':'#f8d7da';
    r.textContent=d.statusName+(d.changed?' (changed!)':'');
    setTimeout(()=>location.reload(),2000);
  }).catch(()=>{r.style.background='#f8d7da';r.textContent='Refresh failed';});
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
  html.replace("%WIFI_STATUS%", WiFi.status() == WL_CONNECTED ? "â—" : "â—‹");

  String statusText = "None";
  if (assignmentStatus == 0) {
    // Provide specific error message based on error code
    switch(currentErrorCode) {
      case ERR_WIFI_DISCONNECT:
        statusText = "WiFi Disconnected";
        break;
      case ERR_CANVAS_AUTH:
        statusText = "Canvas Auth Error (Check Token)";
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
  if (consecutiveErrors >= 3) {
    errorAlert = "<div class='alert alert-warning'>âš ï¸ Canvas API experiencing issues (" +
                 String(consecutiveErrors) + " errors). Using last known status.</div>";
  }
  if (!timeSyncComplete) {
    errorAlert += "<div class='alert alert-error'>âŒ Time sync failed. Assignment checks disabled until time is synced.</div>";
  }
  html.replace("%ERROR_ALERT%", errorAlert);

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

void handleReboot() {
  Serial.println("\nðŸ”„ Reboot requested");
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "Rebooting...");
  delay(500);
  ESP.restart();
}

void handleFactoryReset() {
  Serial.println("\nâ•”â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•—");
  Serial.println("â•‘ ðŸ­ FACTORY RESET INITIATED â•‘");
  Serial.println("â•šâ•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•");

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

  systemConfig.bugReportEnabled = server.hasArg("bugReport");
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
    confirmHtml += "<h1>âœ“</h1><h2>Settings Saved!</h2><p>Device is rebooting...<br>You can close this window.</p>";
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

// ============================================
// NEW API HANDLERS: WiFi Scan, Test, Refresh
// ============================================
void handleScan() {
  Serial.println("📡 Scanning WiFi networks...");
  int n = WiFi.scanNetworks();
  
  StaticJsonDocument<2048> doc;
  JsonArray networks = doc.to<JsonArray>();
  
  for (int i = 0; i < n && i < 15; i++) {
    JsonObject net = networks.createNestedObject();
    net["ssid"] = WiFi.SSID(i);
    net["rssi"] = WiFi.RSSI(i);
    net["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  
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
  
  Serial.printf("🔌 Testing WiFi: %s\n", testSsid.c_str());
  
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
  
  // Disconnect test connection
  WiFi.disconnect();
  delay(100);
  
  // Restart AP mode
  WiFi.softAP(systemConfig.deviceName);
  
  String response = "{\"success\":" + String(success ? "true" : "false") + ",\"message\":\"" + msg + "\"}";
  server.send(200, "application/json", response);
}

void handleTestCanvas() {
  if (!server.hasArg("token")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing token\"}");
    return;
  }
  
  String testToken = server.arg("token");
  Serial.println("🎓 Testing Canvas token...");
  
  // Need WiFi to test Canvas - check if we have a stored connection
  if (WiFi.status() != WL_CONNECTED) {
    // Try to connect briefly
    if (strlen(wifiConfig.ssid) > 0) {
      WiFi.begin(wifiConfig.ssid, wifiConfig.password);
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 10) {
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
  
  String testUrl = String(canvasConfig.apiUrl) + "?per_page=1";
  
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
  server.on("/reboot", HTTP_POST, handleReboot);
  server.on("/factory-reset", HTTP_POST, handleFactoryReset);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleRoot);

  server.begin();
  webServerRunning = true;
  Serial.println("âœ… Web server started");
}

void startSettingsAP() {
  Serial.println("ðŸ”§ Starting Settings AP");
  WiFi.mode(WIFI_AP_STA);

  bool apStarted = systemConfig.setupComplete
    ? WiFi.softAP(systemConfig.deviceName, systemConfig.apPassword)
    : WiFi.softAP(systemConfig.deviceName);

  if (!apStarted) {
    Serial.println("âŒ Failed to start AP!");
    return;
  }

  Serial.println("ðŸ“¡ AP: " + String(systemConfig.deviceName));
  Serial.println("ðŸŒ AP URL: http://" + WiFi.softAPIP().toString());

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  if (!webServerRunning) startWebServer();
  Serial.println("âœ“ Settings AP ready");
}

void monitorSystem() {
  static unsigned long lastTempCheck = 0, lastPowerCheck = 0;
  unsigned long now = millis();

  // Temperature check every 5 minutes
  if (now - lastTempCheck >= 300000) {
    float temp = temperatureRead();
    Serial.printf("ðŸŒ¡ï¸ Temp: %.1fÂ°C%s\n", temp, temp > 80 ? " âš ï¸ HIGH" : (temp > 70 ? " (warm)" : ""));
    lastTempCheck = now;
  }

  // Power estimate every 5 minutes
  if (now - lastPowerCheck >= 300000) {
    // Power calculation removed - analogRead() conflicts with WiFi on ADC2 pins (GPIO 25/27)
    lastPowerCheck = now;
  }
}

// ============================================
// SETUP & LOOP
// ============================================
// GitHub Bug Reporting Functions

bool shouldReportBug() {
  // Check if bug reporting is enabled
  if (!systemConfig.bugReportEnabled) {
    return false;
  }
  
  // Check cooldown (1 hour = 3600000ms)
  unsigned long now = millis();
  if (now - systemConfig.lastBugReport < 3600000) {
    if (systemConfig.debugMode) {
      unsigned long remaining = (3600000 - (now - systemConfig.lastBugReport)) / 60000;
      Serial.printf("[BUG] Cooldown active: %lu minutes remaining\n", remaining);
    }
    return false;
  }
  
  return true;
}

String collectDiagnostics() {
  String diagnostics = "{";
  
  // Device info
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", 
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  
  diagnostics += "\"device_id\":\"" + String(macStr) + "\",";
  diagnostics += "\"firmware_version\":\"" + String(FIRMWARE_VERSION) + "\",";
  diagnostics += "\"error_code\":" + String(currentErrorCode) + ",";
  
  // Error name
  const char* errorNames[] = {
    "NONE", "WIFI_DISCONNECT", "CANVAS_AUTH", "CANVAS_SERVER",
    "TIME_SYNC", "MEMORY_LOW", "JSON_PARSE", "BUFFER_EXHAUSTED"
  };
  diagnostics += "\"error_name\":\"ERR_" + String(errorNames[currentErrorCode]) + "\",";
  
  // Timestamp
  time_t now;
  time(&now);
  char timestamp[32];
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  diagnostics += "\"timestamp\":\"" + String(timestamp) + "\",";
  
  // System metrics
  diagnostics += "\"uptime_seconds\":" + String(millis() / 1000) + ",";
  diagnostics += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
  diagnostics += "\"max_heap_used\":" + String(ESP.getMaxAllocHeap()) + ",";
  diagnostics += "\"consecutive_errors\":" + String(consecutiveErrors) + ",";
  diagnostics += "\"wifi_rssi\":" + String(WiFi.RSSI()) + ",";
  diagnostics += "\"cpu_temp\":" + String(temperatureRead()) + ",";
  diagnostics += "\"assignment_count\":" + String(assignmentCount) + ",";
  
  // Configuration
  diagnostics += "\"config\":{";
  diagnostics += "\"timezone\":\"" + String(timezoneConfig.displayName) + "\",";
  diagnostics += "\"fetch_interval\":" + String(canvasConfig.fetchInterval / 60000) + ",";
  diagnostics += "\"red_days\":" + String(ledConfig.redLEDDaysAhead) + ",";
  diagnostics += "\"yellow_days\":" + String(ledConfig.yellowLEDDaysAhead);
  diagnostics += "}";
  
  diagnostics += "}";
  return diagnostics;
}

void createGitHubIssue() {
  if (!shouldReportBug()) {
    return;
  }
  
  Serial.println("[BUG] Creating GitHub issue...");
  
  // Collect diagnostics
  String diagnostics = collectDiagnostics();
  
  // Get MAC for title
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char last4[5];
  sprintf(last4, "%02X%02X", mac[4], mac[5]);
  
  // Error name for title
  const char* errorNames[] = {
    "None", "WiFi Disconnect", "Canvas Auth", "Canvas Server",
    "Time Sync", "Memory Low", "JSON Parse", "Buffer Exhausted"
  };
  String errorName = errorNames[currentErrorCode];
  
  // Build issue title
  String title = "[AUTO] " + errorName + " - Device " + String(last4);
  
  // Build issue body (markdown)
  String body = "### Automatic Bug Report\\n\\n";
  body += "**Firmware:** " + String(FIRMWARE_VERSION) + "\\n";
  body += "**Error Code:** " + String(currentErrorCode) + " (" + errorName + ")\\n\\n";
  body += "---\\n\\n### Diagnostics\\n\\n```json\\n";
  body += diagnostics;
  body += "\\n```\\n\\n";
  body += "---\\n\\n*Note: Device will not report again for 1 hour.*";
  
  // Build JSON payload for GitHub API
  String payload = "{";
  payload += "\"title\":\"" + title + "\",";
  payload += "\"body\":\"" + body + "\",";
  payload += "\"labels\":[\"auto-bug-report\",\"critical\",\"firmware-" + String(FIRMWARE_VERSION) + "\"]";
  payload += "}";
  
  // Make HTTPS request to GitHub
  WiFiClientSecure client;
  client.setInsecure(); // Skip cert validation for simplicity
  
  HTTPClient https;
  String url = "https://api.github.com/repos/" + String(GITHUB_REPO) + "/issues";
  https.begin(client, url);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Accept", "application/vnd.github.v3+json");
  https.addHeader("Authorization", "Bearer " + String(GITHUB_TOKEN));
  https.setTimeout(10000);
  
  int httpCode = https.POST(payload);
  
  if (httpCode > 0) {
    if (httpCode == 201) {
      Serial.println("[BUG] GitHub issue created successfully!");
      systemConfig.lastBugReport = millis();
      preferences.begin("config", false);
      preferences.putULong("lastReport", systemConfig.lastBugReport);
      preferences.end();
    } else {
      Serial.printf("[BUG] GitHub API returned: %d\n", httpCode);
      if (systemConfig.debugMode) {
        Serial.println(https.getString());
      }
    }
  } else {
    Serial.printf("[BUG] GitHub request failed: %s\n", https.errorToString(httpCode).c_str());
  }
  
  https.end();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nâ•”â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•—");
  Serial.println("â•‘ Canvas LED Tracker - Optimized â•‘");
  Serial.println("â•šâ•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•\n");

  // Initialize watchdog timer (30 second timeout)
  Serial.printf("📦 Firmware: v%s (%s)\n\n", FIRMWARE_VERSION, BUILD_TIMESTAMP);
  // Support both old (IDF v4.x) and new (IDF v5.x) APIs
  #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    // New API for ESP-IDF v5.x and later (Arduino core 3.x)
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 30000,
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(NULL);
  #else
    // Old API for ESP-IDF v4.x and earlier (Arduino core 2.x)
    esp_task_wdt_init(30, true);
    esp_task_wdt_add(NULL);
  #endif

  int pins[] = LED_PINS;
  for (int p : pins) pinMode(p, OUTPUT);
  setAllLEDsOff();

  Serial.printf("ðŸŒ¡ï¸ CPU Temp: %.1fÂ°C\n\n", temperatureRead());

  // DEV_MODE: Reset non-credential preferences on boot for easier iteration
  #if DEV_MODE
    Serial.println("⚠️  DEV_MODE: Resetting config (keeping credentials)...");
    preferences.begin("config", false);
    
    // Save credentials before clearing
    char savedSsid[64], savedPass[64], savedSsid2[64], savedPass2[64];
    char savedToken[128], savedApPass[64];
    preferences.getString("ssid", savedSsid, sizeof(savedSsid));
    preferences.getString("pass", savedPass, sizeof(savedPass));
    preferences.getString("ssid2", savedSsid2, sizeof(savedSsid2));
    preferences.getString("pass2", savedPass2, sizeof(savedPass2));
    preferences.getString("apiToken", savedToken, sizeof(savedToken));
    preferences.getString("apPass", savedApPass, sizeof(savedApPass));
    bool wasSetupComplete = preferences.getBool("setupDone", false);
    
    // Clear all preferences
    preferences.clear();
    
    // Restore credentials
    preferences.putString("ssid", savedSsid);
    preferences.putString("pass", savedPass);
    preferences.putString("ssid2", savedSsid2);
    preferences.putString("pass2", savedPass2);
    preferences.putString("apiToken", savedToken);
    preferences.putString("apPass", savedApPass);
    preferences.putBool("setupDone", wasSetupComplete);
    
    preferences.end();
    Serial.println("✅ Config reset complete\n");
  #endif

  loadConfig();
  startSettingsAP();

  if (!systemConfig.setupComplete) {
    Serial.println("âš ï¸ FIRST TIME SETUP REQUIRED\n");
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

  Serial.println("ðŸ“‹ Configuration:");
  Serial.printf(" â€¢ LED Mode: %s\n", ledConfig.useFlashing ? "Flashing" : "Solid");
  Serial.printf(" â€¢ Red LED: %d days, Yellow LED: %d days\n", ledConfig.redLEDDaysAhead, ledConfig.yellowLEDDaysAhead);
  Serial.printf(" â€¢ Check Interval: %lu min\n", canvasConfig.fetchInterval / 60000);
  Serial.printf(" â€¢ Timezone: %s\n\n", timezoneConfig.displayName);

  if (timeSyncComplete) {
    Serial.println("ðŸ“¡ Fetching initial status...");
    assignmentStatus = fetchCanvasAssignments();
    lastFetch = millis();
    if (consecutiveErrors == 0) {
      lastSuccessfulFetch = lastFetch;
    }
  } else {
    Serial.println("âš ï¸ Skipping initial fetch until time is synced\n");
  }

  Serial.println("\nâœ… Running!\n");
}


// DEV_MODE: Serial command handler for LED testing
void handleSerialCommands() {
  if (!DEV_MODE || !Serial.available()) return;
  
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  
  if (cmd == "help") {
    Serial.println("\n=== DEV MODE COMMANDS ===");
    Serial.println("test:wifi     - WiFi error (slow 1s)");
    Serial.println("test:auth     - Auth error (double)");
    Serial.println("test:server   - Server error (triple)");
    Serial.println("test:time     - Time sync (medium 500ms)");
    Serial.println("test:memory   - Memory low (quad)");
    Serial.println("test:json     - JSON parse (fast 200ms)");
    Serial.println("test:buffer   - Buffer full (all solid)");
    Serial.println("test:green    - Normal green");
    Serial.println("test:yellow   - Normal yellow");
    Serial.println("test:red      - Normal red");
    Serial.println("test:clear    - Exit test mode");
    Serial.println("ap:start      - Start AP mode for web UI testing\n");
    return;
  }
  
  if (cmd == "ap:start") {
    Serial.println("[AP] Starting Access Point mode...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Canvas_LED_Tracker", systemConfig.apPassword);
    Serial.printf("[AP] SSID: Canvas_LED_Tracker\n");
    Serial.printf("[AP] Password: %s\n", systemConfig.apPassword);
    Serial.printf("[AP] IP: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.println("[AP] Navigate to: http://192.168.4.1/test");
    return;
  }
  
  if (!cmd.startsWith("test:")) return;
  
  String type = cmd.substring(5);
  
  if (type == "wifi") {
    currentErrorCode = ERR_WIFI_DISCONNECT;
    assignmentStatus = 0;
    Serial.println("[TEST] WiFi error active");
  } else if (type == "auth") {
    currentErrorCode = ERR_CANVAS_AUTH;
    assignmentStatus = 0;
    Serial.println("[TEST] Auth error active");
  } else if (type == "server") {
    currentErrorCode = ERR_CANVAS_SERVER;
    assignmentStatus = 0;
    Serial.println("[TEST] Server error active");
  } else if (type == "time") {
    currentErrorCode = ERR_TIME_SYNC;
    assignmentStatus = 0;
    Serial.println("[TEST] Time sync error active");
  } else if (type == "memory") {
    currentErrorCode = ERR_MEMORY_LOW;
    assignmentStatus = 0;
    Serial.println("[TEST] Memory error active");
  } else if (type == "json") {
    currentErrorCode = ERR_JSON_PARSE;
    assignmentStatus = 0;
    Serial.println("[TEST] JSON error active");
  } else if (type == "buffer") {
    currentErrorCode = ERR_BUFFER_EXHAUSTED;
    assignmentStatus = 0;
    Serial.println("[TEST] Buffer error active");
  } else if (type == "green") {
    currentErrorCode = ERR_NONE;
    assignmentStatus = 1;
    Serial.println("[TEST] Green LED");
  } else if (type == "yellow") {
    currentErrorCode = ERR_NONE;
    assignmentStatus = 2;
    Serial.println("[TEST] Yellow LED");
  } else if (type == "red") {
    currentErrorCode = ERR_NONE;
    assignmentStatus = 3;
    Serial.println("[TEST] Red LED");
  } else if (type == "clear") {
    currentErrorCode = ERR_NONE;
    assignmentStatus = 1;
    Serial.println("[TEST] Cleared");
  } else {
    Serial.println("[TEST] Unknown. Type 'help'");
  }
}


void loop() {
  handleSerialCommands();  // DEV_MODE: Test LED patterns via serial
  
  unsigned long now = millis();

  // Reset watchdog
  esp_task_wdt_reset();

  monitorSystem();
  dnsServer.processNextRequest();
  server.handleClient();

  if (systemConfig.setupComplete) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("âš ï¸ WiFi disconnected, reconnecting...");
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
        Serial.println("âœ… Status updated successfully");
      } else {
        Serial.printf("âš ï¸ Using cached status (last success: %lu min ago)\n",
                     (now - lastSuccessfulFetch) / 60000);
      }
      Serial.println("--- End Check ---\n");
    }
  }

  updateLEDs();
  delay(10);
}
