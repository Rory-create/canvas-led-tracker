#pragma once
#include <pgmspace.h>

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
<p class="meta">Firmware v%FW_VERSION%</p>
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
