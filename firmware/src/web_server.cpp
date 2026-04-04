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
<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Due Light &mdash; Setup</title>
<style>
:root{--bg:#ddd6c4;--sf:#faf5eb;--bd:#b5a688;--tx:#1c1408;--mt:#5e4e38;--gr:#3d5a2e;--gw:rgba(61,90,46,.3);}
*{box-sizing:border-box;margin:0;padding:0;}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',system-ui,sans-serif;background:var(--bg);color:var(--tx);font-size:15px;line-height:1.6;-webkit-font-smoothing:antialiased;min-height:100vh;}
.wrap{max-width:540px;margin:0 auto;padding:28px 16px 56px;}
.logo{display:flex;align-items:center;gap:10px;font-weight:600;font-size:16px;margin-bottom:6px;}
.dot{width:9px;height:9px;border-radius:50%;background:var(--gr);box-shadow:0 0 8px var(--gw),0 0 16px var(--gw);animation:pulse 3s ease-in-out infinite;}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.55}}
.tag{font-size:12px;font-weight:600;letter-spacing:.12em;text-transform:uppercase;color:var(--gr);margin-bottom:20px;}
.guide-link{display:block;background:rgba(61,90,46,.08);border:1px solid rgba(61,90,46,.25);color:var(--gr);border-radius:8px;padding:10px 14px;margin-bottom:12px;font-size:14px;text-decoration:none;text-align:center;}
.guide-link:hover{background:rgba(61,90,46,.14);}
.meta{text-align:center;color:var(--mt);font-size:12px;margin-bottom:24px;}
.meta a{color:var(--mt);}
.card{background:var(--sf);border:1px solid var(--bd);border-left:3px solid var(--gr);border-radius:12px;padding:22px;margin-bottom:14px;}
.card-title{font-size:12px;font-weight:700;letter-spacing:.1em;text-transform:uppercase;color:var(--gr);margin-bottom:16px;}
label{display:block;font-size:14px;color:var(--mt);font-weight:500;margin:12px 0 4px;}
input[type=text],input[type=password],input[type=number],textarea,select{width:100%;padding:10px 12px;background:var(--bg);border:1px solid var(--bd);border-radius:7px;color:var(--tx);font-size:14px;font-family:inherit;outline:none;transition:border-color .2s;-webkit-appearance:none;}
input:focus,textarea:focus,select:focus{border-color:rgba(77,107,60,.5);}
select option{background:var(--bg);}
.pass-wrap{position:relative;}
.show-pass{position:absolute;right:10px;top:11px;cursor:pointer;display:none;font-size:11px;color:var(--mt);letter-spacing:.05em;}
.btn{display:block;width:100%;padding:13px;border:none;border-radius:8px;font-size:15px;font-weight:600;cursor:pointer;font-family:inherit;margin:6px 0;}
.btn-save{background:var(--gr);color:#fff;}
.btn-save:hover{opacity:.87;}
.btn-test{background:rgba(77,107,60,.1);color:var(--gr);border:1px solid rgba(77,107,60,.25);}
.btn-test:hover{background:rgba(77,107,60,.18);}
.btn-scan{background:rgba(0,0,0,.05);color:var(--mt);border:1px solid var(--bd);}
.btn-scan:hover{background:rgba(0,0,0,.09);}
.result{padding:10px 12px;margin:8px 0;border-radius:7px;font-size:13px;display:none;}
.result.ok{background:rgba(77,107,60,.1);border:1px solid rgba(77,107,60,.25);color:var(--gr);}
.result.err{background:rgba(239,68,68,.1);border:1px solid rgba(239,68,68,.25);color:#ef4444;}
.result.info{background:rgba(255,255,255,.04);border:1px solid var(--bd);color:var(--mt);}
.hint{font-size:12px;color:var(--mt);line-height:1.55;margin:6px 0;}
.hint b{color:var(--tx);}
.steps{padding-left:16px;margin:8px 0 6px;}
.steps li{font-size:13px;color:var(--mt);margin:4px 0;line-height:1.5;}
.steps li b{color:var(--tx);}
.warn{background:rgba(234,179,8,.07);border:1px solid rgba(234,179,8,.25);color:#c8961a;border-radius:7px;padding:10px 12px;font-size:12px;margin:10px 0;}
.scan-st{font-size:12px;color:var(--mt);margin:8px 0;}
.ferr{color:#ef4444;font-size:12px;margin-top:4px;display:none;}
.footer{text-align:center;margin-top:20px;font-size:12px;}
.footer a{color:var(--mt);}
.footer a:hover{color:var(--tx);}
</style></head>
<body><div class="wrap">
<div class="logo"><div class="dot"></div>Due Light</div>
<div class="tag">Initial Setup</div>
<a href="https://setup.due-light.com" target="_blank" class="guide-link">Need help? Step-by-step setup guide &rarr;</a>
<p class="meta">Firmware v%FW_VERSION% &nbsp;&middot;&nbsp; <a href="/health" target="_blank">Health</a> &nbsp;&middot;&nbsp; <a href="/logs" target="_blank">Logs</a></p>
<form method="POST" action="/save" onsubmit="return validateSetup()">
<div class="card">
<div class="card-title">Wi-Fi</div>
<label>Network name<input type="text" name="ssid" id="ssidSelect" list="ssidList" placeholder="Scanning..." autocomplete="off"><datalist id="ssidList"></datalist></label>
<label>Password<div class="pass-wrap"><input type="password" name="password" id="wifiPass" class="pwd"><span class="show-pass" onclick="togglePass(this)">SHOW</span></div></label>
<button type="button" class="btn btn-test" onclick="testWifi()">Test Wi-Fi connection</button>
<div id="wifiResult" class="result"></div>
<label>Backup network <span style="color:var(--mt);font-weight:400;">(optional)</span><input type="text" name="ssid2" id="ssid2Select" list="ssid2List" placeholder="Leave blank if not needed" autocomplete="off"><datalist id="ssid2List"></datalist></label>
<label>Backup password<div class="pass-wrap"><input type="password" name="password2" class="pwd"><span class="show-pass" onclick="togglePass(this)">SHOW</span></div></label>
<p class="scan-st" id="scanStatus"></p>
<button type="button" id="scanBtn" class="btn btn-scan" onclick="scanWifi()">Re-scan networks</button>
</div>
<div class="card">
<div class="card-title">Canvas API Token</div>
<label>School Canvas address<input type="text" name="canvasSchoolUrl" id="canvasSchoolUrl" placeholder="yourschool.instructure.com" value="ojrsd.instructure.com" autocomplete="off"></label>
<p class="hint"><b>How to get your token:</b></p>
<ol class="steps">
<li>Open Canvas and log in as the <b>student</b></li>
<li>Click <b>Account</b> (top-left) &rarr; <b>Settings</b></li>
<li>Scroll to <b>Approved Integrations</b> &rarr; <b>+ New Access Token</b></li>
<li>Name it <b>DueLight</b>, leave expiry blank or as far ahead as allowed</li>
<li>Click <b>Generate Token</b>, copy and paste it below</li>
</ol>
<div class="warn">Tokens expire after about 4 months. DueLight will warn you when it&rsquo;s time to renew.</div>
<label>Paste token here<textarea name="apiToken" id="apiToken" rows="3" required placeholder="Canvas API token"></textarea></label>
<button type="button" class="btn btn-test" onclick="testCanvas()">Test Canvas token</button>
<div id="canvasResult" class="result"></div>
</div>
<div class="card">
<div class="card-title">Timezone</div>
<label>Your timezone
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
</select></label>
</div>
<div class="card">
<div class="card-title">LED Thresholds</div>
<label>Red LED &mdash; days ahead<input type="number" name="redDays" id="redDays" value="0" min="0" max="7"></label>
<label>Yellow LED &mdash; days ahead<input type="number" name="yellowDays" id="yellowDays" value="1" min="0" max="14"></label>
<p class="ferr" id="daysError">Yellow must be &ge; red days</p>
<label>Brightness (10&ndash;255)<input type="number" name="maxBrightness" value="100" min="10" max="255"></label>
</div>
<div class="card">
<div class="card-title">Access Point Password</div>
<label>AP password <span style="color:var(--mt);font-weight:400;">(leave blank for open)</span><input type="text" name="apPassword" placeholder="canvas123"></label>
</div>
<button type="submit" class="btn btn-save">Save &amp; Continue</button>
</form>
<div class="footer"><a href="https://setup.due-light.com" target="_blank">Setup guide &amp; FAQ</a></div>
</div>
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
function setResult(id,ok,msg){let r=document.getElementById(id);r.style.display='block';r.className='result '+(ok?'ok':'err');r.textContent=msg;}
function scanWifi(){
  let s=document.getElementById('ssidSelect'),dl=document.getElementById('ssidList'),dl2=document.getElementById('ssid2List');
  let sb=document.getElementById('scanBtn'),st=document.getElementById('scanStatus');
  s.placeholder='Scanning...';if(sb)sb.disabled=true;
  if(st)st.textContent='Scanning for networks...';
  let opts={};try{opts.signal=AbortSignal.timeout(15000);}catch(e){}
  fetch('/scan',opts).then(r=>r.json()).then(data=>{
    let nets=data.networks||data;dl.innerHTML='';dl2.innerHTML='';
    if(data.error){if(st)st.textContent='Scan issue: '+(data.errorDetail||data.error)+'. Type name manually.';s.placeholder='Type network name';if(sb)sb.disabled=false;return;}
    if(!nets||nets.length===0){if(st)st.textContent='No networks found. Type manually.';s.placeholder='Type network name';if(sb)sb.disabled=false;return;}
    nets.forEach(n=>{
      let sig=n.rssi>-50?'Strong':n.rssi>-60?'Good':n.rssi>-70?'Fair':'Weak';
      let lbl=n.ssid+' ('+sig+(n.secure?', secured':'')+')';
      dl.innerHTML+='<option value="'+n.ssid+'">'+lbl+'</option>';
      dl2.innerHTML+='<option value="'+n.ssid+'">'+lbl+'</option>';
    });
    s.placeholder='Select or type network name';
    if(st)st.textContent='Found '+nets.length+' network(s).';
    if(sb)sb.disabled=false;
  }).catch(()=>{s.placeholder='Type network name';if(st)st.textContent='Scan failed. Type manually.';if(sb)sb.disabled=false;});
}
function testWifi(){
  let ssid=document.getElementById('ssidSelect').value,pass=document.getElementById('wifiPass').value;
  let r=document.getElementById('wifiResult');r.style.display='block';r.className='result info';r.textContent='Testing...';
  fetch('/test-wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'ssid='+encodeURIComponent(ssid)+'&password='+encodeURIComponent(pass)})
  .then(x=>x.json()).then(d=>{setResult('wifiResult',d.success,d.message);}).catch(()=>{setResult('wifiResult',false,'Test failed');});
}
function testCanvas(){
  let token=document.getElementById('apiToken').value;
  let schoolUrl=document.getElementById('canvasSchoolUrl')?document.getElementById('canvasSchoolUrl').value:'';
  let ssid=document.getElementById('ssidSelect')?document.getElementById('ssidSelect').value:'';
  let pass=document.getElementById('wifiPass')?document.getElementById('wifiPass').value:'';
  let r=document.getElementById('canvasResult');r.style.display='block';r.className='result info';r.textContent='Testing...';
  let body='token='+encodeURIComponent(token);
  if(schoolUrl)body+='&schoolUrl='+encodeURIComponent(schoolUrl);
  if(ssid)body+='&ssid='+encodeURIComponent(ssid);
  if(pass)body+='&wifiPass='+encodeURIComponent(pass);
  fetch('/test-canvas',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})
  .then(x=>x.json()).then(d=>{setResult('canvasResult',d.success,d.message);}).catch(()=>{setResult('canvasResult',false,'Test failed');});
}
document.addEventListener('DOMContentLoaded',scanWifi);
</script></body></html>
)rawliteral";

const char SETTINGS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Due Light &mdash; Settings</title>
<style>
:root{--bg:#ddd6c4;--sf:#faf5eb;--bd:#b5a688;--tx:#1c1408;--mt:#5e4e38;--gr:#3d5a2e;--gw:rgba(61,90,46,.3);--red:#ef4444;}
*{box-sizing:border-box;margin:0;padding:0;}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',system-ui,sans-serif;background:var(--bg);color:var(--tx);font-size:15px;line-height:1.6;-webkit-font-smoothing:antialiased;min-height:100vh;}
.wrap{max-width:600px;margin:0 auto;padding:28px 16px 56px;}
.logo{display:flex;align-items:center;gap:10px;font-weight:600;font-size:16px;margin-bottom:14px;}
.dot{width:9px;height:9px;border-radius:50%;background:var(--gr);box-shadow:0 0 8px var(--gw),0 0 16px var(--gw);animation:pulse 3s ease-in-out infinite;}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.55}}
.status-bar{background:var(--sf);border:1px solid var(--bd);border-left:3px solid var(--gr);border-radius:8px;padding:12px 16px;margin-bottom:14px;font-size:14px;color:var(--mt);display:flex;flex-wrap:wrap;gap:6px 20px;}
.status-bar span{color:var(--tx);font-weight:500;}
.alert{padding:12px 16px;margin-bottom:14px;border-radius:8px;font-size:14px;}
.alert-warn{background:rgba(234,179,8,.08);border:1px solid rgba(234,179,8,.35);color:#8a6200;}
.alert-err{background:rgba(239,68,68,.08);border:1px solid rgba(239,68,68,.4);color:#b91c1c;}
.card{background:var(--sf);border:1px solid var(--bd);border-left:3px solid var(--gr);border-radius:12px;padding:22px;margin-bottom:14px;}
.card-title{font-size:12px;font-weight:700;letter-spacing:.1em;text-transform:uppercase;color:var(--gr);margin-bottom:16px;}
label{display:block;font-size:14px;color:var(--mt);font-weight:500;margin:12px 0 4px;}
input[type=text],input[type=password],input[type=number],textarea,select{width:100%;padding:10px 12px;background:var(--bg);border:1px solid var(--bd);border-radius:7px;color:var(--tx);font-size:14px;font-family:inherit;outline:none;transition:border-color .2s;-webkit-appearance:none;}
input:focus,textarea:focus,select:focus{border-color:rgba(77,107,60,.5);}
select option{background:var(--bg);}
input[type=checkbox]{width:auto;accent-color:var(--gr);margin-right:6px;}
.pass-wrap{position:relative;}
.show-pass{position:absolute;right:10px;top:11px;cursor:pointer;display:none;font-size:11px;color:var(--mt);letter-spacing:.05em;}
small{font-size:12px;color:var(--mt);line-height:1.5;display:block;margin-top:4px;}
.btn{display:block;width:100%;padding:13px;border:none;border-radius:8px;font-size:15px;font-weight:600;cursor:pointer;font-family:inherit;margin:6px 0;}
.btn-save{background:var(--gr);color:#fff;}
.btn-save:hover{opacity:.87;}
.btn-refresh{background:rgba(23,162,184,.12);color:#17a2b8;border:1px solid rgba(23,162,184,.25);}
.btn-refresh:hover{background:rgba(23,162,184,.2);}
.btn-reboot{background:rgba(255,152,0,.1);color:#ff9800;border:1px solid rgba(255,152,0,.25);}
.btn-reboot:hover{background:rgba(255,152,0,.18);}
.btn-reset{background:rgba(239,68,68,.1);color:var(--red);border:1px solid rgba(239,68,68,.25);}
.btn-reset:hover{background:rgba(239,68,68,.18);}
.result{padding:10px 12px;margin:8px 0;border-radius:7px;font-size:13px;display:none;}
.result.ok{background:rgba(77,107,60,.1);border:1px solid rgba(77,107,60,.25);color:var(--gr);}
.result.err{background:rgba(239,68,68,.1);border:1px solid rgba(239,68,68,.25);color:var(--red);}
.ferr{color:var(--red);font-size:12px;margin-top:4px;display:none;}
.snooze-card{background:rgba(77,107,60,.08);border:1px solid rgba(77,107,60,.25);border-radius:12px;padding:18px 20px;margin:6px 0;}
.snooze-label{font-size:14px;font-weight:700;color:var(--gr);}
.snooze-sub{font-size:11px;color:var(--mt);margin-top:2px;}
.snooze-status{margin-top:8px;font-size:12px;color:var(--mt);}
.snooze-controls{display:flex;align-items:center;gap:8px;margin-top:10px;flex-wrap:wrap;}
.snooze-controls select{width:auto;padding:6px 10px;background:var(--bg);color:var(--mt);border:1px solid var(--bd);border-radius:6px;font-size:13px;}
.snooze-btn{background:var(--gr);color:#fff;border:none;padding:7px 16px;border-radius:6px;cursor:pointer;font-size:13px;font-family:inherit;}
.snooze-btn:hover{opacity:.87;}
.cancel-snooze{background:rgba(239,68,68,.1);color:#dc2626;border:1px solid rgba(239,68,68,.25);padding:7px 16px;border-radius:6px;cursor:pointer;font-size:13px;font-family:inherit;display:none;}
.cancel-snooze:hover{background:rgba(239,68,68,.18);}
.snooze-result{font-size:12px;color:var(--gr);}
.footer{text-align:center;margin-top:24px;font-size:12px;}
.footer a{color:var(--mt);}
.footer a:hover{color:var(--tx);}
</style></head>
<body><div class="wrap">
<div class="logo"><div class="dot"></div>Due Light</div>
<div class="status-bar">
Device: <span>%DEVICE_NAME%</span>
WiFi: %WIFI_STATUS%
Assignment: <span>%ASSIGNMENT_STATUS%</span>
Checked: <span>%LAST_CHECK%</span>
</div>
%ERROR_ALERT%
%ASSIGNMENTS_SECTION%
<form method="POST" action="/save" onsubmit="return validateSettings()">
<div class="card">
<div class="card-title">Wi-Fi</div>
<label>Primary network<input type="text" name="ssid" value="%SSID%"></label>
<label>Password<div class="pass-wrap"><input type="password" name="password" value="%PASSWORD%" class="pwd"><span class="show-pass" onclick="togglePass(this)">SHOW</span></div></label>
<label>Backup network<input type="text" name="ssid2" value="%SSID2%"></label>
<label>Backup password<div class="pass-wrap"><input type="password" name="password2" value="%PASSWORD2%" class="pwd"><span class="show-pass" onclick="togglePass(this)">SHOW</span></div></label>
</div>
<div class="card">
<div class="card-title">Canvas API</div>
%TOKEN_ALERT%
<label>Canvas school URL<input type="text" name="canvasSchoolUrl" value="%CANVAS_SCHOOL_URL%"></label>
<label>API token<textarea name="apiToken" rows="3">%API_TOKEN%</textarea></label>
%TOKEN_STATUS%
</div>
<div class="card">
<div class="card-title">Timezone</div>
<label>Your timezone
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
</select></label>
<small>Current: %TIMEZONE%</small>
</div>
<div class="card">
<div class="card-title">LED Settings</div>
<label><input type="checkbox" name="useFlashing" %FLASH_CHECKED%> Use pulsing effect</label>
<label>Brightness (10&ndash;255)<input type="number" name="maxBrightness" value="%MAX_BRIGHTNESS%" min="10" max="255"></label>
<label>Flash interval (ms)<input type="number" name="flashInterval" value="%FLASH_INT%" min="1"></label>
<label>Flash step<input type="number" name="flashStep" value="%FLASH_STEP%" min="1"></label>
<label>Red LED &mdash; days ahead<input type="number" name="redDays" id="redDays" value="%RED_DAYS%" min="0" max="7"></label>
<label>Yellow LED &mdash; days ahead<input type="number" name="yellowDays" id="yellowDays" value="%YELLOW_DAYS%" min="0" max="14"></label>
<p class="ferr" id="daysError">Yellow must be &ge; red days</p>
</div>
<div class="card">
<div class="card-title">Quiet Hours</div>
<label><input type="checkbox" name="quietHours" %QUIET_CHECKED%> Enable quiet hours</label>
<label>Start hour (0&ndash;23)<input type="number" name="quietStart" value="%QUIET_START%" min="0" max="23"></label>
<label>End hour (0&ndash;23)<input type="number" name="quietEnd" value="%QUIET_END%" min="0" max="23"></label>
</div>
<div class="card">
<div class="card-title">System</div>
<label>Device name<input type="text" name="deviceName" value="%DEVICE_NAME%"></label>
<label>Check interval (minutes)<input type="number" name="fetchInterval" value="%FETCH_INT%" min="1"></label>
<label><input type="checkbox" name="includeOverdue" %INCLUDE_OVERDUE_CHECKED%> Include overdue assignments</label>
<small>If you use browser extensions like BetterCanvas, leave this OFF to avoid seeing old completed assignments.</small>
<label>AP password<input type="text" name="apPassword" value="%AP_PASSWORD%"></label>
<label><input type="checkbox" name="bugReport" %BUG_REPORT_CHECKED%> Enable auto bug reports</label>
<small>Automatically report critical errors to GitHub for faster support.</small>
<label><input type="checkbox" name="debug" %DEBUG_CHECKED%> Debug mode</label>
</div>
<button type="submit" class="btn btn-save">Save Settings</button>
<button type="button" class="btn btn-refresh" onclick="manualRefresh()">Check Canvas Now</button>
<div id="refreshResult" class="result"></div>
<button type="button" class="btn btn-reboot" onclick="if(confirm('Reboot device?'))fetch('/reboot',{method:'POST'});">Reboot</button>
<button type="button" class="btn btn-reset" onclick="if(confirm('FACTORY RESET? All settings erased!'))fetch('/factory-reset',{method:'POST'});">Factory Reset</button>
<div class="snooze-card">
<div class="snooze-label">Snooze Alerts</div>
<div class="snooze-sub">Temporarily force the LED green for N hours.</div>
<div class="snooze-status" id="snoozeStatus"></div>
<div class="snooze-controls">
<select id="snoozeHours">
<option value="1">1 hour</option><option value="2">2 hours</option><option value="4">4 hours</option>
<option value="8">8 hours</option><option value="12">12 hours</option><option value="24">24 hours</option>
</select>
<button type="button" class="snooze-btn" onclick="activateSnooze()">Snooze</button>
<button type="button" id="cancelSnoozeBtn" class="cancel-snooze" onclick="cancelSnooze()">Cancel Snooze</button>
<span id="snoozeResult" class="snooze-result"></span>
</div>
</div>
</form>
<div class="footer"><a href="https://setup.due-light.com" target="_blank">Setup guide &amp; FAQ</a></div>
</div>
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
  r.style.display='block';r.className='result';r.style.background='rgba(255,255,255,.04)';r.style.color='var(--mt)';r.textContent='Checking Canvas...';
  fetch('/refresh',{method:'POST'}).then(x=>x.json()).then(d=>{
    r.className='result '+(d.success?'ok':'err');
    r.textContent=d.statusName+(d.changed?' (changed!)':'');
    setTimeout(()=>location.reload(),2000);
  }).catch(()=>{r.className='result err';r.textContent='Refresh failed';});
}
function activateSnooze(){
  let h=document.getElementById('snoozeHours').value;
  let r=document.getElementById('snoozeResult');
  fetch('/snooze?hours='+h,{method:'POST'}).then(x=>x.text()).then(msg=>{
    r.textContent=msg;
    setTimeout(checkSnoozeStatus,500);
  }).catch(()=>{r.style.color='#ff6b6b';r.textContent='Snooze failed';});
}
function cancelSnooze(){
  fetch('/snooze?cancel=1',{method:'POST'}).then(()=>{
    document.getElementById('snoozeResult').textContent='';
    checkSnoozeStatus();
  });
}
function checkSnoozeStatus(){
  fetch('/snooze/status').then(x=>x.json()).then(d=>{
    let s=document.getElementById('snoozeStatus');
    let btn=document.getElementById('cancelSnoozeBtn');
    if(d.active){s.textContent='Snooze active — '+d.remaining_minutes+' min remaining';s.style.color='#7fff7f';btn.style.display='inline-block';}
    else{s.textContent='No snooze active';s.style.color='var(--mt)';btn.style.display='none';}
  }).catch(()=>{});
}
checkSnoozeStatus();
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
  html.replace("%WIFI_STATUS%", WiFi.status() == WL_CONNECTED ? "" : "");

  String statusText = "None";
  if (assignmentStatus == 0) {
    // Provide specific error message based on error code
    switch(currentErrorCode) {
      case ERR_WIFI_DISCONNECT:
        statusText = "WiFi Disconnected";
        break;
      case ERR_CANVAS_AUTH:
        statusText = "Token expired - visit Settings to renew";
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
  if (currentErrorCode == ERR_CANVAS_AUTH) {
    errorAlert = "<div class='alert alert-error' style='text-align:left;'><b>&#10060; Canvas token expired or invalid.</b><br>"
                 "Follow these steps to fix it:<ol style='margin:8px 0 0 18px;line-height:1.9;'>"
                 "<li>Open Canvas in a browser and log in as the student</li>"
                 "<li>Click <b>Account</b> (top-left) &rarr; <b>Settings</b></li>"
                 "<li>Scroll to <b>Approved Integrations</b> &rarr; click <b>+ New Access Token</b></li>"
                 "<li>Name it <b>DueLight</b>, leave expiry blank (or as far ahead as allowed)</li>"
                 "<li>Click <b>Generate Token</b>, copy it, and paste it in the Canvas API section below</li>"
                 "<li>Click <b>Save Settings</b></li></ol></div>";
  } else if (consecutiveErrors >= 3) {
    errorAlert = "<div class='alert alert-warning'>&#9888; Canvas API experiencing issues (" +
                 String(consecutiveErrors) + " errors). Using last known status.</div>";
  }
  if (!timeSyncComplete) {
    errorAlert += "<div class='alert alert-error'>&#10060; Time sync failed. Assignment checks disabled until time is synced.</div>";
  }
  html.replace("%ERROR_ALERT%", errorAlert);

  // Token age status
  time_t nowTs;
  time(&nowTs);
  String tokenStatus = "";
  String tokenAlert = "";
  if (canvasConfig.tokenLastValidated > 0 && nowTs > 1000000) {
    long tokenAgeDays = ((long)nowTs - (long)canvasConfig.tokenLastValidated) / 86400;
    if (tokenAgeDays < 0) tokenAgeDays = 0;
    if (currentErrorCode != ERR_CANVAS_AUTH) {
      if (tokenAgeDays >= 110) {
        tokenStatus = "<p style='font-size:13px;background:#f8d7da;border:1px solid #f44336;color:#721c24;padding:8px;border-radius:5px;margin:6px 0;'>"
                      "&#128308; Token likely expired (last verified " + String(tokenAgeDays) + " days ago). Generate a new one in Canvas.</p>";
      } else if (tokenAgeDays >= 80) {
        tokenStatus = "<p style='font-size:13px;background:#fff3cd;border:1px solid #ffc107;color:#856404;padding:8px;border-radius:5px;margin:6px 0;'>"
                      "&#9888; Token expires soon (last verified " + String(tokenAgeDays) + " days ago). Consider renewing before it stops working.</p>";
      } else {
        tokenStatus = "<p style='font-size:12px;color:#666;margin:4px 0;'>Last verified " + String(tokenAgeDays) + " day(s) ago &#10003;</p>";
      }
    }
  }
  html.replace("%TOKEN_STATUS%", tokenStatus);
  html.replace("%TOKEN_ALERT%", tokenAlert);

  // Extract school domain from stored API URL
  String canvasSchoolUrl = "";
  String apiUrlStr = String(canvasConfig.apiUrl);
  if (apiUrlStr.startsWith("https://")) {
    String noProto = apiUrlStr.substring(8);
    int slash = noProto.indexOf('/');
    canvasSchoolUrl = (slash > 0) ? noProto.substring(0, slash) : noProto;
  }
  html.replace("%CANVAS_SCHOOL_URL%", canvasSchoolUrl);

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
  // POST /snooze?hours=N  activate snooze
  // POST /snooze?cancel=1  cancel snooze
  if (server.hasArg("cancel") && server.arg("cancel") == "1") {
    snoozeUntil = 0;
    Serial.println("[SNOOZE] Cancelled");
    server.send(200, "text/plain", "Snooze cancelled");
    return;
  }
  int hours = 1;
  if (server.hasArg("hours")) {
    hours = server.arg("hours").toInt();
    if (hours < 1) hours = 1;
    if (hours > 24) hours = 24;
  }
  snoozeUntil = millis() + (unsigned long)hours * 3600000UL;
  Serial.printf("[SNOOZE] Active for %d hour(s)\n", hours);
  server.send(200, "text/plain", ("Snooze active for " + String(hours) + " hour(s)").c_str());
}

void handleSnoozeStatus() {
  unsigned long now = millis();
  if (snoozeUntil == 0 || now >= snoozeUntil) {
    server.send(200, "application/json", "{\"active\":false,\"remaining_minutes\":0}");
  } else {
    unsigned long remainMs = snoozeUntil - now;
    unsigned long remainMin = remainMs / 60000;
    server.send(200, "application/json",
      ("{\"active\":true,\"remaining_minutes\":" + String(remainMin) + "}").c_str());
  }
}

void handleReboot() {
  Serial.println("\nReboot requested");
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "Rebooting...");
  delay(500);
  ESP.restart();
}

void handleFactoryReset() {
  Serial.println("\n========================================");
  Serial.println("  FACTORY RESET INITIATED");
  Serial.println("========================================");

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

  // Build Canvas API URL from school domain field
  if (server.hasArg("canvasSchoolUrl")) {
    String school = server.arg("canvasSchoolUrl");
    school.trim();
    if (school.startsWith("https://")) school = school.substring(8);
    if (school.startsWith("http://")) school = school.substring(7);
    int slash = school.indexOf('/');
    if (slash > 0) school = school.substring(0, slash);
    if (school.length() > 0) {
      String fullUrl = "https://" + school + "/api/v1/users/self/todo";
      fullUrl.toCharArray(canvasConfig.apiUrl, sizeof(canvasConfig.apiUrl));
    }
  }

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
  // dashboardUrl and dashboardApiKey are baked in at build time  not user-configurable

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
    confirmHtml += "<h1>&#10003;</h1><h2>Settings Saved!</h2><p>Device is rebooting...<br>You can close this window.</p>";
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
  Serial.println(" Scanning WiFi networks...");

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
    Serial.printf("   Scan failed with code: %d after retries\n", n);
  } else if (n == 0) {
    root["error"] = "no_networks";
    root["errorDetail"] = "No networks found. Your network may be hidden or out of range.";
    Serial.println("   No networks found");
  } else {
    Serial.printf("   Found %d raw networks, deduplicating...\n", n);
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
    Serial.printf("   Returning %d unique networks\n", networks.size());
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
  
  Serial.printf(" Testing WiFi: %s\n", testSsid.c_str());
  
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
  
  // On failure clean up; on success leave STA connected so Canvas test can reuse it.
  // Do NOT call WiFi.softAP() — the AP was never disrupted (WIFI_AP_STA keeps AP
  // and STA independent). Restarting the AP kills the browser TCP connection
  // before the response is sent, causing the button to hang forever.
  if (!success) WiFi.disconnect(false);
  
  String response = "{\"success\":" + String(success ? "true" : "false") + ",\"message\":\"" + msg + "\"}";
  server.send(200, "application/json", response);
}

void handleTestCanvas() {
  if (!server.hasArg("token")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing token\"}");
    return;
  }
  
  String testToken = server.arg("token");
  Serial.println(" Testing Canvas token...");
  
  // Need WiFi to test Canvas
  if (WiFi.status() != WL_CONNECTED) {
    // Try credentials from the form (setup page) then fall back to stored config
    const char* connectSsid = nullptr;
    const char* connectPass = nullptr;
    String formSsid, formPass;
    if (server.hasArg("ssid") && server.arg("ssid").length() > 0) {
      formSsid = server.arg("ssid");
      formPass = server.hasArg("wifiPass") ? server.arg("wifiPass") : "";
      connectSsid = formSsid.c_str();
      connectPass = formPass.c_str();
    } else if (strlen(wifiConfig.ssid) > 0) {
      connectSsid = wifiConfig.ssid;
      connectPass = wifiConfig.password;
    }
    if (connectSsid) {
      WiFi.begin(connectSsid, connectPass);
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 20) {
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
  
  // Use schoolUrl from form (setup page) or fall back to stored API URL
  String testUrl;
  if (server.hasArg("schoolUrl")) {
    String school = server.arg("schoolUrl");
    school.trim();
    if (school.startsWith("https://")) school = school.substring(8);
    if (school.startsWith("http://")) school = school.substring(7);
    int slash = school.indexOf('/');
    if (slash > 0) school = school.substring(0, slash);
    testUrl = "https://" + school + "/api/v1/users/self/todo?per_page=1";
  } else {
    testUrl = String(canvasConfig.apiUrl) + "?per_page=1";
  }
  
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
  server.on("/snooze/status", HTTP_GET, handleSnoozeStatus);
  server.on("/reboot", HTTP_POST, handleReboot);
  server.on("/factory-reset", HTTP_POST, handleFactoryReset);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleRoot);

  server.begin();
  webServerRunning = true;
  Serial.println(" Web server started");
}

void startSettingsAP() {
  Serial.println("Starting Settings AP");
  WiFi.mode(WIFI_AP_STA);

  bool apStarted = systemConfig.setupComplete
    ? WiFi.softAP(systemConfig.deviceName, systemConfig.apPassword)
    : WiFi.softAP(systemConfig.deviceName);

  if (!apStarted) {
    Serial.println(" Failed to start AP!");
    return;
  }

  Serial.println("AP: " + String(systemConfig.deviceName));
  Serial.println(" AP URL: http://" + WiFi.softAPIP().toString());

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  if (!webServerRunning) startWebServer();
  Serial.println("Settings AP ready");
}

void monitorSystem() {
  static unsigned long lastTempCheck = 0, lastPowerCheck = 0;
  unsigned long now = millis();

  // Temperature check every 5 minutes
  if (now - lastTempCheck >= 300000) {
    float temp = temperatureRead();
    Serial.printf(" Temp: %.1fC%s\n", temp, temp > 80 ? "  HIGH" : (temp > 70 ? " (warm)" : ""));
    lastTempCheck = now;
  }

  // Power estimate every 5 minutes
  if (now - lastPowerCheck >= 300000) {
    // Power calculation removed - analogRead() conflicts with WiFi on ADC2 pins (GPIO 25/27)
    lastPowerCheck = now;
  }
}
