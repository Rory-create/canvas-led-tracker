require('dotenv').config();
const express = require('express');
const fs = require('fs');
const path = require('path');
const { exec } = require('child_process');

const app = express();
const PORT = process.env.PORT || 3000;

// Optional shared secret — set DASHBOARD_API_KEY env var on server.
// Devices must send the same value in the X-API-Key header.
// If not set, the server accepts all requests (fine for private deploys).
const API_KEY = process.env.DASHBOARD_API_KEY || null;

// Human-friendly dashboard password — set DASHBOARD_PASSWORD env var.
// On correct password, server issues a random session token stored server-side.
// Tokens expire after 30 days. Rotating the password invalidates all sessions.
const DASHBOARD_PASSWORD = process.env.DASHBOARD_PASSWORD || null;
const crypto = require('crypto');
const _sessions = new Map(); // token → expiry timestamp
const SESSION_TTL_MS = 30 * 24 * 60 * 60 * 1000; // 30 days

function createSession() {
  const token = crypto.randomBytes(32).toString('base64url');
  _sessions.set(token, Date.now() + SESSION_TTL_MS);
  return token;
}
function isValidSession(token) {
  if (!token) return false;
  const expiry = _sessions.get(token);
  if (!expiry) return false;
  if (Date.now() > expiry) { _sessions.delete(token); return false; }
  return true;
}
// Clean up expired sessions hourly
setInterval(() => {
  const now = Date.now();
  for (const [token, expiry] of _sessions) {
    if (now > expiry) _sessions.delete(token);
  }
}, 60 * 60 * 1000);

// Discord webhook for alerting — set DISCORD_WEBHOOK_URL env var to enable
const DISCORD_WEBHOOK = process.env.DISCORD_WEBHOOK_URL || null;

const DATA_DIR = path.join(__dirname, 'data');
const UNITS_FILE = path.join(DATA_DIR, 'units.json');
const BUGS_FILE = path.join(DATA_DIR, 'bugs.json');

// ── Helpers ────────────────────────────────────────────────────────────────

function readJSON(file, fallback) {
  try { return JSON.parse(fs.readFileSync(file, 'utf8')); }
  catch { return fallback; }
}

// Serialised write queue — prevents race-condition data loss when multiple
// devices POST telemetry simultaneously.
let _writing = false;
const _writeQueue = [];
function writeJSON(file, data) {
  if (!fs.existsSync(DATA_DIR)) fs.mkdirSync(DATA_DIR, { recursive: true });
  _writeQueue.push({ file, data });
  if (!_writing) _flushQueue();
}
function _flushQueue() {
  if (!_writeQueue.length) { _writing = false; return; }
  _writing = true;
  const { file, data } = _writeQueue.shift();
  try { fs.writeFileSync(file, JSON.stringify(data, null, 2)); }
  catch (e) { console.error('writeJSON error:', e.message); }
  setImmediate(_flushQueue);
}

// Bug ID: timestamp + random suffix to prevent millisecond collisions
function newBugId() {
  return Date.now() * 1000 + Math.floor(Math.random() * 1000);
}

function authMiddleware(req, res, next) {
  if (!API_KEY) return next();
  const key = req.headers['x-api-key'];
  if (key !== API_KEY) return res.status(401).json({ error: 'Unauthorized' });
  next();
}

// Same as authMiddleware but also allows browser sessions via a dashboard session token.
// Used on read endpoints so the dashboard UI can still load when API_KEY is set.
// Devices posting telemetry use authMiddleware (stricter); browser reads use this.
const DASHBOARD_TOKEN = process.env.DASHBOARD_READ_TOKEN || null;

function isLocalhost(req) {
  if (req.headers['cf-connecting-ip']) return false; // came through Cloudflare Tunnel
  const ip = req.socket.remoteAddress;
  return ip === '127.0.0.1' || ip === '::1' || ip === '::ffff:127.0.0.1';
}

function readAuthMiddleware(req, res, next) {
  // Requests from localhost (the host PC) are always allowed
  if (isLocalhost(req)) return next();
  // If no password is configured, allow all reads (open/private deploy)
  if (!DASHBOARD_PASSWORD) return next();
  // Browser access requires a valid session token (obtained via POST /api/login)
  const key = req.headers['x-api-key'];
  if (isValidSession(key)) return next();
  return res.status(401).json({ error: 'Unauthorized' });
}

// Simple per-IP rate limiting — max 60 requests per minute per IP
const _rateCounts = new Map();
function rateLimitMiddleware(req, res, next) {
  const ip = req.headers['x-forwarded-for'] || req.socket.remoteAddress;
  const now = Date.now();
  const entry = _rateCounts.get(ip) || { count: 0, reset: now + 60000 };
  if (now > entry.reset) { entry.count = 0; entry.reset = now + 60000; }
  entry.count++;
  _rateCounts.set(ip, entry);
  if (entry.count > 60) return res.status(429).json({ error: 'Too many requests' });
  next();
}
// Clean up stale rate-limit entries every 5 minutes
setInterval(() => {
  const now = Date.now();
  for (const [ip, entry] of _rateCounts) {
    if (now > entry.reset) _rateCounts.delete(ip);
  }
}, 5 * 60 * 1000);

const STALE_MS = 30 * 60 * 1000; // 30 minutes offline = stale

// ── Discord alerting ───────────────────────────────────────────────────────

async function sendDiscordAlert(message) {
  if (!DISCORD_WEBHOOK) return;
  try {
    const response = await fetch(DISCORD_WEBHOOK, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ content: message }),
    });
    if (!response.ok) console.error('Discord webhook failed:', response.status);
  } catch (e) {
    console.error('Discord webhook error:', e.message);
  }
}

// Check all units for stale/error conditions and fire alerts (throttled to 1/unit/incident)
function checkAndAlert(units) {
  if (!DISCORD_WEBHOOK) return;
  const now = Date.now();
  let changed = false;

  for (const unit of units) {
    const name = unit.device_name || unit.device_id;
    const isStale = (now - new Date(unit.last_seen).getTime()) > STALE_MS;
    const hasError = unit.error_code > 0 || unit.consecutive_errors > 2;

    // Stale alert — fire once per offline incident (reset when it comes back)
    if (isStale && !unit._alerted_stale) {
      unit._alerted_stale = true;
      changed = true;
      sendDiscordAlert(`⚠️ **${name}** has gone offline (no check-in for >30 min) — last seen ${unit.last_seen}`);
    } else if (!isStale && unit._alerted_stale) {
      unit._alerted_stale = false;
      changed = true;
      sendDiscordAlert(`✅ **${name}** is back online`);
    }

    // Error alert — fire once per new error code
    if (hasError && unit._alerted_error_code !== unit.error_code) {
      unit._alerted_error_code = unit.error_code;
      changed = true;
      const ERROR_NAMES = ['None','WiFi Disconnect','Canvas Auth','Canvas Server','Time Sync','Memory Low','JSON Parse','Buffer Exhausted'];
      const errName = ERROR_NAMES[unit.error_code] || `Code ${unit.error_code}`;
      sendDiscordAlert(`🔴 **${name}** reporting error: **${errName}** (×${unit.consecutive_errors}) — firmware v${unit.firmware_version}`);
    } else if (!hasError && unit._alerted_error_code > 0) {
      unit._alerted_error_code = 0;
      changed = true;
      sendDiscordAlert(`✅ **${name}** error cleared`);
    }
  }

  return changed;
}

// ── Middleware ─────────────────────────────────────────────────────────────

app.use(express.json({ limit: '64kb' }));

// Host-based routing: due-light.com (and www.) → marketing page; setup.due-light.com → setup guide; everything else → dashboard
app.get('/', (req, res, next) => {
  const host = (req.headers.host || '').split(':')[0].toLowerCase();
  if (host === 'due-light.com' || host === 'www.due-light.com') {
    return res.sendFile(path.join(__dirname, 'public', 'home.html'));
  }
  if (host === 'setup.due-light.com') {
    return res.sendFile(path.join(__dirname, 'public', 'setup.html'));
  }
  next();
});

app.use(express.static(path.join(__dirname, 'public')));

// POST /api/login — exchange password for a session token
app.post('/api/login', rateLimitMiddleware, (req, res) => {
  const { password } = req.body || {};
  if (!DASHBOARD_PASSWORD) return res.status(503).json({ error: 'Password auth not configured' });
  if (!password || password !== DASHBOARD_PASSWORD) {
    return res.status(401).json({ error: 'Incorrect password' });
  }
  const token = createSession();
  res.json({ token });
});

// GET /api/whoami — lets the browser check if it's on localhost (auto-auth)
app.get('/api/whoami', (req, res) => {
  res.json({ localhost: isLocalhost(req) });
});

// ── Device API ─────────────────────────────────────────────────────────────

// POST /api/telemetry  — heartbeat from a device
app.post('/api/telemetry', rateLimitMiddleware, authMiddleware, (req, res) => {
  console.log(`[telemetry] POST from ${req.headers['cf-connecting-ip'] || req.socket.remoteAddress} — device_id: ${req.body && req.body.device_id}`);
  const body = req.body;
  if (!body || !body.device_id) {
    return res.status(400).json({ error: 'device_id required' });
  }

  const units = readJSON(UNITS_FILE, []);
  const now = new Date().toISOString();

  const existing = units.findIndex(u => u.device_id === body.device_id);
  const prev = existing >= 0 ? units[existing] : {};

  const record = {
    // Preserve operator-set fields from previous record
    notes: prev.notes || '',
    _alerted_stale: prev._alerted_stale || false,
    _alerted_error_code: prev._alerted_error_code || 0,
    // Device-reported fields
    device_id: body.device_id,
    device_name: body.device_name || body.device_id,
    firmware_version: body.firmware_version || 'unknown',
    setup_complete: !!body.setup_complete,
    last_seen: now,
    first_seen: prev.first_seen || now,
    uptime_seconds: Number(body.uptime_seconds) || 0,
    free_heap: Number(body.free_heap) || 0,
    wifi_rssi: Number(body.wifi_rssi) || 0,
    cpu_temp: Number(body.cpu_temp) || 0,
    assignment_status: Number.isInteger(body.assignment_status) ? body.assignment_status : -1,
    error_code: Number.isInteger(body.error_code) ? Math.max(0, Math.min(body.error_code, 7)) : 0,
    consecutive_errors: Math.max(0, Number(body.consecutive_errors) || 0),
    time_synced: !!body.time_synced,
    ota_version_seen: typeof body.ota_version_seen === 'string' ? body.ota_version_seen : null,
    ip_address: req.headers['x-forwarded-for'] || req.socket.remoteAddress,
  };

  if (existing >= 0) {
    units[existing] = record;
  } else {
    units.push(record);
  }

  checkAndAlert(units);
  writeJSON(UNITS_FILE, units);
  res.json({ ok: true, unit_count: units.length });
});

// POST /api/bug  — bug report from a device
app.post('/api/bug', rateLimitMiddleware, authMiddleware, (req, res) => {
  const body = req.body;
  if (!body || !body.device_id) {
    return res.status(400).json({ error: 'device_id required' });
  }

  const bugs = readJSON(BUGS_FILE, []);
  const bugId = newBugId();
  bugs.unshift({
    id: bugId,
    device_id: body.device_id,
    device_name: body.device_name || body.device_id,
    firmware_version: body.firmware_version || 'unknown',
    error_code: Number.isInteger(body.error_code) ? body.error_code : 0,
    error_name: typeof body.error_name === 'string' ? body.error_name : 'UNKNOWN',
    title: typeof body.title === 'string' ? body.title.slice(0, 200) : 'Bug Report',
    diagnostics: body.diagnostics && typeof body.diagnostics === 'object' ? body.diagnostics : {},
    timestamp: new Date().toISOString(),
    resolved: false,
  });

  // Keep last 500 bug reports
  if (bugs.length > 500) bugs.splice(500);
  writeJSON(BUGS_FILE, bugs);

  // Discord alert for new bug report
  const name = body.device_name || body.device_id;
  sendDiscordAlert(`🐛 **Bug report from ${name}**: ${body.title || body.error_name || 'Unknown error'} (firmware v${body.firmware_version || '?'})`);

  res.json({ ok: true, bug_id: bugId });
});

// ── Dashboard API ──────────────────────────────────────────────────────────

app.get('/api/units', readAuthMiddleware, (req, res) => {
  // Strip internal alert-state fields from public response
  const units = readJSON(UNITS_FILE, []).map(({ _alerted_stale, _alerted_error_code, ...u }) => u);
  res.json(units);
});

app.delete('/api/units/:device_id', authMiddleware, (req, res) => {
  const units = readJSON(UNITS_FILE, []);
  const idx = units.findIndex(u => u.device_id === req.params.device_id);
  if (idx < 0) return res.status(404).json({ error: 'Unit not found' });
  units.splice(idx, 1);
  writeJSON(UNITS_FILE, units);
  res.json({ ok: true });
});

app.get('/api/bugs', readAuthMiddleware, (req, res) => {
  const bugs = readJSON(BUGS_FILE, []);
  const limit = Math.min(parseInt(req.query.limit) || 50, 500);
  const unresolved = req.query.unresolved === 'true';
  const filtered = unresolved ? bugs.filter(b => !b.resolved) : bugs;
  res.json(filtered.slice(0, limit));
});

// Resolve a bug — requires auth if API key is set
app.patch('/api/bugs/:id/resolve', authMiddleware, (req, res) => {
  const bugs = readJSON(BUGS_FILE, []);
  const bug = bugs.find(b => b.id === parseInt(req.params.id));
  if (!bug) return res.status(404).json({ error: 'Not found' });
  bug.resolved = true;
  writeJSON(BUGS_FILE, bugs);
  res.json({ ok: true });
});

// Update per-device notes
app.patch('/api/units/:device_id/notes', authMiddleware, (req, res) => {
  const units = readJSON(UNITS_FILE, []);
  const unit = units.find(u => u.device_id === req.params.device_id);
  if (!unit) return res.status(404).json({ error: 'Unit not found' });
  const notes = typeof req.body.notes === 'string' ? req.body.notes.slice(0, 500) : '';
  unit.notes = notes;
  writeJSON(UNITS_FILE, units);
  res.json({ ok: true, notes });
});

app.get('/api/ota', readAuthMiddleware, (req, res) => {
  const units = readJSON(UNITS_FILE, []);
  const now = Date.now();
  const summary = units.map(u => ({
    device_id: u.device_id,
    device_name: u.device_name,
    firmware_version: u.firmware_version,
    ota_version_seen: u.ota_version_seen,
    last_seen: u.last_seen,
    stale: (now - new Date(u.last_seen).getTime()) > STALE_MS,
  }));
  res.json({ units: summary, total: units.length });
});

app.get('/api/stats', readAuthMiddleware, (req, res) => {
  const units = readJSON(UNITS_FILE, []);
  const bugs = readJSON(BUGS_FILE, []);
  const now = Date.now();

  res.json({
    total_units: units.length,
    activated_units: units.filter(u => u.setup_complete).length,
    online_units: units.filter(u => (now - new Date(u.last_seen).getTime()) < STALE_MS).length,
    error_units: units.filter(u => u.error_code > 0 || u.consecutive_errors > 2).length,
    open_bugs: bugs.filter(b => !b.resolved).length,
    firmware_versions: [...new Set(units.map(u => u.firmware_version))],
  });
});

// POST /api/deploy — pull latest code and restart. Requires API key.
// Hit this from a browser or curl when you can't physically access the machine.
app.post('/api/deploy', authMiddleware, (req, res) => {
  if (!API_KEY) return res.status(503).json({ error: 'API key not configured — deploy endpoint disabled for safety' });
  const repoRoot = path.resolve(__dirname, '..');
  exec(
    'git pull origin claude/duelight-esp32-review-1pTqB && pm2 restart canvas-dashboard',
    { cwd: repoRoot, timeout: 60000 },
    (err, stdout, stderr) => {
      if (err) return res.status(500).json({ error: err.message, stderr });
      res.json({ ok: true, stdout, stderr });
    }
  );
});

// ── Start ──────────────────────────────────────────────────────────────────

app.listen(PORT, () => {
  console.log(`Canvas LED Dashboard running on port ${PORT}`);
  if (!API_KEY) console.log('Note: DASHBOARD_API_KEY not set — device endpoints unprotected');
  if (DASHBOARD_PASSWORD) console.log('Password login enabled');
  else console.log('Note: DASHBOARD_PASSWORD not set — password login disabled');
  if (DISCORD_WEBHOOK) console.log('Discord alerting enabled');
});
