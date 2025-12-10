// Error Reporting System for Canvas LED Tracker
// POST error codes and create GitHub issues for critical errors

#ifndef ERROR_REPORTING_H
#define ERROR_REPORTING_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Error codes with LED patterns
enum ErrorCode {
  ERR_NONE = 0,           // No error
  ERR_WIFI_DISCONNECT = 1,     // Slow flash (1s) - Network issue
  ERR_CANVAS_AUTH = 2,         // Double flash - Invalid token
  ERR_CANVAS_SERVER = 3,       // Triple flash - Canvas server error
  ERR_TIME_SYNC = 4,           // Fast flash (500ms) - Time sync failed
  ERR_MEMORY_LOW = 5,          // Quad flash - Heap below threshold
  ERR_JSON_PARSE = 6,          // Fast flash (200ms) - JSON parse error
  ERR_BUFFER_EXHAUSTED = 7     // All LEDs solid - Buffer at 90% limit
};

struct ErrorReport {
  ErrorCode code;
  String message;
  unsigned long timestamp;
  size_t freeHeap;
  int consecutiveErrors;
};

class ErrorReporter {
private:
  const char* githubToken;
  const char* repoOwner;
  const char* repoName;
  String deviceId;
  ErrorReport lastError;
  unsigned long lastGithubReport;
  const unsigned long GITHUB_REPORT_COOLDOWN = 3600000; // 1 hour between GitHub reports
  
  String getErrorName(ErrorCode code) {
    switch(code) {
      case ERR_WIFI_DISCONNECT: return "WiFi Disconnected";
      case ERR_CANVAS_AUTH: return "Canvas Auth Failed";
      case ERR_CANVAS_SERVER: return "Canvas Server Error";
      case ERR_TIME_SYNC: return "Time Sync Failed";
      case ERR_MEMORY_LOW: return "Low Memory Warning";
      case ERR_JSON_PARSE: return "JSON Parse Error";
      case ERR_BUFFER_EXHAUSTED: return "Buffer Exhausted";
      default: return "Unknown Error";
    }
  }
  
  String getErrorDescription(ErrorCode code) {
    switch(code) {
      case ERR_WIFI_DISCONNECT: return "Device lost WiFi connection";
      case ERR_CANVAS_AUTH: return "Canvas API returned 401 - check token validity";
      case ERR_CANVAS_SERVER: return "Canvas API server error (500-504)";
      case ERR_TIME_SYNC: return "NTP time synchronization failed after multiple attempts";
      case ERR_MEMORY_LOW: return "Free heap below 20KB threshold";
      case ERR_JSON_PARSE: return "Failed to parse Canvas API JSON response";
      case ERR_BUFFER_EXHAUSTED: return "JSON buffer reached 90% heap limit, cannot increase further";
      default: return "No description available";
    }
  }

public:
  ErrorReporter(const char* token, const char* owner, const char* repo, String devId) 
    : githubToken(token), repoOwner(owner), repoName(repo), deviceId(devId), 
      lastGithubReport(0) {
    lastError.code = ERR_NONE;
  }
  
  void reportError(ErrorCode code, String message, size_t heap, int consecutiveErrs) {
    lastError.code = code;
    lastError.message = message;
    lastError.timestamp = millis();
    lastError.freeHeap = heap;
    lastError.consecutiveErrors = consecutiveErrs;
    
    // Log locally
    Serial.printf("\n[ERROR] Code %d: %s\n", code, message.c_str());
    Serial.printf("  Free Heap: %d bytes\n", heap);
    Serial.printf("  Consecutive: %d\n", consecutiveErrs);
    
    // Critical errors trigger GitHub issue
    if (shouldReportToGithub(code, consecutiveErrs)) {
      createGithubIssue();
    }
  }
  
  bool shouldReportToGithub(ErrorCode code, int consecutiveErrs) {
    // Rate limit: 1 hour cooldown
    if (millis() - lastGithubReport < GITHUB_REPORT_COOLDOWN) {
      return false;
    }
    
    // Critical conditions
    if (code == ERR_BUFFER_EXHAUSTED) return true;
    if (code == ERR_MEMORY_LOW && lastError.freeHeap < 15000) return true;
    if (consecutiveErrs >= 10) return true;
    
    return false;
  }
  
  void createGithubIssue() {
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();
    
    String url = String("https://api.github.com/repos/") + repoOwner + "/" + repoName + "/issues";
    
    if (!http.begin(client, url)) {
      Serial.println("Failed to connect to GitHub API");
      return;
    }
    
    // Build issue payload
    StaticJsonDocument<1024> doc;
    doc["title"] = String("[") + deviceId + "] " + getErrorName(lastError.code);
    
    String body = "**Error Code:** " + String((int)lastError.code) + " - " + getErrorName(lastError.code) + "\n\n";
    body += "**Description:** " + getErrorDescription(lastError.code) + "\n\n";
    body += "**Details:**\n";
    body += "- Device: `" + deviceId + "`\n";
    body += "- Message: `" + lastError.message + "`\n";
    body += "- Free Heap: " + String(lastError.freeHeap) + " bytes\n";
    body += "- Consecutive Errors: " + String(lastError.consecutiveErrors) + "\n";
    body += "- Uptime: " + String(millis() / 1000) + " seconds\n";
    body += "- Timestamp: " + String(lastError.timestamp) + "\n\n";
    body += "**Auto-reported by device**";
    
    doc["body"] = body;
    
    JsonArray labels = doc.createNestedArray("labels");
    labels.add("bug");
    labels.add("auto-reported");
    if (lastError.code == ERR_MEMORY_LOW || lastError.code == ERR_BUFFER_EXHAUSTED) {
      labels.add("memory");
    }
    if (lastError.code == ERR_CANVAS_AUTH || lastError.code == ERR_CANVAS_SERVER) {
      labels.add("canvas-api");
    }
    
    String payload;
    serializeJson(doc, payload);
    
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + githubToken);
    http.addHeader("Accept", "application/vnd.github.v3+json");
    http.addHeader("User-Agent", String("CanvasLEDTracker-") + deviceId);
    
    int httpCode = http.POST(payload);
    
    if (httpCode == 201) {
      Serial.println("[GitHub] Issue created successfully");
      lastGithubReport = millis();
    } else {
      Serial.printf("[GitHub] Failed to create issue: %d\n", httpCode);
      if (httpCode > 0) {
        Serial.println(http.getString());
      }
    }
    
    http.end();
  }
  
  ErrorCode getCurrentError() { return lastError.code; }
  unsigned long getLastErrorTime() { return lastError.timestamp; }
};

#endif
