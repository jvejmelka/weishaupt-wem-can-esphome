// Einstellungsseite der App auf dem Board (/einstellungen). Eigenes Admin-Passwort, eigenes Token.
// Liest /api/admin/einstellungen (Schalter und Zahlen des Boards) und schreibt einzelne Werte zurueck.
const TOKEN_KEY = 'wem_admin_token';
const lies = k => { try { return localStorage.getItem(k); } catch { return null; } };
const schreib = (k, v) => { try { if (v == null) localStorage.removeItem(k); else localStorage.setItem(k, v); } catch { /* privat */ } };
const $ = id => document.getElementById(id);

// ── Darstellung (dieselbe Wahl wie in der App)
const hellMq = window.matchMedia ? window.matchMedia('(prefers-color-scheme: light)') : null;
const ZEICHEN = {
  auto: '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="9"/><path d="M12 3a9 9 0 0 1 0 18z" fill="currentColor"/></svg>',
  hell: '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg>',
  dunkel: '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round"><path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z"/></svg>'
};
function themeWahl() { return lies('wem_theme') || lies('wem_theme_vorgabe') || 'auto'; }
function themeAnwenden() {
  const w = themeWahl();
  const echt = w === 'auto' ? (hellMq && hellMq.matches ? 'hell' : 'dunkel') : w;
  document.documentElement.setAttribute('data-theme', echt);
  $('themeBtn').innerHTML = ZEICHEN[w] || ZEICHEN.auto;
  const meta = document.querySelector('meta[name="theme-color"]');
  if (meta) meta.setAttribute('content', echt === 'hell' ? '#f3f3f1' : '#0f0f0f');
}
$('themeBtn').addEventListener('click', () => {
  const f = ['auto', 'hell', 'dunkel'];
  schreib('wem_theme', f[(f.indexOf(themeWahl()) + 1) % f.length]);
  themeAnwenden();
});
if (hellMq) (hellMq.addEventListener ? hellMq.addEventListener('change', themeAnwenden) : hellMq.addListener(themeAnwenden));
themeAnwenden();

// ── Anmeldung
async function api(pfad, opts = {}) {
  const res = await fetch(pfad, {
    ...opts, cache: 'no-store',
    headers: { 'Content-Type': 'application/json', 'x-admin-token': lies(TOKEN_KEY) || '', ...(opts.headers || {}) }
  });
  if (res.status === 401) { schreib(TOKEN_KEY, null); zeigeLogin(); throw new Error('abgemeldet'); }
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(data.error || ('Fehler ' + res.status));
  return data;
}
function zeigeLogin() { $('loginOverlay').classList.remove('hidden'); $('loginPass').focus(); }
async function login() {
  const err = $('loginError');
  err.classList.add('hidden');
  try {
    const res = await fetch('/api/admin/login', {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ pass: $('loginPass').value })
    });
    const data = await res.json();
    if (!res.ok) { err.textContent = data.error || 'Fehler'; err.classList.remove('hidden'); return; }
    schreib(TOKEN_KEY, data.token);
    $('loginPass').value = '';
    $('loginOverlay').classList.add('hidden');
    laden(); infoLaden(); logLaden(); wemLaden();
  } catch { err.textContent = 'Verbindungsfehler'; err.classList.remove('hidden'); }
}
$('loginBtn').addEventListener('click', login);
$('loginPass').addEventListener('keydown', e => { if (e.key === 'Enter') login(); });
$('logoutBtn').addEventListener('click', () => { schreib(TOKEN_KEY, null); zeigeLogin(); });

// ── Einstellungen anzeigen
// Experten-Passwort verdeckt abfragen (prompt() zeigt Klartext); null = abgebrochen
function expertePasswort(frage) {
  return new Promise(fertig => {
    const o = $('exOverlay'), f = $('exFeld');
    $('exFrage').textContent = frage;
    f.value = '';
    o.classList.remove('hidden');
    f.focus();
    const ende = wert => {
      o.classList.add('hidden');
      $('exOk').onclick = null; $('exAbbruch').onclick = null; f.onkeydown = null;
      f.value = '';
      fertig(wert);
    };
    $('exOk').onclick = () => ende(f.value);
    $('exAbbruch').onclick = e => { e.preventDefault(); ende(null); };
    f.onkeydown = e => { if (e.key === 'Enter') ende(f.value); if (e.key === 'Escape') ende(null); };
  });
}

async function setzen(id, wert, meldungEl, experte) {
  try {
    let headers = {};
    if (experte) {
      const pw = await expertePasswort('Diese Einstellung betrifft den Bus');
      if (!pw) { laden(); return; }
      headers = { 'x-experte': pw };
    }
    await api('/api/admin/einstellungen', { method: 'POST', body: JSON.stringify({ id, wert }), headers });
    if (meldungEl) { meldungEl.textContent = 'gespeichert'; meldungEl.className = 'meldung ok'; }
    setTimeout(laden, 1500);   // das Board uebernimmt den Wert in der Hauptschleife
  } catch (e) {
    if (meldungEl) { meldungEl.textContent = e.message; meldungEl.className = 'meldung fehler'; }
    else { alert(e.message); laden(); }
  }
}

// ── Zusatzanzeigen (Liste wie cmd/zusatz)
let anzeigeGeaendert = false, anzeigeStand = [];
const FELDER = [['name', 'Name', 24], ['topic', 'MQTT-Topic', 100], ['feld', 'Feld (leer = Zahl)', 32], ['einheit', 'Einheit', 8]];
function anzeigeZeigen(liste) {
  const b = $('anzeigeBlock');
  if (liste === undefined) { b.classList.add('hidden'); return; }
  b.classList.remove('hidden');
  anzeigeStand = liste;
  $('anzeigeZustand').textContent = `${liste.length} von 6`;
  const ziel = $('anzeigeListe');
  ziel.textContent = '';
  liste.forEach(e => ziel.appendChild(anzeigeZeile(e)));
}
function anzeigeZeile(e = {}) {
  const z = document.createElement('div');
  z.className = 'anzeige-eintrag';
  z.style.cssText = 'border-top:1px solid var(--line, rgba(128,128,128,.25));padding:.5rem 0;display:grid;gap:.3rem';
  for (const [k, ph, max] of FELDER) {
    const f = document.createElement('input');
    f.className = 'feld'; f.dataset.k = k; f.placeholder = ph; f.maxLength = max; f.value = e[k] || '';
    f.autocomplete = 'off'; f.spellcheck = false;
    f.addEventListener('input', () => { anzeigeGeaendert = true; });
    z.appendChild(f);
  }
  const r = document.createElement('div');
  r.style.cssText = 'display:flex;gap:.5rem;align-items:center;flex-wrap:wrap';
  r.innerHTML = '<label class="hinweis"><input type="checkbox" data-k="laeuft"> läuft ab Schwelle</label>' +
    '<input class="zahl" data-k="schwelle" type="number" step="any" style="width:6rem">' +
    '<button class="knopf" style="margin-left:auto">ENTFERNEN</button>';
  r.querySelector('[data-k=laeuft]').checked = e.art === 'laeuft';
  r.querySelector('[data-k=schwelle]').value = e.schwelle ?? 1;
  r.querySelectorAll('input').forEach(i => i.addEventListener('input', () => { anzeigeGeaendert = true; }));
  r.querySelector('button').addEventListener('click', () => { z.remove(); anzeigeGeaendert = true; });
  z.appendChild(r);
  return z;
}
function anzeigeLesen() {
  return [...document.querySelectorAll('#anzeigeListe .anzeige-eintrag')].map(z => {
    const w = k => z.querySelector(`[data-k=${k}]`);
    const e = { name: w('name').value.trim(), topic: w('topic').value.trim() };
    if (w('feld').value.trim()) e.feld = w('feld').value.trim();
    if (w('einheit').value.trim()) e.einheit = w('einheit').value.trim();
    e.art = w('laeuft').checked ? 'laeuft' : 'wert';
    if (e.art === 'laeuft') e.schwelle = Number(w('schwelle').value || 1);
    return e;
  });
}
$('anzeigeNeu').addEventListener('click', () => {
  if (document.querySelectorAll('#anzeigeListe .anzeige-eintrag').length >= 6) return;
  $('anzeigeListe').appendChild(anzeigeZeile()); anzeigeGeaendert = true;
});
$('anzeigeVerwerfen').addEventListener('click', () => { anzeigeGeaendert = false; anzeigeZeigen(anzeigeStand); $('anzeigeMeldung').textContent = ''; });
$('anzeigeSpeichern').addEventListener('click', async () => {
  const m = $('anzeigeMeldung');
  try {
    await api('/api/admin/anzeige', { method: 'POST', body: JSON.stringify({ eintraege: anzeigeLesen() }) });
    anzeigeGeaendert = false;
    m.textContent = 'gespeichert – die App zeigt es beim nächsten Abruf'; m.className = 'meldung ok';
    setTimeout(laden, 1500);
  } catch (e) { m.textContent = e.message; m.className = 'meldung fehler'; }
});

// ── Teilweise zuruecksetzen
document.querySelectorAll('#teilreset button').forEach(b => b.addEventListener('click', async () => {
  const m = $('teilMeldung'), bereich = b.dataset.b;
  if (!confirm(`Bereich „${b.textContent}“ wirklich zurücksetzen?`)) return;
  try {
    const res = await fetch('/api/admin/teilreset', {
      method: 'POST', body: JSON.stringify({ bereich }),
      headers: { 'Content-Type': 'application/json', 'x-admin-token': lies(TOKEN_KEY) || '', 'x-experte': $('teilPass').value }
    });
    const d = await res.json().catch(() => ({}));
    if (!res.ok) throw new Error(d.error || ('Fehler ' + res.status));
    $('teilPass').value = '';
    if (bereich === 'zugang') { schreib(TOKEN_KEY, null); m.textContent = 'Zugang zurückgesetzt – Board startet neu, danach /app aufrufen'; }
    else m.textContent = 'zurückgesetzt';
    m.className = 'meldung ok';
    anzeigeGeaendert = false;
    setTimeout(() => { laden(); infoLaden(); }, 1500);
  } catch (e) { m.textContent = e.message; m.className = 'meldung fehler'; }
}));

function zeile(e) {
  const z = document.createElement('div');
  z.className = 'zeile';
  const n = document.createElement('span');
  n.className = 'name';
  n.textContent = e.name;
  if (e.experte) {
    const x = document.createElement('span');
    x.className = 'hinweis';
    x.textContent = '🔒 nur mit Experten-Passwort';
    n.appendChild(x);
  }
  if (e.vorgabe !== undefined) {
    const abw = e.art === 'schalter' ? e.wert !== e.vorgabe : e.wert !== e.vorgabe;
    const v = document.createElement('span');
    v.className = 'hinweis';
    v.textContent = `◆ Empfehlung: ${e.art === 'schalter' ? (e.vorgabe ? 'an' : 'aus') : e.vorgabe}${abw ? ' – abweichend' : ''}`;
    if (abw) v.style.color = 'var(--orange, #e0a030)';
    n.appendChild(v);
  }
  z.appendChild(n);
  if (e.art === 'schalter') {
    const s = document.createElement('button');
    s.className = 'schalter' + (e.wert ? ' an' : '');
    s.setAttribute('aria-label', e.name);
    s.addEventListener('click', () => { s.classList.toggle('an'); setzen(e.id, s.classList.contains('an'), null, e.experte); });
    z.appendChild(s);
  } else {
    const f = document.createElement('input');
    f.className = 'zahl'; f.type = 'number'; f.inputMode = 'decimal';
    f.min = e.min; f.max = e.max; f.step = e.schritt;
    f.value = e.wert == null ? '' : e.wert;
    const m = document.createElement('span');
    m.className = 'hinweis';
    m.textContent = `${e.min} – ${e.max}`;
    n.appendChild(m);
    f.addEventListener('change', () => {
      const v = Number(f.value);
      if (!Number.isFinite(v) || v < e.min || v > e.max) { m.textContent = `erlaubt ${e.min} – ${e.max}`; m.style.color = 'var(--red)'; return; }
      m.style.color = ''; m.textContent = `${e.min} – ${e.max}`;
      setzen(e.id, v, null, e.experte);
    });
    z.appendChild(f);
  }
  return z;
}

let offen = false;   // waehrend eine Zahl bearbeitet wird, nicht neu zeichnen
document.addEventListener('focusin', e => { offen = e.target.classList && e.target.classList.contains('zahl'); });
document.addEventListener('focusout', () => { offen = false; });

async function laden() {
  if (offen) return;
  let d;
  try { d = await api('/api/admin/einstellungen'); } catch { return; }
  $('version').textContent = `${d.firmware || ''} · App ${d.app_version || ''}`;
  $('fwAktuell').textContent = d.firmware || '–';
  $('experteZustand').textContent = d.experte_gesetzt ? (d.experte_extern_min ? (d.experte_extern_min < 0 ? 'von außen: IMMER erlaubt' : `von außen erlaubt, noch ${d.experte_extern_min} min`) : 'nur im Heimnetz') : 'gesperrt (kein Passwort)';
  $('experteZustand').style.color = d.experte_extern_min ? 'var(--red)' : '';
  $('benutzerAnzeige').textContent = d.benutzer || '–';
  document.querySelectorAll('#darstellung button').forEach(b => b.classList.toggle('aktiv', b.dataset.wert === d.darstellung));
  if (!anzeigeGeaendert) anzeigeZeigen(d.zusatz);
  const gruppen = {};
  for (const e of d.liste || []) (gruppen[e.gruppe] = gruppen[e.gruppe] || []).push(e);
  const ziel = $('gruppen');
  ziel.textContent = '';
  for (const [name, liste] of Object.entries(gruppen)) {
    const s = document.createElement('section');
    s.className = 'block';
    s.style.marginBottom = '.8rem';
    s.innerHTML = '<div class="block-head"><span class="block-title"></span></div>';
    s.querySelector('.block-title').textContent = name.toUpperCase();
    liste.forEach(e => s.appendChild(zeile(e)));
    ziel.appendChild(s);
  }
}
document.querySelectorAll('#darstellung button').forEach(b => b.addEventListener('click', () => {
  document.querySelectorAll('#darstellung button').forEach(x => x.classList.toggle('aktiv', x === b));
  schreib('wem_theme_vorgabe', b.dataset.wert);
  themeAnwenden();
  setzen('darstellung', b.dataset.wert);
}));

// ── Passwoerter
async function passwort(art) {
  const p1 = $(art + 'Pass').value, p2 = $(art + 'Pass2').value, m = $(art + 'PassMeldung');
  m.className = 'meldung';
  if (p1 !== p2) { m.textContent = 'Passwörter stimmen nicht überein'; m.className = 'meldung fehler'; return; }
  const body = { art, passwort: p1 };
  if (art === 'app' && $('appUser').value.trim()) body.benutzer = $('appUser').value.trim();
  try {
    const d = await api('/api/admin/passwort', { method: 'POST', body: JSON.stringify(body) });
    if (d.token) schreib(TOKEN_KEY, d.token);
    $(art + 'Pass').value = ''; $(art + 'Pass2').value = ''; if (art === 'app') $('appUser').value = '';
    m.textContent = art === 'app' ? 'geändert – alle App-Geräte sind abgemeldet' : 'geändert';
    m.className = 'meldung ok';
    setTimeout(laden, 1500);
  } catch (e) { m.textContent = e.message; m.className = 'meldung fehler'; }
}
$('appPassBtn').addEventListener('click', () => passwort('app'));
$('adminPassBtn').addEventListener('click', () => passwort('admin'));

if (lies(TOKEN_KEY)) laden(); else zeigeLogin();
setInterval(laden, 10000);

// ── Firmware-Update: Pruefsumme anzeigen (und mit dem Release vergleichen), dann /update (ESPHome-OTA)
// SHA-256 auch ohne crypto.subtle (das gibt es im Browser nur ueber HTTPS, das Board spricht HTTP)
function sha256(bytes) {
  const K = new Uint32Array([0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,
    0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,
    0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,
    0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2]);
  const H = new Uint32Array([0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19]);
  const n = bytes.length, gesamt = Math.ceil((n + 9) / 64) * 64;
  const m = new Uint8Array(gesamt);
  m.set(bytes); m[n] = 0x80;
  const bits = n * 8, dv = new DataView(m.buffer);
  dv.setUint32(gesamt - 8, Math.floor(bits / 0x100000000)); dv.setUint32(gesamt - 4, bits >>> 0);
  const w = new Uint32Array(64), r = (x, k) => (x >>> k) | (x << (32 - k));
  for (let i = 0; i < gesamt; i += 64) {
    for (let t = 0; t < 16; t++) w[t] = dv.getUint32(i + t * 4);
    for (let t = 16; t < 64; t++) {
      const s0 = r(w[t-15], 7) ^ r(w[t-15], 18) ^ (w[t-15] >>> 3), s1 = r(w[t-2], 17) ^ r(w[t-2], 19) ^ (w[t-2] >>> 10);
      w[t] = (w[t-16] + s0 + w[t-7] + s1) | 0;
    }
    let [a, b, c, d, e, f, g, h] = H;
    for (let t = 0; t < 64; t++) {
      const t1 = (h + (r(e, 6) ^ r(e, 11) ^ r(e, 25)) + ((e & f) ^ (~e & g)) + K[t] + w[t]) | 0;
      const t2 = ((r(a, 2) ^ r(a, 13) ^ r(a, 22)) + ((a & b) ^ (a & c) ^ (b & c))) | 0;
      h = g; g = f; f = e; e = (d + t1) | 0; d = c; c = b; b = a; a = (t1 + t2) | 0;
    }
    H[0] += a; H[1] += b; H[2] += c; H[3] += d; H[4] += e; H[5] += f; H[6] += g; H[7] += h;
  }
  return Array.from(H, x => x.toString(16).padStart(8, '0')).join('');
}

let fwBytes = null, fwSumme = '';
function fwPruefen() {
  const soll = $('fwSoll').value.trim().toLowerCase();
  const passt = !soll || soll === fwSumme;
  $('fwBtn').disabled = !fwBytes || !passt || !$('fwPass').value;
  if (fwBytes && soll) {
    $('fwMeldung').textContent = passt ? 'Prüfsumme stimmt mit dem Release überein' : 'Prüfsumme stimmt NICHT – Datei nicht installieren';
    $('fwMeldung').className = 'meldung ' + (passt ? 'ok' : 'fehler');
  } else if (fwBytes) { $('fwMeldung').textContent = ''; }
}
$('fwDatei').addEventListener('change', async () => {
  const f = $('fwDatei').files[0];
  fwBytes = null; fwSumme = '';
  $('fwInfo').textContent = f ? 'berechne Prüfsumme …' : '';
  if (!f) return fwPruefen();
  const buf = new Uint8Array(await f.arrayBuffer());
  // ESP32-Abbild beginnt mit 0xE9 - schuetzt vor Verwechslungen (z. B. Bilddatei, ZIP)
  if (buf[0] !== 0xE9) { $('fwInfo').textContent = 'keine ESP32-Firmware (erstes Byte nicht 0xE9)'; return fwPruefen(); }
  fwSumme = sha256(buf);
  fwBytes = buf;
  $('fwInfo').textContent = `${f.name} · ${(buf.length / 1024).toFixed(0)} KB · SHA-256 ${fwSumme}`;
  fwPruefen();
});
$('fwSoll').addEventListener('input', fwPruefen);
$('fwPass').addEventListener('input', fwPruefen);
$('fwBtn').addEventListener('click', () => {
  const m = $('fwMeldung');
  const form = new FormData();
  form.append('update', new Blob([fwBytes], { type: 'application/octet-stream' }), 'firmware.bin');
  const xhr = new XMLHttpRequest();
  // Benutzer/Passwort an open(): der Browser beantwortet die Digest-Anfrage des Boards selbst
  xhr.open('POST', '/update', true, 'admin', $('fwPass').value);
  xhr.upload.onprogress = e => { if (e.lengthComputable) { m.textContent = `übertrage … ${Math.round(e.loaded / e.total * 100)} %`; m.className = 'meldung'; } };
  xhr.onload = () => {
    if (xhr.status === 401) { m.textContent = 'Admin-Passwort falsch'; m.className = 'meldung fehler'; return; }
    if (xhr.status !== 200 || !/success/i.test(xhr.responseText)) { m.textContent = 'Update abgelehnt: ' + (xhr.responseText || xhr.status); m.className = 'meldung fehler'; return; }
    m.textContent = 'installiert – Board startet neu, bitte etwa 30 s warten …'; m.className = 'meldung ok';
    $('fwPass').value = ''; $('fwBtn').disabled = true;
    const start = Date.now();
    const warten = setInterval(async () => {
      try {
        const res = await fetch('/api/einrichtung', { cache: 'no-store' });
        if (res.ok && Date.now() - start > 8000) { clearInterval(warten); m.textContent = 'Board läuft wieder'; laden(); }
      } catch { /* startet noch */ }
    }, 3000);
  };
  xhr.onerror = () => { m.textContent = 'Verbindung abgebrochen'; m.className = 'meldung fehler'; };
  xhr.send(form);
  $('fwBtn').disabled = true;
});

// ── Board: Infos, Log, Neustart
function dauer(s) {
  const t = Math.floor(s / 86400), h = Math.floor(s / 3600) % 24, m = Math.floor(s / 60) % 60;
  return (t ? t + ' d ' : '') + h + ' h ' + m + ' min';
}
async function infoLaden() {
  let d;
  try { d = await api('/api/admin/info'); } catch { return; }
  $('obZustand').textContent = d.oberflaeche || '';
  const zeilen = [
    ['Firmware', d.firmware], ['Oberfläche', `${d.oberflaeche || 'eingebaut'} (API ${d.api})`], ['Uhrzeit', d.uhr || '–'],
    ['Laufzeit', dauer(d.laufzeit_s)], ['Letzter Neustart', d.neustart_grund],
    ['WLAN', d.wlan ? `${d.wlan} (${d.wlan_rssi} dBm)` : '–'], ['IP', d.ip || '–'],
    ['Speicher frei', `${Math.round(d.speicher_frei / 1024)} KB (min. ${Math.round(d.speicher_min / 1024)} KB, Block ${Math.round(d.groesster_block / 1024)} KB)`],
    ['Dateisystem', !d.dateisystem ? 'nicht eingebaut' : d.dateisystem.bereit ? `${Math.round(d.dateisystem.kb / 1024 * 10) / 10} MB, belegt ${d.dateisystem.belegt_kb} KB` : d.dateisystem.fehler],
    ['SD-Karte', !d.sd ? 'nicht eingebaut' : d.sd.eingebunden ? `${d.sd.name || 'Karte'} · ${d.sd.mb} MB, frei ${d.sd.frei_mb} MB` : (d.sd.fehler || 'keine')],
    ['Flash', d.flash_echt ? `${d.flash_echt / 1048576} MB im Chip (Firmware für ${d.flash / 1048576} MB gebaut)` : '–'],
    ['CAN-Bus', d.bus_lebt ? `lebt (letzte Nachricht vor ${d.letzter_frame_s} s)` : 'still']
  ];
  const ziel = $('info');
  ziel.textContent = '';
  for (const [k, v] of zeilen) {
    const z = document.createElement('div');
    z.className = 'zeile';
    const a = document.createElement('span'); a.className = 'name'; a.textContent = k;
    const b = document.createElement('span'); b.style.cssText = 'font-family:var(--mono);font-size:.85rem;text-align:right'; b.textContent = v;
    z.append(a, b);
    ziel.appendChild(z);
  }
}
async function logLaden() {
  try {
    const zeilen = await api('/api/admin/log');
    const el = $('log');
    const unten = el.scrollTop + el.clientHeight >= el.scrollHeight - 5;
    el.textContent = zeilen.length ? zeilen.join('\n') : '(noch nichts)';
    if (unten) el.scrollTop = el.scrollHeight;
  } catch { /* abgemeldet */ }
}
$('logBtn').addEventListener('click', logLaden);
$('neustartBtn').addEventListener('click', async () => {
  if (!confirm('Board neu starten? Die Heizung läuft dabei normal weiter.')) return;
  try { await api('/api/admin/neustart', { method: 'POST', body: '{}' }); $('info').textContent = 'startet neu …'; } catch (e) { alert(e.message); }
});

// ── Sicherung
$('sicherungBtn').addEventListener('click', async () => {
  try {
    const d = await api('/api/admin/sicherung');
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([JSON.stringify(d, null, 2)], { type: 'application/json' }));
    a.download = `heizung-einstellungen-${(d.uhr || 'stand').replace(/[: ]/g, '-')}.json`;
    a.click();
  } catch (e) { $('sicherungMeldung').textContent = e.message; $('sicherungMeldung').className = 'meldung fehler'; }
});
$('sicherungDatei').addEventListener('change', () => { $('einspielenBtn').disabled = !$('sicherungDatei').files[0]; });
$('einspielenBtn').addEventListener('click', async () => {
  const m = $('sicherungMeldung');
  try {
    const text = await $('sicherungDatei').files[0].text();
    const pw = await expertePasswort('Leer lassen = Einstellungen der Gruppen Lesen und Experten bleiben unverändert') || '';
    const d = await api('/api/admin/sicherung', { method: 'POST', body: text, headers: pw ? { 'x-experte': pw } : {} });
    m.textContent = `${d.gesetzt} Einstellungen übernommen` + (d.fehler && d.fehler.length ? ` · übersprungen: ${d.fehler.join(', ')}` : '');
    m.className = 'meldung ok';
    setTimeout(laden, 1500);
  } catch (e) { m.textContent = e.message; m.className = 'meldung fehler'; }
});

// ── Werksreset
$('resetText').addEventListener('input', () => { $('resetBtn').disabled = $('resetText').value !== 'WERKSRESET'; });
$('resetBtn').addEventListener('click', async () => {
  if (!confirm('Wirklich alle gespeicherten Einstellungen und Zugangsdaten des Boards löschen?')) return;
  const m = $('resetMeldung');
  try {
    await api('/api/admin/werksreset', { method: 'POST', body: JSON.stringify({ bestaetigung: 'WERKSRESET', experte: $('resetPass').value }) });
    schreib(TOKEN_KEY, null);
    m.textContent = 'zurückgesetzt – Board startet neu, danach /app aufrufen'; m.className = 'meldung ok';
  } catch (e) { m.textContent = e.message; m.className = 'meldung fehler'; }
});

if (lies(TOKEN_KEY)) { infoLaden(); logLaden(); }
setInterval(() => { if (lies(TOKEN_KEY) && !document.hidden) { infoLaden(); logLaden(); } }, 10000);

// ── Expertenstufe
$('exPassBtn').addEventListener('click', async () => {
  const m = $('exPassMeldung');
  if ($('exPass').value !== $('exPass2').value) { m.textContent = 'Passwörter stimmen nicht überein'; m.className = 'meldung fehler'; return; }
  try {
    await api('/api/admin/passwort', { method: 'POST', body: JSON.stringify({ art: 'experte', passwort: $('exPass').value, experte_alt: $('exAlt').value }) });
    ['exAlt', 'exPass', 'exPass2'].forEach(id => { $(id).value = ''; });
    m.textContent = 'gesetzt – gilt sofort für /experten und das Update'; m.className = 'meldung ok';
    setTimeout(laden, 1500);
  } catch (e) { m.textContent = e.message; m.className = 'meldung fehler'; }
});
document.querySelectorAll('#exExtern button').forEach(b => b.addEventListener('click', async () => {
  const h = Number(b.dataset.h), m = $('exExternMeldung');
  if (h !== 0 && !confirm(h < 0 ? 'Expertenzugang UNBEGRENZT von außen erlauben? Das ist riskant.' : `Expertenzugang ${h} Stunde(n) von außen erlauben?`)) return;
  try {
    await api('/api/admin/experte-extern', { method: 'POST', body: JSON.stringify({ stunden: h, experte: $('exExternPass').value }) });
    $('exExternPass').value = '';
    m.textContent = h === 0 ? 'nur noch im Heimnetz' : 'erlaubt'; m.className = 'meldung ' + (h === 0 ? 'ok' : 'fehler');
    setTimeout(laden, 1000);
  } catch (e) { m.textContent = e.message; m.className = 'meldung fehler'; }
}));

// ── Oberflaechen-Paket
$('obBtn').addEventListener('click', async () => {
  const m = $('obMeldung'), f = $('obDatei').files[0];
  if (!f) { m.textContent = 'Paket wählen'; m.className = 'meldung fehler'; return; }
  m.textContent = 'übertrage …'; m.className = 'meldung';
  try {
    const res = await fetch('/api/admin/oberflaeche', {
      method: 'POST', body: await f.arrayBuffer(),
      headers: { 'Content-Type': 'application/octet-stream', 'x-admin-token': lies(TOKEN_KEY) || '', 'x-experte': $('obPass').value }
    });
    const d = await res.json().catch(() => ({}));
    if (!res.ok) throw new Error(d.error || ('Fehler ' + res.status));
    $('obPass').value = '';
    m.textContent = `Paket ${d.version} aktiv – Seite neu laden`; m.className = 'meldung ok';
    infoLaden();
  } catch (e) { m.textContent = e.message; m.className = 'meldung fehler'; }
});
$('obZurueckBtn').addEventListener('click', async () => {
  const m = $('obMeldung');
  try {
    const res = await fetch('/api/admin/oberflaeche-zurueck', {
      method: 'POST', body: '{}', headers: { 'Content-Type': 'application/json', 'x-admin-token': lies(TOKEN_KEY) || '', 'x-experte': $('obPass').value }
    });
    const d = await res.json().catch(() => ({}));
    if (!res.ok) throw new Error(d.error || ('Fehler ' + res.status));
    $('obPass').value = '';
    m.textContent = 'eingebaute Oberfläche aktiv – Seite neu laden'; m.className = 'meldung ok';
    infoLaden();
  } catch (e) { m.textContent = e.message; m.className = 'meldung fehler'; }
});

// ── Heizung (WEM): Adresse per CAN
async function wemLaden() {
  try {
    const d = await api('/api/admin/wem');
    $('wemEingetragen').textContent = d.eingetragen || '–';
    $('wemZustand').textContent = d.bus_lebt === false ? 'CAN-Bus still' : '';
    const e = $('wemErgebnis');
    if (d.laeuft) e.textContent = 'frage den WEM …';
    else if (d.ip) {
      e.textContent = `WEM meldet: ${d.ip}` + (d.maske ? ` · Maske ${d.maske}` : '') + (d.gateway ? ` · Gateway ${d.gateway}` : '') +
        (d.ip === d.eingetragen ? ' – stimmt mit der eingetragenen überein ✓' : ' – weicht ab');
      if (!$('wemIp').value) $('wemIp').value = d.ip;
    } else if (d.fehler) e.textContent = d.fehler;
    return d;
  } catch { return {}; }
}
$('wemSuchen').addEventListener('click', async () => {
  const m = $('wemMeldung');
  m.textContent = '';
  try {
    await api('/api/admin/wem-suchen', { method: 'POST', body: '{}' });
    $('wemIp').value = '';
    for (let i = 0; i < 12; i++) { await new Promise(r => setTimeout(r, 1000)); const d = await wemLaden(); if (!d.laeuft && (d.ip || d.fehler)) break; }
  } catch (e) { m.textContent = e.message; m.className = 'meldung fehler'; }
});
$('wemUebernehmen').addEventListener('click', async () => {
  const m = $('wemMeldung');
  try {
    await api('/api/admin/wem-adresse', { method: 'POST', body: JSON.stringify({ ip: $('wemIp').value.trim() }) });
    m.textContent = 'übernommen'; m.className = 'meldung ok';
    setTimeout(wemLaden, 1500);
  } catch (e) { m.textContent = e.message; m.className = 'meldung fehler'; }
});
if (lies(TOKEN_KEY)) wemLaden();

// WEM im Netz suchen (ohne CAN-Kabel): je Adresse eine Anfrage an Port 80, etwa 10 s
async function wemNetzSuchen(meldung, feld) {
  melde2(meldung, 'suche im Netz … (etwa 30 s)', true);
  try {
    await api('/api/admin/wem-netzsuche', { method: 'POST', body: '{}' });
    for (let i = 0; i < 90; i++) {
      await new Promise(r => setTimeout(r, 1000));
      const d = await api('/api/admin/wem');
      const n = d.netzsuche || {};
      if (n.laeuft) { melde2(meldung, `suche im Netz … ${n.fortschritt || 0} von 254`, true); continue; }
      const g = n.gefunden || [];
      if (!g.length) melde2(meldung, 'kein WEM im Netz gefunden – Adresse von Hand eintragen');
      else { $(feld).value = g[0]; melde2(meldung, `WEM gefunden: ${g.join(', ')} – mit ÜBERNEHMEN eintragen`, true); }
      return;
    }
  } catch (e) { melde2(meldung, e.message); }
}
function melde2(el, text, ok) { $(el).textContent = text; $(el).className = 'meldung ' + (ok ? 'ok' : 'fehler'); }
$('wemNetz').addEventListener('click', () => wemNetzSuchen('wemMeldung', 'wemIp'));

// Werte einmal per JSON abrufen (Paket json-lesen)
async function jsonStand() {
  try { const d = await api('/api/admin/json-abruf'); if (d.meldung) $('jsonStand').textContent = d.meldung + (d.warten_s ? ` · nächster in ${d.warten_s} s` : ''); }
  catch { /* nicht eingebunden */ }
}
$('jsonAbruf').addEventListener('click', async () => {
  try {
    await api('/api/admin/json-abruf', { method: 'POST', body: '{}' });
    $('jsonStand').textContent = 'frage den WEM …';
    setTimeout(jsonStand, 5000);
  } catch (e) { $('jsonStand').textContent = e.message; }
});
if (lies(TOKEN_KEY)) jsonStand();
