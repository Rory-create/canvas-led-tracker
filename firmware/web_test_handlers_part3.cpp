
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
