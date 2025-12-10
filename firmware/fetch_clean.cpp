int fetchCanvasAssignments() {
  connectWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected - returning error state");
    consecutiveErrors++;
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

        // Memory analysis
        Serial.printf("\nCanvas Response Analysis:\n");
        Serial.printf("   Response size: %d bytes\n", response.length());
        Serial.printf("   JSON buffer:   %d bytes\n", bufferSize);
        Serial.printf("   Buffer usage:  %.1f%%\n", (response.length() / (float)bufferSize) * 100);
        Serial.printf("   Free heap:     %d bytes\n", freeHeap);
        Serial.printf("   Max buffer:    %d bytes (90%% of heap)\n", maxBuffer);

        DynamicJsonDocument doc(bufferSize);
        DeserializationError error = deserializeJson(doc, response);

        if (!error) {
          time_t now, redDeadline = 0, yellowDeadline = 0;
          time(&now);
          redDeadline = now + (ledConfig.redLEDDaysAhead * 86400);
          yellowDeadline = now + (ledConfig.yellowLEDDaysAhead * 86400);

          if (systemConfig.debugMode) {
            Serial.printf("Checking: Red=%d days, Yellow=%d days\n",
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
                  Serial.printf("Invalid date detected: %s\n", dueDate);
                  continue;
                }

                if (due_time <= redDeadline) {
                  newStatus = 3;  // RED (3) = due today
                  if (systemConfig.debugMode) Serial.println("RED");
                  break;
                } else if (due_time <= yellowDeadline) {
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
            
            // Calculate new buffer size (150% of current, or response size + 20KB, whichever is smaller)
            size_t newBufferSize = min((bufferSize * 3) / 2, response.length() + 20480);
            
            if (newBufferSize <= maxBuffer && newBufferSize > bufferSize) {
              Serial.printf("   Increasing buffer: %d -> %d bytes\n", bufferSize, newBufferSize);
              canvasConfig.jsonBufferSize = newBufferSize;
              
              // Retry with new buffer size
              Serial.println("   Retrying with increased buffer...\n");
              continue;  // Retry this attempt with new buffer
            } else {
              Serial.println("\nCRITICAL: Cannot increase buffer further!");
              Serial.printf("   New buffer would be: %d bytes\n", newBufferSize);
              Serial.printf("   Max allowed (90%% heap): %d bytes\n", maxBuffer);
              Serial.println("   Returning ERROR state - all LEDs will flash");
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
