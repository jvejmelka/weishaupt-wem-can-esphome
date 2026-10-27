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
    laden();
  } catch { err.textContent = 'Verbindungsfehler'; err.classList.remove('hidden'); }
}
$('loginBtn').addEventListener('click', login);
$('loginPass').addEventListener('keydown', e => { if (e.key === 'Enter') login(); });
$('logoutBtn').addEventListener('click', () => { schreib(TOKEN_KEY, null); zeigeLogin(); });

// ── Einstellungen anzeigen
async function setzen(id, wert, meldungEl) {
  try {
    await api('/api/admin/einstellungen', { method: 'POST', body: JSON.stringify({ id, wert }) });
    if (meldungEl) { meldungEl.textContent = 'gespeichert'; meldungEl.className = 'meldung ok'; }
    setTimeout(laden, 1500);   // das Board uebernimmt den Wert in der Hauptschleife
  } catch (e) {
    if (meldungEl) { meldungEl.textContent = e.message; meldungEl.className = 'meldung fehler'; }
  }
}

function zeile(e) {
  const z = document.createElement('div');
  z.className = 'zeile';
  const n = document.createElement('span');
  n.className = 'name';
  n.textContent = e.name;
  z.appendChild(n);
  if (e.art === 'schalter') {
    const s = document.createElement('button');
    s.className = 'schalter' + (e.wert ? ' an' : '');
    s.setAttribute('aria-label', e.name);
    s.addEventListener('click', () => { s.classList.toggle('an'); setzen(e.id, s.classList.contains('an')); });
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
      setzen(e.id, v);
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
  $('benutzerAnzeige').textContent = d.benutzer || '–';
  document.querySelectorAll('#darstellung button').forEach(b => b.classList.toggle('aktiv', b.dataset.wert === d.darstellung));
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
