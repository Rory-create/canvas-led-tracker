const express = require('express');
const fs = require('fs');
const path = require('path');

const app = express();
const PORT = process.env.PORT || 3000;

// Optional shared secret — set DASHBOARD_API_KEY env var on server.
// Devices must send the same value in the X-API-Key header.
// If not set, the server accepts all requests (fine for private deploys).
const API_KEY = process.env.DASHBOARD_API_KEY || null;

const DATA_DIR = path.join(__dirname, 'data');
const UNITS_FILE = path.join(DATA_DIR, 'units.json');
const BUGS_FILE = path.join(DATA_DIR, 'bugs.json');

// ── Helpers ────────────────────────────────────────────────────────────────

function readJSON(file, fallback) {
  try { return JSON.parse(fs.readFileSync(file, 'utf8')); }
  catch { return fallback; }
}

function writeJSON(file, data) {
  if (!fs.existsSync(DATA_DIR)) fs.mkdirSync(DATA_DIR, { recursive: true });
  fs.writeFileSync(file, JSON.stringify(data, null, 2));
}

function authMiddleware(req, res, next) {
  if (!API_KEY) return next();
  const key = req.headers['x-api-key'];
  if (key !== API_KEY) return res.status(401).json({ error: 'Unauthorized' });
  next();
}

// ── Middleware ─────────────────────────────────────────────────────────────

app.use(express.json({ limit: '64kb' }));
app.use(express.static(path.join(__dirname, 'public')));

// ── Device API ─────────────────────────────────────────────────────────────

// POST /api/telemetry  — heartbeat from a device
// Body: { device_id, firmware_version, uptime_seconds, free_heap, wifi_rssi,
//         assignment_status, error_code, consecutive_errors, time_synced,
//         setup_complete, cpu_temp, device_name, ota_version_seen }
app.post('/api/telemetry', authMiddleware, (req, res) => {
  const body = req.body;
  if (!body || !body.device_id) {
    return res.status(400).json({ error: 'device_id required' });
  }

  const units = readJSON(UNITS_FILE, []);
  const now = new Date().toISOString();

  const existing = units.findIndex(u => u.device_id === body.device_id);
  const record = {
    device_id: body.device_id,
    device_name: body.device_name || body.device_id,
    firmware_version: body.firmware_version || 'unknown',
    setup_complete: !!body.setup_complete,
    last_seen: now,
    first_seen: existing >= 0 ? units[existing].first_seen : now,
    uptime_seconds: body.uptime_seconds || 0,
    free_heap: body.free_heap || 0,
    wifi_rssi: body.wifi_rssi || 0,
    cpu_temp: body.cpu_temp || 0,
    assignment_status: body.assignment_status ?? -1,
    error_code: body.error_code ?? 0,
    consecutive_errors: body.consecutive_errors || 0,
    time_synced: !!body.time_synced,
    ota_version_seen: body.ota_version_seen || null,
    ip_address: req.headers['x-forwarded-for'] || req.socket.remoteAddress,
  };

  if (existing >= 0) {
    units[existing] = record;
  } else {
    units.push(record);
  }

  writeJSON(UNITS_FILE, units);
  res.json({ ok: true, unit_count: units.length });
});

// POST /api/bug  — bug report from a device
// Body: { device_id, firmware_version, error_code, error_name, title, body, diagnostics }
app.post('/api/bug', authMiddleware, (req, res) => {
  const body = req.body;
  if (!body || !body.device_id) {
    return res.status(400).json({ error: 'device_id required' });
  }

  const bugs = readJSON(BUGS_FILE, []);
  bugs.unshift({
    id: Date.now(),
    device_id: body.device_id,
    device_name: body.device_name || body.device_id,
    firmware_version: body.firmware_version || 'unknown',
    error_code: body.error_code ?? 0,
    error_name: body.error_name || 'UNKNOWN',
    title: body.title || 'Bug Report',
    diagnostics: body.diagnostics || {},
    timestamp: new Date().toISOString(),
    resolved: false,
  });

  // Keep last 500 bug reports
  if (bugs.length > 500) bugs.splice(500);
  writeJSON(BUGS_FILE, bugs);
  res.json({ ok: true, bug_id: bugs[0].id });
});

// ── Dashboard API ──────────────────────────────────────────────────────────

app.get('/api/units', (req, res) => {
  res.json(readJSON(UNITS_FILE, []));
});

app.get('/api/bugs', (req, res) => {
  const bugs = readJSON(BUGS_FILE, []);
  const limit = Math.min(parseInt(req.query.limit) || 50, 500);
  const unresolved = req.query.unresolved === 'true';
  const filtered = unresolved ? bugs.filter(b => !b.resolved) : bugs;
  res.json(filtered.slice(0, limit));
});

app.patch('/api/bugs/:id/resolve', (req, res) => {
  const bugs = readJSON(BUGS_FILE, []);
  const bug = bugs.find(b => b.id === parseInt(req.params.id));
  if (!bug) return res.status(404).json({ error: 'Not found' });
  bug.resolved = true;
  writeJSON(BUGS_FILE, bugs);
  res.json({ ok: true });
});

app.get('/api/ota', (req, res) => {
  // Return OTA system status based on last known unit states
  const units = readJSON(UNITS_FILE, []);
  const now = Date.now();
  const summary = units.map(u => ({
    device_id: u.device_id,
    device_name: u.device_name,
    firmware_version: u.firmware_version,
    ota_version_seen: u.ota_version_seen,
    last_seen: u.last_seen,
    stale: (now - new Date(u.last_seen).getTime()) > 30 * 60 * 1000,
  }));
  res.json({ units: summary, total: units.length });
});

app.get('/api/stats', (req, res) => {
  const units = readJSON(UNITS_FILE, []);
  const bugs = readJSON(BUGS_FILE, []);
  const now = Date.now();
  const STALE_MS = 30 * 60 * 1000; // 30 minutes

  res.json({
    total_units: units.length,
    activated_units: units.filter(u => u.setup_complete).length,
    online_units: units.filter(u => (now - new Date(u.last_seen).getTime()) < STALE_MS).length,
    error_units: units.filter(u => u.error_code > 0 || u.consecutive_errors > 2).length,
    open_bugs: bugs.filter(b => !b.resolved).length,
    firmware_versions: [...new Set(units.map(u => u.firmware_version))],
  });
});

// ── Start ──────────────────────────────────────────────────────────────────

app.listen(PORT, () => {
  console.log(`Canvas LED Dashboard running on port ${PORT}`);
  if (API_KEY) {
    console.log('API key authentication enabled');
  } else {
    console.log('WARNING: No DASHBOARD_API_KEY set — all telemetry requests accepted');
  }
});
