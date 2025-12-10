
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
    Serial.println("test:clear    - Exit test mode\n");
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

