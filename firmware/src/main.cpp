#include "config.h"
#include "wifi_manager.h"
#include "canvas_api.h"
#include "led_control.h"
#include "web_server.h"
#include "bug_report.h"
#include "ota_update.h"
#include "telemetry.h"
#include "version.h"
#include <esp_task_wdt.h>

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
    Serial.println("ota:check     - Force OTA update check now");
    Serial.println("version:info  - Show firmware version and build info");
    Serial.println("ap:start      - Start AP mode for web UI testing\n");
    return;
  }
  
  if (cmd == "version:info") {
    Serial.println("\n=== Firmware Information ===");
    Serial.printf("Version: %s\n", FIRMWARE_VERSION);
    Serial.printf("Build: %s\n", BUILD_TIMESTAMP);
    Serial.printf("Device: %s\n", systemConfig.deviceName);
    Serial.println("===========================\n");
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

  if (cmd == "ota:check") {
    Serial.println("[OTA] 🔄 Forcing OTA update check...");
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[OTA] ❌ Not connected to WiFi. Connect first.");
      return;
    }
    checkForOTAUpdate();
    Serial.println("[OTA] ✅ Check complete. See output above.");
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

void loop() {
  handleSerialCommands();  // DEV_MODE: Test LED patterns via serial
  
  unsigned long now = millis();

  // Reset watchdog
  esp_task_wdt_reset();

  monitorSystem();
  dnsServer.processNextRequest();
  server.handleClient();

  if (systemConfig.setupComplete) {
    // WiFi reconnect with 30-second backoff to avoid hammering the AP on poor signal
    static unsigned long lastReconnectAttempt = 0;
    if (WiFi.status() != WL_CONNECTED && now - lastReconnectAttempt > 30000UL) {
      Serial.println("WiFi disconnected, reconnecting...");
      connectWiFi();
      lastReconnectAttempt = now;
    }

    // Periodic NTP retry if time sync failed on boot
    static unsigned long lastNTPRetry = 0;
    if (!timeSyncComplete && WiFi.status() == WL_CONNECTED && now - lastNTPRetry > 60000UL) {
      initTime();
      lastNTPRetry = now;
    }

    // OTA rate-limiting is handled internally by checkForOTAUpdate()
    checkForOTAUpdate();

    // Telemetry heartbeat every 5 minutes — no-op if dashboardUrl is not set
    static unsigned long lastTelemetry = 0;
    if (now - lastTelemetry >= 5UL * 60UL * 1000UL) {
      sendTelemetry();
      lastTelemetry = now;
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
