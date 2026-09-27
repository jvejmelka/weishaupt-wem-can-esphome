// Paket VERDACHT: Auswertung der Regeln (ab v30). Die festen Regeln R1-R5 sind hier als DATEN
// hinterlegt - im selben Format wie die eigenen Regeln (verdacht.h) - und laufen durch denselben
// Auswerter. Reines C++ ohne ESPHome; getestet in tests/ (u.a. Aequivalenz zu v29).
//
// Ablauf je Frame: frame() liefert eine Liste von Aktionen (Logzeile DEBUG/INFO oder "Regel
// ausfuehren" mit Ziel, Abstand, an). Die YAML-Lambda gibt sie aus bzw. ruft das Skript vd_regel,
// das Mindestabstand, Obergrenze und Schattenmodus prueft (rl::regel_pruefen) - fuer alle Regeln gleich.
//
// Platzhalter in meldung_ruht / meldung_unterdrueckt:
//   {regel} Name   {grund} "eigener Befehl" bzw. ruhe_nach.grund   {text} Kennung des Ausloesers
//   {alt} {neu} Werte der Flanke (dezimal)   {althex} {neuhex} dieselben als 0x%04X   {durch} unterdrueckt_durch
// Vorgaben: "{regel} ({text}) ruht ({grund})" (ohne text: "{regel} ruht ({grund})") und
//           "{regel}: {durch} liest schon".
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>
#include "register_gen.h"
#include "regellogik.h"
#include "verdacht.h"

namespace rw {

struct Aktion {
  enum Art { LOG_D, LOG_I, AUSFUEHREN } art = LOG_D;
  std::string text;          // LOG_D/LOG_I
  std::string regel;         // AUSFUEHREN
  int ziel = 0, abstand = 0;
  bool an = false;
};

// Was die YAML-Seite je Frame mitgibt
struct Kontext {
  uint32_t jetzt = 0;
  bool eigen = false;                    // eigener Schaltbefehl < 2 min: alle Regeln ruhen
  bool fest_an[5] = {false, false, false, false, false};
  int fest_ab[5] = {10, 10, 10, 10, 10};
  bool eigene_haupt = false;             // Hauptschalter der eigenen Regeln
  int hk = -1, ww = -1;                  // zuletzt gelesene Betriebsarten
  int *plaetze[2] = {nullptr, nullptr};  // gespeicherte Vorwerte (R3, R5)
};

// Beobachtungen fuer Bausteine und die festen Regeln
struct Werk {
  std::vector<vd::Regel> feste;
  float at = NAN, vl = NAN;              // Aussentemperatur (0x201), Vorlaufsoll HZ (0x241)
};

inline bool passt(const vd::Glied &g, uint32_t can_id, const std::vector<uint8_t> &x) {
  if (can_id != g.can_id || x.size() < g.laenge_min) return false;
  if (g.muster.empty()) return true;
  for (auto &m : g.muster)
    if (rl::maske_passt(g.maske.data(), m.data(), x)) return true;
  return false;
}

inline bool in_liste(const std::vector<int> &l, int v) {
  if (l.empty()) return true;
  for (int x : l) if (x == v) return true;
  return false;
}

inline void ersetze(std::string &s, const char *was, const std::string &durch) {
  std::string w = was;
  for (size_t p = s.find(w); p != std::string::npos; p = s.find(w, p + durch.size())) s.replace(p, w.size(), durch);
}

// Ergebnis eines Ausloesers fuer einen Frame
struct Treffer {
  bool ja = false;
  int alt = -1, neu = -1;
  std::string ruht, info;   // Bausteine liefern ihre Texte selbst (ruht mit Platzhalter {grund})
};

// Baustein r4_vorlauf: Vorlaufsoll HZ (PDO 0x241) gegen die bekannte Raumsoll-Stufe
// (6 Komfort 21, 7 Normal 20, 8 Absenk 18). Rueckrechnung RT = (VL - 1,4 + 1,1 * AT) / 2,1,
// geeicht bei AT 12,9-15,2 C - eine Rechnung mit Gleitkomma und Stufen, kein Frame-Muster.
// Zustand (zuletzt gemeldeter Widerspruch) je Ausloeser in a.vorher.
inline Treffer baustein(vd::Ausloeser &a, const std::string &regel, uint32_t can_id, const std::vector<uint8_t> &x,
                        const Werk &w, const Kontext &k) {
  Treffer t;
  if (a.baustein == "r4_vorlauf") {
    if (can_id != reg::VORLAUFSOLL_HZ.can_id || x.size() < 2) return t;
    rl::R4Befund b;
    if (!rl::r4_vorlauf(a.vorher, w.vl, w.at, k.hk, b)) return t;
    char s[160];
    snprintf(s, sizeof(s), "%s (Raumsoll-Stufe %d statt %d) ruht ({grund})", regel.c_str(), b.stufe, b.soll);
    t.ruht = s;
    snprintf(s, sizeof(s), "%s: Vorlaufsoll %.1f, AT %.1f -> Raumsoll ~%.1f (Stufe %d), erwartet %d", regel.c_str(), w.vl,
             w.at, b.rt, b.stufe, b.soll);
    t.info = s;
    t.ja = true;
  }
  return t;
}

inline Treffer pruefen(vd::Ausloeser &a, const std::string &regel, uint32_t can_id, const std::vector<uint8_t> &x,
                       const Werk &w, const Kontext &k) {
  Treffer t;
  uint32_t j = k.jetzt;
  switch (a.art) {
    case vd::Art::FRAME:
      t.ja = passt(a.glieder[0], can_id, x);
      break;
    case vd::Art::FOLGE: {
      size_t n = a.glieder.size();
      if (a.t.size() != n) a.t.assign(n, 0);
      for (size_t i = 0; i < n; i++) {
        if (!passt(a.glieder[i], can_id, x)) continue;
        if (i + 1 < n) { a.t[i] = j | 1; return t; }
        bool ok = true;
        for (size_t m = 0; m + 1 < n; m++) {
          if (a.t[m] == 0) ok = false;
          else if (a.glieder[m].fenster_ms != 0 && j - a.t[m] >= a.glieder[m].fenster_ms) ok = false;
          if (m > 0 && (int32_t) (a.t[m] - a.t[m - 1]) < 0) ok = false;
        }
        a.t.assign(n, 0);
        t.ja = ok;
        break;
      }
      break;
    }
    case vd::Art::FLANKE: {
      if (!passt(a.glieder[0], can_id, x) || x.size() < (size_t) (a.byte + a.bytes)) return t;
      int v = x[a.byte];
      if (a.bytes == 2) v |= x[a.byte + 1] << 8;
      v &= a.bits;
      if ((a.bits & (a.bits - 1)) == 0) v = v ? 1 : 0;   // genau ein Bit: 0/1
      int *vorher = (a.platz >= 0 && a.platz < 2 && k.plaetze[a.platz] != nullptr) ? k.plaetze[a.platz] : &a.vorher;
      t.alt = *vorher;
      t.neu = v;
      *vorher = v;
      bool geaendert = a.erster ? v != t.alt : (t.alt >= 0 && v != t.alt);
      t.ja = geaendert && in_liste(a.gleich, v);
      break;
    }
    case vd::Art::BAUSTEIN:
      return baustein(a, regel, can_id, x, w, k);
  }
  return t;
}

inline std::string meldung(const std::string &vorlage, const vd::Regel &r, const vd::Ausloeser &a, const Treffer &t,
                           const std::string &grund) {
  std::string s = vorlage;
  char h[12];
  ersetze(s, "{regel}", r.name);
  ersetze(s, "{grund}", grund);
  ersetze(s, "{text}", a.text);
  ersetze(s, "{alt}", std::to_string(t.alt));
  ersetze(s, "{neu}", std::to_string(t.neu));
  snprintf(h, sizeof(h), "0x%04X", t.alt);
  ersetze(s, "{althex}", h);
  snprintf(h, sizeof(h), "0x%04X", t.neu);
  ersetze(s, "{neuhex}", h);
  ersetze(s, "{durch}", r.unterdrueckt_durch);
  return s;
}

// Eine Regel fuer einen Frame. liest = Regeln, die in diesem Frame eingeschaltet ausgefuehrt wurden.
inline void regel_frame(vd::Regel &r, bool an, int abstand, uint32_t can_id, const std::vector<uint8_t> &x, const Werk &w,
                        const Kontext &k, std::set<std::string> &liest, std::vector<Aktion> &out) {
  uint32_t j = k.jetzt;
  if (r.ruhe_ms != 0 && passt(r.ruhe, can_id, x)) r.ruhe_seit = j | 1;
  for (auto &a : r.ausloeser) {
    Treffer t = pruefen(a, r.name, can_id, x, w, k);
    if (!t.ja) continue;
    if (!in_liste(a.hk, k.hk) || !in_liste(a.ww, k.ww)) continue;
    std::string grund;
    if (k.eigen) grund = "eigener Befehl";
    else if (r.ruhe_ms != 0 && r.ruhe_seit != 0 && j - r.ruhe_seit < r.ruhe_ms) grund = r.ruhe_grund;
    if (!grund.empty()) {
      std::string v = a.meldung_ruht.empty() ? (a.text.empty() ? "{regel} ruht ({grund})" : "{regel} ({text}) ruht ({grund})")
                                             : a.meldung_ruht;
      out.push_back({Aktion::LOG_D, meldung(t.ruht.empty() ? v : t.ruht, r, a, t, grund), "", 0, 0, false});
      continue;
    }
    if (!r.unterdrueckt_durch.empty() && liest.count(r.unterdrueckt_durch)) {
      std::string v = a.meldung_unterdrueckt.empty() ? "{regel}: {durch} liest schon" : a.meldung_unterdrueckt;
      out.push_back({Aktion::LOG_D, meldung(v, r, a, t, ""), "", 0, 0, false});
      continue;
    }
    if (!t.info.empty()) out.push_back({Aktion::LOG_I, t.info, "", 0, 0, false});
    out.push_back({Aktion::AUSFUEHREN, "", r.name, (int) r.lesen, abstand, an});
    if (an) liest.insert(r.name);
  }
}

// Alle Regeln fuer einen Frame: erst die festen (R1..R5), dann die eigenen.
inline std::vector<Aktion> frame(Werk &w, std::vector<vd::Regel> &eigene, uint32_t can_id, const std::vector<uint8_t> &x,
                                 const Kontext &k) {
  std::vector<Aktion> out;
  // Beobachtungen fuer den Baustein r4_vorlauf
  if (can_id == reg::AUSSENTEMP.can_id && x.size() >= 3) w.at = reg::pdo_wert(reg::AUSSENTEMP, x);
  if (can_id == reg::VORLAUFSOLL_HZ.can_id && x.size() >= 2) w.vl = reg::pdo_wert(reg::VORLAUFSOLL_HZ, x);
  std::set<std::string> liest;
  for (auto &r : w.feste) {
    int i = r.nr - 1;
    if (i < 0 || i > 4) continue;
    regel_frame(r, k.fest_an[i], k.fest_ab[i], can_id, x, w, k, liest, out);
  }
  if (rl::gesperrte_id(can_id)) return out;   // eigene Anfragen (0x601) und ihre Antworten (0x581) nie
  for (auto &r : eigene) regel_frame(r, k.eigene_haupt && r.an, (int) r.abstand_min, can_id, x, w, k, liest, out);
  return out;
}

// ---------------------------------------------------------------- die festen Regeln als Daten

inline vd::Glied g_id(uint16_t can_id, uint8_t laenge_min) {
  vd::Glied g;
  g.can_id = can_id;
  g.laenge_min = laenge_min;
  return g;
}
// Muster "Kommandobyte, Index LE, Subindex" einer SDO-Anfrage an den Kessel (Maske FF FF FF FF)
inline vd::Glied g_sdo(const reg::Sdo &o, std::initializer_list<uint8_t> kommandos, uint32_t fenster_ms) {
  vd::Glied g = g_id(reg::anfrage_id(o), 5);
  g.maske = {0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0};
  for (uint8_t k : kommandos) g.muster.push_back({k, (uint8_t) (o.index & 0xFF), (uint8_t) (o.index >> 8), o.sub, 0, 0, 0, 0});
  g.fenster_ms = fenster_ms;
  return g;
}
// Schreiben des WEM an den Kessel: Kommandobyte 0x2x (Maske E0), Index LE; Wert in Byte 4
inline vd::Glied g_schreiben(const reg::Sdo &o) {
  vd::Glied g = g_id(reg::anfrage_id(o), 5);
  g.maske = {0xE0, 0xFF, 0xFF, 0, 0, 0, 0, 0};
  g.muster.push_back({0x20, (uint8_t) (o.index & 0xFF), (uint8_t) (o.index >> 8), 0, 0, 0, 0, 0});
  return g;
}
inline vd::Ausloeser flanke(vd::Glied g, uint8_t byte, uint8_t bytes, uint16_t bits, bool erster) {
  vd::Ausloeser a;
  a.art = vd::Art::FLANKE;
  a.glieder = {g};
  a.byte = byte;
  a.bytes = bytes;
  a.bits = bits;
  a.erster = erster;
  return a;
}
inline vd::Ausloeser mit(vd::Ausloeser a, std::vector<int> gleich, std::vector<int> hk, std::vector<int> ww, const char *text) {
  a.gleich = gleich;
  a.hk = hk;
  a.ww = ww;
  a.text = text;
  return a;
}

static const uint16_t BIT_STANDBY = 0x1000;              // Statusbit Heizkreis Standby
static const uint16_t BITS_UEBRIGE = 0xFFFF & ~0x1050;   // ohne Standby, WW-Ladung 0x0010, Heizbetrieb 0x0040

inline std::vector<vd::Regel> feste_regeln() {
  std::vector<vd::Regel> v;
  const vd::Glied w252b = g_schreiben(reg::W_252B);
  const vd::Glied kstatus = g_id(reg::KESSELSTATUS.can_id, reg::KESSELSTATUS.byte + 1);
  const vd::Glied sbits = g_id(reg::STATUSBITS_PDO.can_id, reg::STATUSBITS_PDO.byte + reg::STATUSBITS_PDO.laenge);
  // R1: Warmwasser umgeschaltet - der WEM fragt den Kessel 2101/0A, 2102/0D, 2102/01 ab
  // (Kommandobyte 0xA4 Block-Upload, selten 0x40). Ruht 5 min nach einem Bootup des WEM.
  {
    vd::Regel r;
    r.name = "R1"; r.nr = 1; r.lesen = 2;
    vd::Ausloeser a;
    a.art = vd::Art::FOLGE;
    a.glieder = {g_sdo(reg::R1_A, {0xA4, 0x40}, 15000), g_sdo(reg::R1_B, {0xA4, 0x40}, 10000), g_sdo(reg::R1_C, {0xA4, 0x40}, 0)};
    a.meldung_ruht = "{regel}-Folge erkannt, ruht ({grund})";
    r.ausloeser = {a};
    r.ruhe = g_id(0x701, 1);
    r.ruhe.maske = {0xFF, 0, 0, 0, 0, 0, 0, 0};
    r.ruhe.muster = {{0x00, 0, 0, 0, 0, 0, 0, 0}};   // Heartbeat 00 = Bootup
    r.ruhe_ms = 300000;
    r.ruhe_grund = "WEM-Neustart";
    v.push_back(r);
  }
  // R2: Warmwasserbetrieb (WEM schreibt 0x252B = 0F bzw. Kesselstatus 15), bekannt ist "Aus"
  {
    vd::Regel r;
    r.name = "R2"; r.nr = 2; r.lesen = 2;
    r.ausloeser = {mit(flanke(w252b, 4, 1, 0xFF, true), {0x0F}, {}, {2}, "0x252B=0F"),
                   mit(flanke(kstatus, reg::KESSELSTATUS.byte, 1, 0xFF, false), {15}, {}, {2}, "Kesselstatus 15")};
    v.push_back(r);
  }
  // R3: Standby-Bit der WEM-Statusbits (PDO 0x1C1, Bytes 2-3 LE) geaendert
  {
    vd::Regel r;
    r.name = "R3"; r.nr = 3; r.lesen = 1;
    vd::Ausloeser a = flanke(sbits, reg::STATUSBITS_PDO.byte, 2, BIT_STANDBY, false);
    a.platz = 0;
    a.meldung_ruht = "{regel} Standby-Bit {alt} -> {neu} ruht ({grund})";
    r.ausloeser = {a};
    v.push_back(r);
  }
  // R4: Heizanforderung passt nicht zur bekannten Heizkreis-Betriebsart
  {
    vd::Regel r;
    r.name = "R4"; r.nr = 4; r.lesen = 1;
    vd::Ausloeser b;
    b.art = vd::Art::BAUSTEIN;
    b.baustein = "r4_vorlauf";
    b.glieder = {g_id(reg::VORLAUFSOLL_HZ.can_id, 2)};
    r.ausloeser = {mit(flanke(w252b, 4, 1, 0xFF, true), {0x0A}, {1, 5}, {}, "0x252B=0A"),
                   mit(flanke(kstatus, reg::KESSELSTATUS.byte, 1, 0xFF, false), {10}, {1, 5}, {}, "Kesselstatus 10"), b};
    v.push_back(r);
  }
  // R5: uebrige Statusbits geaendert - nicht zusaetzlich, wenn R3 im selben Frame schon liest
  {
    vd::Regel r;
    r.name = "R5"; r.nr = 5; r.lesen = 1;
    vd::Ausloeser a = flanke(sbits, reg::STATUSBITS_PDO.byte, 2, BITS_UEBRIGE, false);
    a.platz = 1;
    a.meldung_ruht = "{regel} Statusbits {althex} -> {neuhex} ruht ({grund})";
    a.meldung_unterdrueckt = "{regel} Statusbits {althex} -> {neuhex}: {durch} liest schon";
    r.ausloeser = {a};
    r.unterdrueckt_durch = "R3";
    v.push_back(r);
  }
  return v;
}

}  // namespace rw
