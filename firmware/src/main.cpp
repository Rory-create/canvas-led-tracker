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
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

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
    Serial.println("[OTA]  Forcing OTA update check...");
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[OTA]  Not connected to WiFi. Connect first.");
      return;
    }
    checkForOTAUpdate();
    Serial.println("[OTA]  Check complete. See output above.");
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
  Serial.println("\n========================================");
  Serial.println("  Canvas LED Tracker - Optimized");
  Serial.println("========================================\n");

  // Initialize watchdog timer (30 second timeout)
  Serial.printf(" Firmware: v%s (%s)\n\n", FIRMWARE_VERSION, BUILD_TIMESTAMP);
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

  // ── Boot button factory reset ──────────────────────────────────────────────
  // Hold BOOT (GPIO 0) for 5 seconds at power-on to wipe all settings.
  // Blinks all LEDs while counting down so the user gets visual feedback.
  #define BOOT_BTN 0
  pinMode(BOOT_BTN, INPUT_PULLUP);
  if (digitalRead(BOOT_BTN) == LOW) {
    Serial.println("[RESET] Boot button held — starting factory reset countdown...");
    unsigned long holdStart = millis();
    bool cancelled = false;
    int lastBlink = -1;
    while (millis() - holdStart < 5000) {
      esp_task_wdt_reset();
      if (digitalRead(BOOT_BTN) == HIGH) { cancelled = true; break; }
      // Blink count matches seconds elapsed so user knows how long to hold
      int elapsed = (millis() - holdStart) / 1000;
      if (elapsed != lastBlink) {
        lastBlink = elapsed;
        setAllLEDsOff();
        delay(80);
        for (int i = 0; i <= elapsed; i++) {
          int ledPins[] = LED_PINS;
          for (int p : ledPins) digitalWrite(p, HIGH);
          delay(120);
          setAllLEDsOff();
          delay(80);
        }
      }
      delay(20);
    }
    if (!cancelled && digitalRead(BOOT_BTN) == LOW) {
      Serial.println("[RESET] Factory reset confirmed — wiping all preferences...");
      // Flash all LEDs rapidly to confirm
      for (int i = 0; i < 6; i++) {
        int ledPins[] = LED_PINS;
        for (int p : ledPins) digitalWrite(p, HIGH);
        delay(100);
        setAllLEDsOff();
        delay(100);
      }
      preferences.begin("config", false);
      preferences.clear();
      preferences.end();
      preferences.begin("boot", false);
      preferences.clear();
      preferences.end();
      Serial.println("[RESET] Done — restarting in setup mode.");
      delay(500);
      ESP.restart();
    } else {
      Serial.println("[RESET] Cancelled.");
    }
  }
  // ──────────────────────────────────────────────────────────────────────────

  Serial.printf(" CPU Temp: %.1fC\n\n", temperatureRead());

  // DEV_MODE: Reset non-credential preferences on boot for easier iteration
  #if DEV_MODE
    Serial.println("  DEV_MODE: Resetting config (keeping credentials)...");
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
    Serial.println(" Config reset complete\n");
  #endif

  loadConfig();

  // Boot-failure auto-recovery: track consecutive boots without a successful Canvas fetch.
  // On 5+ consecutive failures, force an immediate OTA check after WiFi connects.
  int bootAttempts = 0;
  {
    preferences.begin("boot", false);
    bootAttempts = preferences.getInt("bootAttempts", 0) + 1;
    preferences.putInt("bootAttempts", bootAttempts);

    // Rapid power-cycle factory reset: if the device is power-cycled 5 times before
    // the boot is marked stable (30s uptime), wipe NVS config and restart.
    // This lets users recover from a bad WiFi config without a serial monitor.
    bool wasStable = preferences.getBool("bootStable", true); // true on very first boot
    if (!wasStable) {
      int quickReboots = preferences.getInt("quickReboots", 0) + 1;
      preferences.putInt("quickReboots", quickReboots);
      Serial.printf("[BOOT] Quick reboot #%d (unplug 5x to factory reset)\n", quickReboots);
      if (quickReboots >= 5) {
        Serial.println("[BOOT] *** FACTORY RESET — wiping config ***");
        preferences.putInt("quickReboots", 0);
        preferences.putBool("bootStable", true);
        preferences.end();

        // Notify dashboard before wiping — WiFi creds still loaded in wifiConfig
        if (strlen(systemConfig.dashboardUrl) > 0 && strlen(wifiConfig.ssid) > 0) {
          Serial.println("[BOOT] Notifying dashboard of factory reset...");
          WiFi.mode(WIFI_STA);
          WiFi.begin(wifiConfig.ssid, wifiConfig.password);
          unsigned long t = millis();
          while (WiFi.status() != WL_CONNECTED && millis() - t < 8000) {
            delay(200);
            esp_task_wdt_reset();
          }
          if (WiFi.status() != WL_CONNECTED && strlen(wifiConfig.ssid2) > 0) {
            WiFi.begin(wifiConfig.ssid2, wifiConfig.password2);
            t = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - t < 8000) {
              delay(200);
              esp_task_wdt_reset();
            }
          }
          if (WiFi.status() == WL_CONNECTED) {
            uint8_t mac[6]; WiFi.macAddress(mac);
            char macStr[18];
            sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            String url = String(systemConfig.dashboardUrl);
            if (!url.endsWith("/")) url += "/";
            url += "api/telemetry";
            String payload = String("{\"device_id\":\"") + macStr +
                             "\",\"device_name\":\"" + systemConfig.deviceName +
                             "\",\"firmware_version\":\"" + FIRMWARE_VERSION +
                             "\",\"factory_reset\":true}";
            WiFiClientSecure client; client.setInsecure();
            HTTPClient https; https.setTimeout(5000);
            if (https.begin(client, url)) {
              https.addHeader("Content-Type", "application/json");
              if (strlen(systemConfig.dashboardApiKey) > 0)
                https.addHeader("X-API-Key", systemConfig.dashboardApiKey);
              int code = https.POST(payload);
              Serial.printf("[BOOT] Factory reset notified (%d)\n", code);
              https.end();
            }
            WiFi.disconnect(true);
          } else {
            Serial.println("[BOOT] WiFi unavailable — skipping dashboard notification");
          }
        }

        // Flash all LEDs 3× fast to confirm reset visually
        int fpins[] = LED_PINS;
        for (int i = 0; i < 3; i++) {
          for (int p : fpins) analogWrite(p, 200);
          delay(200);
          for (int p : fpins) analogWrite(p, 0);
          delay(200);
        }
        preferences.begin("config", false);
        preferences.clear();
        preferences.end();
        ESP.restart();
      }
    } else {
      // Stable previous boot — clear any accumulated quick-reboot count
      preferences.putInt("quickReboots", 0);
    }
    preferences.putBool("bootStable", false); // cleared to true after 30s in loop()

    preferences.end();
    Serial.printf("[BOOT] Attempt #%d\n", bootAttempts);
  }

  startSettingsAP();

  if (!systemConfig.setupComplete) {
    Serial.println(" FIRST TIME SETUP REQUIRED\n");
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

  if (bootAttempts >= 5 && WiFi.status() == WL_CONNECTED) {
    Serial.println("[BOOT] WARNING: 5+ consecutive boots without successful fetch  forcing OTA check");
    checkForOTAUpdate();
  }

  Serial.println("Configuration:");
  Serial.printf("  LED Mode: %s\n", ledConfig.useFlashing ? "Flashing" : "Solid");
  Serial.printf("  Red LED: %d days, Yellow LED: %d days\n", ledConfig.redLEDDaysAhead, ledConfig.yellowLEDDaysAhead);
  Serial.printf("  Check Interval: %lu min\n", canvasConfig.fetchInterval / 60000);
  Serial.printf("  Timezone: %s\n\n", timezoneConfig.displayName);

  // Initial fetch happens in loop() so the web server is reachable immediately.
  // lastFetch == 0 triggers fetch on the first loop iteration.
  Serial.println("\n Running!\n");
}

void loop() {
  handleSerialCommands();  // DEV_MODE: Test LED patterns via serial
  
  unsigned long now = millis();

  // Reset watchdog
  esp_task_wdt_reset();

  monitorSystem();
  dnsServer.processNextRequest();
  server.handleClient();

  // Mark boot stable after 30s — resets the power-cycle factory reset counter
  static bool bootMarkedStable = false;
  if (!bootMarkedStable && now >= 30000UL) {
    bootMarkedStable = true;
    preferences.begin("boot", false);
    preferences.putBool("bootStable", true);
    preferences.putInt("quickReboots", 0);
    preferences.end();
  }

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

    // Telemetry heartbeat every 5 minutes  no-op if dashboardUrl is not set
    static unsigned long lastTelemetry = ULONG_MAX - (4UL * 60UL * 1000UL); // fires ~1 min after boot
    if (now - lastTelemetry >= 5UL * 60UL * 1000UL) {
      sendTelemetry();
      lastTelemetry = now;
    }

    if (lastFetch == 0 || now - lastFetch >= canvasConfig.fetchInterval) {
      Serial.println("\n--- Canvas Check Cycle ---");
      assignmentStatus = fetchCanvasAssignments();
      lastFetch = now;
      pulseBrightness = 0;
      fadeDirection = 1;

      // Show status summary
      if (consecutiveErrors == 0) {
        Serial.println(" Status updated successfully");
      } else {
        Serial.printf(" Using cached status (last success: %lu min ago)\n",
                     (now - lastSuccessfulFetch) / 60000);
      }
      Serial.println("--- End Check ---\n");
    }
  }

  updateLEDs();
  delay(10);
}
