<button class="err" onclick="test('memory')">Memory (Quad)</button>
<button class="err" onclick="test('json')">JSON (Fast 200ms)</button>
<button class="err" onclick="test('buffer')">Buffer (All Solid)</button>

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
