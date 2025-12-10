// Add after handleHealth() function

void handleTestMode() {
  if (!DEV_MODE) {
    server.send(403, "text/plain", "Test mode only available in DEV_MODE");
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
.section{border:1px solid #444;padding:20px;margin:15px 0;border-radius:8px;background:#333;}
.section h3{margin-top:0;color:#ffa500;}
button{width:100%;padding:15px;margin:8px 0;border:none;cursor:pointer;border-radius:5px;font-size:16px;font-weight:bold;}
.error-btn{background:linear-gradient(135deg,#f44336,#e91e63);color:white;}
.normal-btn{background:linear-gradient(135deg,#4caf50,#8bc34a);color:white;}
.clear-btn{background:linear-gradient(135deg,#2196f3,#03a9f4);color:white;}
button:hover{opacity:0.8;}
.status{padding:15px;background:#1a1a1a;border-radius:5px;margin-top:20px;font-family:monospace;}
</style>
</head><body><div class="container">
<h1>🔧 LED Test Mode</h1>
<p style="text-align:center;color:#ffa500;">DEV_MODE Active - Test Error Patterns</p>

<div class="section">
<h3>Error Patterns</h3>
<button class="error-btn" onclick="test('wifi')">WiFi Error (Slow 1s)</button>
<button class="error-btn" onclick="test('auth')">Auth Error (Double Flash)</button>
<button class="error-btn" onclick="test('server')">Server Error (Triple Flash)</button>
<button class="error-btn" onclick="test('time')">Time Sync (Medium 500ms)</button>
<button class="error-btn" onclick="test('memory')">Memory Low (Quad Flash)</button>
<button class="error-btn" onclick="test('json')">JSON Parse (Fast 200ms)</button>
<button class="error-btn" onclick="test('buffer')">Buffer Full (All Solid)</button>
</div>

<div class="section">
<h3>Normal Operation</h3>
<button class="normal-btn" onclick="test('green')">🟢 Green LED (All Clear)</button>
<button class="normal-btn" onclick="test('yellow')">🟡 Yellow LED (Due Soon)</button>
<button class="normal-btn" onclick="test('red')">🔴 Red LED (Due Today)</button>
</div>

<button class="clear-btn" onclick="test('clear')">✓ Clear Test Mode</button>

<div class="status" id="status">Ready - Click button to test</div>
</div>

<script>
function test(type) {
  fetch('/test-trigger?type=' + type)
    .then(r => r.text())
    .then(msg => {
      document.getElementById('status').innerHTML = msg;
    })
    .catch(() => {
      document.getElementById('status').innerHTML = 'Error sending command';
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
  String response = "Unknown command";
  
  if (type == "wifi") {
    currentErrorCode = ERR_WIFI_DISCONNECT;
    assignmentStatus = 0;
    response = "[ACTIVE] WiFi Error - Slow flash (1s intervals)";
  }
  else if (type == "auth") {
    currentErrorCode = ERR_CANVAS_AUTH;
    assignmentStatus = 0;
    response = "[ACTIVE] Auth Error - Double flash pattern";
  }
  else if (type == "server") {
    currentErrorCode = ERR_CANVAS_SERVER;
    assignmentStatus = 0;
    response = "[ACTIVE] Server Error - Triple flash pattern";
  }
  else if (type == "time") {
    currentErrorCode = ERR_TIME_SYNC;
    assignmentStatus = 0;
    response = "[ACTIVE] Time Sync Error - Medium flash (500ms)";
  }
  else if (type == "memory") {
    currentErrorCode = ERR_MEMORY_LOW;
    assignmentStatus = 0;
    response = "[ACTIVE] Memory Low - Quad flash pattern";
  }
  else if (type == "json") {
    currentErrorCode = ERR_JSON_PARSE;
    assignmentStatus = 0;
    response = "[ACTIVE] JSON Parse Error - Fast flash (200ms)";
  }
  else if (type == "buffer") {
    currentErrorCode = ERR_BUFFER_EXHAUSTED;
    assignmentStatus = 0;
    response = "[ACTIVE] Buffer Exhausted - All LEDs solid";
  }
  else if (type == "green") {
    currentErrorCode = ERR_NONE;
    assignmentStatus = 1;
    response = "[NORMAL] Green LED - All clear";
  }
  else if (type == "yellow") {
    currentErrorCode = ERR_NONE;
    assignmentStatus = 2;
    response = "[NORMAL] Yellow LED - Assignment due soon";
  }
  else if (type == "red") {
    currentErrorCode = ERR_NONE;
    assignmentStatus = 3;
    response = "[NORMAL] Red LED - Assignment due today";
  }
  else if (type == "clear") {
    currentErrorCode = ERR_NONE;
    assignmentStatus = 1;
    response = "[CLEARED] Returned to normal operation";
  }
  
  server.send(200, "text/plain", response);
}
