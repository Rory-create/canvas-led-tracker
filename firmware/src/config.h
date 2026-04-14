#pragma once
#include <Arduino.h>
#include <Preferences.h>

// ============================================
// CONFIG STRUCTURES
// ============================================
struct LEDConfig {
  bool useFlashing = true;
  int flashInterval = 1, flashStep = 1, solidBrightness = 100, maxBrightness = 100;
  bool quietHoursEnabled = false;
  int quietHourStart = 22, quietHourEnd = 7;
  int redLEDDaysAhead = 0, yellowLEDDaysAhead = 1;
};

struct WiFiConfig {
  char ssid[64] = "", password[64] = "", ssid2[64] = "", password2[64] = "";
  bool useSecondaryNetwork = false;
};

struct CanvasConfig {
  char apiUrl[256] = "https://ojrsd.instructure.com/api/v1/users/self/todo";
  char apiToken[128] = "";
  int itemsPerPage = 8;
  unsigned long fetchInterval = 10UL * 60UL * 1000UL;
  size_t jsonBufferSize = 8192;  // 8KB; adaptive scaling is in fetchCanvasAssignments()
  bool includeOverdue = false;  // Default: exclude overdue assignments
  unsigned long tokenLastValidated = 0;  // Unix timestamp of last successful Canvas fetch
};

struct TimezoneConfig {
  char tzString[64] = "EST5EDT,M3.2.0/2,M11.1.0/2";
  char displayName[32] = "US Eastern";
};

struct SystemConfig {
  bool setupComplete = false;
  char deviceName[32] = "";
  bool debugMode = true;
  char apPassword[64] = "canvas123";
  bool bugReportEnabled = true;
  unsigned long lastBugReport = 0;
  char dashboardUrl[128] = "";     // e.g. https://your-app.railway.app
  char dashboardApiKey[64] = "";   // matches DASHBOARD_API_KEY env var on server
};

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

// Assignment storage for web UI display
struct Assignment {
  String name;
  String dueAt;
  String description;
  String htmlUrl;
  time_t dueTimestamp;
  int urgency; // 0=none, 1=coming up (yellow), 2=urgent (red)
};

// ============================================
// CONSTANTS
// ============================================
extern const int greenLED;
extern const int yellowLED;
extern const int redLED;
extern const int MAX_RETRIES;
extern const int HTTPS_TIMEOUT;
extern const int DNS_PORT;
extern const byte LOG_BUFFER_SIZE;

#define LED_PINS {greenLED, yellowLED, redLED}

const int MAX_DISPLAYED_ASSIGNMENTS = 5;

// ============================================
// GLOBAL VARIABLE DECLARATIONS (defined in config.cpp)
// ============================================
extern LEDConfig ledConfig;
extern WiFiConfig wifiConfig;
extern CanvasConfig canvasConfig;
extern TimezoneConfig timezoneConfig;
extern SystemConfig systemConfig;

extern int currentErrorCode;
extern Assignment displayedAssignments[MAX_DISPLAYED_ASSIGNMENTS];
extern int assignmentCount;

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>

extern WiFiClientSecure client;
extern HTTPClient http;
extern WebServer server;
extern DNSServer dnsServer;
extern Preferences preferences;

extern String serialLog[];
extern int logIndex;
extern int assignmentStatus;
extern unsigned long lastFetch;
extern unsigned long lastPulseTime;
extern unsigned long lastSuccessfulFetch;
extern int pulseBrightness;
extern int fadeDirection;
extern int consecutiveErrors;
extern bool webServerRunning;
extern bool timeSyncComplete;
extern char otaVersionSeen[16];  // last version string read from version.json
extern unsigned long snoozeUntil; // millis() timestamp until which LEDs stay green

// ============================================
// FUNCTION DECLARATIONS
// ============================================
void addToLog(String msg);
void saveConfig();
void loadConfig();
