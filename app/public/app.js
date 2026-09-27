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



// ── Schaltbefehle: Kennung und Phasen ─────────────────────────
// Jeder Knopfdruck schickt einen Befehl mit Kennung (id). Das Board meldet jede Phase:
//   angenommen -> vorgemerkt -> gesendet -> pruefe_bus -> bestaetigt | gescheitert (grund)
//   dazu: ersetzt (neuerer Wunsch), abgelehnt (ungueltig, nie gesendet)
// Die App liest nur diese festen Felder (status/json "schalten" und befehl/status), keinen Freitext.
let eigene = {};              // ziel -> { id, wert, t } eigener, noch nicht vom Board gemeldeter Befehl
let beobachtung = null;       // { id, ziel, seit, phase } fuer die Meldungszeile
function startBeobachtung(id, ziel) {
  beobachtung = { id: String(id), ziel, seit: Date.now(), phase: null };
  for (const ms of [1500, 4000]) setTimeout(refreshStatus, ms);
  for (let i = 1; i <= 18; i++) setTimeout(refreshStatus, i * 10000);
}

const zahlText = v => v != null ? v.toFixed(1).replace('.', ',') : '--,-';
const uhrzeit = (s, sek) => s ? new Date(s * 1000).toLocaleTimeString('de-DE',
  sek ? { hour: '2-digit', minute: '2-digit', second: '2-digit' } : { hour: '2-digit', minute: '2-digit' }) : null;

// ── Vorgabe / Ist / Zustand ───────────────────────────────────
// IST     = am Bus gelesene Betriebsart (status/json heizkreis.vorgabe bzw. warmwasser.vorgabe) -> voll markierter Knopf.
// VORGABE = eigener Schaltbefehl: vorgemerkt (Warteschlange) bzw. gesendet (Bus-Kontrolle steht aus),
//           gescheitert/abgelehnt rot gestrichelt mit Kurztext. Steht der Bus nach einem bestaetigten
//           Befehl auf etwas anderem, wurde am Display/Portal umgestellt -> "von außen geändert".
// ZUSTAND = Laufzustand (Statusbits des WEM bzw. Warmwasser-Ladung), rechts neben dem Titel.
const HK_NAMEN = ['?', 'Standby', 'Zeitprogramm 1', 'Zeitprogramm 2', 'Zeitprogramm 3', 'Sommer', 'Komfort', 'Normal', 'Absenk'];
const ZIELNAME = { hk: 'heizkreis', ww: 'warmwasser' };
const wertVon = (ziel, b) => !b ? null : ziel === 'hk' ? (b.wert ?? null) : (b.wert_text === 'Ein' || b.wert_text === 'Aus' ? b.wert_text : null);
const LAEUFT = new Set(['gesendet', 'pruefe_bus']);
const OFFEN = new Set(['angenommen', 'vorgemerkt']);
const ENDE_FEHLER = new Set(['gescheitert', 'abgelehnt']);
function kurzGrund(b) {
  const g = { cm05: 'abgelehnt (CM=05)', nicht_uebernommen: 'nicht übernommen', keine_rueckmeldung: 'keine Rückmeldung',
              geleert: 'verworfen', ungueltig: 'ungültig', ersetzt: 'ersetzt' }[b.grund];
  return g || b.grund || b.phase;
}

// Befehl fuer ein Ziel: laufend > vorgemerkt > zuletzt abgeschlossen; eigener, noch nicht
// gemeldeter Befehl (Knopfdruck vor der naechsten Meldung) zaehlt als "angenommen".
function befehlFuer(ziel, data) {
  const sc = data.schalten || {};
  const zn = ZIELNAME[ziel];
  let b = null;
  if (sc.laufend && sc.laufend.ziel === zn) b = sc.laufend;
  else b = (sc.warteschlange || []).find(x => x.ziel === zn) || null;
  // Rueckmeldungen auf befehl/status koennen neuer sein als status/json
  const e = eigene[ziel];
  if (e) {
    const gemeldet = (data.befehle || []).find(x => String(x.id) === e.id) ||
                     (b && String(b.id) === e.id ? b : null);
    if (gemeldet) { if (!b || String(b.id) === e.id) b = gemeldet; }
    else if (Date.now() - e.t < 60000) return { id: e.id, ziel: zn, wert: ziel === 'hk' ? e.wert : null, wert_text: ziel === 'ww' ? e.wert : null, phase: 'angenommen' };
  }
  if (b) return b;
  return sc.letzte ? sc.letzte[zn] || null : null;
}
function ist_ende(b) { return !!b && (b.ende === true || ['bestaetigt', 'gescheitert', 'ersetzt', 'abgelehnt'].includes(b.phase)); }

// Stand der Vorgabe fuer ein Ziel: null | {art: vorgemerkt|gesendet|fehler|aussen, wert, kurz}
function vorgabeStand(ziel, ist, data) {
  const b = befehlFuer(ziel, data);
  if (!b) return null;
  const wert = wertVon(ziel, b);
  if (OFFEN.has(b.phase)) return wert != null ? { art: 'vorgemerkt', wert } : null;
  if (LAEUFT.has(b.phase)) return wert != null ? { art: 'gesendet', wert } : null;
  if (b.phase === 'bestaetigt') return ist != null && wert != null && ist !== wert ? { art: 'aussen', wert } : null;
  if (ENDE_FEHLER.has(b.phase) && wert != null && ist !== wert) return { art: 'fehler', wert, kurz: kurzGrund(b) };
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
  const hkIst = data.modeCode >= 1 && data.modeCode <= 8 ? data.modeCode : null;
  const wwIst = /^(Ein|Aus)$/.test(data.warmwasser || '') ? data.warmwasser : null;
  zeigeKnoepfe([...document.querySelectorAll('#modes .mode-btn')].map(b => [b, Number(b.dataset.mode)]),
               hkIst, vorgabeStand('hk', hkIst, data));
  zeigeKnoepfe([[document.getElementById('wwEin'), 'Ein'], [document.getElementById('wwAus'), 'Aus']],
               wwIst, vorgabeStand('ww', wwIst, data));
  zeigeFortschritt(document.getElementById('hkFort'), fortschrittStand('hk', data));
  zeigeFortschritt(document.getElementById('wwFort'), fortschrittStand('ww', data));
}
// eigene Befehle vergessen, sobald das Board sie abgeschlossen gemeldet hat (oder nach 60 s ohne Meldung)
function eigeneAufraeumen(data) {
  for (const z of ['hk', 'ww']) {
    const e = eigene[z];
    if (!e) continue;
    const g = (data.befehle || []).find(x => String(x.id) === e.id);
    if ((g && ist_ende(g)) || (!g && Date.now() - e.t > 60000)) delete eigene[z];
  }
}

// ── Fortschritt eines Schaltbefehls ───────────────────────────
// Zeile unter den Knoepfen des betroffenen Blocks, allein aus der Phase des Befehls:
//   vorgemerkt (ab HH:MM) → an WEM gesendet (JSON) → Bus liest nach → bestätigt ✓ / ✗ Grund
// Nach dem Abschluss 3 min stehen lassen (Zeit des Boards, sonst erste Sichtung), ohne Befehl keine Zeile.
const SCHRITTE = ['vorgemerkt', 'an WEM gesendet (JSON)', 'Bus liest nach', 'bestätigt'];
const gesehen = {};           // id+phase -> erste Sichtung (fuer Befehle ohne Board-Uhrzeit)
function fortschrittStand(ziel, data) {
  const b = befehlFuer(ziel, data);
  if (!b || b.phase === 'ersetzt') return null;
  if (OFFEN.has(b.phase)) {
    const fa = data.schalten && data.schalten.frei_ab;
    return { schritt: 0, ab: fa && fa * 1000 > Date.now() ? uhrzeit(fa) : null };
  }
  if (b.phase === 'gesendet') return { schritt: 1 };
  if (b.phase === 'pruefe_bus') {
    return { schritt: 2, hinweis: b.wem === 'unklar' ? 'WEM-Antwort unklar' : b.wem === 'nicht_erreichbar' ? 'WEM nicht erreichbar' : '' };
  }
  const k = String(b.id) + ':' + b.phase;
  if (!gesehen[k]) gesehen[k] = Date.now();
  const fertig = b.zeit ? Math.min(b.zeit * 1000, gesehen[k]) : gesehen[k];
  if (Date.now() - fertig > 180000) return null;
  if (b.phase === 'bestaetigt') return { schritt: 3, ok: true };
  const steht = b.grund === 'nicht_uebernommen' && b.ist_text ? ` (steht auf ${b.ist_text})` : '';
  return { schritt: 3, ok: false, kurz: kurzGrund(b) + steht,
           fehlerBei: b.grund === 'cm05' ? 1 : (b.grund === 'geleert' || b.grund === 'ungueltig') ? 0 : 3 };
}
function zeigeFortschritt(el, f) {
  el.innerHTML = '';
  if (!f) { el.classList.add('hidden'); return; }
  el.classList.remove('hidden');
  const letzter = f.schritt === 3 && !f.ok ? f.fehlerBei : 3;
  for (let i = 0; i <= letzter; i++) {
    let text = SCHRITTE[i], cls = 'offen';
    if (i === 0 && f.ab) text += ` ab ${f.ab}`;
    if (f.schritt < 3) cls = i < f.schritt ? 'fertig' : i === f.schritt ? 'aktiv' : 'offen';
    else if (f.ok) { cls = i < 3 ? 'fertig' : 'ok'; if (i === 3) text += ' ✓'; }
    else if (i < letzter) cls = 'fertig';
    else { cls = 'fehler'; text = '✗ ' + f.kurz; }
    if (i === 2 && f.schritt === 2 && f.hinweis) text += ` (${f.hinweis})`;
    if (i > 0) { const p = document.createElement('span'); p.className = 'pfeil'; p.textContent = '→'; el.appendChild(p); }
    const s = document.createElement('span'); s.className = cls; s.textContent = text; el.appendChild(s);
  }
}

// Meldungszeile zum eigenen Befehl (Phase aus befehl/status bzw. status/json)
function meldeBeobachtung(data) {
  if (!beobachtung || Date.now() - beobachtung.seit > 180000) { beobachtung = null; return; }
  const b = befehlFuer(beobachtung.ziel, data);
  if (!b || String(b.id) !== beobachtung.id || b.phase === beobachtung.phase) return;
  beobachtung.phase = b.phase;
  const name = beobachtung.ziel === 'hk' ? 'Heizkreis' : 'Warmwasser';
  const wert = b.wert_text || '';
  const txt = {
    angenommen: `${name} ${wert} angenommen`,
    vorgemerkt: `${name} ${wert} vorgemerkt` + (b.warten_s ? ` – wird in ${b.warten_s} s gesendet` : ''),
    gesendet: `${name} ${wert}: an WEM gesendet`,
    pruefe_bus: `${name} ${wert}: prüfe am Bus …`,
    bestaetigt: `${name} steht auf ${b.ist_text || wert} ✓`,
    gescheitert: `${name} ${wert}: ${kurzGrund(b)}` + (b.ist_text && b.grund === 'nicht_uebernommen' ? ` (steht auf ${b.ist_text})` : ''),
    abgelehnt: `${name}: abgelehnt – ${b.text || kurzGrund(b)}`,
    ersetzt: `${name} ${wert}: durch neueren Befehl ersetzt`
  }[b.phase];
  if (!txt) return;
  setStatus(b.phase === 'bestaetigt' ? 'ok' : ENDE_FEHLER.has(b.phase) ? 'err' : '');
  document.getElementById('message').textContent = txt;
  if (ist_ende(b)) beobachtung = null;
  return true;
}

// Zustand aus den Statusbits: ist "zeitprogramm" + heizt + WW-Ladung-Bit -> "Zeitprogramm, heizt · WW lädt"
function hkZustandText(hk, bits) {
  const ist = Number.isFinite(bits) ? ((bits & 0x1000) ? 'standby' : 'zeitprogramm') : (hk && hk.ist ? hk.ist : null);
  if (!ist) return '—';
  let t = ist === 'standby' ? 'Standby' : 'Zeitprogramm';
  const b = Number.isFinite(bits) ? bits : (hk && hk.statusbits != null ? hk.statusbits : NaN);
  if (Number.isFinite(b) && (b & 0x0040)) t += ', heizt';
  if (Number.isFinite(b) && (b & 0x0010)) t += ' · WW lädt';
  return t;
}

// nach einem Knopfdruck sofort mit dem letzten Stand neu zeichnen
let letzteDaten = null;
function refreshKnoepfe() { if (letzteDaten) zeigeWuensche(letzteDaten); passeAnWennNoetig(); }

// Zusatzanzeigen (Liste vom Board): "Wohnzimmer 21,3 °C · WP ◉ läuft"; Werte aelter 10 min = "--"
function zeigeZusatz(liste) {
  const block = document.getElementById('zusatzBlock');
  const box = document.getElementById('zusatzListe');
  const eintraege = Array.isArray(liste) ? liste : [];
  block.classList.toggle('hidden', eintraege.length === 0);
  box.replaceChildren(...eintraege.map(e => {
    const item = document.createElement('span');
    item.className = 'zusatz-item';
    const wert = document.createElement('span');
    wert.className = 'zwert';
    if (e.art === 'laeuft') {
      wert.textContent = e.laeuft === true ? 'läuft' : e.laeuft === false ? 'aus' : '--';
      item.classList.toggle('an', e.laeuft === true);
    } else {
      const nk = Math.abs(e.wert) >= 100 ? 0 : 1;
      wert.textContent = e.wert == null ? '--' :
        e.wert.toLocaleString('de-DE', { minimumFractionDigits: nk, maximumFractionDigits: nk }) + (e.einheit ? ' ' + e.einheit : '');
    }
    if (e.wert == null) item.classList.add('alt');
    const alter = e.alter_s == null ? 'noch kein Wert' : `vor ${e.alter_s < 120 ? e.alter_s + ' s' : Math.round(e.alter_s / 60) + ' min'}`;
    item.title = `${e.name}: ${e.wert == null ? '--' : e.wert + (e.einheit ? ' ' + e.einheit : '')} (${alter})`;
    item.append(document.createTextNode(e.name), wert);
    return item;
  }));
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

    // Zustand rechts neben den Blocktiteln (Statusbits bzw. Ladung) - getrennt von Vorgabe/Ist
    const hkZustand = document.getElementById('hkZustand');
    const bits = data.heizkreisBits != null ? data.heizkreisBits : null;
    hkZustand.textContent = hkZustandText(data.heizkreis, bits);
    hkZustand.parentElement.title = bits != null ? 'Statusbits 0x' + bits.toString(16).toUpperCase().padStart(4, '0') : '';
    // Ladung im Warmwasser-Block: Gas = Kessel im Warmwasserbetrieb (Kesselstatus 15), sonst "aus"
    const gas = data.ladung ? data.ladung.gas : null;
    document.getElementById('ladungText').textContent = gas === true ? 'Gas' : gas === false ? 'aus' : '—';
    document.getElementById('ladungZeile').classList.toggle('an', gas === true);

    // Brenner im Kessel-Block: Aus / Vorlüften / An / Nachlüften, dahinter der Zweck aus dem Kesselstatus
    const bt = data.brennerText ? data.brennerText.toLowerCase() : null;
    document.getElementById('brennerText').textContent = bt || '—';
    document.getElementById('brennerZeile').classList.toggle('an', data.brennerAn === true);
    const zweckEl = document.getElementById('brennerZweck');
    let zweck = '';
    if (data.kesselZweckWarnung) zweck = ' · ' + data.kesselZweck;
    else if (bt && bt !== 'aus' && (data.kesselZweck === 'Heizung' || data.kesselZweck === 'Warmwasser'))
      zweck = ' · ' + data.kesselZweck + (data.kesselZweck === 'Warmwasser' && data.heizungWartet ? ' (Heizung wartet)' : '');
    zweckEl.textContent = zweck;
    zweckEl.classList.toggle('warn', !!data.kesselZweckWarnung);
    document.getElementById('brennerZeile').title = data.heizungWartet ? 'Kessel lädt Warmwasser, der Heizkreis fordert gleichzeitig Wärme an' : '';

    zeigeZusatz(data.zusatz);

    letzteDaten = data;
    zeigeWuensche(data);
    passeAnWennNoetig();

    // Rot wenn Gerät hängt (alle Werte null)
    const allNull = data.outsideTempC == null && data.kesselTempC == null && data.ruecklaufTempC == null;
    const now = new Date();
    const time = now.toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
    const hkZeit = data.heizkreis && data.heizkreis.zeit;
    const gelesen = hkZeit ? ` · Status gelesen ${uhrzeit(hkZeit)}` : '';
    // die Warteschlange steht an den Knoepfen - hier nur, ab wann der naechste Befehl geht
    const fa = data.schalten && data.schalten.frei_ab;
    const warte = fa && fa * 1000 > Date.now() && data.schalten.warteschlange && data.schalten.warteschlange.length
      ? ` · nächster Befehl ab ${uhrzeit(fa)}` : '';
    document.getElementById('message').textContent = `Aktualisiert ${time}${gelesen}${warte}`;
    setStatus(allNull ? 'err' : 'ok');

    meldeBeobachtung(data);
    eigeneAufraeumen(data);
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
    eigene.hk = { id: String(data.id), wert: modeCode, t: Date.now() }; refreshKnoepfe();
    setStatus('');
    document.getElementById('message').textContent = `${modeLabel} gesendet – das Board meldet jeden Schritt`;
    startBeobachtung(data.id, 'hk');
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
    eigene.ww = { id: String(data.id), wert: an ? 'Ein' : 'Aus', t: Date.now() }; refreshKnoepfe();
    setStatus('');
    document.getElementById('message').textContent = `Warmwasser ${an ? 'Ein' : 'Aus'} gesendet – das Board meldet jeden Schritt`;
    startBeobachtung(data.id, 'ww');
  } catch (err) {
    setStatus('err');
    document.getElementById('message').textContent = err.message || 'Fehler';
  }
}
document.getElementById('wwEin').addEventListener('click', () => setWarmwasser(true));
document.getElementById('wwAus').addEventListener('click', () => setWarmwasser(false));

// ── Status lesen (Betriebsarten einmal vom Bus) ───────────────
// Das Board liest Heizkreis- und Warmwasser-Betriebsart nur bei Anlass. Der Knopf schickt
// cmd/status; danach 5x im 4-s-Abstand abfragen, bis sich heizkreis.zeit (status/json) aendert.
let statusLesung = null;
const lesenBtn = document.getElementById('statusLesenBtn');
async function statusLesen() {
  if (statusLesung) return;
  lesenBtn.disabled = true; lesenBtn.classList.add('busy');
  try {
    const res = await apiFetch('/api/lesen', { method: 'POST' });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || 'Fehler');
    statusLesung = { seit: Date.now(), vorher: letzteLesezeit };
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
let letzteLesezeit = null;
const lesezeit = data => Math.max((data.heizkreis && data.heizkreis.zeit) || 0, (data.warmwasserStatus && data.warmwasserStatus.zeit) || 0) || null;
function pruefeStatusLesung(data) {
  const neu = lesezeit(data);
  if (statusLesung) {
    if (neu && neu !== statusLesung.vorher) {
      statusLesung = null; setStatus('ok');
      document.getElementById('message').textContent = 'Status gelesen ' + uhrzeit(neu, true);
    } else if (data.bus && data.bus.lebt === false) {
      statusLesung = null; setStatus('err');
      document.getElementById('message').textContent = 'Bus schweigt – nicht gelesen';
    } else if (data.bus && data.bus.anlaufpause) {
      document.getElementById('message').textContent = 'Status angefordert – Anlaufpause, liest danach';
    }
  }
  letzteLesezeit = neu;
}
lesenBtn.addEventListener('click', statusLesen);

async function loadMeta() {
  const res = await apiFetch('/api/meta');
  const data = await res.json();
  // Knopf zur Weboberflaeche des Boards: nur, wenn BOARD_URL gesetzt ist
  const boardBtn = document.getElementById('boardBtn');
  if (data.boardUrl) { boardBtn.href = data.boardUrl; boardBtn.classList.remove('hidden'); }
  else { boardBtn.removeAttribute('href'); boardBtn.classList.add('hidden'); }
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
  passeAnWennNoetig();
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

// ── Fit-to-Screen ─────────────────────────────────────────────
// Die Grundschrift (html font-size) wird so gewaehlt, dass der ganze Inhalt ohne Scrollen passt:
// binaere Suche zwischen 11 px und der Breitengrenze (Spalte / 22,5, sonst passen vier Knoepfe nicht
// nebeneinander; hoechstens 30 px). Gemessen wird der echte Inhalt, nicht geschaetzt - Browserleisten,
// Statusleiste und Schriftmetrik des Geraets sind damit automatisch drin. Neu gerechnet wird nur bei
// Groessenaenderung oder wenn sich die Inhaltshoehe aendert (z. B. Fortschrittszeile erscheint).
const DIAG = /[?&]diag\b/.test(location.search);
let fitSchrift = null, fitHoehe = null, fitVerfuegbar = null, fitBreite = null, fitSchritte = 0;
const appEl = document.querySelector('.app');
function verfuegbarH() { return Math.floor(window.visualViewport ? Math.min(window.visualViewport.height, window.innerHeight) : window.innerHeight); }
function verfuegbarB() { return window.visualViewport ? Math.min(window.visualViewport.width, window.innerWidth) : window.innerWidth; }
function inhaltH() { return Math.ceil(appEl.getBoundingClientRect().height); }
function passeAn() {
  const html = document.documentElement;
  const h = verfuegbarH(), b = verfuegbarB();
  const oben = Math.min(30, Math.min(b, 520) / 22.5);
  let lo = 11, hi = Math.max(11, oben), n = 0;
  html.style.fontSize = hi + 'px';
  if (inhaltH() > h) {
    while (hi - lo > 0.25 && n < 12) {                      // groesste Schrift, bei der alles passt
      const mitte = (lo + hi) / 2;
      html.style.fontSize = mitte + 'px'; n++;
      if (inhaltH() <= h) lo = mitte; else hi = mitte;
    }
    html.style.fontSize = lo + 'px';
  } else lo = hi;
  fitSchrift = lo; fitHoehe = inhaltH(); fitVerfuegbar = h; fitBreite = b; fitSchritte = n;
  zeigeDiag();
}
function passeAnWennNoetig() {
  if (fitSchrift == null || verfuegbarH() !== fitVerfuegbar || verfuegbarB() !== fitBreite ||
      Math.abs(inhaltH() - fitHoehe) > 2) passeAn();
  else zeigeDiag();
}
function zeigeDiag() {
  if (!DIAG) return;
  const el = document.getElementById('diag');
  const vv = window.visualViewport;
  el.classList.remove('hidden');
  el.textContent = ` · ${window.innerWidth}×${window.innerHeight}` +
    (vv ? ` vv ${Math.round(vv.width)}×${Math.round(vv.height)}` : '') +
    ` · dpr ${window.devicePixelRatio.toFixed(2)} · Schrift ${fitSchrift != null ? fitSchrift.toFixed(2) : '?'} px` +
    ` · Inhalt ${fitHoehe} · scroll ${document.documentElement.scrollHeight}/${window.innerHeight}`;
}
let fitTimer = null;
function passeAnSpaeter() { clearTimeout(fitTimer); fitTimer = setTimeout(passeAn, 80); }
window.addEventListener('resize', passeAnSpaeter);
window.addEventListener('orientationchange', passeAnSpaeter);
if (window.visualViewport) window.visualViewport.addEventListener('resize', passeAnSpaeter);
if (document.fonts && document.fonts.ready) document.fonts.ready.then(passeAn);
passeAn();

if (getToken()) { init(); } else { showLogin(); }
