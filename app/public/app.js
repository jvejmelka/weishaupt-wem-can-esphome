// ── Token-Auth ────────────────────────────────────────────────
const TOKEN_KEY  = 'wem_token';
const DETAIL_KEY = 'wem_details_open';

function getToken() { return localStorage.getItem(TOKEN_KEY) || ''; }
function saveToken(t) { localStorage.setItem(TOKEN_KEY, t); }
function clearToken() { localStorage.removeItem(TOKEN_KEY); }

async function apiFetch(path, opts = {}) {
  const res = await fetch(path, {
    ...opts,
    headers: { 'x-app-token': getToken(), ...(opts.headers || {}) }
  });
  if (res.status === 401) { clearToken(); showLogin(); throw new Error('Nicht autorisiert'); }
  return res;
}

// ── Login UI ──────────────────────────────────────────────────
function showLogin() {
  document.getElementById('loginOverlay').classList.remove('hidden');
  document.getElementById('loginUser').focus();
}
function hideLogin() {
  document.getElementById('loginOverlay').classList.add('hidden');
}

document.getElementById('loginBtn').addEventListener('click', tryLogin);
document.getElementById('loginPass').addEventListener('keydown', e => { if (e.key === 'Enter') tryLogin(); });

async function tryLogin() {
  const user = document.getElementById('loginUser').value.trim();
  const pass = document.getElementById('loginPass').value;
  const errEl = document.getElementById('loginError');
  errEl.classList.add('hidden');
  if (!user || !pass) { errEl.textContent = 'Bitte ausfüllen'; errEl.classList.remove('hidden'); return; }
  try {
    const res = await fetch('/api/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ user, pass })
    });
    const data = await res.json();
    if (!res.ok) { errEl.textContent = data.error || 'Fehler'; errEl.classList.remove('hidden'); return; }
    saveToken(data.token);
    hideLogin();
    init();
  } catch {
    errEl.textContent = 'Verbindungsfehler';
    errEl.classList.remove('hidden');
  }
}

document.getElementById('logoutBtn').addEventListener('click', () => { clearToken(); showLogin(); });

// ── PWA Install ───────────────────────────────────────────────
let deferredPrompt = null;
window.addEventListener('beforeinstallprompt', (e) => {
  e.preventDefault(); deferredPrompt = e;
  document.getElementById('installBtn').classList.remove('hidden');
});
document.getElementById('installBtn').addEventListener('click', async () => {
  if (!deferredPrompt) return;
  deferredPrompt.prompt(); await deferredPrompt.userChoice;
  deferredPrompt = null;
  document.getElementById('installBtn').classList.add('hidden');
});

// ── Details aufklappen ────────────────────────────────────────
function getDetailsOpen() { return localStorage.getItem(DETAIL_KEY) === 'true'; }
function setDetailsOpen(v) { localStorage.setItem(DETAIL_KEY, v); }

function applyDetails(open) {
  const section = document.getElementById('detailsSection');
  const toggle  = document.getElementById('detailsToggle');
  if (open) {
    section.classList.remove('collapsed');
    toggle.textContent = '▲ Details';
  } else {
    section.classList.add('collapsed');
    toggle.textContent = '▼ Details';
  }
}

document.getElementById('detailsToggle').addEventListener('click', () => {
  const next = !getDetailsOpen();
  setDetailsOpen(next);
  applyDetails(next);
});

// ── Progress Bar ──────────────────────────────────────────────
const POLL_MS = 30000;
let progressStart = null;
let progressRaf = null;

function startProgress() {
  const fill = document.getElementById('progressFill');
  if (!fill) return;
  if (progressRaf) cancelAnimationFrame(progressRaf);
  fill.style.transition = 'none';
  fill.style.width = '0%';
  progressStart = performance.now();

  function tick(now) {
    const elapsed = now - progressStart;
    const pct = Math.min(elapsed / POLL_MS * 100, 100);
    fill.style.width = pct + '%';
    if (pct < 100) progressRaf = requestAnimationFrame(tick);
  }
  progressRaf = requestAnimationFrame(tick);
}


let currentModeCode = null;

function abbrevMode(label) {
  if (!label) return '—';
  if (/zeitprogramm\s*1/i.test(label)) return 'Z1';
  if (/zeitprogramm\s*2/i.test(label)) return 'Z2';
  if (/zeitprogramm\s*3/i.test(label)) return 'Z3';
  return label.slice(0, 2).toUpperCase();
}

async function refreshStatus() {
  try {
    const res = await apiFetch('/api/status');
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || 'Fehler');

    const setCard = (id, value) => {
      const el = document.getElementById(id);
      if (el) el.textContent = value != null ? value.toFixed(1) : '--,-';
    };

    setCard('outsideTemp', data.outsideTempC);
    setCard('kesselTemp',  data.kesselTempC);
    setCard('ruecklaufTemp', data.ruecklaufTempC);
    setCard('vorlaufIst',  data.vorlaufIstC);
    setCard('vorlaufSoll', data.vorlaufSollC);

    const ww = document.getElementById('wwIst');
    ww.textContent = data.warmwasserC != null ? data.warmwasserC.toFixed(1) : '--,-';
    document.getElementById('wwVorgabe').textContent = data.warmwasser ? data.warmwasser.toUpperCase() : '—';
    document.getElementById('wwAktiv').textContent = data.warmwasserAktiv ? data.warmwasserAktiv.toUpperCase() : '—';
    document.getElementById('wwEin').classList.toggle('active', data.warmwasser === 'Ein');
    document.getElementById('wwAus').classList.toggle('active', data.warmwasser === 'Aus');

    const brennerBar = document.getElementById('brennerBar');
    document.getElementById('brennerLabel').textContent =
      data.brennerAn === true ? 'BRENNER — AN' : 'BRENNER — AUS';
    brennerBar.classList.toggle('active', data.brennerAn === true);

    document.getElementById('modeBadge').textContent   = abbrevMode(data.modeLabel);
    document.getElementById('modeAktuell').textContent = abbrevMode(data.modeAktuellLabel);

    if (currentModeCode !== data.modeCode) {
      currentModeCode = data.modeCode;
      document.querySelectorAll('.mode-btn').forEach(btn => {
        btn.classList.toggle('active', Number(btn.dataset.mode) === data.modeCode);
      });
    }

    // Rot wenn Gerät hängt (alle Werte null)
    const allNull = data.outsideTempC == null && data.kesselTempC == null && data.ruecklaufTempC == null;
    const now = new Date();
    const time = now.toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
    const warte = data.warteschlange && data.warteschlange !== 'leer' ? ` · ${data.warteschlange}` : '';
    document.getElementById('message').textContent = `Aktualisiert ${time}${warte}`;
    setStatus(allNull ? 'err' : 'ok');
    startProgress();
  } catch (err) {
    setStatus('err');
    document.getElementById('message').textContent = err.message || 'Keine Daten';
  }
}

async function setMode(modeCode, modeLabel) {
  try {
    setStatus('');
    const res = await apiFetch('/api/mode', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ modeCode })
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || 'Fehler');
    setStatus('ok');
    document.getElementById('message').textContent = `${modeLabel} vorgemerkt – wird in bis zu einer Minute geschaltet`;
    setTimeout(refreshStatus, 15000);
  } catch (err) {
    setStatus('err');
  }
}

const KURZ = { 'Zeitprogramm 1': 'ZP 1', 'Zeitprogramm 2': 'ZP 2', 'Zeitprogramm 3': 'ZP 3' };

async function setWarmwasser(an) {
  try {
    setStatus('');
    const res = await apiFetch('/api/warmwasser', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ an })
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || 'Fehler');
    setStatus('ok');
    document.getElementById('message').textContent = `Warmwasser ${an ? 'Ein' : 'Aus'} vorgemerkt – wird in bis zu einer Minute geschaltet`;
    setTimeout(refreshStatus, 15000);
  } catch (err) {
    setStatus('err');
    document.getElementById('message').textContent = err.message || 'Fehler';
  }
}
document.getElementById('wwEin').addEventListener('click', () => setWarmwasser(true));
document.getElementById('wwAus').addEventListener('click', () => setWarmwasser(false));

async function loadMeta() {
  const res = await apiFetch('/api/meta');
  const data = await res.json();
  const container = document.getElementById('modes');
  container.innerHTML = '';
  for (const mode of data.modes) {
    const btn = document.createElement('button');
    btn.className = 'mode-btn';
    btn.textContent = KURZ[mode.label] || mode.label;
    btn.title = mode.label;
    btn.dataset.mode = mode.value;
    btn.onclick = () => setMode(mode.value, mode.label);
    container.appendChild(btn);
  }
}

function setStatus(type) {
  const el = document.getElementById('message');
  el.className = 'message' + (type ? ' ' + type : '');
  document.getElementById('statusDot').className = 'status-dot' + (type ? ' ' + type : '');
}

// ── Init ──────────────────────────────────────────────────────
let intervalId = null;

// Einmalig registrieren — nicht in init() sonst doppelte Handler bei Re-Login
document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    clearInterval(intervalId);
  } else {
    refreshStatus();
    if (intervalId) intervalId = setInterval(refreshStatus, POLL_MS);
    startProgress();
  }
});

async function init() {
  applyDetails(getDetailsOpen());
  currentModeCode = null;
  try { await loadMeta(); } catch (e) { console.warn('loadMeta failed:', e); }
  await refreshStatus();

  if (intervalId) clearInterval(intervalId);
  intervalId = setInterval(refreshStatus, POLL_MS);
}

// Versionsanzeige unten: der Build schreibt die Unix-Zeit hinein, gezeigt wird ein Datum
(function () {
  const el = document.getElementById('appVersion');
  const n = el ? parseInt(el.textContent.replace(/\D/g, ''), 10) : NaN;
  if (el && n > 1e9) {
    el.title = el.textContent.trim();
    el.textContent = 'Stand ' + new Date(n * 1000).toLocaleString('de-DE', {
      day: '2-digit', month: '2-digit', year: 'numeric', hour: '2-digit', minute: '2-digit'
    });
  }
})();

if (getToken()) { init(); } else { showLogin(); }
