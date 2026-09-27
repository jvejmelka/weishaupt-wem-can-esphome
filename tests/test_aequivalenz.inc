// ---------------------------------------------------------------- Aequivalenz v29 <-> Datenregeln (ab v30)
// Wird in test_regeln.cpp eingebunden. Seit v30 sind R1-R5 Datenregeln (regelwerk.h). Hier laufen
// beide Fassungen nebeneinander: links die eingefrorene v29-Logik (tests/referenz_v29/), rechts der
// neue Auswerter. Beide bekommen dieselben Frames, Zeiten (auch ueber den Ueberlauf von millis()),
// Schalter, Abstaende, Obergrenzen und Antworten; verglichen wird nach JEDEM Frame die komplette
// Spur: Logzeilen, Aufrufe von vd_regel, deren Ergebnis (Ausloesung, Schatten, Abstand, Obergrenze,
// Bus) samt Ereignis und Protokoll, sowie die gespeicherten Vorwerte (R3/R5) und bekannten Betriebsarten.

namespace aeq {

using F = std::vector<uint8_t>;

// Abdeckung: wie oft kam welche Art Zeile in den (gleichen) Spuren vor
inline std::map<std::string, int> &abdeckung() { static std::map<std::string, int> m; return m; }
static const char *const ABDECKUNG[] = {
    "X R1 ", "X R2 ", "X R3 ", "X R4 ", "X R5 ", "X e1 ", "X e2 ", "X e4 ", "R1-Folge erkannt, ruht (eigener Befehl)",
    "R1-Folge erkannt, ruht (WEM-Neustart)", "R2 (0x252B=0F) ruht", "R2 (Kesselstatus 15) ruht", "R4 (0x252B=0A) ruht",
    "R4 (Kesselstatus 10) ruht", "R3 Standby-Bit", "Statusbits 0x", "R3 liest schon", "Raumsoll-Stufe", "I R4: Vorlaufsoll",
    "e1 ruht (eigener Befehl)", "waere ausgeloest (Regel aus) - nicht gelesen", "nicht gemeldet", "Obergrenze",
    "Mindestabstand", "Bus nicht bereit", "ausgeloest -> lese", "P "};

// Das Skript vd_regel (verdacht.yaml) - unveraendert seit v29, hier fuer beide Seiten nachgebildet.
// P ist rl29::regel_pruefen (links) bzw. rl::regel_pruefen (rechts).
template<typename E, typename P>
void vd_regel(wc::Verdacht &v, wc::Anlass &an, const rw::Aktion &a, int max_h, bool bus_ok, uint32_t jetzt, P pruefen,
              std::vector<std::string> &spur) {
  const std::string &regel = a.regel;
  char b[200];
  switch (pruefen(regel, a.an, a.abstand, max_h, bus_ok, jetzt, v.letzt, v.letzt_schatten, v.zeiten)) {
    case E::SCHATTEN_ABSTAND:
      snprintf(b, sizeof(b), "D %s waere ausgeloest (Regel aus), Mindestabstand %d min - nicht gemeldet", regel.c_str(), a.abstand);
      spur.push_back(b); return;
    case E::SCHATTEN_GEMELDET:
      spur.push_back("I " + regel + " waere ausgeloest (Regel aus) - nicht gelesen");
      spur.push_back(std::string("E ") + vd::ereignis_json(regel, "waere", a.ziel == 1 ? "hk" : (a.ziel == 2 ? "ww" : "beide"), "", -1, ""));
      return;
    case E::BUS_NICHT_BEREIT:
      spur.push_back("D " + regel + ": Bus nicht bereit (Anlaufpause/Bus schweigt) - nicht gelesen"); return;
    case E::ABSTAND:
      snprintf(b, sizeof(b), "D %s: Mindestabstand %d min - nicht gelesen", regel.c_str(), a.abstand);
      spur.push_back(b); return;
    case E::OBERGRENZE:
      snprintf(b, sizeof(b), "D %s: Obergrenze %d/h erreicht - nicht gelesen", regel.c_str(), max_h);
      spur.push_back(b); return;
    case E::AUSLOESEN:
      break;
  }
  v.ausgeloest(regel, a.ziel, jetzt, an);
  spur.push_back("I " + regel + " ausgeloest -> lese " + (a.ziel == 1 ? "Heizkreis" : (a.ziel == 2 ? "Warmwasser" : "beide")));
  spur.push_back(std::string("E ") + vd::ereignis_json(regel, "ausgeloest", a.ziel == 1 ? "hk" : (a.ziel == 2 ? "ww" : "beide"), "", -1, ""));
  char z[40];
  snprintf(z, sizeof(z), "Z %s %u %u %u", regel.c_str(), (unsigned) an.hk_ms, (unsigned) an.ww_ms, (unsigned) an.hk_letzte_lesung);
  spur.push_back(z);
}

// eine Seite der Simulation
struct Seite {
  wc::Verdacht vd;
  wc::Anlass an;
  v29::Z z;                                 // nur links
  std::vector<v29::AltRegel> alt;           // nur links
  std::vector<vd::Regel> eigene;            // nur rechts
};

struct Welt {
  Seite l, r;
  v29::Ein e;
  int max_h = 6;
  bool bus_ok = true;
  bool zustand_vergleichen = true;
  std::string fehler;

  static v29::AltRegel als_alt(const vd::Regel &n) {
    v29::AltRegel a;
    a.name = n.name; a.an = n.an; a.lesen = n.lesen; a.abstand_min = n.abstand_min;
    const vd::Glied &g = n.ausloeser[0].glieder[0];
    a.can_id = g.can_id;
    for (int i = 0; i < 8; i++) { a.maske[i] = g.maske[i]; a.muster[i] = g.muster[0][i]; }
    return a;
  }
  // eigene Regeln (Kurzform) auf beide Seiten
  void eigene(const std::vector<vd::Regel> &v) {
    r.eigene = v;
    l.alt.clear();
    for (auto &n : v) l.alt.push_back(als_alt(n));
  }

  static std::string text(const rw::Aktion &a) {
    if (a.art == rw::Aktion::LOG_D) return "D " + a.text;
    if (a.art == rw::Aktion::LOG_I) return "I " + a.text;
    char b[80];
    snprintf(b, sizeof(b), "X %s ziel=%d abstand=%d an=%d", a.regel.c_str(), a.ziel, a.abstand, (int) a.an);
    return b;
  }

  // ein Frame auf beiden Seiten. false = Spuren oder Zustand verschieden (fehler gesetzt)
  bool schritt(uint32_t can_id, const F &x, uint32_t j) {
    std::vector<std::string> sl, sr;
    // 0x581-Handler (Antworten) - vor dem Regel-Handler, auf beiden Seiten gleich
    for (Seite *s : {&l, &r}) {
      auto &sp = s == &l ? sl : sr;
      wc::Verdacht::Ergebnis er;
      if (can_id == 0x581 && s->vd.antwort(x, er)) sp.push_back("P " + er.eintrag);
    }
    // links: v29
    v29::Ein el = e;
    el.hk = l.vd.hk_bekannt; el.ww = l.vd.ww_bekannt;
    el.eigener_befehl_ms = e.eigener_befehl_ms;
    auto al = v29::frame(l.z, l.alt, el, can_id, x, j);
    for (auto &a : al) {
      sl.push_back(text(a));
      if (a.art == rw::Aktion::AUSFUEHREN) vd_regel<rl29::Ergebnis>(l.vd, l.an, a, max_h, bus_ok, j, rl29::regel_pruefen, sl);
    }
    // rechts: Datenregeln
    rw::Kontext k;
    k.jetzt = j;
    k.eigen = rl::eigen_ruht(e.eigener_befehl_ms, j);
    for (int i = 0; i < 5; i++) { k.fest_an[i] = e.fest_an[i]; k.fest_ab[i] = e.fest_ab[i]; }
    k.eigene_haupt = e.haupt;
    vd::eigene() = r.eigene;
    auto ar = r.vd.frame(can_id, x, k);
    r.eigene = vd::eigene();                  // Laufzeitwerte der eigenen Regeln behalten
    for (auto &a : ar) {
      sr.push_back(text(a));
      if (a.art == rw::Aktion::AUSFUEHREN) vd_regel<rl::Ergebnis>(r.vd, r.an, a, max_h, bus_ok, j, rl::regel_pruefen, sr);
    }
    for (auto &z : sl)
      for (auto a : ABDECKUNG) if (z.find(a) != std::string::npos) abdeckung()[a]++;
    if (sl != sr) {
      fehler = "Spur verschieden bei ID " + std::to_string(can_id) + ", t=" + std::to_string(j) + ":";
      for (size_t n = 0; n < std::max(sl.size(), sr.size()); n++)
        fehler += "\n    v29: " + (n < sl.size() ? sl[n] : "-") + "\n    neu: " + (n < sr.size() ? sr[n] : "-");
      return false;
    }
    bool zu = l.vd.hk_bekannt == r.vd.hk_bekannt && l.vd.ww_bekannt == r.vd.ww_bekannt && l.vd.zeiten == r.vd.zeiten &&
              l.vd.letzt == r.vd.letzt && l.vd.letzt_schatten == r.vd.letzt_schatten;
    if (zustand_vergleichen) zu = zu && l.z.stby_vorher == r.vd.stby_vorher && l.z.rest_vorher == r.vd.rest_vorher;
    if (!zu) { fehler = "Zustand verschieden bei t=" + std::to_string(j); return false; }
    return true;
  }
};

// zufaellige, aber "interessante" Frames (ausgedacht nach PROTOKOLL.md, keine Mitschnitte)
struct Zufall {
  std::mt19937 g;
  explicit Zufall(uint32_t s) : g(s) {}
  int n(int a, int b) { return std::uniform_int_distribution<int>(a, b)(g); }
  bool p(int prozent) { return n(1, 100) <= prozent; }
  uint8_t aus(std::initializer_list<int> l) { return (uint8_t) *(l.begin() + n(0, (int) l.size() - 1)); }
  F kuerzen(F x) { if (p(8)) x.resize(n(0, (int) x.size())); return x; }
  F sdo(uint8_t k, uint16_t idx, uint8_t sub, uint8_t d0 = 0) {
    return F{k, (uint8_t) (idx & 0xFF), (uint8_t) (idx >> 8), sub, d0, (uint8_t) n(0, 255), 0, 0};
  }
  // (can_id, Frame)
  std::pair<uint32_t, F> frame(int hk_bias) {
    switch (n(0, 11)) {
      case 0: case 1: {
        uint16_t idx = (uint16_t) aus({0x2101, 0x2102, 0x2102, 0x252B, 0x252B, 0x252C, 0x273F, 0x2532, 0x223E});
        return {0x602, kuerzen(sdo(aus({0xA4, 0xA4, 0x40, 0x2F, 0x2B, 0x23, 0x43, 0x80, 0x60}), idx,
                                   aus({0x0A, 0x0D, 0x01, 0x00, 0x02}), aus({0x0F, 0x0A, 0x0A, 0x0F, 0x01, 0x00})))};
      }
      case 2: return {0x182, kuerzen(F{aus({10, 15, 0, 15, 10, 3}), 0, 0, 0, 0, 0, 0, 0})};
      case 3: case 4: return {0x1C1, kuerzen(F{0, 0, aus({0x00, 0x04, 0x10, 0x40, 0x50, 0x14, 0x80}), aus({0x00, 0x10, 0x04, 0x14, 0x80}), 0, 0, 0, 0})};
      case 5: {
        int at = n(100, 170);
        return {0x201, kuerzen(F{3, (uint8_t) (at & 0xFF), (uint8_t) (at >> 8), 0, 0, 0, 0, 0})};
      }
      case 6: {
        int vl = p(10) ? 0 : n(200, 330);
        return {0x241, kuerzen(F{(uint8_t) (vl & 0xFF), (uint8_t) (vl >> 8), 0xA8, 0, 0, 0, 0x5E, 1})};
      }
      case 7: return {0x701, kuerzen(F{aus({0x00, 0x05, 0x05, 0x7F})})};
      case 8: {
        bool ww = p(40);
        int w = ww ? n(1, 2) : (hk_bias ? aus({1, 5, 6, 7, 8, 2}) : n(1, 8));
        return {0x581, F{0x4F, (uint8_t) (ww ? 0x20 : 0x33), (uint8_t) (ww ? 0x2A : 0x29), 0x02, (uint8_t) w, 0, 0, 0}};
      }
      case 9: return {0x601, F{0x40, 0x33, 0x29, 0x02, 0, 0, 0, 0}};
      default: {
        F x(n(0, 8));
        for (auto &b : x) b = (uint8_t) aus({0x00, 0x3E, 0x22, 0xA4, 0x40, 0xFF, 0x10, n(0, 255)});
        return {(uint32_t) aus({0x602, 0x123, 0x181, 0x241, 0x1C1, 0x582}), x};
      }
    }
  }
};

// eigene Regeln fuer die Vergleiche (Kurzform, wie bis v29)
inline std::vector<vd::Regel> eigene_beispiele() {
  vd::Datei d;
  std::string f;
  bool ok = vd::datei_lesen(
      "{\"eigene\":["
      "{\"name\":\"e1\",\"can_id\":\"0x602\",\"maske\":\"00 FF FF\",\"muster\":\"00 3E 22\",\"abstand_min\":2},"
      "{\"name\":\"e2\",\"can_id\":\"0x1C1\",\"maske\":\"00 00 10\",\"muster\":\"00 00 10\",\"lesen\":\"hk\",\"abstand_min\":1},"
      "{\"name\":\"e3\",\"an\":false,\"can_id\":\"0x123\",\"muster\":\"A4\",\"lesen\":\"ww\",\"abstand_min\":1},"
      "{\"name\":\"e4\",\"can_id\":\"0x182\",\"maske\":\"FF\",\"muster\":\"0F\",\"lesen\":\"ww\",\"abstand_min\":3}]}",
      d, f);
  if (!ok) std::printf("  eigene_beispiele: %s\n", f.c_str());
  return d.eigene;
}

// Zufallslauf: Rueckgabe leer = gleich, sonst Beschreibung der ersten Abweichung.
// umbau: wird rechts vor dem Lauf auf die Datenregeln angewandt (Gegenprobe, JSON-Rundreise)
inline std::string zufallslauf(uint32_t seed, int schritte, uint32_t start,
                               std::function<void(Welt &)> umbau = nullptr) {
  Zufall z(seed);
  Welt w;
  w.eigene(eigene_beispiele());
  if (umbau) umbau(w);
  uint32_t j = start;
  for (int s = 0; s < schritte; s++) {
    // Zeit: meist Sekunden, manchmal Minuten, selten viel
    int art = z.n(1, 100);
    j += art <= 70 ? (uint32_t) z.n(0, 4000) : (art <= 97 ? (uint32_t) z.n(4000, 700000) : (uint32_t) z.n(700000, 4000000));
    // Schalter, Abstaende, Obergrenze, Bus, eigener Befehl
    if (z.p(3)) w.e.fest_an[z.n(0, 4)] ^= true;
    if (z.p(2)) w.e.fest_ab[z.n(0, 4)] = z.n(1, 12);
    if (z.p(2)) w.max_h = z.n(1, 8);
    if (z.p(2)) w.e.haupt = !w.e.haupt;
    if (z.p(1)) w.e.eigener_befehl_ms = j | 1;
    w.bus_ok = !z.p(10);
    // R1-Folge gezielt einstreuen (0xA4/0x40 gemischt, mit Zeitabstaenden rund um die Fenster)
    if (z.p(4)) {
      uint32_t t = j;
      const uint16_t idx[3] = {0x2101, 0x2102, 0x2102};
      const uint8_t sub[3] = {0x0A, 0x0D, 0x01};
      for (int g = 0; g < 3; g++) {
        if (g) t += z.p(80) ? (uint32_t) z.n(100, 2000) : (uint32_t) z.n(4000, 16000);
        if (!w.schritt(0x602, z.sdo(z.aus({0xA4, 0x40}), idx[g], sub[g]), t))
          return "Schritt " + std::to_string(s) + " (R1-Folge): " + w.fehler;
      }
      j = t;
      continue;
    }
    // Szene "Warmwasser/Heizung umgeschaltet": bekannte Staende, dann Anforderungen des WEM
    if (z.p(3)) {
      uint32_t t = j;
      std::vector<std::pair<uint32_t, F>> sz = {
          {0x581, F{0x4F, 0x20, 0x2A, 0x02, (uint8_t) z.aus({2, 2, 1}), 0, 0, 0}},
          {0x581, F{0x4F, 0x33, 0x29, 0x02, (uint8_t) z.aus({1, 5, 7}), 0, 0, 0}},
          {0x602, z.sdo(0x2F, 0x252B, 0x00, z.aus({0x0F, 0x0A}))}, {0x602, z.sdo(0x2F, 0x252B, 0x00, z.aus({0x0F, 0x0A}))},
          {0x182, F{z.aus({10, 15}), 0, 0, 0, 0, 0, 0, 0}}, {0x602, z.sdo(z.aus({0xA4, 0x40}), 0x223E, 0x00)}};
      for (auto &e : sz) {
        t += (uint32_t) z.n(50, 3000);
        if (!w.schritt(e.first, e.second, t)) return "Schritt " + std::to_string(s) + " (Szene): " + w.fehler;
      }
      j = t;
      continue;
    }
    auto f = z.frame(z.p(50));
    if (!w.schritt(f.first, f.second, j)) return "Schritt " + std::to_string(s) + ": " + w.fehler;
  }
  return "";
}

// feste Folge (Zeit, ID, Frame) mit festen Schaltern
struct Ereignis { uint32_t t; uint32_t id; F x; };
inline std::string folge(const std::vector<Ereignis> &l, std::function<void(Welt &)> setup) {
  Welt w;
  if (setup) setup(w);
  for (auto &e : l)
    if (!w.schritt(e.id, e.x, e.t)) return w.fehler;
  return "";
}

inline F sdo(uint8_t k, uint16_t idx, uint8_t sub, uint8_t d0 = 0) {
  return F{k, (uint8_t) (idx & 0xFF), (uint8_t) (idx >> 8), sub, d0, 0, 0, 0};
}
inline F antwort(bool ww, int wert) {
  return F{0x4F, (uint8_t) (ww ? 0x20 : 0x33), (uint8_t) (ww ? 0x2A : 0x29), 0x02, (uint8_t) wert, 0, 0, 0};
}

}  // namespace aeq

static void aeq_melde(const std::string &s) {
  if (!s.empty()) std::printf("  %s\n", s.c_str());
}

TEST(aequivalenz_zufall_v29_gleich_neu) {
  // viele Laeufe, jeder mit anderem Startwert; die Haelfte startet kurz vor dem Ueberlauf von millis()
  for (uint32_t seed = 1; seed <= 400; seed++) {
    uint32_t start = seed % 2 ? 0xFFFFFFFFu - (uint32_t) (seed * 7919u % 3000000u) : 1000u + seed;
    std::string r = aeq::zufallslauf(seed, 1500, start);
    PRUEFE(r.empty());
    if (!r.empty()) { aeq_melde("seed " + std::to_string(seed) + ": " + r); break; }
  }
  // der Vergleich beweist nur etwas, wenn jeder Fall auch vorkam
  for (auto a : aeq::ABDECKUNG) {
    PRUEFE(aeq::abdeckung()[a] > 0);
    if (aeq::abdeckung()[a] == 0) aeq_melde(std::string("nie vorgekommen: ") + a);
  }
  if (std::getenv("ABDECKUNG"))
    for (auto &kv : aeq::abdeckung()) std::printf("    %6d  %s\n", kv.second, kv.first.c_str());
}

TEST(aequivalenz_szenarien_wie_v29) {
  using aeq::sdo;
  using aeq::antwort;
  auto alle_an = [](aeq::Welt &w) {
    for (int i = 0; i < 5; i++) { w.e.fest_an[i] = true; w.e.fest_ab[i] = 1; }
    w.max_h = 60;
    w.eigene(aeq::eigene_beispiele());
    w.e.haupt = true;
  };
  // R1: 0xA4, 0x40, gemischt, falsche Reihenfolge, Fenster, WEM-Neustart
  PRUEFE(aeq::folge({{100000, 0x602, sdo(0xA4, 0x2101, 0x0A)}, {100400, 0x602, sdo(0xA4, 0x2102, 0x0D)},
                     {100800, 0x602, sdo(0xA4, 0x2102, 0x01)}, {101200, 0x602, sdo(0x40, 0x2101, 0x0A)},
                     {101300, 0x602, sdo(0x40, 0x2102, 0x0D)}, {101400, 0x602, sdo(0xA4, 0x2102, 0x01)},
                     {200000, 0x602, sdo(0xA4, 0x2102, 0x0D)}, {200400, 0x602, sdo(0xA4, 0x2101, 0x0A)},
                     {200800, 0x602, sdo(0xA4, 0x2102, 0x01)}, {300000, 0x602, sdo(0xA4, 0x2101, 0x0A)},
                     {301000, 0x602, sdo(0xA4, 0x2102, 0x0D)}, {315999, 0x602, sdo(0xA4, 0x2102, 0x01)},
                     {400000, 0x701, aeq::F{0x00}}, {500000, 0x602, sdo(0xA4, 0x2101, 0x0A)},
                     {500100, 0x602, sdo(0xA4, 0x2102, 0x0D)}, {500200, 0x602, sdo(0xA4, 0x2102, 0x01)},
                     {700001, 0x602, sdo(0xA4, 0x2101, 0x0A)}, {700100, 0x602, sdo(0xA4, 0x2102, 0x0D)},
                     {700200, 0x602, sdo(0xA4, 0x2102, 0x01)},
                     // Fenstergrenzen genau (ungerade Zeiten: gespeichert wird millis() | 1):
                     // 2101/0A -> 2102/01 = 15000 ms, 2102/0D -> 2102/01 = 10000 ms, je 1 ms darunter
                     {800001, 0x602, sdo(0xA4, 0x2101, 0x0A)}, {806001, 0x602, sdo(0xA4, 0x2102, 0x0D)},
                     {815001, 0x602, sdo(0xA4, 0x2102, 0x01)}, {900001, 0x602, sdo(0xA4, 0x2101, 0x0A)},
                     {906001, 0x602, sdo(0xA4, 0x2102, 0x0D)}, {915000, 0x602, sdo(0xA4, 0x2102, 0x01)},
                     {1000001, 0x602, sdo(0xA4, 0x2101, 0x0A)}, {1001001, 0x602, sdo(0xA4, 0x2102, 0x0D)},
                     {1011001, 0x602, sdo(0xA4, 0x2102, 0x01)}, {1100001, 0x602, sdo(0xA4, 0x2101, 0x0A)},
                     {1101001, 0x602, sdo(0xA4, 0x2102, 0x0D)}, {1111000, 0x602, sdo(0xA4, 0x2102, 0x01)},
                     // gleiche Zeitpunkte und ueber den Ueberlauf von millis()
                     {0xFFFFFF00u, 0x602, sdo(0xA4, 0x2101, 0x0A)}, {0xFFFFFF00u, 0x602, sdo(0xA4, 0x2102, 0x0D)},
                     {0x00000F00u, 0x602, sdo(0xA4, 0x2102, 0x01)}}, alle_an).empty());
  // R3/R5: Standby, WW-Ladung/Heizbetrieb egal, beide Bits zugleich, R3 aus -> R5 wird gerufen
  auto r3_aus = [&](aeq::Welt &w) { alle_an(w); w.e.fest_an[2] = false; };
  std::vector<aeq::Ereignis> bits = {{1000, 0x1C1, {0, 0, 0x00, 0x10}}, {2000, 0x1C1, {0, 0, 0x04, 0x10}},
                                     {3000, 0x1C1, {0, 0, 0x04, 0x04}}, {4000, 0x1C1, {0, 0, 0x50, 0x04}},
                                     {200000, 0x1C1, {0, 0, 0x00, 0x10}}, {300000, 0x1C1, {0, 0}}};
  PRUEFE(aeq::folge(bits, alle_an).empty());
  PRUEFE(aeq::folge(bits, r3_aus).empty());
  // R2/R4 ueber 0x252B und Kesselstatus, mit bekannten Betriebsarten
  std::vector<aeq::Ereignis> r24 = {{1000, 0x581, antwort(true, 2)}, {1100, 0x581, antwort(false, 1)},
                                    {2000, 0x602, sdo(0x2F, 0x252B, 0, 0x0F)}, {3000, 0x602, sdo(0x2F, 0x252B, 0, 0x0A)},
                                    {4000, 0x182, {10}}, {5000, 0x182, {15}}, {6000, 0x182, {10}},
                                    {70000, 0x581, antwort(false, 5)}, {80000, 0x602, sdo(0x2F, 0x252B, 0, 0x0F)},
                                    {90000, 0x602, sdo(0x2F, 0x252B, 0, 0x0A)}, {95000, 0x602, sdo(0xA4, 0x252B, 0)}};
  PRUEFE(aeq::folge(r24, alle_an).empty());
  // R4-Baustein: Normal bekannt, Vorlaufsoll passt nicht
  std::vector<aeq::Ereignis> r4 = {{1000, 0x581, antwort(false, 7)}, {1500, 0x201, {3, 140, 0}},
                                   {2000, 0x241, {45, 1, 0, 0}}, {3000, 0x241, {45, 1, 0, 0}},
                                   {4000, 0x241, {9, 1, 0, 0}}, {200000, 0x241, {45, 1, 0, 0}}};
  PRUEFE(aeq::folge(r4, alle_an).empty());
  // eigener Befehl: alles ruht (Logtexte), Abstaende, Obergrenze, Schatten
  auto eigen = [&](aeq::Welt &w) { alle_an(w); w.e.eigener_befehl_ms = 1; };
  PRUEFE(aeq::folge(r24, eigen).empty());
  PRUEFE(aeq::folge(bits, eigen).empty());
  PRUEFE(aeq::folge(r4, eigen).empty());
  auto knapp = [&](aeq::Welt &w) { alle_an(w); w.max_h = 2; w.e.fest_ab[1] = 30; w.e.fest_an[3] = false; };
  PRUEFE(aeq::folge(r24, knapp).empty());
}

TEST(aequivalenz_gegenprobe_fehler_wird_erkannt) {
  // Absichtliche Fehler in den Datenregeln muessen den Vergleich rot machen - sonst beweist er nichts.
  auto rot = [](std::function<void(aeq::Welt &)> f) {
    for (uint32_t seed = 1; seed <= 60; seed++)
      if (!aeq::zufallslauf(seed, 1500, 5000 + seed, f).empty()) return true;
    return false;
  };
  auto fest = [](aeq::Welt &w, int nr) -> vd::Regel & { return w.r.vd.werk.feste[nr - 1]; };
  PRUEFE(rot([&](aeq::Welt &w) { fest(w, 1).ausloeser[0].glieder[0].fenster_ms = 16000; }));        // R1-Fenster
  PRUEFE(rot([&](aeq::Welt &w) { fest(w, 1).ausloeser[0].glieder[1].muster.pop_back(); }));         // R1 nur 0xA4
  PRUEFE(rot([&](aeq::Welt &w) { fest(w, 1).ruhe_ms = 0; }));                                        // WEM-Neustart
  PRUEFE(rot([&](aeq::Welt &w) { fest(w, 2).ausloeser[1].ww = {1, 2}; }));                           // R2-Bedingung
  PRUEFE(rot([&](aeq::Welt &w) { fest(w, 3).ausloeser[0].bits = 0x1010; }));                         // R3-Bits
  PRUEFE(rot([&](aeq::Welt &w) { fest(w, 4).ausloeser[1].hk = {1}; }));                              // R4 ohne Sommer
  PRUEFE(rot([&](aeq::Welt &w) { fest(w, 5).unterdrueckt_durch.clear(); }));                         // R5 neben R3
  PRUEFE(rot([&](aeq::Welt &w) { fest(w, 5).ausloeser[0].meldung_ruht = "{regel} ruht ({grund})"; })); // Logtext
  PRUEFE(rot([&](aeq::Welt &w) { fest(w, 2).lesen = 3; }));                                          // Ziel
}

TEST(aequivalenz_feste_regeln_als_json) {
  // Die festen Regeln lassen sich im Dateiformat ausdruecken: als JSON schreiben, wieder einlesen
  // (reservierte Namen intern erlaubt) und gegen v29 vergleichen. Nicht im Format: der Speicherplatz
  // der Vorwerte R3/R5 (platz) - deshalb hier ohne Zustandsvergleich dieser beiden Werte.
  std::vector<vd::Regel> neu;
  for (auto &r : rw::feste_regeln()) {
    JsonDocument d;
    vd::regel_json(d.to<JsonObject>(), r, false);
    std::string s;
    serializeJson(d, s);
    JsonDocument d2;
    deserializeJson(d2, s);
    vd::Regel n;
    std::string f;
    PRUEFE(vd::regel_lesen(d2.as<JsonObject>(), n, f, true));
    if (!f.empty()) aeq_melde(r.name + ": " + f);
    n.nr = r.nr;
    neu.push_back(n);
  }
  PRUEFE(neu.size() == 5);
  for (uint32_t seed = 1000; seed <= 1100; seed++) {
    std::string r = aeq::zufallslauf(seed, 1500, seed * 1000u, [&](aeq::Welt &w) { w.r.vd.werk.feste = neu; w.zustand_vergleichen = false; });
    PRUEFE(r.empty());
    if (!r.empty()) { aeq_melde(r); break; }
  }
}
