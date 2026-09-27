// Heizungs-App fuer das Weishaupt-CAN-Board - liest nur MQTT, spricht nie mit dem WEM.
//
//   Lesen:     MQTT-Werte des Boards (<geraet>/sensor/+/state), die das Board am Kesselbus
//              mithoert oder selbst abfragt.
//   Schalten:  MQTT-Befehl an das Board (cmd/heizkreis, cmd/warmwasser). Das Board setzt ihn
//              mit Sperrminute, Warteschlange und Kontrolle am Bus um.
//   Status:    MQTT-Befehl cmd/status - das Board liest die Betriebsarten einmal vom Bus
//              (nur CAN-Leseanfragen, kein WEM-JSON).
// Frueher fragte die App die JSON-Schnittstelle des WEM direkt ab - die ist empfindlich und
// wurde dadurch mehrfach bis zum Stromlos-Machen gesperrt.

import express from 'express';
import path from 'path';
import fs from 'fs';
import mqtt from 'mqtt';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const app = express();
app.use(express.json());

const PORT = Number(process.env.PORT || 3000);
const APP_USERNAME = process.env.APP_USERNAME || '';
const APP_PASSWORD = process.env.APP_PASSWORD || '';
// Ohne Login startet die App nicht - sonst stuende ein offener Heizungsschalter im Netz.
if (!APP_USERNAME || !APP_PASSWORD) {
  console.error('FEHLER: APP_USERNAME und APP_PASSWORD muessen gesetzt sein (.env). Die App startet ohne Login nicht.');
  process.exit(1);
}
const MQTT_URL = process.env.MQTT_URL || 'mqtt://localhost:1883';
const MQTT_USER = process.env.MQTT_USER || '';
const MQTT_PASS = process.env.MQTT_PASS || '';
const GERAET = process.env.MQTT_GERAET || 'weact-can485-weishaupt';
// Werte, die laenger nicht aktualisiert wurden, gelten als veraltet (Board weg / Bus tot)
const VERALTET_MS = Number(process.env.VERALTET_MS || 10 * 60 * 1000);
// Weboberflaeche des Boards (nur als Link in der Kopfzeile; die App reicht nichts durch, das Board
// fragt sein Passwort selbst ab). Leer oder kein http(s) = Knopf ausgeblendet.
const BOARD_URL = /^https?:\/\/\S+$/i.test((process.env.BOARD_URL || '').trim()) ? process.env.BOARD_URL.trim() : '';

// Heizkreis-Betriebsarten (Code wie am Bus, Knoten 1 0x2933/2)
const modeMap = {
  1: 'Standby',
  2: 'Zeitprogramm 1',
  3: 'Zeitprogramm 2',
  4: 'Zeitprogramm 3',
  5: 'Sommer',
  6: 'Komfort',
  7: 'Normal',
  8: 'Absenk'
};

// ── MQTT: letzte Werte des Boards ─────────────────────────────
const werte = new Map();     // name -> { v: string, t: ms }
let mqttVerbunden = false;

const client = mqtt.connect(MQTT_URL, {
  username: MQTT_USER, password: MQTT_PASS,
  clientId: `heizungsapp-${Math.random().toString(16).slice(2, 8)}`,
  reconnectPeriod: 5000
});
client.on('connect', () => {
  mqttVerbunden = true;
  client.subscribe(`${GERAET}/sensor/+/state`);
  client.subscribe(`${GERAET}/status`);
  console.log(`MQTT verbunden: ${MQTT_URL}`);
});
client.on('close', () => { mqttVerbunden = false; });
client.on('error', e => console.log(`MQTT-Fehler: ${e.message}`));
client.on('message', (topic, payload) => {
  const teile = topic.split('/');
  const name = teile.length === 4 ? teile[2] : teile[teile.length - 1];
  werte.set(name, { v: payload.toString(), t: Date.now() });
});

// Einstellungen (Betriebsart, Sollwerte) liest das Board nur bei Anlass - sie veralten nicht.
const OHNE_ALTERSGRENZE = new Set(['aussentemperatur', 'heizkreis_betriebsart_code',
  'warmwasser_betriebsart_code', 'warmwasser_soll_normal', 'warmwasser_soll_aktuell']);
function zahl(name) {
  const w = werte.get(name);
  if (!w || (Date.now() - w.t > VERALTET_MS && !OHNE_ALTERSGRENZE.has(name))) return null;
  const n = Number(w.v);
  return Number.isFinite(n) ? n : null;     // "nan" (unbekannt) -> null
}
function text(name) {
  const w = werte.get(name);
  if (!w || w.v === '' || w.v === 'unbekannt') return null;
  return w.v;
}

// ── Token-Auth (unveraendert) ─────────────────────────────────
const TOKEN_DATEI = process.env.TOKEN_FILE || '/data/tokens.json';
const sessionTokens = new Set();
try {
  const arr = JSON.parse(fs.readFileSync(TOKEN_DATEI, 'utf8'));
  if (Array.isArray(arr)) arr.forEach(t => typeof t === 'string' && sessionTokens.add(t));
  console.log(`${sessionTokens.size} Sitzungs-Token aus ${TOKEN_DATEI} geladen`);
} catch { /* keine Datei = frischer Start */ }
function tokensSichern() {
  try {
    fs.mkdirSync(path.dirname(TOKEN_DATEI), { recursive: true });
    fs.writeFileSync(TOKEN_DATEI, JSON.stringify([...sessionTokens]), { mode: 0o600 });
  } catch (e) { console.log(`Token-Datei nicht schreibbar: ${e.message}`); }
}
function generateToken() {
  return Math.random().toString(36).slice(2) + Math.random().toString(36).slice(2);
}
function authMiddleware(req, res, next) {
  if (req.path === '/api/login') return next();
  const token = req.headers['x-app-token'] || '';
  if (!sessionTokens.has(token)) return res.status(401).json({ error: 'Nicht autorisiert' });
  next();
}

app.use(express.static(path.join(__dirname, '..', 'public')));
app.use(authMiddleware);

app.post('/api/login', (req, res) => {
  const { user, pass } = req.body || {};
  if (user === APP_USERNAME && pass === APP_PASSWORD) {
    const token = generateToken();
    sessionTokens.add(token); tokensSichern();
    return res.json({ token });
  }
  res.status(401).json({ error: 'Falsche Zugangsdaten' });
});

// ── API ───────────────────────────────────────────────────────
app.get('/api/status', (req, res) => {
  const online = text('status') === 'online';
  // Brenner aus den ZAHLEN (mit Altersgrenze): "brenner" 1 = Flamme an, "brennerphase" 0-4.
  // Bis Firmware v19 gab es keine Brennerphase - dann nur an/aus.
  const flamme = zahl('brenner');
  const phase = zahl('brennerphase');
  const PHASE = ['Aus', 'Vorlüften', 'An', 'An', 'Nachlüften'];
  const modeCode = zahl('heizkreis_betriebsart_code');
  const letzterFrame = zahl('letzter_can-frame_vor');
  // Kesselstatus (PDO 0x182, alle 5 s): 0 Standby, 1 Aus, 10 Heizbetrieb, 15 Warmwasserbetrieb, 101 Kaminfeger, 104 Wartung
  const kstatus = zahl('kesselstatus_code');
  const ZWECK = { 10: 'Heizung', 15: 'Warmwasser', 101: 'Kaminfeger', 104: 'Wartung' };
  const kesselZweck = kstatus != null ? (ZWECK[Math.round(kstatus)] || null) : null;
  // Warmwasser-Ladung durch den Kessel (Gas) = Kesselstatus Warmwasserbetrieb; ohne Status: "Warmwasser aktiv"
  const wwAktiv = text('warmwasser_aktiv');
  // Heizkreis fordert Waerme an: Statusbit 0x0040 (PDO 0x1C1, nur bei Aenderung - daher ohne Altersgrenze)
  // oder Heizanforderung > 0 (PDO 0x241). Steht der Kessel dabei im Warmwasserbetrieb, wartet die Heizung.
  const hkBitsRoh = werte.get('heizkreis_status_code');
  const hkBits = hkBitsRoh ? Number(hkBitsRoh.v) : NaN;
  const heizanf = zahl('heizanforderung');
  const heizungFordert = (Number.isFinite(hkBits) && (Math.round(hkBits) & 0x0040) !== 0) || (heizanf != null && heizanf > 0);
  const gas = kstatus != null ? Math.round(kstatus) === 15 : (wwAktiv === 'Ein' ? true : wwAktiv === 'Aus' ? false : null);
  if (!mqttVerbunden || !online) {
    return res.status(503).json({ error: mqttVerbunden ? 'CAN-Board nicht erreichbar' : 'MQTT-Broker nicht erreichbar' });
  }
  res.json({
    outsideTempC:     zahl('aussentemperatur'),
    kesselTempC:      zahl('kesseltemperatur'),
    abgasTempC:       zahl('abgastemperatur'),
    ruecklaufTempC:   zahl('ruecklauf'),          // Ruecklauf VPT, passiv vom Board (seit v17)
    vorlaufIstC:      zahl('vorlauf_vpt') ?? zahl('vorlauf_heizkreis'),   // passiv; HZK0-Vorlauf nur bei Abfrage
    vorlaufSollC:     zahl('vorlauf_soll'),
    warmwasserC:      zahl('warmwasser'),
    warmwasser:       text('warmwasser_betriebsart'),          // Vorgabe "Ein" / "Aus" (im WEM, gelesen nach Start und Schalten)
    warmwasserAktiv:  text('warmwasser_aktiv'),                // Ist: laedt gerade "Ein" / "Aus" (passiv, Kesselstatus)
    ladung:           { gas },                                  // Speicherladung durch den Kessel
    kesselZweck,                                               // Heizung / Warmwasser / Kaminfeger / Wartung / null
    heizungWartet:    kesselZweck === 'Warmwasser' && heizungFordert,
    kesselZweckWarnung: kesselZweck === 'Kaminfeger' || kesselZweck === 'Wartung',
    brennerAn:        flamme == null ? null : flamme === 1,
    brennerText:      phase != null && PHASE[phase] ? PHASE[phase] : (flamme == null ? null : (flamme === 1 ? 'An' : 'Aus')),
    modeCode:         modeCode != null ? Math.round(modeCode) : null,
    modeLabel:        text('heizkreis_betriebsart'),
    modeAktuellLabel: text('heizkreis_status'),
    schaltStatus:     text('ergebnis_letzter_schaltbefehl'),
    schaltProtokoll:  text('schaltprotokoll'),                 // letzte Befehle mit Ergebnis, neueste zuerst (App: Vorgabe)
    warteschlange:    text('warteschlange'),
    statusGelesen:    text('status_gelesen'),                  // "27.09. 14:05:12 gelesen" / "angefordert ..." (ab Firmware v21)
    busAlterS:        letzterFrame,
    source: 'can-board'
  });
});

app.get('/api/energie', (req, res) => {
  res.json({
    heizungKwh: zahl('w__rmemenge_vortag_heizung'),
    warmwasserKwh: zahl('w__rmemenge_vortag_ww'),
    gesamtKwh: zahl('w__rmemenge_vortag_gesamt'),
    source: 'can-board'
  });
});

app.post('/api/mode', (req, res) => {
  const code = Number((req.body || {}).modeCode);
  if (!modeMap[code]) return res.status(400).json({ error: 'Unbekannte Betriebsart' });
  if (!mqttVerbunden) return res.status(503).json({ error: 'MQTT-Broker nicht erreichbar' });
  client.publish(`${GERAET}/cmd/heizkreis`, String(code), { qos: 1 }, err => {
    if (err) return res.status(500).json({ error: `Senden fehlgeschlagen: ${err.message}` });
    res.json({ ok: true, queued: true, modeCode: code, modeLabel: modeMap[code], source: 'can-board' });
  });
});

app.post('/api/warmwasser', (req, res) => {
  const an = (req.body || {}).an;
  if (typeof an !== 'boolean') return res.status(400).json({ error: 'an: true oder false' });
  if (!mqttVerbunden) return res.status(503).json({ error: 'MQTT-Broker nicht erreichbar' });
  client.publish(`${GERAET}/cmd/warmwasser`, an ? 'Ein' : 'Aus', { qos: 1 }, err => {
    if (err) return res.status(500).json({ error: `Senden fehlgeschlagen: ${err.message}` });
    res.json({ ok: true, queued: true, an });
  });
});

// Betriebsarten einmal vom Bus lesen lassen (Board nimmt das hoechstens alle 10 s an)
app.post('/api/lesen', (req, res) => {
  if (!mqttVerbunden) return res.status(503).json({ error: 'MQTT-Broker nicht erreichbar' });
  client.publish(`${GERAET}/cmd/status`, '1', { qos: 1 }, err => {
    if (err) return res.status(500).json({ error: `Senden fehlgeschlagen: ${err.message}` });
    res.json({ ok: true });
  });
});

app.get('/api/meta', (req, res) => {
  res.json({
    app: 'weishaupt-heizungsapp',
    boardUrl: BOARD_URL || null,   // nur fuer angemeldete Nutzer (authMiddleware)
    modes: Object.entries(modeMap).map(([value, label]) => ({ value: Number(value), label }))
  });
});

app.get('*', (req, res) => {
  res.sendFile(path.join(__dirname, '..', 'public', 'index.html'));
});

app.listen(PORT, () => console.log(`Heizungs-App auf Port ${PORT}, Daten vom CAN-Board ueber MQTT`));
