#pragma once
// Stoermeldungen (CANopen-EMCY) des WEM (0x081) und des Kessels (0x082) als Text - rein mitgehoert, nichts angefragt.
//
// Aufbau, wie am Bus gesehen (WTC-GW 15-B, November 2026):
//   081: FF FF 01 02 05 DD 01 00   Raumgeraet RG2 30 s nicht erreichbar -> Warnung 1501 (0x05DD)
//   081: 00 00 01 00 00 00 01 00   Meldung erledigt (Fehlercode 0000)
//   Byte 0-1 Fehlercode (0000 = keine/erledigt, FFFF = herstellereigen), Byte 2 Fehlerregister,
//   Byte 3 Art (2 = Warnung belegt; 1 = Fehler ist ANNAHME nach dem Fehlerblock 0x273F), Byte 4-5 Nummer (big endian).
// Texte, in dieser Reihenfolge:
//   1. hochgeladene Originaltexte (Einstellungsseite, /fs/fehlertexte.json; erzeugt mit werkzeuge/fehlertexte_aus_handbuch.py)
//   2. beim Bau eingebundene Originaltexte (components/weishaupt_can/fehlertexte_original.h, nicht im Repo)
//   3. eigene Kurztexte aus fehlercodes.yaml (fehlercodes_gen.h)
#include <ArduinoJson.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>
#include "fehlercodes_gen.h"
#if defined(__has_include)
#if __has_include("fehlertexte_original.h")
#include "fehlertexte_original.h"
#endif
#endif

namespace fehlercodes {

struct Meldung {
  bool gueltig{false};  // Frame lang genug
  bool aktiv{false};    // Fehlercode != 0
  char typ{'?'};        // 'W', 'F' oder '?'
  uint16_t nr{0};
  std::string text;     // z. B. "W 1501: Verbindung zum Raumgeraet RG2 unterbrochen (Teilnehmer 1)"
};

// ---- Hochgeladene Originaltexte (zur Laufzeit) ----
// Datei wie von werkzeuge/fehlertexte_aus_handbuch.py: {"de": {"W 36": ["…"], "W 1501-1532": ["…"]}, "en": {…}}
struct Geladen { char typ; uint16_t nr; std::string text; };
struct Laufzeit {
  std::mutex sperre;
  std::vector<Geladen> de, en;
};
inline Laufzeit &laufzeit() { static Laufzeit l; return l; }

constexpr size_t DATEI_MAX = 64 * 1024;
constexpr size_t TEXT_MAX = 400;

// Prueft und uebernimmt die Datei. false + f = Grund, dann bleibt der bisherige Stand.
inline bool laden(const std::string &json, std::string &f, unsigned &n_de, unsigned &n_en) {
  n_de = n_en = 0;
  if (json.size() > DATEI_MAX) { f = "Datei zu gross"; return false; }
  JsonDocument d;
  if (deserializeJson(d, json)) { f = "kein gueltiges JSON"; return false; }
  std::vector<Geladen> neu[2];
  const char *const SPRACHEN[2] = {"de", "en"};
  for (int i = 0; i < 2; i++) {
    JsonObject o = d[SPRACHEN[i]];
    if (o.isNull()) continue;
    for (JsonPair kv : o) {
      const char *k = kv.key().c_str();
      if ((k[0] != 'W' && k[0] != 'F') || k[1] != ' ' || k[2] < '0' || k[2] > '9') { f = std::string("unbekannter Code ") + k; return false; }
      long nr = strtol(k + 2, nullptr, 10);
      if (nr <= 0 || nr > 9999) { f = std::string("Nummer ungueltig: ") + k; return false; }
      std::string t;
      if (kv.value().is<JsonArrayConst>()) {
        for (JsonVariantConst v : kv.value().as<JsonArrayConst>()) {
          if (!v.is<const char *>()) continue;
          if (!t.empty()) t += " / ";
          t += v.as<const char *>();
        }
      } else if (kv.value().is<const char *>()) {
        t = kv.value().as<const char *>();
      }
      if (t.empty() || t.size() > TEXT_MAX) { f = std::string("Text fehlt oder zu lang: ") + k; return false; }
      neu[i].push_back(Geladen{k[0], (uint16_t) nr, t});
    }
  }
  if (neu[0].empty() && neu[1].empty()) { f = "weder \"de\" noch \"en\" enthalten"; return false; }
  n_de = neu[0].size();
  n_en = neu[1].size();
  Laufzeit &l = laufzeit();
  std::lock_guard<std::mutex> g(l.sperre);
  l.de.swap(neu[0]);
  l.en.swap(neu[1]);
  return true;
}

inline void leeren() {
  Laufzeit &l = laufzeit();
  std::lock_guard<std::mutex> g(l.sperre);
  l.de.clear();
  l.en.clear();
}

inline unsigned geladen(bool englisch) {
  Laufzeit &l = laufzeit();
  std::lock_guard<std::mutex> g(l.sperre);
  return englisch ? l.en.size() : l.de.size();
}

// Text zu Art und Nummer; mehrere Module unter einer Nummer werden mit " / " verbunden
inline std::string text_zu(char typ, uint16_t nr, bool englisch = false) {
  std::string t;
  uint16_t anfang = nr;
  bool bereich = false;
  for (const auto &e : CODES) {
    if ((typ != '?' && e.typ != typ) || nr < e.nr || nr > e.bis) continue;
    if (!t.empty()) t += " / ";
    if (e.modul[0] && std::string(e.modul) != "WTC") { t += e.modul; t += ": "; }
    t += englisch ? e.en : e.de;
    if (e.bis != e.nr) { bereich = true; anfang = e.nr; }
  }
#ifdef FEHLERTEXTE_ORIGINAL
  if (englisch == (FEHLERTEXTE_ORIGINAL_EN != 0)) {  // nur in der Sprache, aus der die Datei erzeugt wurde
    for (const auto &o : ORIGINAL) {
      if ((typ == '?' || o.typ == typ) && o.nr == anfang) { t = o.text; break; }
    }
  }
#endif
  {
    Laufzeit &l = laufzeit();
    std::lock_guard<std::mutex> g(l.sperre);
    for (const auto &o : englisch ? l.en : l.de) {
      if ((typ == '?' || o.typ == typ) && o.nr == anfang) { t = o.text; break; }
    }
  }
  if (t.empty()) return "unbekannte Nummer";
  if (bereich) {
    char b[24];
    snprintf(b, sizeof(b), englisch ? " (device %u)" : " (Teilnehmer %u)", (unsigned) (nr % 100));
    t += b;
  }
  return t;
}

inline Meldung emcy(const std::vector<uint8_t> &x, bool englisch = false) {
  Meldung m;
  if (x.size() < 6) return m;
  m.gueltig = true;
  uint16_t code = x[0] | (x[1] << 8);
  if (code == 0) { m.text = englisch ? "no message" : "keine Meldung"; return m; }
  m.aktiv = true;
  uint8_t art = x[3] & 0x0F;
  m.typ = art == 2 ? 'W' : art == 1 ? 'F' : '?';
  m.nr = (x[4] << 8) | x[5];
  char k[16];
  snprintf(k, sizeof(k), "%c %u: ", m.typ, (unsigned) m.nr);
  m.text = std::string(k) + text_zu(m.typ, m.nr, englisch);
  return m;
}

}  // namespace fehlercodes
