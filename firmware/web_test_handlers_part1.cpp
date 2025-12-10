
void handleTestMode() {
  if (!DEV_MODE) {
    server.send(403, "text/plain", "Test mode disabled");
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
button{width:100%;padding:15px;margin:8px 0;border:none;cursor:pointer;border-radius:5px;font-size:16px;font-weight:bold;color:white;}
.err{background:#f44336;} .norm{background:#4caf50;} .clr{background:#2196f3;}
button:hover{opacity:0.8;}
#status{padding:15px;background:#1a1a1a;border-radius:5px;margin-top:20px;font-family:monospace;}
</style>
</head><body><div class="container">
<h1>LED Test Mode</h1>
<p style="text-align:center;color:#ffa500;">DEV_MODE Active</p>

<h3>Error Patterns</h3>
<button class="err" onclick="test('wifi')">WiFi (Slow 1s)</button>
<button class="err" onclick="test('auth')">Auth (Double)</button>
<button class="err" onclick="test('server')">Server (Triple)</button>
<button class="err" onclick="test('time')">Time (Medium 500ms)</button>
