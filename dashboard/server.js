require('dotenv').config();
const express = require('express');
const fs = require('fs');
const path = require('path');
const { exec } = require('child_process');
const Stripe = require('stripe');

const stripe = process.env.STRIPE_SECRET_KEY ? Stripe(process.env.STRIPE_SECRET_KEY) : null;

const app = express();
const PORT = process.env.PORT || 3000;

// Optional shared secret — set DASHBOARD_API_KEY env var on server.
// Devices must send the same value in the X-API-Key header.
// If not set, the server accepts all requests (fine for private deploys).
// SECURITY LOW: API key is static — no rotation mechanism without reflashing all firmware.
// When scaling, consider per-device keys or signed payloads (HMAC) so a single leaked key
// doesn't compromise the whole fleet.
const API_KEY = process.env.DASHBOARD_API_KEY || null;

// Human-friendly dashboard password — set DASHBOARD_PASSWORD env var.
// On correct password, server issues a random session token stored server-side.
// Tokens expire after 7 days. Rotating the password invalidates all sessions.
// SECURITY LOW: sessions are in-memory only — a server restart logs everyone out and
// there is no way to invalidate all sessions without restarting the process.
// Fix when needed: persist sessions to a small SQLite table or signed JWTs.
const DASHBOARD_PASSWORD = process.env.DASHBOARD_PASSWORD || null;
const crypto = require('crypto');
const _sessions = new Map(); // token → expiry timestamp
const SESSION_TTL_MS = 7 * 24 * 60 * 60 * 1000; // 7 days

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

// Dedicated login rate limiter — stricter than the global limiter.
// 10 attempts per 15 minutes per IP. Separate from general traffic so
// a burst of page requests can't exhaust login attempts.
const _loginAttempts = new Map();
function loginRateLimitMiddleware(req, res, next) {
  const ip = req.headers['x-forwarded-for'] || req.socket.remoteAddress;
  const now = Date.now();
  const WINDOW_MS = 15 * 60 * 1000; // 15 minutes
  const MAX     = 10;
  const entry = _loginAttempts.get(ip) || { count: 0, reset: now + WINDOW_MS };
  if (now > entry.reset) { entry.count = 0; entry.reset = now + WINDOW_MS; }
  entry.count++;
  _loginAttempts.set(ip, entry);
  if (entry.count > MAX) {
    const minsLeft = Math.ceil((entry.reset - now) / 60000);
    return res.status(429).json({ error: `Too many login attempts — try again in ${minsLeft} min` });
  }
  next();
}
setInterval(() => {
  const now = Date.now();
  for (const [ip, e] of _loginAttempts) if (now > e.reset) _loginAttempts.delete(ip);
}, 30 * 60 * 1000);

// Discord webhook for alerting — set DISCORD_WEBHOOK_URL env var to enable
const DISCORD_WEBHOOK = process.env.DISCORD_WEBHOOK_URL || null;

const DATA_DIR = path.join(__dirname, 'data');
const UNITS_FILE = path.join(DATA_DIR, 'units.json');
const BUGS_FILE = path.join(DATA_DIR, 'bugs.json');
const STARTS_FILE = path.join(DATA_DIR, 'server-starts.json');
const REVIEWS_FILE   = path.join(DATA_DIR, 'reviews.json');
const ORDERS_FILE    = path.join(DATA_DIR, 'orders.json');
const INVENTORY_FILE = path.join(DATA_DIR, 'inventory.json');
const CAPITAL_FILE   = path.join(DATA_DIR, 'capital.json');

// ── Server metrics ─────────────────────────────────────────────────────────
const os = require('os');

// Ring buffer: one entry per minute, max 480 = 8 hours
const metricsHistory = [];
let _lastCpuUsage = process.cpuUsage();
let _lastCpuTime   = Date.now();
let _reqCount      = 0;

function _sampleMetrics() {
  const cpuDelta   = process.cpuUsage(_lastCpuUsage);
  const timeDelta  = (Date.now() - _lastCpuTime) * 1000; // µs
  const cpuPct     = Math.min(100, parseFloat(((cpuDelta.user + cpuDelta.system) / timeDelta * 100).toFixed(1)));
  _lastCpuUsage    = process.cpuUsage();
  _lastCpuTime     = Date.now();

  const mem = process.memoryUsage();
  metricsHistory.push({
    t:          Date.now(),
    cpu:        cpuPct,
    heapMB:     Math.round(mem.heapUsed / 1048576),
    rssMB:      Math.round(mem.rss / 1048576),
    reqPerMin:  _reqCount,
  });
  if (metricsHistory.length > 480) metricsHistory.shift();
  _reqCount = 0;
}
setInterval(_sampleMetrics, 60_000);

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

// ── Orders helpers ─────────────────────────────────────────────────────────

// Seed data — written once when orders.json doesn't exist yet
const _SEED_ORDERS = [
  { id:'manual_1776988800000', stripe_session_id:null, created_at:'2026-04-24T12:00:00Z', customer_name:'Luca Campanella',   customer_email:'', items:['Due Light','USB-C Cable'], subtotal_cents:2000, shipping_cents:0, total_cents:2000, material_cost_cents:700,  profit_cents:1300, payment_method:'cash',       fulfillment:'local', shipping_address:null, promo_code:'', status:'delivered', tracking_number:'', notes:'' },
  { id:'manual_1776643200004', stripe_session_id:null, created_at:'2026-04-20T16:00:00Z', customer_name:'Daniel Pak',        customer_email:'', items:['Due Light','Due Light'],    subtotal_cents:3400, shipping_cents:0, total_cents:3400, material_cost_cents:1200, profit_cents:2200, payment_method:'cash',       fulfillment:'local', shipping_address:null, promo_code:'', status:'delivered', tracking_number:'', notes:'' },
  { id:'manual_1776643200003', stripe_session_id:null, created_at:'2026-04-20T14:00:00Z', customer_name:'Advay Kavathekar', customer_email:'', items:['Due Light','USB-C Cable'], subtotal_cents:2000, shipping_cents:0, total_cents:2000, material_cost_cents:700,  profit_cents:1300, payment_method:'cash',       fulfillment:'local', shipping_address:null, promo_code:'', status:'delivered', tracking_number:'', notes:'' },
  { id:'manual_1776643200002', stripe_session_id:null, created_at:'2026-04-20T12:00:00Z', customer_name:'Justin Lucas',      customer_email:'', items:['Due Light'],             subtotal_cents:1700, shipping_cents:0, total_cents:1700, material_cost_cents:600,  profit_cents:1100, payment_method:'apple_cash', fulfillment:'local', shipping_address:null, promo_code:'', status:'delivered', tracking_number:'', notes:'' },
  { id:'manual_1776643200001', stripe_session_id:null, created_at:'2026-04-20T10:00:00Z', customer_name:'Caleb Knechel',     customer_email:'', items:['Due Light'],             subtotal_cents:1700, shipping_cents:0, total_cents:1700, material_cost_cents:600,  profit_cents:1100, payment_method:'cash',       fulfillment:'local', shipping_address:null, promo_code:'', status:'delivered', tracking_number:'', notes:'' },
  { id:'manual_1776643200000', stripe_session_id:null, created_at:'2026-04-20T09:20:00Z', customer_name:'Lance Belcher',     customer_email:'', items:['Due Light'],             subtotal_cents:1700, shipping_cents:0, total_cents:1700, material_cost_cents:600,  profit_cents:1100, payment_method:'cash',       fulfillment:'local', shipping_address:null, promo_code:'', status:'delivered', tracking_number:'', notes:'' },
  { id:'manual_1760486400000', stripe_session_id:null, created_at:'2025-10-15T00:00:00Z', customer_name:'Kelly Harris Soto', customer_email:'', items:['Due Light'],             subtotal_cents:2000, shipping_cents:0, total_cents:2000, material_cost_cents:688,  profit_cents:1312, payment_method:'stripe',     fulfillment:'local', shipping_address:null, promo_code:'', status:'delivered', tracking_number:'', notes:'' },
];

const _SEED_INVENTORY = {
  updated_at: '2026-05-02T00:00:00Z',
  components: {
    esp32:          { label: 'ESP32 Boards',      qty: 2 },
    perfboard:      { label: 'Perfboards',        qty: 12 },
    resistors_220:  { label: '220Ω Resistors', qty: 999 },
    resistors_330:  { label: '330Ω Resistors', qty: 10 },
    rgb_leds:       { label: 'RGB LED Sets',       qty: 96 },
    usb_cables:     { label: 'USB-C Cables',       qty: 0 },
    bubble_mailers: { label: '#00 Bubble Mailers', qty: 0 },
  },
  assembled: {
    units_complete:       { label: 'Assembled Units',      qty: 4 },
    perfboards_assembled: { label: 'Assembled Perfboards', qty: 0 },
    cases_printed:        { label: 'Printed Cases',        qty: 1 },
  },
};

const _SEED_CAPITAL = { initial_investment_cents: 0, transactions: [] };

// Determine items from Stripe session amount (price points: $20 base, $2 cable)
function _itemsFromSession(session) {
  const sub = session.amount_subtotal || 0;
  const items = ['Due Light'];
  if (sub >= 2200) items.push('USB-C Cable');
  return items;
}

// Material cost: $6 base + $1 cable + $1 bubble mailer if shipping + Stripe fee
function _materialCost(session) {
  const sub   = session.amount_subtotal || 0;
  const total = session.amount_total    || 0;
  const hasCable    = sub >= 2200;
  const hasShipping = (session.shipping_cost?.amount_total || 0) > 0;
  const stripeFee   = Math.round(total * 0.029 + 30);
  return 600 + (hasCable ? 100 : 0) + (hasShipping ? 100 : 0) + stripeFee;
}

function _profit(session) {
  return (session.amount_total || 0) - _materialCost(session);
}

// Material cost for manual orders from items array
function _manualMaterialCost(items, fulfillment) {
  const unitCount  = items.filter(i => i === 'Due Light').length;
  const cableCount = items.filter(i => i === 'USB-C Cable').length;
  const hasShipping = fulfillment === 'shipping';
  return 600 * unitCount + 100 * cableCount + (hasShipping ? 100 : 0);
}

// ── Middleware ─────────────────────────────────────────────────────────────

// Count requests for req/min metric
app.use((req, res, next) => { _reqCount++; next(); });

// Webhook must be registered before express.json() — Stripe needs the raw body for signature verification
app.post('/webhook', express.raw({ type: 'application/json' }), (req, res) => {
  const sig = req.headers['stripe-signature'];
  const webhookSecret = process.env.STRIPE_WEBHOOK_SECRET;

  let event;
  try {
    event = webhookSecret
      ? stripe.webhooks.constructEvent(req.body, sig, webhookSecret)
      : JSON.parse(req.body);
  } catch (err) {
    console.error('[stripe] webhook signature error:', err.message);
    return res.status(400).send(`Webhook Error: ${err.message}`);
  }

  if (event.type === 'checkout.session.completed') {
    const session = event.data.object;
    const email = session.customer_details?.email || 'unknown';
    const name  = session.customer_details?.name  || '';
    const total = `$${((session.amount_total || 0) / 100).toFixed(2)}`;
    console.log(`[stripe] order completed — ${email} — ${total}`);

    const shipping = session.shipping_details;

    // Persist order to orders.json
    const order = {
      id:                 session.id,
      stripe_session_id:  session.id,
      created_at:         new Date(session.created * 1000).toISOString(),
      customer_name:      name,
      customer_email:     email === 'unknown' ? '' : email,
      items:              _itemsFromSession(session),
      subtotal_cents:     session.amount_subtotal  || 0,
      shipping_cents:     session.shipping_cost?.amount_total || 0,
      total_cents:        session.amount_total     || 0,
      material_cost_cents: _materialCost(session),
      profit_cents:       _profit(session),
      payment_method:     'stripe',
      fulfillment:        shipping ? 'shipping' : 'local',
      shipping_address:   shipping ? shipping.address : null,
      promo_code:         session.metadata?.promoCode || '',
      status:             'new',
      tracking_number:    '',
      notes:              '',
    };
    const orders = readJSON(ORDERS_FILE, _SEED_ORDERS);
    // Avoid duplicates if webhook fires twice
    if (!orders.find(o => o.id === order.id)) {
      orders.unshift(order);
      writeJSON(ORDERS_FILE, orders);
    }

    let alert;
    if (shipping) {
      const a = shipping.address;
      const addrLines = [a.line1, a.line2, `${a.city}, ${a.state} ${a.postal_code}`].filter(Boolean).join('\n');
      alert = `🛒 **New order — SHIP IT** ${name} (${email}) — ${total}\n📦 **Ship to:**\n\`\`\`\n${shipping.name}\n${addrLines}\n\`\`\``;
    } else {
      alert = `🛒 **New order — LOCAL** ${name || email} — ${total}\n🤝 Contact customer to arrange delivery`;
    }
    sendDiscordAlert(alert);
  }

  res.json({ received: true });
});

app.use(express.json({ limit: '64kb' }));
app.use(express.urlencoded({ extended: false }));  // parse HTML form POSTs

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

// GET /success — Stripe post-payment landing page. Explicit route so both
// /success (Stripe redirect) and /success.html (direct link) work without Caddy rewriting.
app.get('/success', (req, res) => {
  res.sendFile(path.join(__dirname, 'public', 'success.html'));
});

// GET /robots.txt — host-aware: block dashboard from crawlers, allow public pages
app.get('/robots.txt', (req, res) => {
  const host = (req.headers.host || '').split(':')[0].toLowerCase();
  res.type('text/plain');
  if (host === 'dashboard.due-light.com') {
    // Block crawlers entirely — dashboard is behind a password, shouldn't appear in search
    res.send('User-agent: *\nDisallow: /\n');
  } else if (host === 'due-light.com' || host === 'www.due-light.com') {
    res.send([
      'User-agent: *',
      'Allow: /',
      'Disallow: /api/',
      '',
      `Sitemap: https://due-light.com/sitemap.xml`,
    ].join('\n') + '\n');
  } else {
    // setup.due-light.com and everything else — allow
    res.send('User-agent: *\nAllow: /\n');
  }
});

// GET /sitemap.xml — public pages only, served on due-light.com
app.get('/sitemap.xml', (req, res) => {
  const host = (req.headers.host || '').split(':')[0].toLowerCase();
  if (host !== 'due-light.com' && host !== 'www.due-light.com') {
    return res.status(404).send('Not found');
  }
  const today = new Date().toISOString().slice(0, 10);
  res.type('application/xml');
  res.send(`<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">
  <url>
    <loc>https://due-light.com/</loc>
    <lastmod>${today}</lastmod>
    <changefreq>weekly</changefreq>
    <priority>1.0</priority>
  </url>
  <url>
    <loc>https://setup.due-light.com/</loc>
    <lastmod>${today}</lastmod>
    <changefreq>monthly</changefreq>
    <priority>0.8</priority>
  </url>
  <url>
    <loc>https://due-light.com/success</loc>
    <lastmod>${today}</lastmod>
    <changefreq>monthly</changefreq>
    <priority>0.3</priority>
  </url>
</urlset>`);
});

app.use(express.static(path.join(__dirname, 'public')));

// POST /api/login — exchange password for a session token
app.post('/api/login', loginRateLimitMiddleware, rateLimitMiddleware, (req, res) => {
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

// DELETE /api/logout — invalidate the session token server-side
app.delete('/api/logout', (req, res) => {
  const key = req.headers['x-api-key'];
  if (key) _sessions.delete(key);
  res.json({ ok: true });
});

// ── Device API ─────────────────────────────────────────────────────────────

// POST /api/telemetry  — heartbeat from a device
// SECURITY LOW: no request signing — any client with the API key can spoof telemetry for any
// device_id, including overwriting data for devices it doesn't own. Fix when needed: include
// an HMAC of (device_id + timestamp) signed with a per-device secret so the server can verify
// the payload originated from the real device.
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

  const isFactoryReset = !!body.factory_reset;

  const record = {
    // Preserve operator-set fields from previous record
    notes: prev.notes || '',
    _alerted_stale: prev._alerted_stale || false,
    _alerted_error_code: prev._alerted_error_code || 0,
    // Factory reset flag: set on reset notification, cleared on normal check-in
    factory_reset_pending: isFactoryReset ? true : false,
    // Device-reported fields
    device_id: body.device_id,
    device_name: body.device_name || body.device_id,
    firmware_version: body.firmware_version || 'unknown',
    setup_complete: isFactoryReset ? false : !!body.setup_complete,
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
    diagnostics: (body.diagnostics && typeof body.diagnostics === 'object' && JSON.stringify(body.diagnostics).length <= 2000) ? body.diagnostics : {},
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

// ── Stripe Checkout ────────────────────────────────────────────────────────

// POST /api/checkout — create a Stripe Checkout Session and return the URL
app.post('/api/checkout', rateLimitMiddleware, async (req, res) => {
  if (!stripe) return res.status(503).json({ error: 'Stripe not configured' });

  const addCable = !!(req.body && req.body.addCable);
  const addShipping = !!(req.body && req.body.addShipping);
  const rawPromo = (req.body && typeof req.body.promoCode === 'string') ? req.body.promoCode : '';
  // Sanitize: only alphanumeric + dash/underscore, max 30 chars
  const promoCode = rawPromo.replace(/[^A-Za-z0-9_-]/g, '').slice(0, 30);

  const lineItems = [
    {
      price_data: {
        currency: 'usd',
        product_data: { name: 'Due Light', description: 'ESP32 WiFi LED assignment tracker' },
        unit_amount: 2000, // $20.00
      },
      quantity: 1,
    },
  ];

  if (addCable) {
    lineItems.push({
      price_data: {
        currency: 'usd',
        product_data: { name: 'USB-C Cable', description: 'USB-C charging cable add-on' },
        unit_amount: 200, // $2.00
      },
      quantity: 1,
    });
  }

  const sessionParams = {
    mode: 'payment',
    line_items: lineItems,
    success_url: 'https://due-light.com/success?session_id={CHECKOUT_SESSION_ID}',
    cancel_url: 'https://due-light.com/#pricing',
  };

  if (addShipping) {
    // Stripe collects the shipping address automatically when shipping_options is set
    sessionParams.shipping_options = [
      {
        shipping_rate_data: {
          type: 'fixed_amount',
          fixed_amount: { amount: 400, currency: 'usd' },
          display_name: 'Standard Shipping',
          delivery_estimate: {
            minimum: { unit: 'business_day', value: 3 },
            maximum: { unit: 'business_day', value: 7 },
          },
        },
      },
    ];
  }

  // Store promo code in metadata so webhook can read it
  sessionParams.metadata = { promoCode: promoCode || '' };

  // Pre-apply referral promo code if provided; fall back to user-typed if not found
  if (promoCode) {
    try {
      const promos = await stripe.promotionCodes.list({ code: promoCode, active: true, limit: 1 });
      if (promos.data.length) {
        sessionParams.discounts = [{ promotion_code: promos.data[0].id }];
      } else {
        sessionParams.allow_promotion_codes = true;
      }
    } catch {
      sessionParams.allow_promotion_codes = true;
    }
  } else {
    sessionParams.allow_promotion_codes = true;
  }

  try {
    const session = await stripe.checkout.sessions.create(sessionParams);
    res.json({ url: session.url });
  } catch (err) {
    console.error('[stripe] checkout session error:', err.message);
    res.status(500).json({ error: 'Could not start checkout' });
  }
});

// ── Reviews ────────────────────────────────────────────────────────────────

// POST /api/review — public, rate-limited
app.post('/api/review', rateLimitMiddleware, (req, res) => {
  const { rating, name, comment, email } = req.body || {};
  const r = parseInt(rating);
  if (!Number.isInteger(r) || r < 1 || r > 5) return res.status(400).json({ error: 'rating must be 1–5' });
  if (typeof name !== 'string' || !name.trim()) return res.status(400).json({ error: 'name required' });
  if (typeof comment !== 'string' || !comment.trim()) return res.status(400).json({ error: 'comment required' });

  const review = {
    id: newBugId(),
    rating: r,
    name: name.trim().slice(0, 100),
    comment: comment.trim().slice(0, 1000),
    email: typeof email === 'string' ? email.trim().slice(0, 200) : '',
    created_at: new Date().toISOString(),
  };

  const reviews = readJSON(REVIEWS_FILE, []);
  reviews.unshift(review);
  if (reviews.length > 1000) reviews.length = 1000;
  writeJSON(REVIEWS_FILE, reviews);
  res.json({ ok: true, review_id: review.id });
});

// GET /api/reviews — public, strips email field
app.get('/api/reviews', (req, res) => {
  const reviews = readJSON(REVIEWS_FILE, []);
  const limit = Math.min(parseInt(req.query.limit) || 20, 100);
  res.json(reviews.slice(0, limit).map(({ email: _e, ...r }) => r));
});

// DELETE /api/reviews/:id — admin only
app.delete('/api/reviews/:id', authMiddleware, (req, res) => {
  const reviews = readJSON(REVIEWS_FILE, []);
  const id = parseInt(req.params.id);
  const idx = reviews.findIndex(r => r.id === id);
  if (idx < 0) return res.status(404).json({ error: 'Not found' });
  reviews.splice(idx, 1);
  writeJSON(REVIEWS_FILE, reviews);
  res.json({ ok: true });
});

// ── Orders ─────────────────────────────────────────────────────────────────

// GET /api/orders — all orders newest-first + summary stats
app.get('/api/orders', readAuthMiddleware, (req, res) => {
  const orders = readJSON(ORDERS_FILE, _SEED_ORDERS);
  const nonFinal = ['new', 'building', 'packed'];
  const stats = {
    total:         orders.length,
    revenue_cents: orders.reduce((s, o) => s + (o.total_cents || 0), 0),
    profit_cents:  orders.reduce((s, o) => s + (o.profit_cents || 0), 0),
    pending:       orders.filter(o => nonFinal.includes(o.status)).length,
  };
  res.json({ orders, stats });
});

// POST /api/orders — manual order entry
app.post('/api/orders', readAuthMiddleware, (req, res) => {
  const b = req.body || {};
  if (!b.customer_name || typeof b.customer_name !== 'string') return res.status(400).json({ error: 'customer_name required' });
  if (!Array.isArray(b.items) || !b.items.length) return res.status(400).json({ error: 'items array required' });
  if (!Number.isInteger(b.total_cents) || b.total_cents < 0) return res.status(400).json({ error: 'total_cents required' });

  const fulfillment = b.fulfillment === 'shipping' ? 'shipping' : 'local';
  const material    = _manualMaterialCost(b.items, fulfillment);
  const order = {
    id:                 'manual_' + newBugId(),
    stripe_session_id:  null,
    created_at:         new Date().toISOString(),
    customer_name:      String(b.customer_name).trim().slice(0, 100),
    customer_email:     typeof b.customer_email === 'string' ? b.customer_email.trim().slice(0, 200) : '',
    items:              b.items.map(i => String(i).trim()).filter(Boolean),
    subtotal_cents:     b.total_cents - (b.shipping_cents || 0),
    shipping_cents:     Number.isInteger(b.shipping_cents) ? b.shipping_cents : 0,
    total_cents:        b.total_cents,
    material_cost_cents: material,
    profit_cents:       b.total_cents - material,
    payment_method:     ['cash','cash_app','venmo','apple_cash'].includes(b.payment_method) ? b.payment_method : 'cash',
    fulfillment,
    shipping_address:   (fulfillment === 'shipping' && b.shipping_address && typeof b.shipping_address === 'object') ? b.shipping_address : null,
    promo_code:         typeof b.promo_code === 'string' ? b.promo_code.slice(0, 30) : '',
    status:             'new',
    tracking_number:    '',
    notes:              typeof b.notes === 'string' ? b.notes.trim().slice(0, 500) : '',
  };

  const orders = readJSON(ORDERS_FILE, _SEED_ORDERS);
  orders.unshift(order);
  writeJSON(ORDERS_FILE, orders);
  res.json({ ok: true, order });
});

// PATCH /api/orders/:id/status — update status field only
app.patch('/api/orders/:id/status', readAuthMiddleware, (req, res) => {
  const STATUSES = ['new','building','packed','shipped','delivered','refunded'];
  const { status } = req.body || {};
  if (!STATUSES.includes(status)) return res.status(400).json({ error: 'invalid status' });

  const orders = readJSON(ORDERS_FILE, _SEED_ORDERS);
  const order  = orders.find(o => o.id === req.params.id);
  if (!order) return res.status(404).json({ error: 'Not found' });
  order.status = status;
  writeJSON(ORDERS_FILE, orders);
  res.json({ ok: true });
});

// PATCH /api/orders/:id — update tracking_number and/or notes
app.patch('/api/orders/:id', readAuthMiddleware, (req, res) => {
  const orders = readJSON(ORDERS_FILE, _SEED_ORDERS);
  const order  = orders.find(o => o.id === req.params.id);
  if (!order) return res.status(404).json({ error: 'Not found' });
  if (typeof req.body.tracking_number === 'string') order.tracking_number = req.body.tracking_number.slice(0, 100);
  if (typeof req.body.notes          === 'string') order.notes           = req.body.notes.slice(0, 500);
  writeJSON(ORDERS_FILE, orders);
  res.json({ ok: true });
});

// DELETE /api/orders/:id — permanently remove an order
app.delete('/api/orders/:id', readAuthMiddleware, (req, res) => {
  const orders = readJSON(ORDERS_FILE, _SEED_ORDERS);
  const idx    = orders.findIndex(o => o.id === req.params.id);
  if (idx < 0) return res.status(404).json({ error: 'Not found' });
  orders.splice(idx, 1);
  writeJSON(ORDERS_FILE, orders);
  res.json({ ok: true });
});

// ── Inventory ──────────────────────────────────────────────────────────────

app.get('/api/inventory', readAuthMiddleware, (req, res) => {
  res.json(readJSON(INVENTORY_FILE, _SEED_INVENTORY));
});

app.patch('/api/inventory', readAuthMiddleware, (req, res) => {
  const body = req.body || {};
  if (!body.components || !body.assembled) return res.status(400).json({ error: 'components and assembled required' });
  body.updated_at = new Date().toISOString();
  writeJSON(INVENTORY_FILE, body);
  res.json({ ok: true });
});

// ── Capital ────────────────────────────────────────────────────────────────

// GET /api/capital — initial investment + transactions + computed summary
app.get('/api/capital', readAuthMiddleware, (req, res) => {
  const capital = readJSON(CAPITAL_FILE, _SEED_CAPITAL);
  const orders  = readJSON(ORDERS_FILE,  _SEED_ORDERS);

  const revenue_cents  = orders.reduce((s, o) => s + (o.total_cents         || 0), 0);
  const cogs_cents     = orders.reduce((s, o) => s + (o.material_cost_cents || 0), 0);
  const gross_profit   = revenue_cents - cogs_cents;

  const txns           = capital.transactions || [];
  const expense_cents  = txns.filter(t => t.type === 'expense').reduce((s, t) => s + t.amount_cents, 0);
  const payout_cents   = txns.filter(t => t.type === 'payout' ).reduce((s, t) => s + t.amount_cents, 0);
  const working_capital = capital.initial_investment_cents + gross_profit - expense_cents - payout_cents;

  res.json({
    initial_investment_cents: capital.initial_investment_cents,
    transactions: txns,
    summary: { revenue_cents, cogs_cents, gross_profit, expense_cents, payout_cents, working_capital },
  });
});

// PATCH /api/capital — update initial investment
app.patch('/api/capital', readAuthMiddleware, (req, res) => {
  const cents = parseInt(req.body.initial_investment_cents);
  if (!Number.isFinite(cents) || cents < 0) return res.status(400).json({ error: 'invalid amount' });
  const capital = readJSON(CAPITAL_FILE, _SEED_CAPITAL);
  capital.initial_investment_cents = cents;
  writeJSON(CAPITAL_FILE, capital);
  res.json({ ok: true });
});

// POST /api/capital/transactions — add expense or payout
app.post('/api/capital/transactions', readAuthMiddleware, (req, res) => {
  const { type, label, amount_cents, date } = req.body || {};
  if (!['expense','payout'].includes(type)) return res.status(400).json({ error: 'type must be expense or payout' });
  if (typeof label !== 'string' || !label.trim()) return res.status(400).json({ error: 'label required' });
  if (!Number.isInteger(amount_cents) || amount_cents <= 0) return res.status(400).json({ error: 'amount_cents must be positive integer' });

  const txn = {
    id:           'txn_' + newBugId(),
    type,
    label:        label.trim().slice(0, 200),
    amount_cents,
    date:         typeof date === 'string' ? date : new Date().toISOString(),
  };
  const capital = readJSON(CAPITAL_FILE, _SEED_CAPITAL);
  capital.transactions = [txn, ...(capital.transactions || [])];
  writeJSON(CAPITAL_FILE, capital);
  res.json({ ok: true, transaction: txn });
});

// DELETE /api/capital/transactions/:id
app.delete('/api/capital/transactions/:id', readAuthMiddleware, (req, res) => {
  const capital = readJSON(CAPITAL_FILE, _SEED_CAPITAL);
  const txns    = capital.transactions || [];
  const idx     = txns.findIndex(t => t.id === req.params.id);
  if (idx < 0) return res.status(404).json({ error: 'Not found' });
  txns.splice(idx, 1);
  capital.transactions = txns;
  writeJSON(CAPITAL_FILE, capital);
  res.json({ ok: true });
});

// GET /api/server-metrics — process health: CPU, memory, request rate, boot history
app.get('/api/server-metrics', readAuthMiddleware, (req, res) => {
  const h = metricsHistory;
  const avg = key => h.length ? parseFloat((h.reduce((s, e) => s + e[key], 0) / h.length).toFixed(1)) : null;
  const peak = key => h.length ? Math.max(...h.map(e => e[key])) : null;

  const starts = readJSON(STARTS_FILE, []);
  const since24h = Date.now() - 24 * 60 * 60 * 1000;
  const recentStarts = starts.filter(s => new Date(s.t).getTime() >= since24h);

  res.json({
    uptime_seconds: Math.floor(process.uptime()),
    current: h.length ? { ...h[h.length - 1] } : null,
    avg8h:  { cpu: avg('cpu'), heapMB: avg('heapMB'), rssMB: avg('rssMB') },
    peak8h: { cpu: peak('cpu'), heapMB: peak('heapMB'), rssMB: peak('rssMB') },
    history: h,
    recent_starts: recentStarts,
  });
});

// POST /api/deploy — pull latest code and restart. Requires API key.
// Hit this from a browser or curl when you can't physically access the machine.
app.post('/api/deploy', authMiddleware, (req, res) => {
  if (!API_KEY) return res.status(503).json({ error: 'API key not configured — deploy endpoint disabled for safety' });
  const repoRoot = path.resolve(__dirname, '..');
  exec(
    'git pull origin main && pm2 restart canvas-dashboard',
    { cwd: repoRoot, timeout: 60000 },
    (err, stdout, stderr) => {
      if (err) {
        console.error('[deploy] error:', err.message, stderr);
        return res.status(500).json({ error: 'Deploy failed — check server logs' });
      }
      res.json({ ok: true });
    }
  );
});

// ── Start ──────────────────────────────────────────────────────────────────

// SECURITY LOW: no TLS at the Node level — HTTPS depends entirely on the Cloudflare tunnel.
// If someone reaches port 3000 directly (e.g. on the local network) traffic is plain HTTP.
// Fix when needed: add a self-signed cert + https.createServer, or move behind a local
// reverse proxy (Caddy/nginx) that handles TLS before traffic reaches Node.
app.listen(PORT, () => {
  console.log(`Canvas LED Dashboard running on port ${PORT}`);
  if (!DASHBOARD_PASSWORD)
    console.warn('⚠️  WARNING: DASHBOARD_PASSWORD not set — dashboard is publicly readable over the internet!');
  else
    console.log('✓  Password login enabled');
  if (!process.env.STRIPE_WEBHOOK_SECRET)
    console.warn('⚠️  WARNING: STRIPE_WEBHOOK_SECRET not set — Stripe webhooks are unverified (fake orders possible)');
  else
    console.log('✓  Stripe webhook signature verification enabled');
  if (!API_KEY) console.log('Note: DASHBOARD_API_KEY not set — device endpoints unprotected');
  if (DISCORD_WEBHOOK) console.log('Discord alerting enabled');

  // Record this boot in server-starts.json (used by /api/server-metrics for crash detection)
  const starts = readJSON(STARTS_FILE, []);
  starts.push({ t: new Date().toISOString(), pid: process.pid });
  if (starts.length > 50) starts.splice(0, starts.length - 50);
  writeJSON(STARTS_FILE, starts);
});
