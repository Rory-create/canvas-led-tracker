#include "canvas_api.h"
#include "config.h"
#include "wifi_manager.h"
#include "bug_report.h"
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

int fetchCanvasAssignments() {
  connectWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected - returning error state");
    consecutiveErrors++;

    // Report critical errors to GitHub
    if (consecutiveErrors >= 10) {
      reportBug();
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

        // With ArduinoJson filtering, responses are ~2KB instead of 49KB.
        // Track buffer size in a static local so it never permanently inflates canvasConfig.
        // Capped at 75% of free heap (not 90%) to leave headroom for other allocations.
        static size_t adaptiveBufferSize = 8192;
        size_t bufferSize = adaptiveBufferSize;
        size_t freeHeap = ESP.getFreeHeap();
        size_t maxBuffer = (freeHeap * 75) / 100;  // 75% of free heap

        // Check for critical memory condition
        if (freeHeap < 15000) {
          currentErrorCode = ERR_MEMORY_LOW;
          Serial.println("[CRITICAL] Memory below 15KB threshold!");
          reportBug();
        }

        // Memory analysis
        Serial.printf("\nCanvas Response Analysis:\n");
        Serial.printf("   Response size: %d bytes\n", response.length());
        Serial.printf("   JSON buffer:   %d bytes\n", bufferSize);
        Serial.printf("   Buffer usage:  %.1f%%\n", (response.length() / (float)bufferSize) * 100);
        Serial.printf("   Free heap:     %d bytes\n", freeHeap);
        Serial.printf("   Max buffer:    %d bytes (75%% of heap)\n", maxBuffer);

        // ArduinoJson Filter: Skip description field to avoid HTML/image bloat
        // This reduces memory usage from ~49KB to ~2KB per response
        StaticJsonDocument<256> filter;
        filter[0]["assignment"]["name"] = true;
        filter[0]["assignment"]["due_at"] = true;
        filter[0]["assignment"]["html_url"] = true;
        // NOTE: "description" intentionally excluded (contains embedded images)
        filter[0]["completed"] = true;

        Serial.println("   Using ArduinoJson filter to skip description field");

        DynamicJsonDocument doc(bufferSize);

        // Temporarily remove loop task from watchdog during slow JSON parsing
        // (HTML-heavy Canvas responses can take 10+ seconds to parse).
        // WDT is always re-added before evaluating the result so it is never
        // left disabled regardless of what deserializeJson does internally.
        esp_task_wdt_delete(NULL);
        Serial.println("   Parsing JSON (watchdog disabled for this task)...");

        DeserializationError error = deserializeJson(doc, response,
                                                     DeserializationOption::Filter(filter));

        // Re-add unconditionally — must happen before any branch on `error`
        esp_task_wdt_add(NULL);
        esp_task_wdt_reset();
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

                // Skip overdue assignments unless user enabled them
                if (!canvasConfig.includeOverdue && due_time_local < now) {
                  if (systemConfig.debugMode) {
                    Serial.printf("Skipping overdue assignment: %s (due %s)\n",
                                 item["assignment"]["name"].as<const char*>(), dueDate);
                  }
                  continue;  // Skip this assignment
                }

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

                  // Description field excluded via ArduinoJson filter (contained HTML/images)
                  a.description = "(Description not available)";

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

          // Success! Reset adaptive buffer so transient spikes don't permanently grow it.
          consecutiveErrors = 0;
          lastSuccessfulFetch = millis();
          adaptiveBufferSize = 8192;
          // Record timestamp for token expiry tracking on settings page
          time_t nowTs;
          time(&nowTs);
          if (nowTs > 1000000) {
            canvasConfig.tokenLastValidated = (unsigned long)nowTs;
            preferences.begin("config", false);
            preferences.putULong("tokenAge", canvasConfig.tokenLastValidated);
            preferences.end();
          }
          Serial.println("Canvas fetch successful");
          // Reset boot-failure counter now that we know the firmware is working
          preferences.begin("boot", false);
          preferences.putInt("bootAttempts", 0);
          preferences.end();
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
              adaptiveBufferSize = newBufferSize;

              // Retry with new buffer size
              Serial.println("   Retrying with increased buffer...\n");
              continue;  // Retry this attempt with new buffer
            } else {
              Serial.println("\nCRITICAL: Cannot increase buffer further!");
              Serial.printf("   Already at maximum: %d bytes\n", bufferSize);
              Serial.printf("   Max allowed (90%% heap): %d bytes\n", maxBuffer);
              Serial.println("   Returning ERROR state - all LEDs will flash");
              currentErrorCode = ERR_BUFFER_EXHAUSTED;
              reportBug();
              http.end();
              consecutiveErrors++;
              return 0;  // Return ERROR state
            }
          } else {
            Serial.println("   (Non-memory error)\n");
          }
        }
      } else if (httpCode == 401) {
        Serial.println("Canvas API: Invalid or expired token (401)");
        currentErrorCode = ERR_CANVAS_AUTH;
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
