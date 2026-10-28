// Einrichtungs-Assistent auf dem Board (/einrichtung, ab v42): 1 Netz, 2 Heizung, 3 Fertig.
// Kommt direkt nach der Ersteinrichtung der Passwoerter (die App gibt das Admin-Token mit) oder von der
// Einstellungsseite. Nutzt nur die Admin-API des Boards.
const TOKEN_KEY = 'wem_admin_token';
const lies = k => { try { return localStorage.getItem(k); } catch { return null; } };
const schreib = (k, v) => { try { if (v == null) localStorage.removeItem(k); else localStorage.setItem(k, v); } catch { /* privat */ } };
const $ = id => document.getElementById(id);
const warte = ms => new Promise(r => setTimeout(r, ms));

async function api(pfad, opts = {}) {
  const res = await fetch(pfad, {
    ...opts, cache: 'no-store',
    headers: { 'Content-Type': 'application/json', 'x-admin-token': lies(TOKEN_KEY) || '', ...(opts.headers || {}) }
  });
  if (res.status === 401) { schreib(TOKEN_KEY, null); $('loginOverlay').classList.remove('hidden'); throw new Error('abgemeldet'); }
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(data.error || ('Fehler ' + res.status));
  return data;
}
function melde(el, text, ok) { $(el).textContent = text; $(el).className = 'meldung ' + (ok ? 'ok' : 'fehler'); }

// ── Anmeldung (nur wenn kein Admin-Token da ist)
$('loginBtn').addEventListener('click', async () => {
  try {
    const res = await fetch('/api/admin/login', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ pass: $('loginPass').value }) });
    const d = await res.json();
    if (!res.ok) throw new Error(d.error || 'Fehler');
    schreib(TOKEN_KEY, d.token);
    $('loginOverlay').classList.add('hidden');
    zeige(1);
  } catch (e) { $('loginError').textContent = e.message; $('loginError').classList.remove('hidden'); }
});
$('loginPass').addEventListener('keydown', e => { if (e.key === 'Enter') $('loginBtn').click(); });

// ── Schritte
let schritt = 1, stand = {}, liste = [];
function zeige(n) {
  schritt = n;
  document.querySelectorAll('.schritt').forEach(s => s.classList.toggle('hidden', Number(s.dataset.s) !== n));
  document.querySelectorAll('.schritte span').forEach(s => s.classList.toggle('aktiv', Number(s.dataset.s) === n));
  laden();
  if (n === 1 && !gesucht) { gesucht = true; wlanSuchen(); }   // empfangbare WLANs gleich beim Oeffnen
  if (n === 2) heizungVorbereiten();
  if (n === 3) zusammenfassen();
}
document.querySelectorAll('.weiter').forEach(b => b.addEventListener('click', () => zeige(Number(b.dataset.z))));

function signal(rssi) { return rssi >= -60 ? 'gut' : rssi >= -72 ? 'mittel' : 'schwach'; }

async function laden() {
  try { stand = await api('/api/admin/einrichtung'); } catch { return; }
  const w = stand.wlan || {};
  $('netzStand').innerHTML = '';
  const n = document.createElement('span');
  n.className = 'name';
  n.textContent = w.verbunden ? `✓ verbunden mit „${w.ssid}“` : '✗ nicht mit einem WLAN verbunden';
  const h = document.createElement('span');
  h.className = 'hinweis';
  h.textContent = w.verbunden ? `Adresse ${w.ip || '–'} · Signal ${signal(w.rssi)} (${w.rssi} dBm)` : (w.hotspot ? 'Hotspot aktiv' : '');
  n.appendChild(h);
  $('netzStand').appendChild(n);
  if (w.ergebnis && $('wlanMeldung').dataset.laeuft) $('wlanMeldung').textContent = w.ergebnis;
  if (w.netze) netzeZeigen(w.netze, w.ssid);

  const b = $('busStand');
  b.innerHTML = '';
  const bn = document.createElement('span');
  bn.className = 'name';
  bn.textContent = stand.bus_lebt ? '✓ CAN-Bus lebt' : '✗ CAN-Bus still';
  const bh = document.createElement('span');
  bh.className = 'hinweis';
  bh.textContent = stand.bus_lebt ? 'das Board hört die Heizung' : 'Kabel an H/L (und GND) prüfen – ohne Bus geht nur der JSON-Weg über das Netz';
  bn.appendChild(bh);
  b.appendChild(bn);
  const g = $('geraete');
  g.textContent = '';
  for (const k of stand.knoten || []) {
    const z = document.createElement('div');
    z.className = 'zeile';
    z.textContent = `Knoten ${k.nr}: ${k.name}`;
    g.appendChild(z);
  }
}
setInterval(() => { if (lies(TOKEN_KEY) && !document.hidden) laden(); }, 3000);

// ── Schritt 1: empfangbare WLANs
function netzeZeigen(netze, aktuell) {
  const l = $('wlanListe');
  l.textContent = '';
  if (!netze.length) { l.innerHTML = '<div class="zeile"><span class="name">keine gefunden</span></div>'; return; }
  for (const n of netze) {
    const z = document.createElement('div');
    z.className = 'zeile';
    z.style.cursor = 'pointer';
    const t = document.createElement('span');
    t.className = 'name';
    t.textContent = (n.ssid === aktuell ? '✓ ' : '') + n.ssid + (n.gesichert ? ' 🔒' : '');
    const s = document.createElement('span');
    s.className = 'hinweis';
    s.textContent = `${signal(n.rssi)} (${n.rssi} dBm)`;
    z.append(t, s);
    z.addEventListener('click', () => { $('wlanSsid').value = n.ssid; $('wlanPass').focus(); });
    l.appendChild(z);
  }
}
let gesucht = false;
async function wlanSuchen() {
  $('wlanListe').innerHTML = '<div class="zeile"><span class="name">suche empfangbare WLANs … (einige Sekunden)</span></div>';
  try {
    await api('/api/admin/wlan-suchen', { method: 'POST', body: '{}' });
    await warte(5000);
    await laden();
  } catch (e) { melde('wlanMeldung', e.message); }
}
$('wlanSuchen').addEventListener('click', wlanSuchen);

// ── Schritt 1: WLAN wechseln
$('wlanBtn').addEventListener('click', async () => {
  const ssid = $('wlanSsid').value.trim();
  if (!ssid) return melde('wlanMeldung', 'WLAN-Namen eintragen');
  if (!confirm(`Board mit „${ssid}“ verbinden? Klappt es nicht binnen 30 s, bleibt das bisherige WLAN. Gelingt es, hat das Board danach evtl. eine neue Adresse.`)) return;
  try {
    await api('/api/admin/wlan', { method: 'POST', body: JSON.stringify({ ssid, passwort: $('wlanPass').value }) });
    $('wlanPass').value = '';
    $('wlanMeldung').dataset.laeuft = '1';
    melde('wlanMeldung', 'verbinde … (bis 30 s)', true);
  } catch (e) { melde('wlanMeldung', e.message); }
});

// ── Schritt 2: Heizung
async function wemLaden() {
  const d = await api('/api/admin/wem');
  $('wemText').textContent = d.eingetragen ? `eingetragen: ${d.eingetragen}` : 'noch keine eingetragen';
  return d;
}
async function wemSuchen() {
  melde('wemMeldung', 'frage den WEM am Bus …', true);
  try {
    await api('/api/admin/wem-suchen', { method: 'POST', body: '{}' });
    for (let i = 0; i < 12; i++) {
      await warte(1000);
      const d = await wemLaden();
      if (d.laeuft) continue;
      if (d.ip) {
        $('wemIp').value = d.ip;
        melde('wemMeldung', d.ip === d.eingetragen ? `WEM meldet ${d.ip} – stimmt ✓` : `WEM meldet ${d.ip} – mit ÜBERNEHMEN eintragen`, true);
      } else if (d.fehler) melde('wemMeldung', d.fehler);
      return;
    }
  } catch (e) { melde('wemMeldung', e.message); }
}
$('wemSuchen').addEventListener('click', wemSuchen);
$('wemUebernehmen').addEventListener('click', async () => {
  try {
    await api('/api/admin/wem-adresse', { method: 'POST', body: JSON.stringify({ ip: $('wemIp').value.trim() }) });
    melde('wemMeldung', 'übernommen ✓', true);
    setTimeout(wemLaden, 1500);
  } catch (e) { melde('wemMeldung', e.message); }
});

// Schaltweg: der Schalter "Schalten per CAN" aus der Liste der Einstellungen
function wegZeigen() {
  const e = liste.find(x => x.name.startsWith('Schalten per CAN'));
  document.querySelectorAll('#schaltweg button').forEach(b => b.classList.toggle('aktiv', !!e && (b.dataset.can === '1') === !!e.wert));
  $('wegHinweis').textContent = stand.bus_lebt ? 'empfohlen: CAN (schnell, direkt am Bus)' : 'Bus still – JSON über das Netz, bis das Kabel dran ist';
}
document.querySelectorAll('#schaltweg button').forEach(b => b.addEventListener('click', async () => {
  const e = liste.find(x => x.name.startsWith('Schalten per CAN'));
  if (!e) return melde('wegMeldung', 'Schalter nicht gefunden');
  try {
    await api('/api/admin/einstellungen', { method: 'POST', body: JSON.stringify({ id: e.id, wert: b.dataset.can === '1' }) });
    melde('wegMeldung', 'gespeichert', true);
    await warte(1200);
    liste = (await api('/api/admin/einstellungen')).liste || [];
    wegZeigen();
  } catch (x) { melde('wegMeldung', x.message); }
}));

async function heizungVorbereiten() {
  try {
    const e = await api('/api/admin/einstellungen');
    liste = e.liste || [];
    $('version').textContent = e.firmware || '';
    await laden();
    wegZeigen();
    const d = await wemLaden();
    if (stand.bus_lebt && !d.ip) wemSuchen();   // Bus lebt: gleich einmal fragen
  } catch { /* abgemeldet */ }
}

// ── Schritt 3
async function zusammenfassen() {
  const z = $('zusammenfassung');
  z.textContent = '';
  let wem = {};
  try { wem = await api('/api/admin/wem'); } catch { /* */ }
  const e = liste.find(x => x.name.startsWith('Schalten per CAN'));
  const zeilen = [
    [stand.wlan && stand.wlan.verbunden, `WLAN: ${stand.wlan && stand.wlan.ssid ? stand.wlan.ssid : '–'}`],
    [stand.bus_lebt, stand.bus_lebt ? 'CAN-Bus lebt' : 'CAN-Bus still'],
    [!!wem.eingetragen, `WEM-Adresse: ${wem.eingetragen || 'fehlt'}`],
    [true, `Schalten über ${e && e.wert ? 'CAN' : 'JSON'}`],
  ];
  for (const [ok, text] of zeilen) {
    const r = document.createElement('div');
    r.className = 'zeile';
    r.innerHTML = `<span class="${ok ? 'ok-zeichen' : 'warn-zeichen'}">${ok ? '✓' : '!'}</span>`;
    const t = document.createElement('span');
    t.className = 'name';
    t.textContent = text;
    r.prepend(t);
    z.appendChild(r);
  }
}

if (lies(TOKEN_KEY)) zeige(1); else $('loginOverlay').classList.remove('hidden');
