#pragma once
#include <pgmspace.h>

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
.btn-refresh{background:rgba(23,162,184,.22);color:#17a2b8;border:1px solid rgba(23,162,184,.35);}
.btn-refresh:hover{background:rgba(23,162,184,.32);}
.btn-reboot{background:rgba(255,152,0,.2);color:#ff9800;border:1px solid rgba(255,152,0,.35);}
.btn-reboot:hover{background:rgba(255,152,0,.28);}
.btn-reset{background:rgba(239,68,68,.2);color:var(--red);border:1px solid rgba(239,68,68,.35);}
.btn-reset:hover{background:rgba(239,68,68,.28);}
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
.toast{position:fixed;top:20px;left:50%;transform:translateX(-50%);background:var(--gr);color:#fff;padding:12px 24px;border-radius:8px;font-size:14px;font-weight:600;z-index:999;opacity:0;transition:opacity .3s;pointer-events:none;white-space:nowrap;}
.toast.show{opacity:1;}
</style></head>
<body><div class="wrap">
<div id="toast" class="toast">&#10003; Settings saved</div>
<div class="logo"><div class="dot"></div>Due Light</div>
<div class="status-bar">
Device: <span>%DEVICE_NAME%</span>
WiFi: %WIFI_STATUS%
Assignment: <span>%ASSIGNMENT_STATUS%</span>
Checked: <span>%LAST_CHECK%</span>
Firmware: <span>%FW_VERSION%</span>
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
<label><input type="checkbox" name="bugReport" %BUG_REPORT_CHECKED%> Send automatic error reports</label>
<small>When the device hits a critical error, it sends a brief report to help diagnose issues. No personal data is included. Disable if you prefer full privacy.</small>
<label><input type="checkbox" name="debug" %DEBUG_CHECKED%> Verbose serial logging</label>
<small>Prints detailed Canvas API and assignment parsing info over USB serial. Useful for troubleshooting — leave on unless you need to reduce serial noise.</small>
</div>
<button type="submit" class="btn btn-save">Save Settings</button>
<button type="button" class="btn btn-refresh" onclick="manualRefresh()">Check Canvas Now</button>
<div id="refreshResult" class="result"></div>
<button type="button" class="btn btn-reboot" onclick="if(confirm('Reboot device?'))fetch('/reboot',{method:'POST'});">Reboot</button>
<button type="button" class="btn btn-reset" onclick="if(confirm('FACTORY RESET? All settings erased!'))fetch('/factory-reset',{method:'POST'});">Factory Reset</button>
<small style="display:block;margin-top:4px;color:var(--muted);">No access to this page? Press the small button in the bottom left corner 5 times, pausing 1 second between each press — all LEDs will flash to confirm.</small>
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
if(new URLSearchParams(location.search).get('saved')==='1'){
  var t=document.getElementById('toast');
  t.classList.add('show');
  setTimeout(function(){t.classList.remove('show');},3500);
  history.replaceState(null,'','/settings');
}
</script></body></html>
)rawliteral";
