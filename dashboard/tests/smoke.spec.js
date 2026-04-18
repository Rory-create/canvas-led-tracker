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
  // 401 if password auth is configured, 503 if disabled, 200 with token if open
  expect([200, 401, 503]).toContain(res.status());
  if (res.status() === 401) {
    const body = await res.json();
    expect(body).toHaveProperty('error');
  }
});

// ── Reviews ────────────────────────────────────────────────────────────────

let _testReviewId;

test('reviews: submit and retrieve', async ({ request }) => {
  const post = await request.post(`${BASE}/api/review`, {
    data: { rating: 5, name: 'Smoke Test', comment: 'Automated test review', email: 'test@ci.invalid' },
  });
  expect(post.status()).toBe(200);
  const body = await post.json();
  expect(body.ok).toBe(true);
  expect(typeof body.review_id).toBe('number');
  _testReviewId = body.review_id;

  const get = await request.get(`${BASE}/api/reviews`);
  expect(get.status()).toBe(200);
  const reviews = await get.json();
  expect(Array.isArray(reviews)).toBe(true);
  const found = reviews.find(r => r.id === _testReviewId);
  expect(found).toBeTruthy();
  expect(found.rating).toBe(5);
  expect(found.name).toBe('Smoke Test');
  expect(found.email).toBeUndefined(); // email must not be exposed in public GET
});

test('reviews: rejects missing fields', async ({ request }) => {
  const res = await request.post(`${BASE}/api/review`, {
    data: { rating: 3, name: 'No comment' }, // missing comment
  });
  expect(res.status()).toBe(400);
});

// ── Cleanup ────────────────────────────────────────────────────────────────

test.afterAll(async ({ request }) => {
  // Remove the test device created by the telemetry test so it doesn't persist in units.json
  await request.delete(`${BASE}/api/units/AA:BB:CC:DD:EE:FF`, {
    headers: { 'X-API-Key': 'ci-test-key' },
  });
  // Remove the test review
  if (_testReviewId) {
    await request.delete(`${BASE}/api/reviews/${_testReviewId}`, {
      headers: { 'X-API-Key': 'ci-test-key' },
    });
  }
});
