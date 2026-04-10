#include "test_mode.h"
#include "config.h"
#include "version.h"

void handleTestMode() {
  if (!DEV_MODE) { server.send(403, "text/plain", "Test mode disabled"); return; }

  server.send(200, "text/html", R"rawliteral(
<!DOCTYPE html><html><head>
<title>LED Test Mode</title>
<meta name="viewport" content="width=device-width">
<style>
body{font-family:Arial;max-width:600px;margin:20px auto;padding:20px;background:#1a1a1a;color:#fff;}
.container{background:#2a2a2a;padding:30px;border-radius:10px;}
h1{color:#667eea;text-align:center;}
button{width:100%;padding:15px;margin:8px 0;border:none;cursor:pointer;border-radius:5px;font-size:16px;font-weight:bold;color:white;}
.err{background:#f44336;} .norm{background:#4caf50;} .clr{background:#2196f3;}
button:hover{opacity:0.8;}
#status{padding:15px;background:#1a1a1a;border-radius:5px;margin-top:20px;font-family:monospace;}
</style>
</head><body><div class="container">
<h1>LED Test Mode</h1>
<p style="text-align:center;color:#ffa500;">DEV_MODE Active</p>
<h3>Error Patterns</h3>
<button class="err" onclick="test('wifi')">WiFi (Red-Red)</button>
<button class="err" onclick="test('auth')">Auth (Yellow-Red)</button>
<button class="err" onclick="test('server')">Server (Red-Yellow-Red)</button>
<button class="err" onclick="test('time')">Time Sync (Green-Yellow)</button>
<button class="err" onclick="test('memory')">Memory (Red-Red-Red)</button>
<button class="err" onclick="test('json')">JSON Parse (Yellow-Green-Yellow)</button>
<button class="err" onclick="test('buffer')">Buffer Full (All Solid)</button>
<h3>Normal States</h3>
<button class="norm" onclick="test('green')">Green LED</button>
<button class="norm" onclick="test('yellow')">Yellow LED</button>
<button class="norm" onclick="test('red')">Red LED</button>
<button class="clr" onclick="test('clear')">Clear Test</button>
<div id="status">Ready</div>
</div>
<script>
function test(type){fetch('/test-trigger?type='+type).then(r=>r.text()).then(msg=>{document.getElementById('status').innerHTML=msg;}).catch(()=>{document.getElementById('status').innerHTML='Error';});}
</script>
</body></html>
)rawliteral");
}

void handleTestTrigger() {
  if (!DEV_MODE) { server.send(403, "text/plain", "Not authorized"); return; }

  String type = server.arg("type");
  if      (type == "wifi")   { currentErrorCode = ERR_WIFI_DISCONNECT; assignmentStatus = 0; server.send(200, "text/plain", "[ACTIVE] WiFi error"); }
  else if (type == "auth")   { currentErrorCode = ERR_CANVAS_AUTH;     assignmentStatus = 0; server.send(200, "text/plain", "[ACTIVE] Auth error"); }
  else if (type == "server") { currentErrorCode = ERR_CANVAS_SERVER;   assignmentStatus = 0; server.send(200, "text/plain", "[ACTIVE] Server error"); }
  else if (type == "time")   { currentErrorCode = ERR_TIME_SYNC;       assignmentStatus = 0; server.send(200, "text/plain", "[ACTIVE] Time sync"); }
  else if (type == "memory") { currentErrorCode = ERR_MEMORY_LOW;      assignmentStatus = 0; server.send(200, "text/plain", "[ACTIVE] Memory low"); }
  else if (type == "json")   { currentErrorCode = ERR_JSON_PARSE;      assignmentStatus = 0; server.send(200, "text/plain", "[ACTIVE] JSON parse"); }
  else if (type == "buffer") { currentErrorCode = ERR_BUFFER_EXHAUSTED;assignmentStatus = 0; server.send(200, "text/plain", "[ACTIVE] Buffer full"); }
  else if (type == "green")  { currentErrorCode = ERR_NONE; assignmentStatus = 1; server.send(200, "text/plain", "[NORMAL] Green LED"); }
  else if (type == "yellow") { currentErrorCode = ERR_NONE; assignmentStatus = 2; server.send(200, "text/plain", "[NORMAL] Yellow LED"); }
  else if (type == "red")    { currentErrorCode = ERR_NONE; assignmentStatus = 3; server.send(200, "text/plain", "[NORMAL] Red LED"); }
  else if (type == "clear")  { currentErrorCode = ERR_NONE; assignmentStatus = 1; server.send(200, "text/plain", "[CLEARED] Normal operation"); }
  else                       { server.send(400, "text/plain", "Unknown type"); }
}
