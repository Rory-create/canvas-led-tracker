#include "web_server.h"
#include "config.h"
#include "wifi_manager.h"
#include "canvas_api.h"
#include "version.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

// ============================================
// HTML TEMPLATES
// ============================================
const char WELCOME_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>Canvas LED Setup</title>
<style>
body{font-family:Arial;max-width:600px;margin:20px auto;padding:20px;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;}
.container{background:white;padding:30px;border-radius:10px;box-shadow:0 10px 40px rgba(0,0,0,0.2);}
h1{color:#667eea;text-align:center;margin-bottom:10px;}
input,textarea{width:100%;padding:10px;margin:5px 0;box-sizing:border-box;border:1px solid #ddd;border-radius:5px;}
.pass-wrap{position:relative;}
.pass-wrap input:hover{-webkit-text-security:none !important;}
.show-pass{position:absolute;right:10px;top:15px;cursor:pointer;display:none;}
button{background:linear-gradient(135deg,#667eea,#764ba2);color:white;padding:12px;border:none;cursor:pointer;width:100%;margin:10px 0;border-radius:5px;font-size:16px;}
button:hover{opacity:0.9;}
.section{border:1px solid #e0e0e0;padding:15px;margin:15px 0;border-radius:8px;background:#f9f9f9;}
.section h3{margin-top:0;color:#555;}
.error{color:#d32f2f;font-size:13px;margin-top:5px;display:none;}
</style></head><body><div class="container">
<h1>Canvas LED Tracker</h1>
<p style="text-align:center;color:#666;">Initial Setup</p>
<p style="text-align:center;font-size:12px;color:#999;">Firmware v%FW_VERSION% | <a href="/health" target="_blank" style="color:#667eea;">Health</a> | <a href="/logs" target="_blank" style="color:#667eea;">Logs</a></p>
<p><b>Get Canvas Token:</b> Open Canvas in browser â†’ Account â†’ Settings â†’ Approved Integrations â†’ + New Access Token</p>
<form method="POST" action="/save" onsubmit="return validateSetup()">
<div class="section">
<h3>WiFi Settings</h3>
<label>Network Name<input type="text" name="ssid" id="ssidSelect" list="ssidList" placeholder="Scanning..." style="width:100%;padding:10px;" autocomplete="off"><datalist id="ssidList"></datalist></label>
<label>WiFi Password<div class="pass-wrap"><input type="password" name="password" id="wifiPass" class="pwd"><span class="show-pass" onclick="togglePass(this)">SHOW</span></div></label>
<button type="button" onclick="testWifi()" style="background:#28a745;margin:5px 0;">Test WiFi</button>
<div id="wifiResult" style="padding:8px;margin:5px 0;border-radius:5px;display:none;"></div>
<label>Backup SSID<input type="text" name="ssid2" id="ssid2Select" list="ssid2List" placeholder="Optional backup network" style="width:100%;padding:10px;" autocomplete="off"><datalist id="ssid2List"></datalist></label>
<label>Backup Password<div class="pass-wrap"><input type="password" name="password2" class="pwd"><span class="show-pass" onclick="togglePass(this)">SHOW</span></div></label>
<div id="scanStatus" style="font-size:13px;color:#666;margin:8px 0;"></div>
<button type="button" id="scanBtn" onclick="scanWifi()" style="background:#17a2b8;margin:5px 0;">Re-scan Networks</button>
</div>
<div class="section">
<h3>Canvas API Token</h3>
<textarea name="apiToken" id="apiToken" rows="3" required placeholder="Paste your Canvas API token here"></textarea>
<button type="button" onclick="testCanvas()" style="background:#28a745;margin:5px 0;">Test Canvas Token</button>
<div id="canvasResult" style="padding:8px;margin:5px 0;border-radius:5px;display:none;"></div>
</div>
<div class="section">
<h3>Timezone</h3>
<label>Your Timezone
<select name="timezone" style="width:100%;padding:10px;">
<option value="EST5EDT,M3.2.0/2,M11.1.0/2|US Eastern">US Eastern (EST/EDT)</option>
<option value="CST6CDT,M3.2.0/2,M11.1.0/2|US Central">US Central (CST/CDT)</option>
<option value="MST7MDT,M3.2.0/2,M11.1.0/2|US Mountain">US Mountain (MST/MDT)</option>
<option value="PST8PDT,M3.2.0/2,M11.1.0/2|US Pacific">US Pacific (PST/PDT)</option>
<option value="AKST9AKDT,M3.2.0/2,M11.1.0/2|Alaska">Alaska (AKST/AKDT)</option>
<option value="HST10|Hawaii">Hawaii (HST)</option>
<option value="AST4ADT,M3.2.0/2,M11.1.0/2|Atlantic">Atlantic (AST/ADT)</option>
<option value="GMT0BST,M3.5.0/1,M10.5.0/2|UK">UK (GMT/BST)</option>
<option value="CET-1CEST,M3.5.0/2,M10.5.0/3|Central Europe">Central Europe (CET/CEST)</option>
<option value="EET-2EEST,M3.5.0/3,M10.5.0/4|Eastern Europe">Eastern Europe (EET/EEST)</option>
<option value="MSK-3|Moscow">Moscow (MSK)</option>
<option value="GST-4|Gulf">Gulf (GST)</option>
<option value="PKT-5|Pakistan">Pakistan (PKT)</option>
<option value="IST-5:30|India">India (IST)</option>
<option value="BST-6|Bangladesh">Bangladesh (BST)</option>
<option value="ICT-7|Indochina">Indochina (ICT)</option>
<option value="CST-8|China">China (CST)</option>
<option value="JST-9|Japan">Japan (JST)</option>
<option value="KST-9|Korea">Korea (KST)</option>
<option value="AEST-10AEDT,M10.1.0/2,M4.1.0/3|Australia East">Australia East (AEST/AEDT)</option>
<option value="ACST-9:30ACDT,M10.1.0/2,M4.1.0/3|Australia Central">Australia Central (ACST/ACDT)</option>
<option value="AWST-8|Australia West">Australia West (AWST)</option>
<option value="NZST-12NZDT,M9.5.0/2,M4.1.0/3|New Zealand">New Zealand (NZST/NZDT)</option>
<option value="UTC0|UTC">UTC (Universal)</option>
</select>
</label>
</div>
<div class="section">
<h3>LED Settings</h3>
<label>Red LED (days ahead)<input type="number" name="redDays" id="redDays" value="0" min="0" max="7"></label>
<label>Yellow LED (days ahead)<input type="number" name="yellowDays" id="yellowDays" value="1" min="0" max="14"></label>
<div class="error" id="daysError">Yellow days must be greater than or equal to red days</div>
<label>Brightness (10-255)<input type="number" name="maxBrightness" value="100" min="10" max="255"></label>
</div>
<div class="section">
<h3>Access Point Password (optional)</h3>
<label>AP Password (leave blank for no password)<input type="text" name="apPassword" placeholder="canvas123"></label>
</div>
<button type="submit">Save & Continue</button>
</form></div>
<script>
if(/Mobi|Android/i.test(navigator.userAgent)){document.querySelectorAll('.show-pass').forEach(e=>e.style.display='block');}
function togglePass(btn){let inp=btn.previousElementSibling;inp.type=inp.type==='password'?'text':'password';btn.textContent=inp.type==='password'?'SHOW':'HIDE';}
function validateSetup(){
  let red=parseInt(document.getElementById('redDays').value);
  let yellow=parseInt(document.getElementById('yellowDays').value);
  let err=document.getElementById('daysError');
  if(yellow<red){err.style.display='block';return false;}
  err.style.display='none';return true;
}
function scanWifi(){
  let s=document.getElementById('ssidSelect'),dl=document.getElementById('ssidList'),dl2=document.getElementById('ssid2List');
  let sb=document.getElementById('scanBtn'),st=document.getElementById('scanStatus');
  s.placeholder='Scanning...';if(sb)sb.disabled=true;
  if(st)st.textContent='Scanning for networks...';
  let opts={};
  try{opts.signal=AbortSignal.timeout(15000);}catch(e){}
  fetch('/scan',opts).then(r=>r.json()).then(data=>{
    let nets=data.networks||data;
    dl.innerHTML='';dl2.innerHTML='';
    if(data.error){
      if(st)st.textContent='Scan issue: '+(data.errorDetail||data.error)+' Type your network name manually.';
      s.placeholder='Type network name';if(sb)sb.disabled=false;return;
    }
    if(!nets||nets.length===0){
      if(st)st.textContent='No networks found. Type your network name manually.';
      s.placeholder='Type network name';if(sb)sb.disabled=false;return;
    }
    nets.forEach(n=>{
      let sig=n.rssi>-50?'Strong':n.rssi>-60?'Good':n.rssi>-70?'Fair':'Weak';
      let lbl=n.ssid+' ('+sig+(n.secure?', secured':'')+')';
      dl.innerHTML+='<option value="'+n.ssid+'">'+lbl+'</option>';
      dl2.innerHTML+='<option value="'+n.ssid+'">'+lbl+'</option>';
    });
    s.placeholder='Select or type network name';
    if(st)st.textContent='Found '+nets.length+' network(s). You can also type a name manually.';
    if(sb)sb.disabled=false;
  }).catch(e=>{
    s.placeholder='Type network name';
    if(st)st.textContent='Scan failed (timeout or connection error). Type your network name manually.';
    if(sb)sb.disabled=false;
  });
}
function testWifi(){
  let ssid=document.getElementById('ssidSelect').value;
  let pass=document.getElementById('wifiPass').value;
  let r=document.getElementById('wifiResult');
  r.style.display='block';r.style.background='#f0f0f0';r.textContent='Testing...';
  fetch('/test-wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'ssid='+encodeURIComponent(ssid)+'&password='+encodeURIComponent(pass)})
  .then(x=>x.json()).then(d=>{r.style.background=d.success?'#d4edda':'#f8d7da';r.textContent=d.message;})
  .catch(()=>{r.style.background='#f8d7da';r.textContent='Test failed';});
}
function testCanvas(){
  let token=document.getElementById('apiToken').value;
  let r=document.getElementById('canvasResult');
  r.style.display='block';r.style.background='#f0f0f0';r.textContent='Testing...';
  fetch('/test-canvas',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'token='+encodeURIComponent(token)})
  .then(x=>x.json()).then(d=>{r.style.background=d.success?'#d4edda':'#f8d7da';r.textContent=d.message;})
  .catch(()=>{r.style.background='#f8d7da';r.textContent='Test failed';});
}
document.addEventListener('DOMContentLoaded',scanWifi);
</script></body></html>
)rawliteral";

const char SETTINGS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>Canvas LED Settings</title>
<style>
body{font-family:Arial;max-width:600px;margin:20px auto;padding:20px;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;}
.container{background:white;padding:30px;border-radius:10px;box-shadow:0 10px 40px rgba(0,0,0,0.2);}
h1{color:#667eea;text-align:center;margin-bottom:5px;}
.status{text-align:center;padding:10px;background:#f0f0f0;border-radius:5px;margin:15px 0;font-size:14px;}
.status span{font-weight:bold;}
.alert{padding:10px;margin:10px 0;border-radius:5px;text-align:center;}
.alert-warning{background:#fff3cd;border:1px solid #ffc107;color:#856404;}
.alert-error{background:#f8d7da;border:1px solid #f44336;color:#721c24;}
input,textarea,select{width:100%;padding:10px;margin:5px 0;box-sizing:border-box;border:1px solid #ddd;border-radius:5px;}
.pass-wrap{position:relative;}
.pass-wrap input:hover{-webkit-text-security:none !important;}
.show-pass{position:absolute;right:10px;top:15px;cursor:pointer;display:none;}
button{color:white;padding:12px;border:none;cursor:pointer;width:100%;margin:5px 0;border-radius:5px;font-size:15px;}
.btn-save{background:linear-gradient(135deg,#667eea,#764ba2);}
.btn-reboot{background:#ff9800;}
.btn-reset{background:#f44336;}
button:hover{opacity:0.9;}
.section{border:1px solid #e0e0e0;padding:15px;margin:15px 0;border-radius:8px;background:#f9f9f9;}
.section h3{margin-top:0;color:#555;}
label{display:block;margin:8px 0;font-size:14px;}
.error{color:#d32f2f;font-size:13px;margin-top:5px;display:none;}
</style></head><body><div class="container">
<h1>Canvas LED Settings</h1>
<div class="status">
Device: <span>%DEVICE_NAME%</span> | WiFi: %WIFI_STATUS% | Assignment: <span>%ASSIGNMENT_STATUS%</span> | Checked: <span>%LAST_CHECK%</span>
</div>
%ERROR_ALERT%
%ASSIGNMENTS_SECTION%
<form method="POST" action="/save" onsubmit="return validateSettings()">
<div class="section"><h3>WiFi</h3>
<label>Primary SSID<input type="text" name="ssid" value="%SSID%"></label>
<label>Password<div class="pass-wrap"><input type="password" name="password" value="%PASSWORD%" class="pwd"><span class="show-pass" onclick="togglePass(this)">SHOW</span></div></label>
<label>Backup SSID<input type="text" name="ssid2" value="%SSID2%"></label>
<label>Backup Password<div class="pass-wrap"><input type="password" name="password2" value="%PASSWORD2%" class="pwd"><span class="show-pass" onclick="togglePass(this)">SHOW</span></div></label>
</div>
<div class="section"><h3>Canvas API</h3>
<label>API Token<textarea name="apiToken" rows="3">%API_TOKEN%</textarea></label>
</div>
<div class="section"><h3>Timezone</h3>
<label>Your Timezone
<select name="timezone">
<option value="EST5EDT,M3.2.0/2,M11.1.0/2|US Eastern">US Eastern (EST/EDT)</option>
<option value="CST6CDT,M3.2.0/2,M11.1.0/2|US Central">US Central (CST/CDT)</option>
<option value="MST7MDT,M3.2.0/2,M11.1.0/2|US Mountain">US Mountain (MST/MDT)</option>
<option value="PST8PDT,M3.2.0/2,M11.1.0/2|US Pacific">US Pacific (PST/PDT)</option>
<option value="AKST9AKDT,M3.2.0/2,M11.1.0/2|Alaska">Alaska (AKST/AKDT)</option>
<option value="HST10|Hawaii">Hawaii (HST)</option>
<option value="AST4ADT,M3.2.0/2,M11.1.0/2|Atlantic">Atlantic (AST/ADT)</option>
<option value="GMT0BST,M3.5.0/1,M10.5.0/2|UK">UK (GMT/BST)</option>
<option value="CET-1CEST,M3.5.0/2,M10.5.0/3|Central Europe">Central Europe (CET/CEST)</option>
<option value="EET-2EEST,M3.5.0/3,M10.5.0/4|Eastern Europe">Eastern Europe (EET/EEST)</option>
<option value="MSK-3|Moscow">Moscow (MSK)</option>
<option value="GST-4|Gulf">Gulf (GST)</option>
<option value="PKT-5|Pakistan">Pakistan (PKT)</option>
<option value="IST-5:30|India">India (IST)</option>
<option value="BST-6|Bangladesh">Bangladesh (BST)</option>
<option value="ICT-7|Indochina">Indochina (ICT)</option>
<option value="CST-8|China">China (CST)</option>
<option value="JST-9|Japan">Japan (JST)</option>
<option value="KST-9|Korea">Korea (KST)</option>
<option value="AEST-10AEDT,M10.1.0/2,M4.1.0/3|Australia East">Australia East (AEST/AEDT)</option>
<option value="ACST-9:30ACDT,M10.1.0/2,M4.1.0/3|Australia Central">Australia Central (ACST/ACDT)</option>
<option value="AWST-8|Australia West">Australia West (AWST)</option>
<option value="NZST-12NZDT,M9.5.0/2,M4.1.0/3|New Zealand">New Zealand (NZST/NZDT)</option>
<option value="UTC0|UTC">UTC (Universal)</option>
</select>
</label>
<small style="color:#666;">Current: %TIMEZONE%</small>
</div>
<div class="section"><h3>LED Settings</h3>
<label><input type="checkbox" name="useFlashing" %FLASH_CHECKED%> Use Pulsing Effect</label>
<label>Brightness (10-255)<input type="number" name="maxBrightness" value="%MAX_BRIGHTNESS%" min="10" max="255"></label>
<label>Flash Interval (ms)<input type="number" name="flashInterval" value="%FLASH_INT%" min="1"></label>
<label>Flash Step<input type="number" name="flashStep" value="%FLASH_STEP%" min="1"></label>
<label>Red LED (days ahead)<input type="number" name="redDays" id="redDays" value="%RED_DAYS%" min="0" max="7"></label>
<label>Yellow LED (days ahead)<input type="number" name="yellowDays" id="yellowDays" value="%YELLOW_DAYS%" min="0" max="14"></label>
<div class="error" id="daysError">Yellow days must be greater than or equal to red days</div>
</div>
<div class="section"><h3>Quiet Hours</h3>
<label><input type="checkbox" name="quietHours" %QUIET_CHECKED%> Enable Quiet Hours</label>
<label>Start Hour (0-23)<input type="number" name="quietStart" value="%QUIET_START%" min="0" max="23"></label>
<label>End Hour (0-23)<input type="number" name="quietEnd" value="%QUIET_END%" min="0" max="23"></label>
</div>
<div class="section"><h3>System</h3>
<label>Device Name<input type="text" name="deviceName" value="%DEVICE_NAME%"></label>
<label>Check Interval (minutes)<input type="number" name="fetchInterval" value="%FETCH_INT%" min="1"></label>
<label><input type="checkbox" name="includeOverdue" %INCLUDE_OVERDUE_CHECKED%> Include Overdue Assignments</label>
<small style="color:#666;">⚠️ Note: If you use browser extensions like BetterCanvas to mark assignments complete, they may not sync with Canvas API. Leave this OFF to avoid seeing old completed assignments.</small>
<label>AP Password<input type="text" name="apPassword" value="%AP_PASSWORD%"></label>
<label><input type="checkbox" name="bugReport" %BUG_REPORT_CHECKED%> Enable Auto Bug Reports</label>
<small style="color:#666;">Automatically report critical errors to GitHub for faster support</small>
<label><input type="checkbox" name="debug" %DEBUG_CHECKED%> Debug Mode</label>
</div>
<div class="section"><h3>Fleet Dashboard (optional)</h3>
<label>Dashboard URL<input type="text" name="dashboardUrl" value="%DASHBOARD_URL%" placeholder="https://your-app.railway.app"></label>
<small style="color:#666;">Your deployed canvas-led-dashboard server. Leave blank to disable telemetry.</small>
<label>Dashboard API Key<input type="text" name="dashboardApiKey" value="%DASHBOARD_API_KEY%" placeholder="Optional — matches DASHBOARD_API_KEY env var on server"></label>
</div>
<button type="submit" class="btn-save">Save Settings</button>
<button type="button" class="btn-refresh" onclick="manualRefresh()" style="background:#17a2b8;">Check Canvas Now</button>
<div id="refreshResult" style="padding:8px;margin:5px 0;border-radius:5px;display:none;"></div>
<button type="button" class="btn-reboot" onclick="if(confirm('Reboot device?'))fetch('/reboot',{method:'POST'});">Reboot</button>
<button type="button" class="btn-reset" onclick="if(confirm('FACTORY RESET? All settings erased!'))fetch('/factory-reset',{method:'POST'});">Factory Reset</button>
<div style="margin-top:12px;padding:10px;background:#1a2a1a;border-radius:6px;border:1px solid #2e5f2e;">
<b style="color:#7fff7f;">Snooze Alerts</b><br>
<small style="color:#999;">Temporarily force the LED green (suppress red/yellow) for N hours.</small><br>
<select id="snoozeHours" style="margin-top:6px;padding:4px;background:#222;color:#ccc;border:1px solid #444;border-radius:4px;">
<option value="1">1 hour</option><option value="2">2 hours</option><option value="4">4 hours</option>
<option value="8">8 hours</option><option value="12">12 hours</option><option value="24">24 hours</option>
</select>
<button type="button" style="margin-left:8px;background:#2e7d32;color:#fff;border:none;padding:5px 14px;border-radius:4px;cursor:pointer;" onclick="activateSnooze()">Snooze</button>
<span id="snoozeResult" style="margin-left:8px;font-size:0.9em;color:#7fff7f;"></span>
</div>
</form></div>
<script>
if(/Mobi|Android/i.test(navigator.userAgent)){document.querySelectorAll('.show-pass').forEach(e=>e.style.display='block');}
function togglePass(btn){let inp=btn.previousElementSibling;inp.type=inp.type==='password'?'text':'password';btn.textContent=inp.type==='password'?'SHOW':'HIDE';}
function validateSettings(){
  let red=parseInt(document.getElementById('redDays').value);
  let yellow=parseInt(document.getElementById('yellowDays').value);
  let err=document.getElementById('daysError');
  if(yellow<red){err.style.display='block';return false;}
  err.style.display='none';return true;
}
function manualRefresh(){
  let r=document.getElementById('refreshResult');
  r.style.display='block';r.style.background='#f0f0f0';r.textContent='Checking Canvas...';
  fetch('/refresh',{method:'POST'}).then(x=>x.json()).then(d=>{
    r.style.background=d.success?'#d4edda':'#f8d7da';
    r.textContent=d.statusName+(d.changed?' (changed!)':'');
    setTimeout(()=>location.reload(),2000);
  }).catch(()=>{r.style.background='#f8d7da';r.textContent='Refresh failed';});
}
function activateSnooze(){
  let h=document.getElementById('snoozeHours').value;
  let r=document.getElementById('snoozeResult');
  fetch('/snooze?hours='+h,{method:'POST'}).then(x=>x.text()).then(msg=>{
    r.textContent=msg;
  }).catch(()=>{r.style.color='#ff6b6b';r.textContent='Snooze failed';});
}
</script></body></html>
)rawliteral";

// ============================================
// ERROR HELPER
// ============================================
String getErrorMessage() {
  switch(currentErrorCode) {
    case ERR_WIFI_DISCONNECT:
      return "WiFi Disconnected";
    case ERR_CANVAS_AUTH:
      return "Canvas Auth Error (401 - Check Token)";
    case ERR_CANVAS_SERVER:
      return "Canvas Server Error (500+)";
    case ERR_TIME_SYNC:
      return "Time Sync Failed";
    case ERR_MEMORY_LOW:
      return "Memory Critical (<15KB)";
    case ERR_JSON_PARSE:
      return "JSON Parse Failed";
    case ERR_BUFFER_EXHAUSTED:
      return "Too Many Assignments (Buffer Full)";
    default:
      return "ERROR - Check Device";
  }
}

// ============================================
// WEB HANDLERS
// ============================================
void handleRoot() {
  if (systemConfig.setupComplete) {
    server.sendHeader("Location", "/settings");
    server.send(302);
  } else {
    String html = FPSTR(WELCOME_HTML);
    html.replace("%FW_VERSION%", FIRMWARE_VERSION);
    server.send(200, "text/html", html);
  }
}

void handleSettings() {
  String html = FPSTR(SETTINGS_HTML);
  html.replace("%DEVICE_NAME%", systemConfig.deviceName);
  html.replace("%WIFI_STATUS%", WiFi.status() == WL_CONNECTED ? "â—" : "â—‹");

  String statusText = "None";
  if (assignmentStatus == 0) {
    // Provide specific error message based on error code
    switch(currentErrorCode) {
      case ERR_WIFI_DISCONNECT:
        statusText = "WiFi Disconnected";
        break;
      case ERR_CANVAS_AUTH:
        statusText = "Canvas Auth Error (Check Token)";
        break;
      case ERR_CANVAS_SERVER:
        statusText = "Canvas Server Error";
        break;
      case ERR_TIME_SYNC:
        statusText = "Time Sync Failed";
        break;
      case ERR_MEMORY_LOW:
        statusText = "Memory Critical";
        break;
      case ERR_JSON_PARSE:
        statusText = "JSON Parse Error";
        break;
      case ERR_BUFFER_EXHAUSTED:
        statusText = "Buffer Exhausted (Too Many Assignments)";
        break;
      default:
        statusText = "ERROR - Check Device";
    }
  }
  else if (assignmentStatus == 3) statusText = "Today";
  else if (assignmentStatus == 2) statusText = "Tomorrow";
  else statusText = "All Clear";
  html.replace("%ASSIGNMENT_STATUS%", statusText);

  // Show time since last check and last successful fetch
  String lastCheckText = String((millis() - lastFetch) / 60000) + "m ago";
  if (consecutiveErrors > 0) {
    lastCheckText += " (" + String(consecutiveErrors) + " errors)";
  }
  html.replace("%LAST_CHECK%", lastCheckText);

  // Add error alert if needed
  String errorAlert = "";
  if (consecutiveErrors >= 3) {
    errorAlert = "<div class='alert alert-warning'>âš ï¸ Canvas API experiencing issues (" +
                 String(consecutiveErrors) + " errors). Using last known status.</div>";
  }
  if (!timeSyncComplete) {
    errorAlert += "<div class='alert alert-error'>âŒ Time sync failed. Assignment checks disabled until time is synced.</div>";
  }
  html.replace("%ERROR_ALERT%", errorAlert);

  // Build assignments section
  String assignmentsSection = "";
  if (assignmentCount > 0) {
    assignmentsSection = "<div class='section'><h3>Upcoming Assignments</h3>";
    for (int i = 0; i < assignmentCount; i++) {
      Assignment &a = displayedAssignments[i];
      
      // Format due date
      char dueDateStr[50];
      struct tm due_tm;
      localtime_r(&a.dueTimestamp, &due_tm);
      strftime(dueDateStr, sizeof(dueDateStr), "%a %b %d, %I:%M %p", &due_tm);
      
      // Urgency badge
      String urgencyBadge = a.urgency == 2 ? 
        "<span style='background:#f44336;color:white;padding:3px 8px;border-radius:3px;font-size:12px;'>URGENT</span>" :
        "<span style='background:#ff9800;color:white;padding:3px 8px;border-radius:3px;font-size:12px;'>COMING UP</span>";
      
      assignmentsSection += "<div style='border-left:3px solid " + 
        String(a.urgency == 2 ? "#f44336" : "#ff9800") + 
        ";padding:10px;margin:10px 0;background:#f9f9f9;'>";
      assignmentsSection += urgencyBadge + " <strong>" + a.name + "</strong><br>";
      assignmentsSection += "<small style='color:#666;'>Due: " + String(dueDateStr) + "</small><br>";
      
      if (a.description.length() > 0) {
        assignmentsSection += "<small>" + a.description + "</small><br>";
      }
      
      if (a.htmlUrl.length() > 0) {
        assignmentsSection += "<a href='" + a.htmlUrl + "' target='_blank' style='font-size:12px;'>Open in Canvas</a>";
      }
      
      assignmentsSection += "</div>";
    }
    assignmentsSection += "</div>";
  }
  html.replace("%ASSIGNMENTS_SECTION%", assignmentsSection);

  html.replace("%SSID%", wifiConfig.ssid);
  html.replace("%PASSWORD%", wifiConfig.password);
  html.replace("%SSID2%", wifiConfig.ssid2);
  html.replace("%PASSWORD2%", wifiConfig.password2);
  html.replace("%API_TOKEN%", canvasConfig.apiToken);
  html.replace("%TIMEZONE%", timezoneConfig.displayName);
  html.replace("%FLASH_CHECKED%", ledConfig.useFlashing ? "checked" : "");
  html.replace("%FLASH_INT%", String(ledConfig.flashInterval));
  html.replace("%FLASH_STEP%", String(ledConfig.flashStep));
  html.replace("%MAX_BRIGHTNESS%", String(ledConfig.maxBrightness));
  html.replace("%RED_DAYS%", String(ledConfig.redLEDDaysAhead));
  html.replace("%YELLOW_DAYS%", String(ledConfig.yellowLEDDaysAhead));
  html.replace("%QUIET_CHECKED%", ledConfig.quietHoursEnabled ? "checked" : "");
  html.replace("%QUIET_START%", String(ledConfig.quietHourStart));
  html.replace("%QUIET_END%", String(ledConfig.quietHourEnd));
  html.replace("%FETCH_INT%", String(canvasConfig.fetchInterval / 60000));
  html.replace("%AP_PASSWORD%", systemConfig.apPassword);
  html.replace("%DASHBOARD_URL%", systemConfig.dashboardUrl);
  html.replace("%DASHBOARD_API_KEY%", systemConfig.dashboardApiKey);
  html.replace("%INCLUDE_OVERDUE_CHECKED%", canvasConfig.includeOverdue ? "checked" : "");
  html.replace("%BUG_REPORT_CHECKED%", systemConfig.bugReportEnabled ? "checked" : "");
  html.replace("%DEBUG_CHECKED%", systemConfig.debugMode ? "checked" : "");

  server.send(200, "text/html", html);
}

void handleLogs() {
  String logs = "<pre>";
  for (int i = 0; i < LOG_BUFFER_SIZE; i++) {
    logs += serialLog[(logIndex + i) % LOG_BUFFER_SIZE] + "\n";
  }
  logs += "</pre>";
  server.send(200, "text/html", logs);
}

void handleHealth() {
  StaticJsonDocument<256> doc;
  doc["uptime"] = millis() / 1000;
  doc["wifi_connected"] = WiFi.status() == WL_CONNECTED;
  doc["wifi_ssid"] = WiFi.SSID();
  doc["time_synced"] = timeSyncComplete;
  doc["consecutive_errors"] = consecutiveErrors;
  doc["last_check_ago"] = (millis() - lastFetch) / 1000;
  doc["last_success_ago"] = (millis() - lastSuccessfulFetch) / 1000;
  doc["assignment_status"] = assignmentStatus;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["cpu_temp"] = temperatureRead();

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}


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
function test(type) {
  fetch('/test-trigger?type=' + type)
    .then(r => r.text())
    .then(msg => {
      document.getElementById('status').innerHTML = msg;
    })
    .catch(() => {
      document.getElementById('status').innerHTML = 'Error';
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
  } else if (type == "json") {
    currentErrorCode = ERR_JSON_PARSE;
    assignmentStatus = 0;
    server.send(200, "text/plain", "[ACTIVE] JSON parse - fast flash");
  } else if (type == "buffer") {
    currentErrorCode = ERR_BUFFER_EXHAUSTED;
    assignmentStatus = 0;
    server.send(200, "text/plain", "[ACTIVE] Buffer full - all solid");
  } else if (type == "green") {
    currentErrorCode = ERR_NONE;
    assignmentStatus = 1;
    server.send(200, "text/plain", "[NORMAL] Green LED");
  } else if (type == "yellow") {
    currentErrorCode = ERR_NONE;
    assignmentStatus = 2;
    server.send(200, "text/plain", "[NORMAL] Yellow LED");
  } else if (type == "red") {
    currentErrorCode = ERR_NONE;
    assignmentStatus = 3;
    server.send(200, "text/plain", "[NORMAL] Red LED");
  } else if (type == "clear") {
    currentErrorCode = ERR_NONE;
    assignmentStatus = 1;
    server.send(200, "text/plain", "[CLEARED] Normal operation");
  } else {
    server.send(400, "text/plain", "Unknown type");
  }
}

void handleSnooze() {
  int hours = 1;
  if (server.hasArg(“hours”)) {
    hours = server.arg(“hours”).toInt();
    if (hours < 1) hours = 1;
    if (hours > 24) hours = 24;
  }
  snoozeUntil = millis() + (unsigned long)hours * 3600000UL;
  Serial.printf(“[SNOOZE] Active for %d hour(s)\n”, hours);
  server.send(200, “text/plain”, (“Snooze active for “ + String(hours) + “ hour(s)”).c_str());
}

void handleReboot() {
  Serial.println(“\nðŸ”„ Reboot requested”);
  server.sendHeader(“Connection”, “close”);
  server.send(200, “text/plain”, “Rebooting...”);
  delay(500);
  ESP.restart();
}

void handleFactoryReset() {
  Serial.println("\nâ•”â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•—");
  Serial.println("â•‘ ðŸ­ FACTORY RESET INITIATED â•‘");
  Serial.println("â•šâ•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•");

  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "Factory reset in progress...");
  delay(500);

  server.stop();
  WiFi.disconnect(true);
  delay(100);

  preferences.begin("config", false);
  preferences.clear();
  preferences.end();

  // LED feedback
  for (int i = 0; i < 20; i++) {
    int pins[] = LED_PINS;
    for (int p : pins) digitalWrite(p, HIGH);
    delay(100);
    for (int p : pins) digitalWrite(p, LOW);
    delay(100);
  }

  Serial.println("Factory reset complete! Rebooting...\n");
  delay(1000);
  ESP.restart();
}

void handleSave() {
  if (server.hasArg("ssid")) server.arg("ssid").toCharArray(wifiConfig.ssid, sizeof(wifiConfig.ssid));
  if (server.hasArg("password")) server.arg("password").toCharArray(wifiConfig.password, sizeof(wifiConfig.password));
  if (server.hasArg("ssid2")) {
    server.arg("ssid2").toCharArray(wifiConfig.ssid2, sizeof(wifiConfig.ssid2));
    wifiConfig.useSecondaryNetwork = strlen(wifiConfig.ssid2) > 0;
  }
  if (server.hasArg("password2")) server.arg("password2").toCharArray(wifiConfig.password2, sizeof(wifiConfig.password2));
  if (server.hasArg("apiToken")) server.arg("apiToken").toCharArray(canvasConfig.apiToken, sizeof(canvasConfig.apiToken));

  if (server.hasArg("timezone")) {
    String tz = server.arg("timezone");
    int pipe = tz.indexOf('|');
    if (pipe > 0) {
      tz.substring(0, pipe).toCharArray(timezoneConfig.tzString, sizeof(timezoneConfig.tzString));
      tz.substring(pipe + 1).toCharArray(timezoneConfig.displayName, sizeof(timezoneConfig.displayName));
    }
  }

  ledConfig.useFlashing = server.hasArg("useFlashing");
  if (server.hasArg("flashInterval")) ledConfig.flashInterval = server.arg("flashInterval").toInt();
  if (server.hasArg("flashStep")) ledConfig.flashStep = server.arg("flashStep").toInt();
  if (server.hasArg("maxBrightness")) ledConfig.maxBrightness = constrain(server.arg("maxBrightness").toInt(), 10, 255);

  // Validate and set LED days with constraint
  if (server.hasArg("redDays") && server.hasArg("yellowDays")) {
    int redDays = constrain(server.arg("redDays").toInt(), 0, 7);
    int yellowDays = constrain(server.arg("yellowDays").toInt(), 0, 14);

    // Ensure yellow >= red
    if (yellowDays < redDays) {
      yellowDays = redDays;
    }

    ledConfig.redLEDDaysAhead = redDays;
    ledConfig.yellowLEDDaysAhead = yellowDays;
  }

  ledConfig.quietHoursEnabled = server.hasArg("quietHours");
  if (server.hasArg("quietStart")) ledConfig.quietHourStart = server.arg("quietStart").toInt();
  if (server.hasArg("quietEnd")) ledConfig.quietHourEnd = server.arg("quietEnd").toInt();

  if (server.hasArg("deviceName")) server.arg("deviceName").toCharArray(systemConfig.deviceName, sizeof(systemConfig.deviceName));
  if (server.hasArg("fetchInterval")) canvasConfig.fetchInterval = server.arg("fetchInterval").toInt() * 60UL * 1000UL;
  if (server.hasArg("apPassword")) server.arg("apPassword").toCharArray(systemConfig.apPassword, sizeof(systemConfig.apPassword));
  if (server.hasArg("dashboardUrl")) server.arg("dashboardUrl").toCharArray(systemConfig.dashboardUrl, sizeof(systemConfig.dashboardUrl));
  if (server.hasArg("dashboardApiKey")) server.arg("dashboardApiKey").toCharArray(systemConfig.dashboardApiKey, sizeof(systemConfig.dashboardApiKey));

  canvasConfig.includeOverdue = server.hasArg("includeOverdue");
  systemConfig.bugReportEnabled = server.hasArg("bugReport");
  systemConfig.debugMode = server.hasArg("debug");
  systemConfig.setupComplete = true;
  saveConfig();

  // Detect if accessed via AP or local WiFi
  IPAddress clientIP = server.client().remoteIP();
  IPAddress apIP = WiFi.softAPIP();
  bool isFromAP = (clientIP[0] == apIP[0] && clientIP[1] == apIP[1] && clientIP[2] == apIP[2]);

  if (isFromAP) {
    // From AP - show simple success message that auto-closes
    String confirmHtml = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width'>";
    confirmHtml += "<style>body{font-family:Arial;max-width:600px;margin:50px auto;padding:20px;background:linear-gradient(135deg,#667eea,#764ba2);min-height:100vh;text-align:center;}";
    confirmHtml += ".container{background:white;padding:40px;border-radius:10px;box-shadow:0 10px 40px rgba(0,0,0,0.2);}";
    confirmHtml += "h1{color:#4CAF50;font-size:48px;margin:0;}p{font-size:18px;color:#666;}</style></head><body><div class='container'>";
    confirmHtml += "<h1>âœ“</h1><h2>Settings Saved!</h2><p>Device is rebooting...<br>You can close this window.</p>";
    confirmHtml += "<p>Access settings at:<br><b>http://" + WiFi.localIP().toString() + "</b></p>";
    confirmHtml += "</div><script>setTimeout(function(){window.close();},3000);</script></body></html>";

    server.sendHeader("Connection", "close");
    server.send(200, "text/html", confirmHtml);
    delay(2000);
    ESP.restart();
  } else {
    // From local WiFi - show confirmation page with reboot button
    String confirmHtml = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width'>";
    confirmHtml += "<style>body{font-family:Arial;max-width:600px;margin:20px auto;padding:20px;background:linear-gradient(135deg,#667eea,#764ba2);min-height:100vh;}";
    confirmHtml += ".container{background:white;padding:30px;border-radius:10px;box-shadow:0 10px 40px rgba(0,0,0,0.2);text-align:center;}";
    confirmHtml += "h1{color:#4CAF50;}input{width:100%;padding:10px;margin:10px 0;font-size:16px;border:2px solid #667eea;border-radius:5px;text-align:center;}";
    confirmHtml += "button{background:linear-gradient(135deg,#667eea,#764ba2);color:white;padding:12px 30px;border:none;cursor:pointer;border-radius:5px;font-size:16px;margin:10px;}";
    confirmHtml += ".note{background:#d4edda;padding:15px;border-radius:5px;margin:20px 0;border:2px solid #4CAF50;color:#155724;}</style></head><body><div class='container'>";
    confirmHtml += "<h1>Settings Saved Successfully!</h1>";
    confirmHtml += "<div class='note'>Your changes have been saved.<br>Reboot the device to apply all changes.</div>";

    String localUrl = "http://" + WiFi.localIP().toString();
    confirmHtml += "<p>Bookmark this URL:<br><input type='text' id='url' value='" + localUrl + "' readonly onclick='this.select();document.execCommand(\"copy\");alert(\"Copied!\");'></p>";
    confirmHtml += "<button onclick='doReboot()'>Reboot Now</button>";
    confirmHtml += "<button onclick='location.href=\"/settings\"'>Back to Settings</button>";
    confirmHtml += "<script>function doReboot(){if(confirm('Reboot device now?')){fetch('/reboot',{method:'POST'}).then(()=>{document.body.innerHTML='<div class=\"container\"><h2>Rebooting...</h2><p>Please wait 10 seconds then refresh the page.</p></div>';});}}</script>";
    confirmHtml += "</div></body></html>";

    server.send(200, "text/html", confirmHtml);
  }
}

// ============================================
// NEW API HANDLERS: WiFi Scan, Test, Refresh
// ============================================
void handleScan() {
  Serial.println("📡 Scanning WiFi networks...");

  // Clear any previous scan results
  WiFi.scanDelete();
  delay(100);

  // Attempt scan with retries (AP+STA mode can fail intermittently)
  int n = -1;
  for (int attempt = 0; attempt < 3 && n < 0; attempt++) {
    if (attempt > 0) {
      Serial.printf("  Scan retry %d/3...\n", attempt + 1);
      delay(500);
    }
    n = WiFi.scanNetworks(false, false, false, 300);  // sync, no hidden, no passive, 300ms/channel
    Serial.printf("  Scan attempt %d result: %d\n", attempt + 1, n);
  }

  StaticJsonDocument<2048> doc;
  JsonObject root = doc.to<JsonObject>();
  JsonArray networks = root.createNestedArray("networks");

  if (n < 0) {
    root["error"] = (n == -1) ? "scan_failed" : "scan_busy";
    root["errorDetail"] = (n == -1)
      ? "WiFi scan returned no results after 3 attempts. Try re-scanning or type your network name manually."
      : "A scan is already in progress. Please wait a moment and try again.";
    Serial.printf("  ❌ Scan failed with code: %d after retries\n", n);
  } else if (n == 0) {
    root["error"] = "no_networks";
    root["errorDetail"] = "No networks found. Your network may be hidden or out of range.";
    Serial.println("  ⚠️ No networks found");
  } else {
    Serial.printf("  ✅ Found %d raw networks, deduplicating...\n", n);
    for (int i = 0; i < n && i < 20; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;  // Skip hidden networks

      // Check for duplicate SSID (same network on multiple channels)
      bool isDuplicate = false;
      for (size_t j = 0; j < networks.size(); j++) {
        if (networks[j]["ssid"].as<String>() == ssid) {
          if (WiFi.RSSI(i) > networks[j]["rssi"].as<int>()) {
            networks[j]["rssi"] = WiFi.RSSI(i);
          }
          isDuplicate = true;
          break;
        }
      }

      if (!isDuplicate && networks.size() < 15) {
        JsonObject net = networks.createNestedObject();
        net["ssid"] = ssid;
        net["rssi"] = WiFi.RSSI(i);
        net["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
      }
    }
    Serial.printf("  📋 Returning %d unique networks\n", networks.size());
  }

  root["count"] = networks.size();

  WiFi.scanDelete();

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleTestWifi() {
  if (!server.hasArg("ssid") || !server.hasArg("password")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing parameters\"}");
    return;
  }
  
  String testSsid = server.arg("ssid");
  String testPass = server.arg("password");
  
  Serial.printf("🔌 Testing WiFi: %s\n", testSsid.c_str());
  
  // Disconnect current connection temporarily
  WiFi.disconnect();
  delay(100);
  
  WiFi.begin(testSsid.c_str(), testPass.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }
  
  bool success = WiFi.status() == WL_CONNECTED;
  String msg = success ? "Connected successfully! Signal: " + String(WiFi.RSSI()) + " dBm" 
                       : "Connection failed. Check password.";
  
  // Disconnect test connection
  WiFi.disconnect();
  delay(100);
  
  // Restart AP mode
  WiFi.softAP(systemConfig.deviceName);
  
  String response = "{\"success\":" + String(success ? "true" : "false") + ",\"message\":\"" + msg + "\"}";
  server.send(200, "application/json", response);
}

void handleTestCanvas() {
  if (!server.hasArg("token")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing token\"}");
    return;
  }
  
  String testToken = server.arg("token");
  Serial.println("🎓 Testing Canvas token...");
  
  // Need WiFi to test Canvas - check if we have a stored connection
  if (WiFi.status() != WL_CONNECTED) {
    // Try to connect briefly
    if (strlen(wifiConfig.ssid) > 0) {
      WiFi.begin(wifiConfig.ssid, wifiConfig.password);
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 10) {
        delay(500);
        attempts++;
      }
    }
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    server.send(200, "application/json", "{\"success\":false,\"message\":\"Need WiFi connection first. Test WiFi above.\"}");
    return;
  }
  
  WiFiClientSecure testClient;
  testClient.setInsecure();
  HTTPClient testHttp;
  testHttp.setTimeout(15000);
  
  String testUrl = String(canvasConfig.apiUrl) + "?per_page=1";
  
  if (testHttp.begin(testClient, testUrl)) {
    testHttp.addHeader("Authorization", "Bearer " + testToken);
    testHttp.addHeader("Accept", "application/json");
    
    int httpCode = testHttp.GET();
    testHttp.end();
    
    String msg, successStr;
    if (httpCode == 200) {
      msg = "Token valid! Canvas connection working.";
      successStr = "true";
    } else if (httpCode == 401) {
      msg = "Invalid token. Generate a new one in Canvas.";
      successStr = "false";
    } else {
      msg = "Canvas error (HTTP " + String(httpCode) + "). Try again.";
      successStr = "false";
    }
    
    server.send(200, "application/json", "{\"success\":" + successStr + ",\"message\":\"" + msg + "\"}");
  } else {
    server.send(200, "application/json", "{\"success\":false,\"message\":\"Could not connect to Canvas server.\"}");
  }
}

void handleRefresh() {
  Serial.println("Manual refresh requested");
  int oldStatus = assignmentStatus;
  assignmentStatus = fetchCanvasAssignments();
  lastFetch = millis();
  
  String statusName = assignmentStatus == 0 ? getErrorMessage() :
                      assignmentStatus == 3 ? "RED (due today)" : 
                      assignmentStatus == 2 ? "YELLOW (due soon)" : "GREEN (all clear)";
  
  String response = "{\"success\":true,\"status\":" + String(assignmentStatus) + 
                    ",\"statusName\":\"" + statusName + 
                    "\",\"changed\":" + String(oldStatus != assignmentStatus ? "true" : "false") + "}";
  server.send(200, "application/json", response);
}

// ============================================
// SERVER STARTUP
// ============================================
void startWebServer() {
  server.on("/", handleRoot);
  server.on("/settings", handleSettings);
  server.on("/logs", handleLogs);
  server.on("/health", handleHealth);
  server.on("/test", handleTestMode);
  server.on("/test-trigger", handleTestTrigger);
  server.on("/scan", handleScan);
  server.on("/test-wifi", HTTP_POST, handleTestWifi);
  server.on("/test-canvas", HTTP_POST, handleTestCanvas);
  server.on("/refresh", HTTP_POST, handleRefresh);
  server.on("/snooze", HTTP_POST, handleSnooze);
  server.on("/reboot", HTTP_POST, handleReboot);
  server.on("/factory-reset", HTTP_POST, handleFactoryReset);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleRoot);

  server.begin();
  webServerRunning = true;
  Serial.println("âœ… Web server started");
}

void startSettingsAP() {
  Serial.println("ðŸ”§ Starting Settings AP");
  WiFi.mode(WIFI_AP_STA);

  bool apStarted = systemConfig.setupComplete
    ? WiFi.softAP(systemConfig.deviceName, systemConfig.apPassword)
    : WiFi.softAP(systemConfig.deviceName);

  if (!apStarted) {
    Serial.println("âŒ Failed to start AP!");
    return;
  }

  Serial.println("ðŸ“¡ AP: " + String(systemConfig.deviceName));
  Serial.println("ðŸŒ AP URL: http://" + WiFi.softAPIP().toString());

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  if (!webServerRunning) startWebServer();
  Serial.println("âœ“ Settings AP ready");
}

void monitorSystem() {
  static unsigned long lastTempCheck = 0, lastPowerCheck = 0;
  unsigned long now = millis();

  // Temperature check every 5 minutes
  if (now - lastTempCheck >= 300000) {
    float temp = temperatureRead();
    Serial.printf("ðŸŒ¡ï¸ Temp: %.1fÂ°C%s\n", temp, temp > 80 ? " âš ï¸ HIGH" : (temp > 70 ? " (warm)" : ""));
    lastTempCheck = now;
  }

  // Power estimate every 5 minutes
  if (now - lastPowerCheck >= 300000) {
    // Power calculation removed - analogRead() conflicts with WiFi on ADC2 pins (GPIO 25/27)
    lastPowerCheck = now;
  }
}
