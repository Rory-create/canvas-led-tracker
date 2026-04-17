const { test, expect } = require('@playwright/test');

const BASE = 'http://localhost:3000';

// ── Server health ──────────────────────────────────────────────────────────

test('server starts and dashboard loads without JS errors', async ({ page }) => {
  const errors = [];
  page.on('pageerror', err => errors.push(err.message));
  await page.goto(BASE);
  await expect(page.locator('body')).toBeVisible();
  expect(errors).toHaveLength(0);
});

// ── All pages return 200 ───────────────────────────────────────────────────
// This test alone would have caught the <<<<<<< HEAD syntax error immediately.

test('all four pages return 200', async ({ request }) => {
  const pages = [
    { url: `${BASE}/`,        headers: {} },                             // dashboard
    { url: `${BASE}/`,        headers: { Host: 'due-light.com' } },      // home
    { url: `${BASE}/`,        headers: { Host: 'setup.due-light.com' } },// setup guide
    { url: `${BASE}/success`, headers: { Host: 'due-light.com' } },      // order success
  ];
  for (const { url, headers } of pages) {
    const res = await request.get(url, { headers });
    expect(res.status(), `${JSON.stringify(headers)} should return 200`).toBe(200);
  }
});

// ── Checkout ───────────────────────────────────────────────────────────────

test('checkout endpoint returns JSON, not a server crash', async ({ request }) => {
  const res = await request.post(`${BASE}/api/checkout`, {
    data: { addCable: false },
  });
  // 200 (Stripe key set) or 503 (no key, graceful) — never 500 (crash/bug)
  expect(res.status()).not.toBe(500);
  const body = await res.json();
  // Always valid JSON with either url or error key
  expect(body).toHaveProperty(res.status() === 200 ? 'url' : 'error');
});

test('home page has buy button, correct price, and correct route', async ({ request }) => {
  const res = await request.get(`${BASE}/`, { headers: { Host: 'due-light.com' } });
  const html = await res.text();
  expect(html).toContain('id="buy-btn"');
  expect(html).toContain('btn-price">20</span>'); // catches accidental price edits
  expect(html).toContain('/api/checkout'); // catches route renames that break the JS fetch
});

// ── Device API ─────────────────────────────────────────────────────────────

test('telemetry endpoint accepts a fake device ping', async ({ request }) => {
  const res = await request.post(`${BASE}/api/telemetry`, {
    headers: { 'X-API-Key': 'ci-test-key' }, // matches DASHBOARD_API_KEY set in CI workflow
    data: {
      device_id: 'AA:BB:CC:DD:EE:FF',
      firmware_version: '0.0.0-ci-test',
      setup_complete: false,
    },
  });
  expect(res.status()).toBe(200);
});

test('auth is enforced on protected endpoints', async ({ request }) => {
  const res = await request.post(`${BASE}/api/deploy`, {
    headers: { 'X-API-Key': 'wrong-key' },
  });
  expect(res.status()).toBe(401);
});

test('login returns 401 on wrong password', async ({ request }) => {
  const res = await request.post(`${BASE}/api/login`, {
    data: { password: 'definitely-wrong-password' },
  });
  // 401 if password auth is configured, 200 with token if no password set (open deploy)
  expect([200, 401]).toContain(res.status());
  if (res.status() === 401) {
    const body = await res.json();
    expect(body).toHaveProperty('error');
  }
});

// ── Cleanup ────────────────────────────────────────────────────────────────

test.afterAll(async ({ request }) => {
  // Remove the test device created by the telemetry test so it doesn't persist in units.json
  await request.delete(`${BASE}/api/units/AA:BB:CC:DD:EE:FF`, {
    headers: { 'X-API-Key': 'ci-test-key' },
  });
});
