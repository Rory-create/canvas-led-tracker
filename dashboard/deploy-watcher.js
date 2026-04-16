// deploy-watcher.js
// Polls GitHub every 5 minutes. When a new commit lands on main with passing CI,
// pulls the repo and restarts the server automatically — no inbound firewall rules needed.

require('dotenv').config();
const https      = require('https');
const { execSync } = require('child_process');
const path       = require('path');

const REPO      = 'Rory-create/canvas-led-tracker';
const BRANCH    = 'main';
const POLL_MS   = 5 * 60 * 1000; // 5 minutes
const REPO_ROOT = path.join(__dirname, '..');

// ── Helpers ────────────────────────────────────────────────────────────────

const short = sha => sha.slice(0, 7);
const log   = msg  => console.log(`[deploy-watcher] ${new Date().toISOString()} ${msg}`);

function githubGet(apiPath) {
  return new Promise((resolve, reject) => {
    const opts = {
      hostname: 'api.github.com',
      path: apiPath,
      headers: {
        'User-Agent': 'canvas-led-deploy-watcher',
        'Accept': 'application/vnd.github+json',
        // Optional: set GITHUB_TOKEN in .env for private repos or higher rate limits
        ...(process.env.GITHUB_TOKEN && { Authorization: `Bearer ${process.env.GITHUB_TOKEN}` }),
      },
    };
    https.get(opts, res => {
      let body = '';
      res.on('data', chunk => body += chunk);
      res.on('end', () => {
        try { resolve({ status: res.statusCode, body: JSON.parse(body) }); }
        catch (e) { reject(new Error(`JSON parse failed: ${body.slice(0, 100)}`)); }
      });
    }).on('error', reject);
  });
}

async function remoteSHA() {
  const { status, body } = await githubGet(`/repos/${REPO}/commits/${BRANCH}`);
  if (status !== 200) throw new Error(`GitHub API ${status}: ${body.message}`);
  return body.sha;
}

const EXEC_OPTS = { windowsHide: true, stdio: 'pipe' };

function localSHA() {
  return execSync('git rev-parse HEAD', { cwd: REPO_ROOT, ...EXEC_OPTS }).toString().trim();
}

// Returns true (passed), false (failed), or null (still running / no checks yet)
async function ciStatus(sha) {
  const { body } = await githubGet(`/repos/${REPO}/commits/${sha}/check-runs`);
  const runs = (body.check_runs || []).filter(r => r.name !== 'deploy-watcher'); // ignore self
  if (runs.length === 0) return true;                              // no CI configured
  if (runs.some(r => r.status !== 'completed')) return null;      // still running
  return runs.every(r => r.conclusion === 'success');
}

// ── Main poll loop ─────────────────────────────────────────────────────────

async function checkAndDeploy() {
  try {
    const remote = await remoteSHA();
    const local  = localSHA();

    if (remote === local) {
      // Reset working tree so locally corrupted files (e.g. conflict markers from
      // a bad git pull) get cleaned up automatically every poll cycle.
      execSync('git checkout -- .', { cwd: REPO_ROOT, ...EXEC_OPTS });
      log(`up to date (${short(local)})`);
      return;
    }

    log(`new commit detected: ${short(local)} → ${short(remote)}, checking CI...`);

    const passed = await ciStatus(remote);
    if (passed === null) { log('CI still running — will check next poll'); return; }
    if (passed === false) { log('CI failed — skipping deploy');             return; }

    log('CI passed — pulling...');
    execSync('git pull origin main', { cwd: REPO_ROOT, ...EXEC_OPTS });

    log('restarting server...');
    execSync('pm2 restart canvas-dashboard', EXEC_OPTS);

    log(`deployed ${short(remote)} ✓`);
  } catch (err) {
    log(`error: ${err.message}`);
  }
}

log(`started — polling every ${POLL_MS / 60000} min`);
checkAndDeploy();
setInterval(checkAndDeploy, POLL_MS);
