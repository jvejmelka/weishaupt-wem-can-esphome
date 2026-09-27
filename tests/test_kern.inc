// ---------------------------------------------------------------- Zustandsklassen (kern.h, ab v29)
// Wird in test_regeln.cpp eingebunden. Bis v28 standen Zustand und Logik als ESPHome-Globals und
// Lambdas in den YAML-Paketen. Die Namensraeume "alt" unten sind der Code von v28, fast woertlich
// (id(x) -> g.x). Die Tests treiben alte und neue Fassung mit denselben - teils zufaelligen -
// Eingaben und verlangen gleiche Ergebnisse und gleichen Zustand. So ist "Verhalten unveraendert"
// nicht behauptet, sondern geprueft. Einzige Ausnahme ist die Fehlerbehebung in hk_takt (v29), die
// auch in der Vergleichsfassung steht; der Test kern_hk_lesung_wiederholt_nie haelt sie fest.

#include <random>

namespace alt {

// Globals der Pakete kessel/warmwasser (v28)
struct G {
  uint32_t letzter_frame = 0, bus_seit = 0, anlass_ms = 0, anlass_hk_ms = 0, anlass_ww_ms = 0;
  uint32_t pause_ms = 60000, hk_erledigt = 0, hk_letzte_lesung = 0, status_manuell_ms = 0, ww_erledigt = 0;
  bool start_gelesen = false, hk_jetzt_lesen = false, ww_jetzt_lesen = false, statusbits_anlass = true;
  int statusbits_vorher = -1;
  uint32_t anlaufpause_min = 10;
};

// kessel.yaml on_frame 0x000 (erster Teil)
inline void frame(G &g, uint32_t millis) {
  if (g.bus_seit == 0) { g.bus_seit = millis | 1; g.pause_ms = 60000; g.start_gelesen = false; }
  else if (millis - g.letzter_frame > 30000) { g.bus_seit = millis | 1; g.pause_ms = g.anlaufpause_min * 60000UL; g.start_gelesen = false; }
  g.letzter_frame = millis;
}
// kessel.yaml interval 40s/60s/300s: Bedingung
inline bool senden_bed(const G &g, uint32_t millis) {
  return millis - g.letzter_frame < 30000 && g.bus_seit != 0 && millis - g.bus_seit > g.pause_ms;
}
// kessel.yaml interval 5s - mit der Fehlerbehebung aus v29: hk_jetzt_lesen wird vor der
// Bus-Pruefung zurueckgesetzt (v28 setzte es erst danach, siehe Test kern_hk_lesung_wiederholt_nie)
inline void hk_takt(G &g, uint32_t jetzt) {
  bool bus_ok = jetzt - g.letzter_frame < 30000 && g.bus_seit != 0 && jetzt - g.bus_seit > g.pause_ms;
  g.hk_jetzt_lesen = false;
  if (!bus_ok) return;
  if (!g.start_gelesen) { g.start_gelesen = true; g.anlass_ms = (jetzt - 5000) | 1; }
  uint32_t a = g.anlass_ms;
  if (g.anlass_hk_ms != 0 && (a == 0 || (int32_t) (g.anlass_hk_ms - a) > 0)) a = g.anlass_hk_ms;
  if (a == 0 || a == g.hk_erledigt) return;
  if (jetzt - a < 5000) return;
  if (g.hk_letzte_lesung != 0 && jetzt - g.hk_letzte_lesung < 120000) return;
  g.hk_erledigt = a; g.hk_letzte_lesung = jetzt | 1; g.hk_jetzt_lesen = true;
}
// warmwasser.yaml interval 5s
inline void ww_takt(G &g, uint32_t millis) {
  g.ww_jetzt_lesen = false;
  uint32_t a = g.anlass_ms;
  if (g.anlass_ww_ms != 0 && (a == 0 || (int32_t) (g.anlass_ww_ms - a) > 0)) a = g.anlass_ww_ms;
  if (a == 0 || a == g.ww_erledigt) return;
  if (millis - a < 8000) return;
  if (millis - g.letzter_frame > 30000) return;
  g.ww_erledigt = a; g.ww_jetzt_lesen = true;
}
// kessel.yaml script status_lesen: 0 schweigt, 1 verworfen, 2 Anlaufpause, 3 angefordert
inline int status_lesen(G &g, uint32_t jetzt) {
  if (g.bus_seit == 0 || jetzt - g.letzter_frame > 30000) return 0;
  if (g.status_manuell_ms != 0 && jetzt - g.status_manuell_ms < 10000) return 1;
  g.status_manuell_ms = jetzt | 1;
  g.hk_letzte_lesung = 0;
  g.anlass_ms = (jetzt - 5000) | 1;
  return jetzt - g.bus_seit <= g.pause_ms ? 2 : 3;
}
// kessel.yaml on_frame 0x1C1
inline void statusbits(G &g, uint16_t m, uint32_t millis) {
  int kern = m & ~0x0050;
  if (g.statusbits_anlass && g.statusbits_vorher >= 0 && kern != g.statusbits_vorher) g.anlass_ms = millis | 1;
  g.statusbits_vorher = kern;
}
// verdacht.yaml script vd_regel (Anlass-Teil)
inline void regel(G &g, int ziel, uint32_t jetzt) {
  if (ziel & 1) { g.hk_letzte_lesung = 0; g.anlass_hk_ms = (jetzt - 5000) | 1; }
  if (ziel & 2) g.anlass_ww_ms = (jetzt - 8000) | 1;
}
// kessel.yaml text_sensor "Eigene CAN-Anfragen"
inline std::string anfragen_text(bool an, const G &g, uint32_t millis) {
  if (!an) return "aus - nur mithoeren (Betriebsarten bei Anlass)";
  if (g.bus_seit == 0 || millis - g.letzter_frame >= 30000) return "aus - Bus schweigt";
  uint32_t pause = g.pause_ms, seit = millis - g.bus_seit;
  if (seit <= pause) return "Anlaufpause, noch " + std::to_string((pause - seit) / 60000 + 1) + " min nur mithoeren";
  return "aktiv (40 s / 60 s / 5 min)";
}

// mqtt.yaml cmd/lesen bzw. Knopf "Lesen" (Ergebnis als Meldung, Senden als Frame)
struct Lese { bool aktiv = false; int kn = 0, idx = 0, sub = 0; };
inline std::string lesen(Lese &c, const std::string &x, uint32_t lf, uint32_t millis, uint32_t &cid, F &d) {
  unsigned kn = 0, idx = 0, sub = 0;
  if (sscanf(x.c_str(), "%x %x %x", &kn, &idx, &sub) != 3 || kn < 1 || kn > 127 || idx > 0xFFFF || sub > 0xFF)
    return "ungueltig - Format: 01 2933 02";
  if (millis - lf > 30000) return "Bus tot - nicht gesendet";
  c.kn = kn; c.idx = idx; c.sub = sub; c.aktiv = true;
  d = {0x40, (uint8_t) (idx & 0xFF), (uint8_t) (idx >> 8), (uint8_t) sub, 0, 0, 0, 0};
  cid = 0x600 + kn;
  return "gesendet, warte ...";
}
// kessel.yaml on_frame 0x000 (Antwort auf den Lesebefehl); "" = keine
inline std::string antwort(Lese &c, uint32_t can_id, const F &x) {
  if (!c.aktiv || can_id != (uint32_t) (0x580 + c.kn) || x.size() < 8) return "";
  if (((x[2] << 8) | x[1]) != c.idx || x[3] != c.sub) return "";
  char t[80];
  if (x[0] == 0x80) {
    snprintf(t, sizeof(t), "%02X %04X/%02X: Abbruch %02X%02X%02X%02X (Objekt fehlt?)", c.kn, c.idx, c.sub, x[7], x[6], x[5], x[4]);
  } else {
    int n = (x[0] == 0x4F) ? 1 : (x[0] == 0x4B) ? 2 : (x[0] == 0x47) ? 3 : 4;
    uint32_t u = 0;
    for (int i = n - 1; i >= 0; i--) u = (u << 8) | x[4 + i];
    long v = (n == 2) ? (long) (int16_t) u : (n == 1) ? (long) u : (long) (int32_t) u;
    snprintf(t, sizeof(t), "%02X %04X/%02X = %ld (0x%0*lX, %d Byte)", c.kn, c.idx, c.sub, v, n * 2, (unsigned long) u, n);
  }
  c.aktiv = false;
  return t;
}

// mqtt.yaml cmd/scan und interval 25ms
struct Scan { bool aktiv = false; int knoten = 1, idx = 0, ende = 0, sub = 0, maxsub = 0; };
inline bool scan_start(Scan &s, const std::string &x) {
  unsigned kn = 0, a = 0, e = 0, ms = 0;
  if (sscanf(x.c_str(), "%x %x %x %x", &kn, &a, &e, &ms) != 4 || kn < 1 || kn > 127 || a > e || e > 0xFFFF || ms > 10) return false;
  s.knoten = kn; s.idx = a; s.ende = e; s.maxsub = ms; s.sub = 0; s.aktiv = true;
  return true;
}
inline bool scan_schritt(Scan &s, bool tot, uint32_t &cid, F &d) {
  if (!s.aktiv || tot) return false;
  int idx = s.idx, sub = s.sub;
  d = {0x40, (uint8_t) (idx & 0xFF), (uint8_t) (idx >> 8), (uint8_t) sub, 0, 0, 0, 0};
  cid = 0x600 + s.knoten;
  if (++sub > s.maxsub) { sub = 0; idx++; }
  if (idx > s.ende) s.aktiv = false;
  s.idx = idx; s.sub = sub;
  return true;
}

// wem-schalten.yaml / verdacht.yaml: Protokoll
inline void protokoll(std::vector<std::string> &p, const std::string &z, std::string &kurz, std::string &lang) {
  p.insert(p.begin(), z);
  if (p.size() > 10) p.resize(10);
  kurz.clear(); lang.clear();
  for (auto &e : p) {
    if (kurz.size() + e.size() + 3 <= 250) { if (!kurz.empty()) kurz += " | "; kurz += e; }
    if (!lang.empty()) lang += "\n";
    lang += e;
  }
}

// wem-schalten.yaml Knopf "Schreiben" (Pruefung und VG); "" = ok
inline std::string schreiben(const std::string &eingabe, std::string &vg) {
  unsigned mi = 0, mx = 0, ox = 0, os = 0; char w[12] = {0};
  if (sscanf(eingabe.c_str(), "%x %x %x %x %10s", &mi, &mx, &ox, &os, w) != 5 || mx > 0xFF || ox > 0xFFFF || os > 0xFF)
    return "ungueltig - Format: MI MX OX OS WERT, z.B. 03 00 2520 02 02";
  if (mi != 1 && mi != 2 && mi != 3) return "gesperrt: nur MI 01, 02, 03 erlaubt - der Kessel wird nie beschrieben";
  std::string v = w;
  if (v.empty() || v.size() > 4 || v.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return "Wert: 1-4 Hexziffern";
  if (v.size() % 2) v = "0" + v;
  char b[40];
  snprintf(b, sizeof(b), "03%02X%02X%04X%02X%04X%s", mi, mx, ox, os, (unsigned) (v.size() / 2), v.c_str());
  vg = b;
  return "";
}

}  // namespace alt

static uint32_t zufall_zeit(std::mt19937 &r, uint32_t t) {
  switch (r() % 10) {
    case 0: return t + 30000 + r() % 400000;    // lange Pause (Bus weg, Anlaufpause)
    case 1: return t + 29990 + r() % 20;        // genau an der 30-s-Grenze
    default: return t + r() % 9000;             // normaler Takt
  }
}

static bool anlass_gleich(const alt::G &g, const wc::Bus &b, const wc::Anlass &a) {
  return g.letzter_frame == b.letzter_frame && g.bus_seit == b.seit && g.pause_ms == b.pause_ms &&
         g.anlass_ms == a.anlass_ms && g.anlass_hk_ms == a.hk_ms && g.anlass_ww_ms == a.ww_ms &&
         g.hk_erledigt == a.hk_erledigt && g.hk_letzte_lesung == a.hk_letzte_lesung && g.ww_erledigt == a.ww_erledigt &&
         g.status_manuell_ms == a.status_manuell_ms && g.start_gelesen == a.start_gelesen &&
         g.hk_jetzt_lesen == a.hk_jetzt && g.ww_jetzt_lesen == a.ww_jetzt && g.statusbits_vorher == a.statusbits_vorher;
}

TEST(kern_bus_und_anlass_wie_v28) {
  // Zufallsfolgen aus Frames, 5-s-Takten, "Status lesen", Statusbits und Regeln - auch ueber den
  // millis()-Ueberlauf hinweg. Nach JEDEM Schritt muss der Zustand gleich sein.
  for (int lauf = 0; lauf < 400; lauf++) {
    std::mt19937 r(1000 + lauf);
    alt::G g; wc::Bus b; wc::Anlass a;
    g.statusbits_anlass = a.statusbits_anlass = (lauf % 3 != 0);
    uint32_t t = (lauf % 4 == 0) ? 0xFFFFFFFFu - 200000u : r() % 5000;
    for (int schritt = 0; schritt < 400; schritt++) {
      t = zufall_zeit(r, t);
      switch (r() % 7) {
        case 0: case 1: alt::frame(g, t); if (b.frame(t)) a.start_gelesen = false; break;
        case 2: alt::hk_takt(g, t); a.hk_takt(b.senden_erlaubt(t), t); break;
        case 3: alt::ww_takt(g, t); a.ww_takt(b.tot(t), t); break;
        case 4: {
          int alt_e = alt::status_lesen(g, t);
          int neu_e = (int) a.anfordern(b, t);
          PRUEFE(alt_e == neu_e);
          break;
        }
        case 5: { uint16_t m = (r() % 2) ? 0x1000 : (uint16_t) (r() & 0xFFFF); alt::statusbits(g, m, t); a.statusbits(m, t); break; }
        case 6: { int z = 1 + r() % 3; alt::regel(g, z, t); if (z & 1) a.regel_hk(t); if (z & 2) a.regel_ww(t); break; }
      }
      PRUEFE(anlass_gleich(g, b, a));
      PRUEFE(alt::senden_bed(g, t) == b.senden_erlaubt(t));
      PRUEFE(alt::anfragen_text(true, g, t) == wc::anfragen_text(true, b, t));
      PRUEFE(rl::bus_bereit(g.bus_seit, g.letzter_frame, g.pause_ms, t) == b.bereit(t));
      if (g_fehler) return;   // erster Fehler genuegt, sonst tausende Folgefehler
    }
  }
  PRUEFE(wc::anfragen_text(false, wc::Bus(), 5) == "aus - nur mithoeren (Betriebsarten bei Anlass)");
}

TEST(kern_hk_lesung_wiederholt_nie) {
  // Fehler bis v28: war der Bus im 5-s-Takt nicht bereit, blieb hk_jetzt stehen, und 0x2933/2
  // ging alle 5 s erneut hinaus - auch in der Anlaufpause. Ab v29: jede Lesung genau einmal.
  wc::Bus b; wc::Anlass a;
  for (uint32_t t = 1000; t < 70000; t += 1000) b.frame(t);
  a.hk_takt(b.senden_erlaubt(70500), 70500);    // Start-Anlass, noch keine 5 s
  a.hk_takt(b.senden_erlaubt(76000), 76000);
  PRUEFE(a.hk_jetzt);                            // die gewollte Lesung nach dem Start
  a.hk_takt(b.senden_erlaubt(200000), 200000);  // Bus seit 130 s still: nicht bereit
  PRUEFE(!a.hk_jetzt);                           // v28: blieb true -> Wiederholung
  if (b.frame(205000)) a.start_gelesen = false;  // Bus wieder da -> Anlaufpause 10 min (wie WeishauptCan::frame)
  int lesungen = 0;
  for (uint32_t t = 210000; t < 900000; t += 5000) {
    if (t % 25000 == 0 && b.frame(t)) a.start_gelesen = false;
    a.hk_takt(b.senden_erlaubt(t), t);
    if (a.hk_jetzt) {
      lesungen++;
      PRUEFE(!b.in_pause(t));                    // nie in der Anlaufpause
    }
  }
  PRUEFE(lesungen == 1);                         // genau die eine Lesung nach der Anlaufpause
}

TEST(kern_lesebefehl_wie_v28) {
  std::vector<std::string> eingaben = {"01 2933 02", "1 2933 2", "02 2714 02", "00 2933 02", "80 1 1", "01 10000 00",
                                       "01 2933 100", "01 2933", "", "zz 1 1", "7F FFFF FF", "01 2933 02 99"};
  std::mt19937 r(7);
  for (auto &e : eingaben)
    for (uint32_t lf : {0u, 100000u}) {
      alt::Lese c; wc::Lesebefehl n;
      uint32_t cid_a = 0, cid_n = 0; F da, dn; std::string mn;
      std::string ma = alt::lesen(c, e, lf, 120000, cid_a, da);
      wc::Bus b; b.letzter_frame = lf;
      bool s = n.starten(e, b.tot(120000), mn, cid_n, dn);
      PRUEFE(ma == mn);
      PRUEFE(s == (ma == "gesendet, warte ..."));
      if (s) PRUEFE(cid_a == cid_n && da == dn);
      PRUEFE(c.aktiv == n.aktiv && c.kn == n.kn && c.idx == n.idx && c.sub == n.sub);
      // Antworten: passende und unpassende, alle Laengen-Kommandos, Abbruch
      for (int i = 0; i < 60; i++) {
        uint32_t can_id = (r() % 3) ? 0x580 + c.kn : 0x582;
        F x = {(uint8_t) std::vector<int>{0x4F, 0x4B, 0x47, 0x43, 0x80, 0x60}[r() % 6], (uint8_t) (c.idx & 0xFF),
               (uint8_t) (c.idx >> 8), (uint8_t) ((r() % 4) ? c.sub : c.sub + 1), (uint8_t) r(), (uint8_t) r(), (uint8_t) r(),
               (uint8_t) r()};
        if (r() % 10 == 0) x.resize(5);
        std::string ta = alt::antwort(c, can_id, x), tn;
        bool ok = n.antwort(can_id, x, tn);
        PRUEFE(ok == !ta.empty());
        PRUEFE(ta == (ok ? tn : std::string("")));
        PRUEFE(c.aktiv == n.aktiv);
      }
    }
  wc::Lesebefehl n; std::string t;
  n.aktiv = true; n.kn = 1; n.idx = 0x2933; n.sub = 2;
  PRUEFE(n.antwort(0x581, F{0x4F, 0x33, 0x29, 0x02, 0x03, 0, 0, 0}, t) && t == "01 2933/02 = 3 (0x03, 1 Byte)");
  n.aktiv = true;
  PRUEFE(n.antwort(0x581, F{0x80, 0x33, 0x29, 0x02, 0x00, 0x00, 0x02, 0x06}, t) && t == "01 2933/02: Abbruch 06020000 (Objekt fehlt?)");
}

TEST(kern_scan_wie_v28) {
  for (const char *e : {"01 2A00 2A03 2", "01 2A00 2A00 0", "02 0 1 10", "01 2A03 2A00 1", "01 0 0 11", "80 0 1 0", "x"}) {
    alt::Scan sa; wc::Scan sn;
    bool oa = alt::scan_start(sa, e), on = sn.starten(e);
    PRUEFE(oa == on);
    for (int i = 0; i < 80; i++) {
      bool tot = (i % 17) == 5;
      uint32_t ca = 0, cn = 0; F da, dn; bool fertig = false;
      bool ga = alt::scan_schritt(sa, tot, ca, da);
      bool gn = sn.schritt(tot, cn, dn, fertig);
      PRUEFE(ga == gn);
      if (ga) PRUEFE(ca == cn && da == dn);
      PRUEFE(sa.aktiv == sn.aktiv && sa.idx == sn.idx && sa.sub == sn.sub);
      PRUEFE(fertig == (ga && !sa.aktiv));
    }
  }
}

TEST(kern_brenner_zaehlt_flanken) {
  wc::Brenner b;
  int an = 0, starts = 0, vorher = -1;
  std::mt19937 r(3);
  for (int i = 0; i < 2000; i++) {
    int ph = r() % 6;
    if (i % 97 == 0) { b.vorher = -1; vorher = -1; }   // "Eigene CAN-Anfragen" aus
    bool z = b.phase(ph, an);
    int soll = (ph == 2 || ph == 3) ? 1 : 0;           // v28: kessel.yaml 0x582
    bool zs = soll == 1 && vorher == 0;
    if (zs) starts++;
    vorher = soll;
    PRUEFE(an == soll && z == zs && b.starts == starts);
  }
  PRUEFE(std::string(wc::brenner_text(0)) == "Aus" && std::string(wc::brenner_text(3)) == "Ein" &&
         std::string(wc::brenner_text(5)) == "unbekannt");
}

TEST(kern_protokoll_wie_v28) {
  std::vector<std::string> alt_p; wc::Protokoll neu;
  std::mt19937 r(11);
  for (int i = 0; i < 40; i++) {
    std::string z(1 + r() % 90, 'a' + (i % 26));
    std::string ka, la;
    alt::protokoll(alt_p, z, ka, la);
    neu.neu(z);
    PRUEFE(ka == neu.kurz() && la == neu.lang() && alt_p == neu.eintraege);
  }
  PRUEFE(neu.eintraege.size() == 10 && neu.kurz().size() <= 250);
}

TEST(kern_schreibbefehl_wie_v28) {
  for (const char *e : {"03 00 2520 02 02", "3 0 2520 2 1", "02 00 2533 02 0003", "01 00 271E 02 A", "07 00 2532 00 01",
                        "03 00 2520 02 12345", "03 00 2520 02 xy", "03 100 2520 02 01", "03 00 10000 02 01", "03 00 2520",
                        "", "03 00 2520 02 02 99"}) {
    std::string va, vn, f;
    std::string fa = alt::schreiben(e, va);
    bool ok = wc::schreib_vg(e, vn, f);
    PRUEFE(ok == fa.empty());
    PRUEFE(ok ? va == vn : fa == f);
  }
  std::string vg, f;
  PRUEFE(wc::schreib_vg("03 00 2520 02 02", vg, f) && vg == "030300252002000102");
  PRUEFE(!wc::schreib_vg("07 00 2532 00 01", vg, f) && enthaelt(f, "nie beschrieben"));   // Kessel gesperrt
}

TEST(kern_wem_aufruf_wie_v28) {
  // wem-schalten.yaml: Rumpf und VG aus der Antwort
  PRUEFE(wc::wem_rumpf(123456789, "0302002533020001") ==
         "{\"ID\":\"23456789\",\"SRC\":\"DDC\",\"CAPI\":{\"NN\":1,\"N01\":{\"VG\":\"0302002533020001\"}}}");
  for (const char *b : {"{\"CAPI\":{\"NN\":1,\"N01\":{\"VG\":\"0402002533020001\"}}}", "{\"VG\":\"\"}", "", "kein json",
                        "{\"VG\":\"05\"} {\"VG\":\"04\"}", "{\"VG\":\"0402"}) {
    std::string body = b;
    size_t p = body.find("\"VG\":\"");   // v28
    std::string rvg = p == std::string::npos ? std::string("") : body.substr(p + 6, body.find('"', p + 6) - p - 6);
    PRUEFE(wc::wem_vg(body) == rvg);
  }
}

TEST(kern_schalten_sperre_und_warteschlange) {
  wc::Schalten s;
  PRUEFE(!s.sperrminute(5) && s.warten_s(5, false) == 0 && s.warten_s(5, true) == 60);
  s.letzter_befehl = 1000 | 1;
  PRUEFE(s.sperrminute(60000) && !s.sperrminute(61001) && s.warten_s(1001, false) == 61 && s.warten_s(60990, false) == 1);
  bf::Schaltung sch;
  PRUEFE(wc::warteschlange_text(sch, true, "12:00:00") == "leer");
  bf::Anfrage a; a.quelle = "App"; a.wert = 3;
  sch.annehmen(1, a, 0);
  a.wert = 2; a.quelle = "MQTT";
  sch.annehmen(2, a, 0);
  PRUEFE(wc::warteschlange_text(sch, false, "") == "1. Heizkreis -> Zeitprogramm 2 (App) | 2. Warmwasser -> Aus (MQTT)");
  PRUEFE(wc::warteschlange_text(sch, true, "") ==
         "1. Heizkreis -> Zeitprogramm 2 (App) | 2. Warmwasser -> Aus (MQTT) - wartet auf die Sperrminute");
  bf::Befehl b; sch.naechster(b);
  PRUEFE(wc::warteschlange_text(sch, true, "10:01:02") == "1. Warmwasser -> Aus (MQTT) - naechster Befehl ab 10:01:02");
}

TEST(kern_texte_wie_v28) {
  static const char *n[] = {"?", "Standby", "Zeitprogramm 1", "Zeitprogramm 2", "Zeitprogramm 3", "Sommer", "Komfort", "Normal", "Absenk"};
  for (int b = 0; b <= 255; b++) {
    PRUEFE(std::string(wc::hk_name(b, "unbekannt")) == (b <= 8 ? n[b] : "unbekannt"));
    PRUEFE(std::string(wc::hk_name(b, "?")) == (b <= 8 ? n[b] : "?"));
    PRUEFE(std::string(wc::ww_name(b, "unbekannt")) == (b == 1 ? "Ein" : (b == 2 ? "Aus" : "unbekannt")));
    static const char *s[] = {"Aus", "Standby", "Sommer", "Automatik"};
    PRUEFE(std::string(wc::system_text(b)) == (b <= 3 ? s[b] : "unbekannt"));
    const char *k = "unbekannt";   // v28 kessel.yaml 0x182
    switch (b) { case 0: k = "Standby"; break; case 1: k = "Aus"; break; case 10: k = "Heizbetrieb"; break;
                 case 15: k = "Warmwasserbetrieb"; break; case 101: k = "Kaminfeger"; break; case 104: k = "Wartung"; break; }
    PRUEFE(std::string(bf::kessel_text(b)) == k);
  }
  for (uint32_t m : {0x0000u, 0x1000u, 0x1010u, 0x0010u, 0x0040u, 0x1234u, 0xFFFFu}) {
    std::string s = (m & 0x1000) ? "Standby" : "Zeitprogramm";   // v28
    if (m & 0x0010) s += " + WW-Ladung";
    uint16_t rest = m & ~0x1010;
    if (rest) { char b[12]; snprintf(b, sizeof(b), " (0x%04X)", rest); s += b; }
    PRUEFE(wc::hk_status_text(m) == s);
  }
  PRUEFE(wc::uhr_text(F{14, 5, 26, 9, 27, 6}) == "27.09.2026 14:05" && wc::uhr_text(F{1, 2, 3}).empty());
}

TEST(kern_verdacht_offene_lesung_wie_v28) {
  wc::Verdacht v; wc::Anlass a;
  wc::Verdacht::Ergebnis e;
  // unbekannter Stand -> "geaendert: ?"
  v.ausgeloest("R3", 1, 50000, a);
  PRUEFE(v.offen_hk.aktiv && v.offen_hk.alt == -1 && a.hk_letzte_lesung == 0 && a.hk_ms == ((50000 - 5000) | 1));
  v.ausgeloest("R5", 1, 51000, a);                          // haengt sich an die offene Lesung
  PRUEFE(v.offen_hk.regel == "R3+R5" && v.offen_hk.ms == 50000);
  PRUEFE(v.antwort(F{0x4F, 0x33, 0x29, 0x02, 0x01, 0, 0, 0}, e));
  PRUEFE(e.eintrag == "R3+R5: Heizkreis Standby (vorher unbekannt), geaendert: ?" && e.geaendert == -1 && e.wert == "Standby");
  PRUEFE(e.phase == "ergebnis" && e.ziel == "hk" && v.hk_bekannt == 1 && !v.offen_hk.aktiv);
  // ohne offene Lesung: nur der bekannte Stand
  PRUEFE(!v.antwort(F{0x4F, 0x33, 0x29, 0x02, 0x03, 0, 0, 0}, e) && v.hk_bekannt == 3);
  v.ausgeloest("R4", 1, 90000, a);
  PRUEFE(v.antwort(F{0x4F, 0x33, 0x29, 0x02, 0x06, 0, 0, 0}, e) &&
         e.eintrag == "R4: Heizkreis Komfort (vorher Zeitprogramm 2), geaendert: JA" && e.geaendert == 1);
  v.ausgeloest("R1", 2, 90000, a);
  PRUEFE(a.ww_ms == ((90000 - 8000) | 1) && v.offen_ww.alt == -1);
  PRUEFE(v.antwort(F{0x4F, 0x20, 0x2A, 0x02, 0x02, 0, 0, 0}, e) && e.eintrag == "R1: Warmwasser Aus (vorher unbekannt), geaendert: ?");
  v.ausgeloest("R2", 2, 95000, a);
  PRUEFE(v.antwort(F{0x4F, 0x20, 0x2A, 0x02, 0x02, 0, 0, 0}, e) && e.eintrag == "R2: Warmwasser Aus, geaendert: nein" && e.geaendert == 0);
  // falscher Subindex / Abbruch: keine Antwort
  v.ausgeloest("R3", 1, 100000, a);
  PRUEFE(!v.antwort(F{0x4F, 0x33, 0x29, 0x01, 0x01, 0, 0, 0}, e) && v.offen_hk.aktiv);
  // keine Antwort binnen 3 min (Grenze: mehr als 180 s)
  PRUEFE(!v.abgelaufen(v.offen_hk, "hk", "Heizkreis", 280000, e));
  PRUEFE(v.abgelaufen(v.offen_hk, "hk", "Heizkreis", 280001, e) && e.eintrag == "R3: Heizkreis - keine Antwort" &&
         e.phase == "keine_antwort" && e.ziel == "hk" && e.wert.empty() && !v.offen_hk.aktiv);
  PRUEFE(!v.abgelaufen(v.offen_ww, "ww", "Warmwasser", 999999, e));
}

TEST(kern_json_wie_v28) {
  // verdacht.yaml vd_stand / vd_ereignis, zusatz.yaml zs_senden - Aufbau Feld fuer Feld wie v28
  vd::eigene().clear();
  bool an[5] = {true, true, true, false, false};
  int ab[5] = {1, 10, 1, 30, 5};
  PRUEFE(vd::stand_json("v29", 5, an, ab, 6, false) ==
         "{\"firmware\":\"v29\",\"version\":5,\"R1\":{\"an\":true,\"abstand_min\":1},\"R2\":{\"an\":true,\"abstand_min\":10},"
         "\"R3\":{\"an\":true,\"abstand_min\":1},\"R4\":{\"an\":false,\"abstand_min\":30},\"R5\":{\"an\":false,\"abstand_min\":5},"
         "\"max_pro_stunde\":6,\"eigene_regeln_an\":false,\"eigene\":[]}");
  PRUEFE(!enthaelt(vd::stand_json("v29", -1, an, ab, 6, true), "\"version\""));
  PRUEFE(vd::ereignis_json("R1", "ausgeloest", "ww", "", -1, "") == "{\"regel\":\"R1\",\"phase\":\"ausgeloest\",\"ziel\":\"ww\"}");
  PRUEFE(vd::ereignis_json("R3", "ergebnis", "hk", "Standby", -1, "2026-09-27T10:00:00") ==
         "{\"regel\":\"R3\",\"phase\":\"ergebnis\",\"ziel\":\"hk\",\"wert\":\"Standby\",\"geaendert\":null,\"zeit\":\"2026-09-27T10:00:00\"}");
  PRUEFE(enthaelt(vd::ereignis_json("R3", "ergebnis", "hk", "Standby", 1, ""), "\"geaendert\":true"));
  PRUEFE(enthaelt(vd::ereignis_json("R3", "ergebnis", "hk", "Standby", 0, ""), "\"geaendert\":false"));
  zs::liste().clear();
  PRUEFE(zs::app_json("v29", -1) == "{\"firmware\":\"v29\",\"eintraege\":[]}");
  zs::Eintrag z; z.name = "WP"; z.topic = "a/b"; z.feld = "apower"; z.einheit = "W"; z.laeuft = true; z.schwelle = 50;
  zs::liste().push_back(z);
  PRUEFE(zs::app_json("v29", 1) ==
         "{\"firmware\":\"v29\",\"version\":1,\"eintraege\":[{\"name\":\"WP\",\"topic\":\"a/b\",\"feld\":\"apower\","
         "\"einheit\":\"W\",\"art\":\"laeuft\",\"schwelle\":50}]}");
  zs::liste().clear();
}

TEST(kern_regeln_aktiv_text_wie_v28) {
  wc::Verdacht v; wc::Bus b;
  bool an[5] = {true, false, true, false, false};
  int ab[5] = {1, 10, 1, 30, 5};
  vd::eigene().clear();
  PRUEFE(wc::regeln_aktiv_text(an, ab, false, 6, v, b, 100000) ==
         "R1 1 min, R3 1 min | eigene Regeln aus | max 6/h, letzte Stunde 0 | Bus schweigt");
  b.frame(100000);
  v.zeiten = {50000, 99000};
  PRUEFE(wc::regeln_aktiv_text(an, ab, true, 6, v, b, 100010) ==
         "R1 1 min, R3 1 min | eigene Regeln an: 0 aktiv | max 6/h, letzte Stunde 2 | Anlaufpause");
  for (uint32_t t = 100000; t <= 200000; t += 10000) b.frame(t);
  bool aus[5] = {false, false, false, false, false};
  PRUEFE(wc::regeln_aktiv_text(aus, ab, true, 3, v, b, 3700000 + 50000) ==
         "R1-R5 aus (gelesen wird nur bei Start, Schaltbefehl und 'Status lesen') | eigene Regeln an: 0 aktiv | max 3/h, "
         "letzte Stunde 0 | Bus schweigt");
}

TEST(kern_speicher_feste_groesse) {
  std::array<char, 8> m{};
  PRUEFE(wc::speicher_schreiben(m, "[1,2]") && std::string(m.data()) == "[1,2]");
  PRUEFE(!wc::speicher_schreiben(m, "12345678") && std::string(m.data()) == "[1,2]");   // unveraendert
  PRUEFE(wc::speicher_schreiben(m, "") && m[0] == 0);
  // Datentypen und Groessen wie die Globals bis v28 (sonst passt der gespeicherte Wert nicht mehr)
  PRUEFE(sizeof(wc::Verdacht().speicher) == 2400 && sizeof(wc::Zusatz().speicher) == 1600);
  PRUEFE(sizeof(wc::Verdacht().datei_hash) == 4 && sizeof(wc::Zusatz().datei_hash) == 4 && sizeof(wc::Brenner().starts) == sizeof(int));
  PRUEFE(wc::Verdacht().stby_vorher == -1 && wc::Verdacht().rest_vorher == -1 && wc::Verdacht().datei_version == -1 &&
         wc::Zusatz().version == -1 && wc::Brenner().starts == 0);
}
