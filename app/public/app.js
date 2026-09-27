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

// ── Vorgabe / Ist / Zustand ───────────────────────────────────
// IST     = am Bus bestaetigte Betriebsart (Board liest 0x2933/2 bzw. 0x2A20/2) -> voll markierter Knopf.
// VORGABE = eigener Schaltbefehl ueber das Board (Warteschlange, laufender Befehl, Schaltprotokoll).
//           Solange nicht am Bus bestaetigt: gestrichelt "vorgemerkt" (Warteschlange) bzw. "gesendet"
//           (Befehl raus, Bus-Kontrolle steht aus); meldet das Board "NICHT uebernommen", "abgelehnt
//           (CM=05)" oder "keine Rueckmeldung": rot gestrichelt mit Kurztext.
//           Steht der Bus nach einem erfolgreichen eigenen Befehl auf etwas anderem (kein Befehl
//           unterwegs), wurde am Display/Portal umgestellt -> Hinweis "von außen geändert".
// ZUSTAND = Laufzustand (Statusbits des WEM bzw. Warmwasser-Ladung), rechts neben dem Titel.
const HK_NAMEN = ['?', 'Standby', 'Zeitprogramm 1', 'Zeitprogramm 2', 'Zeitprogramm 3', 'Sommer', 'Komfort', 'Normal', 'Absenk'];
const MERK_KEY = 'wem_befehle';
// merk.hk / merk.ww = offener Befehl { wert, proto, t } (proto = neuester Protokolleintrag dieses Ziels beim Merken)
// merk.erg.hk / .ww = { eintrag, wert } - Zielwert zu Protokolleintraegen ohne "-> Wert" (CM=05, keine Rueckmeldung)
let merk = {};
try { merk = JSON.parse(localStorage.getItem(MERK_KEY)) || {}; } catch { merk = {}; }
function merkSichern() { try { localStorage.setItem(MERK_KEY, JSON.stringify(merk)); } catch { /* egal */ } }

const hkCode = name => { const i = HK_NAMEN.indexOf((name || '').trim()); return i > 0 ? i : null; };
const wertAus = (ziel, s) => ziel === 'hk' ? hkCode(s) : (/^(Ein|Aus)$/.test((s || '').trim()) ? s.trim() : null);

// Warteschlange des Boards: "1. Heizkreis -> Sommer (App) | 2. Warmwasser -> Ein (App) - ..."
function wunschAusWarteschlange(w) {
  const r = { hk: null, ww: null };
  if (!w || w === 'leer') return r;
  const m1 = w.match(/Heizkreis -> ([A-Za-zäöü]+(?: \d)?)/);
  if (m1) r.hk = hkCode(m1[1]);
  const m2 = w.match(/Warmwasser -> (Ein|Aus)/);
  if (m2) r.ww = m2[1];
  return r;
}
// Schaltstatus "vorgemerkt (MQTT): Heizkreis Sommer" / "vorgemerkt: Warmwasser Ein ..." -> Zielwert
function wunschAusSchaltstatus(s) {
  const r = { hk: null, ww: null };
  const m = (s || '').match(/^vorgemerkt[^:]*: (Heizkreis|Warmwasser) ([A-Za-zäöü]+(?: \d)?)/);
  if (m) { if (m[1] === 'Heizkreis') r.hk = hkCode(m[2]); else r.ww = wertAus('ww', m[2]); }
  return r;
}
// Schaltprotokoll (neueste zuerst): "27.09. 01:22 Warmwasser -> Aus (App): ok | 27.09. 01:21 Heizkreis (App): abgelehnt, CM=05"
function protokollEintraege(p) {
  if (!p) return [];
  return p.split(' | ').map(e => {
    const m = e.match(/(Heizkreis|Warmwasser)(?: -> (.+?))? \(([^)]*)\): (.*)$/);
    if (!m) return null;
    const ziel = m[1] === 'Heizkreis' ? 'hk' : 'ww';
    const erg = m[4];
    return {
      text: e, ziel, wert: m[2] ? wertAus(ziel, m[2]) : null, ok: /^ok/.test(erg),
      kurz: /NICHT/.test(erg) ? 'nicht übernommen' : /CM=05/.test(erg) ? 'abgelehnt (CM=05)'
          : /Rueckmeldung/.test(erg) ? 'keine Rückmeldung' : /nicht erreichbar/.test(erg) ? 'WEM nicht erreichbar' : erg
    };
  }).filter(Boolean);
}
const LAEUFT = /sende an WEM|pruefe (am Bus|trotzdem)|WEM hat bestaetigt|WEM-Antwort unklar/;
let letzteProtokolle = [];
const protokollKopf = (prot, ziel) => { const e = prot.find(x => x.ziel === ziel); return e ? e.text : ''; };

// Eigenen Befehl merken (Knopfdruck, Warteschlange oder "vorgemerkt"-Meldung des Boards)
function merkeBefehl(ziel, wert, prot) {
  if (wert == null) return;
  const m = merk[ziel];
  if (m && m.wert === wert) return;
  merk[ziel] = { wert, proto: protokollKopf(prot, ziel), t: Date.now() };
  merkSichern();
}

// Stand der Vorgabe fuer ein Ziel: null | {art: vorgemerkt|gesendet|fehler|aussen, wert, kurz}
function vorgabeStand(ziel, ist, data, warte, prot) {
  const kopf = prot.find(e => e.ziel === ziel) || null;
  const kopfText = kopf ? kopf.text : '';
  const laeuft = LAEUFT.test(data.schaltStatus || '');
  let m = merk[ziel];
  if (m) {
    const fertig = kopfText !== m.proto;                          // neuer Protokolleintrag = abgeschlossen
    const erreicht = !prot.length && !laeuft && ist === m.wert;   // ohne Protokoll: Bus steht auf dem Ziel
    if (fertig || erreicht || Date.now() - m.t > 300000) {
      if (fertig) { merk.erg = merk.erg || {}; merk.erg[ziel] = { eintrag: kopfText, wert: m.wert }; }
      m = merk[ziel] = null; merkSichern();
    }
  }
  if (warte[ziel] != null) return { art: 'vorgemerkt', wert: warte[ziel] };
  if (m) return { art: laeuft ? 'gesendet' : 'vorgemerkt', wert: m.wert };
  if (!kopf) return null;
  const e = merk.erg && merk.erg[ziel];
  const wert = kopf.wert != null ? kopf.wert : (e && e.eintrag === kopfText ? e.wert : null);
  if (wert == null) return null;
  if (!kopf.ok && ist !== wert) return { art: 'fehler', wert, kurz: kopf.kurz };
  if (kopf.ok && ist != null && ist !== wert) return { art: 'aussen', wert };
  return null;
}

// Hinweis-Zeile im Knopf setzen/entfernen
function markiere(btn, klasse, text) {
  btn.classList.remove('vorgemerkt', 'gesendet', 'fehler', 'aussen', 'mit-hinweis');
  const alt = btn.querySelector('.hinweis'); if (alt) alt.remove();
  if (!klasse) return;
  btn.classList.add(klasse, 'mit-hinweis');
  const h = document.createElement('span');
  h.className = 'hinweis'; h.textContent = text;
  btn.appendChild(h);
}
function zeigeKnoepfe(knoepfe, ist, stand) {
  for (const [btn, wert] of knoepfe) {
    btn.classList.toggle('active', ist != null && wert === ist);
    if (stand && stand.art === 'aussen') markiere(btn, wert === ist ? 'aussen' : null, 'von außen geändert');
    else if (stand && wert === stand.wert) markiere(btn, stand.art, stand.art === 'fehler' ? stand.kurz : stand.art);
    else markiere(btn, null);
  }
}

function zeigeWuensche(data) {
  const prot = protokollEintraege(data.schaltProtokoll);
  letzteProtokolle = prot;
  const warte = wunschAusWarteschlange(data.warteschlange);
  const vorg = wunschAusSchaltstatus(data.schaltStatus);
  for (const z of ['hk', 'ww']) merkeBefehl(z, warte[z] ?? vorg[z], prot);
  const hkIst = data.modeCode >= 1 && data.modeCode <= 8 ? data.modeCode : null;
  const wwIst = /^(Ein|Aus)$/.test(data.warmwasser || '') ? data.warmwasser : null;
  zeigeKnoepfe([...document.querySelectorAll('#modes .mode-btn')].map(b => [b, Number(b.dataset.mode)]),
               hkIst, vorgabeStand('hk', hkIst, data, warte, prot));
  zeigeKnoepfe([[document.getElementById('wwEin'), 'Ein'], [document.getElementById('wwAus'), 'Aus']],
               wwIst, vorgabeStand('ww', wwIst, data, warte, prot));
}

// Zustand aus den Statusbits: "Zeitprogramm + WW-Ladung (0x0040)" -> "Zeitprogramm, heizt · WW lädt"
function hkZustandText(s) {
  if (!s) return '—';
  const hex = s.match(/\(0x([0-9A-F]+)\)/i);
  let t = s.replace(/\s*\(0x[0-9A-F]+\)/i, '').replace(/\s*\+\s*WW-Ladung/, '');
  if (hex && (parseInt(hex[1], 16) & 0x0040)) t += ', heizt';
  if (/WW-Ladung/.test(s)) t += ' · WW lädt';
  return t;
}

// nach einem Knopfdruck sofort mit dem letzten Stand neu zeichnen
let letzteDaten = null;
function refreshKnoepfe() { if (letzteDaten) zeigeWuensche(letzteDaten); }

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

    // Zustand rechts neben den Blocktiteln (Statusbits bzw. Ladung) - getrennt von Vorgabe/Ist
    const hkZustand = document.getElementById('hkZustand');
    hkZustand.textContent = hkZustandText(data.modeAktuellLabel);
    hkZustand.parentElement.title = data.modeAktuellLabel || '';
    document.getElementById('wwAktiv').textContent =
      data.warmwasserAktiv === 'Ein' ? 'lädt gerade' : data.warmwasserAktiv === 'Aus' ? 'keine Ladung' : '—';

    // Brenner im Kessel-Block: Aus / Vorlüften / An / Nachlüften
    document.getElementById('brennerText').textContent = data.brennerText ? data.brennerText.toLowerCase() : '—';
    document.getElementById('brennerZeile').classList.toggle('an', data.brennerAn === true);

    letzteDaten = data;
    zeigeWuensche(data);

    // Rot wenn Gerät hängt (alle Werte null)
    const allNull = data.outsideTempC == null && data.kesselTempC == null && data.ruecklaufTempC == null;
    const now = new Date();
    const time = now.toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
    const gm = data.statusGelesen && data.statusGelesen.match(/(\d\d:\d\d)(?::\d\d)?\s+gelesen/);
    const gelesen = gm ? ` · Status gelesen ${gm[1]}` : '';
    // die Warteschlange steht an den Knoepfen - hier nur, ab wann der naechste Befehl geht
    const ab = (data.warteschlange || '').match(/naechster Befehl ab ([\d:]+)/);
    const warte = ab ? ` · nächster Befehl ab ${ab[1]}` : '';
    document.getElementById('message').textContent = `Aktualisiert ${time}${gelesen}${warte}`;
    setStatus(allNull ? 'err' : 'ok');

    if (schaltBeobachtung && Date.now() - schaltBeobachtung.seit < 180000 &&
        data.schaltStatus && data.schaltStatus !== schaltBeobachtung.vorher) {
      const t = data.schaltStatus;
      const fertig = /OK:|NICHT|abgelehnt|Rueckmeldung|nicht erreichbar/.test(t);
      setStatus(/OK:/.test(t) ? 'ok' : fertig ? 'err' : '');
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
    merkeBefehl('hk', modeCode, letzteProtokolle); refreshKnoepfe();
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
    merkeBefehl('ww', an ? 'Ein' : 'Aus', letzteProtokolle); refreshKnoepfe();
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
