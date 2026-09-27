// ── Token-Auth ────────────────────────────────────────────────
const TOKEN_KEY  = 'wem_token';

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

// Details werden immer angezeigt (seit 27.09.2026, der Aufklapp-Knopf ist entfallen)

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



// ── Ergebnis eines Schaltbefehls verfolgen ────────────────────
// Nach einem Knopfdruck 3 min lang alle 10 s abfragen und das "Ergebnis letzter
// Schaltbefehl" des Boards zeigen: vorgemerkt -> sende an WEM -> OK / NICHT uebernommen.
let letzterSchaltStatus = null;
let schaltBeobachtung = null;
function startBeobachtung() {
  schaltBeobachtung = { seit: Date.now(), vorher: letzterSchaltStatus };
  for (let i = 1; i <= 18; i++) setTimeout(refreshStatus, i * 10000);
}
function schoen(t) {
  return t.replace(/uebernommen/g, 'übernommen').replace(/Rueckmeldung/g, 'Rückmeldung')
          .replace(/bestaetigt/g, 'bestätigt').replace(/pruefe/g, 'prüfe').replace(/naechster/g, 'nächster').replace(/ -> /g, ' → ');
}

const zahlText = v => v != null ? v.toFixed(1).replace('.', ',') : '--,-';

// Lokal vorgemerkte Wuensche (bis das Board sie in seiner Warteschlange meldet oder
// der Stand erreicht ist) - hoechstens 3 min, danach zaehlt nur noch das Board.
const lokalWunsch = { hk: null, ww: null };
const HK_NAMEN = ['?', 'Standby', 'Zeitprogramm 1', 'Zeitprogramm 2', 'Zeitprogramm 3', 'Sommer', 'Komfort', 'Normal', 'Absenk'];

// Warteschlange des Boards: "1. Heizkreis -> Sommer (App) | 2. Warmwasser -> Ein (App) - ..."
function wunschAusWarteschlange(w) {
  const r = { hk: null, ww: null };
  if (!w || w === 'leer') return r;
  const m1 = w.match(/Heizkreis -> ([A-Za-zäöü]+(?: \d)?)/);
  if (m1) { const i = HK_NAMEN.indexOf(m1[1]); if (i > 0) r.hk = i; }
  const m2 = w.match(/Warmwasser -> (Ein|Aus)/);
  if (m2) r.ww = m2[1];
  return r;
}

// Hinweis-Zeile im Knopf setzen/entfernen
function markiere(btn, art) {
  btn.classList.remove('vorgemerkt', 'abweichend', 'mit-hinweis');
  const alt = btn.querySelector('.hinweis'); if (alt) alt.remove();
  if (!art) return;
  btn.classList.add(art, 'mit-hinweis');
  const h = document.createElement('span');
  h.className = 'hinweis'; h.textContent = art;
  btn.appendChild(h);
}

// Weicht der Heizkreis-Status (Statusbits) von der Vorgabe ab? Die Statusbits kennen nur
// "Standby" oder "Zeitprogramm" (ohne Nummer) - verglichen wird deshalb nur Standby gegen
// Zeitprogramm 1-3. Sommer/Komfort/Normal/Absenk lassen sich daraus nicht pruefen.
function hkAbweichend(code, ist) {
  if (code == null || !ist) return false;
  const istStandby = /^Standby/.test(ist);
  const istZp = /^Zeitprogramm/.test(ist);
  if (code === 1) return istZp;
  if (code >= 2 && code <= 4) return istStandby;
  return false;
}

function zeigeWuensche(data) {
  const board = wunschAusWarteschlange(data.warteschlange);
  const jetzt = Date.now();
  // lokale Wuensche verfallen nach 3 min oder sobald der Stand erreicht ist
  if (lokalWunsch.hk && (jetzt - lokalWunsch.hk.t > 180000 || data.modeCode === lokalWunsch.hk.v)) lokalWunsch.hk = null;
  if (lokalWunsch.ww && (jetzt - lokalWunsch.ww.t > 180000 || data.warmwasser === lokalWunsch.ww.v)) lokalWunsch.ww = null;
  const hkWunsch = board.hk ?? (lokalWunsch.hk && lokalWunsch.hk.v);
  const wwWunsch = board.ww ?? (lokalWunsch.ww && lokalWunsch.ww.v);

  document.querySelectorAll('#modes .mode-btn').forEach(btn => {
    const v = Number(btn.dataset.mode);
    btn.classList.toggle('active', v === data.modeCode);
    let art = null;
    if (hkWunsch && hkWunsch !== data.modeCode && v === hkWunsch) art = 'vorgemerkt';
    else if (!hkWunsch && v === data.modeCode && hkAbweichend(data.modeCode, data.modeAktuellLabel)) art = 'abweichend';
    markiere(btn, art);
  });
  for (const [id, wert] of [['wwEin', 'Ein'], ['wwAus', 'Aus']]) {
    const btn = document.getElementById(id);
    btn.classList.toggle('active', data.warmwasser === wert);
    markiere(btn, (wwWunsch && wwWunsch !== data.warmwasser && wwWunsch === wert) ? 'vorgemerkt' : null);
  }
}

function hkIstText(ist) {
  if (!ist) return '—';
  return ist.replace(/\s*\+\s*WW-Ladung/, ' · WW lädt').replace(/\s*\(0x[0-9A-F]+\)/i, '');
}

async function refreshStatus() {
  try {
    const res = await apiFetch('/api/status');
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || 'Fehler');

    const setText = (id, value) => {
      const el = document.getElementById(id);
      if (el) el.textContent = zahlText(value);
    };
    setText('outsideTemp',   data.outsideTempC);
    setText('kesselTemp',    data.kesselTempC);
    setText('ruecklaufTemp', data.ruecklaufTempC);
    setText('vorlaufIst',    data.vorlaufIstC);
    setText('vorlaufSoll',   data.vorlaufSollC);
    setText('wwIst',         data.warmwasserC);

    // Ist-Anzeigen rechts neben den Blocktiteln
    const hkIst = document.getElementById('hkIst');
    hkIst.textContent = hkIstText(data.modeAktuellLabel);
    hkIst.parentElement.title = data.modeAktuellLabel || '';
    document.getElementById('wwAktiv').textContent =
      data.warmwasserAktiv === 'Ein' ? 'lädt gerade' : data.warmwasserAktiv === 'Aus' ? 'keine Ladung' : '—';

    // Brenner im Kessel-Block: Aus / Vorlüften / An / Nachlüften
    document.getElementById('brennerText').textContent = data.brennerText ? data.brennerText.toLowerCase() : '—';
    document.getElementById('brennerZeile').classList.toggle('an', data.brennerAn === true);

    zeigeWuensche(data);

    // Rot wenn Gerät hängt (alle Werte null)
    const allNull = data.outsideTempC == null && data.kesselTempC == null && data.ruecklaufTempC == null;
    const now = new Date();
    const time = now.toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
    const gm = data.statusGelesen && data.statusGelesen.match(/(\d\d:\d\d)(?::\d\d)?\s+gelesen/);
    const gelesen = gm ? ` · Status gelesen ${gm[1]}` : '';
    const warte = data.warteschlange && data.warteschlange !== 'leer' ? ` · ${schoen(data.warteschlange)}` : '';
    document.getElementById('message').textContent = `Aktualisiert ${time}${gelesen}${warte}`;
    setStatus(allNull ? 'err' : 'ok');

    if (schaltBeobachtung && Date.now() - schaltBeobachtung.seit < 180000 &&
        data.schaltStatus && data.schaltStatus !== schaltBeobachtung.vorher) {
      const t = data.schaltStatus;
      const fertig = /OK:|NICHT|abgelehnt|Rueckmeldung|nicht erreichbar/.test(t);
      setStatus(/OK:/.test(t) ? 'ok' : fertig ? 'err' : '');
      if (fertig) { lokalWunsch.hk = null; lokalWunsch.ww = null; }
      document.getElementById('message').textContent = schoen(t);
    }
    letzterSchaltStatus = data.schaltStatus;
    pruefeStatusLesung(data);
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
    lokalWunsch.hk = { v: modeCode, t: Date.now() };
    setStatus('ok');
    document.getElementById('message').textContent = `${modeLabel} vorgemerkt – wird in bis zu einer Minute geschaltet`;
    startBeobachtung();
  } catch (err) {
    setStatus('err');
    document.getElementById('message').textContent = err.message || 'Fehler';
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
    lokalWunsch.ww = { v: an ? 'Ein' : 'Aus', t: Date.now() };
    setStatus('ok');
    document.getElementById('message').textContent = `Warmwasser ${an ? 'Ein' : 'Aus'} vorgemerkt – wird in bis zu einer Minute geschaltet`;
    startBeobachtung();
  } catch (err) {
    setStatus('err');
    document.getElementById('message').textContent = err.message || 'Fehler';
  }
}
document.getElementById('wwEin').addEventListener('click', () => setWarmwasser(true));
document.getElementById('wwAus').addEventListener('click', () => setWarmwasser(false));

// ── Status lesen (Betriebsarten einmal vom Bus) ───────────────
// Das Board liest Heizkreis- und Warmwasser-Betriebsart nur bei Anlass. Der Knopf schickt
// cmd/status; danach 5x im 4-s-Abstand abfragen, bis "Status gelesen" sich aendert.
let statusLesung = null;
const lesenBtn = document.getElementById('statusLesenBtn');
async function statusLesen() {
  if (statusLesung) return;
  lesenBtn.disabled = true; lesenBtn.classList.add('busy');
  try {
    const res = await apiFetch('/api/lesen', { method: 'POST' });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || 'Fehler');
    statusLesung = { seit: Date.now(), vorher: letzterStatusGelesen };
    setStatus('');
    document.getElementById('message').textContent = 'Status angefordert …';
    for (let i = 1; i <= 5; i++) setTimeout(refreshStatus, i * 4000);
    setTimeout(() => {
      if (!statusLesung) return;
      statusLesung = null; setStatus('err');
      document.getElementById('message').textContent = 'Keine Rückmeldung vom Board';
    }, 22000);
  } catch (err) {
    setStatus('err');
    document.getElementById('message').textContent = err.message || 'Fehler';
  }
  setTimeout(() => { lesenBtn.disabled = false; lesenBtn.classList.remove('busy'); }, 10000);
}
let letzterStatusGelesen = null;
function pruefeStatusLesung(data) {
  const neu = data.statusGelesen || null;
  if (statusLesung && neu && neu !== statusLesung.vorher) {
    if (/gelesen/.test(neu)) {
      statusLesung = null; setStatus('ok');
      document.getElementById('message').textContent = 'Status ' + neu.replace(/^\d\d\.\d\d\.\s*/, '');
    } else if (/schweigt/.test(neu)) {
      statusLesung = null; setStatus('err');
      document.getElementById('message').textContent = 'Bus schweigt – nicht gelesen';
    } else {
      document.getElementById('message').textContent = 'Status ' + neu.replace(/^[\d:]+\s*/, '');
      statusLesung.vorher = neu;
    }
  }
  letzterStatusGelesen = neu;
}
lesenBtn.addEventListener('click', statusLesen);

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
