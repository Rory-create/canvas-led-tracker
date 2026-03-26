#include "config.h"

// ============================================
// GLOBAL VARIABLE DEFINITIONS
// ============================================
LEDConfig ledConfig;
WiFiConfig wifiConfig;
CanvasConfig canvasConfig;
TimezoneConfig timezoneConfig;
SystemConfig systemConfig;

const int greenLED = 32, yellowLED = 25, redLED = 27;
const int MAX_RETRIES = 3, HTTPS_TIMEOUT = 60000, DNS_PORT = 53;
const byte LOG_BUFFER_SIZE = 50;

int currentErrorCode = ERR_NONE;

Assignment displayedAssignments[MAX_DISPLAYED_ASSIGNMENTS];
int assignmentCount = 0;

WiFiClientSecure client;
HTTPClient http;
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

String serialLog[50];
int logIndex = 0, assignmentStatus = 0;
unsigned long lastFetch = 0, lastPulseTime = 0, lastSuccessfulFetch = 0;
int pulseBrightness = 0, fadeDirection = 1, consecutiveErrors = 0;
bool webServerRunning = false;
bool timeSyncComplete = false;
char otaVersionSeen[16] = "";
unsigned long snoozeUntil = 0;

// ============================================
// LOG FUNCTION
// ============================================
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
  preferences.putBool("inclOverdue", canvasConfig.includeOverdue);

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
  preferences.putString("dashUrl", systemConfig.dashboardUrl);
  preferences.putString("dashKey", systemConfig.dashboardApiKey);

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
    canvasConfig.includeOverdue = preferences.getBool("inclOverdue", false);  // Default: exclude overdue

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
    preferences.getString("dashUrl", systemConfig.dashboardUrl, sizeof(systemConfig.dashboardUrl));
    preferences.getString("dashKey", systemConfig.dashboardApiKey, sizeof(systemConfig.dashboardApiKey));

    Serial.println("Configuration loaded");
  } else {
    Serial.println("⚠️ First time setup required");
  }
  preferences.end();

  // Always apply compile-time dashboard credentials — overrides any NVS value so
  // a firmware update is sufficient to change the URL across the whole fleet.
  // If the build has no URL compiled in, whatever is in NVS remains untouched.
  if (strlen(DASHBOARD_URL) > 0) {
    strncpy(systemConfig.dashboardUrl, DASHBOARD_URL, sizeof(systemConfig.dashboardUrl) - 1);
    systemConfig.dashboardUrl[sizeof(systemConfig.dashboardUrl) - 1] = '\0';
  }
  if (strlen(DASHBOARD_API_KEY) > 0) {
    strncpy(systemConfig.dashboardApiKey, DASHBOARD_API_KEY, sizeof(systemConfig.dashboardApiKey) - 1);
    systemConfig.dashboardApiKey[sizeof(systemConfig.dashboardApiKey) - 1] = '\0';
  }
}
