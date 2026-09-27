// Paket VERDACHT (verdacht.yaml): gemeinsame Bausteine der Regeln - Ruhezeit nach eigenem Befehl,
// Bus bereit, Masken, Rueckrechnung R4, Pruefung vor dem Lesen. Seit v30 stehen R1-R5 selbst als
// Daten in regelwerk.h; die bisherigen Funktionen je Regel liegen eingefroren in
// tests/referenz_v29/ (Vergleichsfassung des Aequivalenztests). Bewusst OHNE ESPHome-Abhaengigkeiten: die Zeit (millis) und alle
// Zustaende kommen als Parameter herein. So laesst sich die Logik auf dem PC mit g++
// pruefen (tests/), ohne Board und ohne Bus. Die YAML-Lambdas rufen nur diese Funktionen
// auf und kuemmern sich um Log, Schalter, MQTT und das eigentliche Nachlesen.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include "register_gen.h"

namespace rl {

static const uint32_t RUHE_EIGENER_BEFEHL_MS = 120000;   // R1-R5 und eigene Regeln ruhen 2 min
static const uint32_t BUS_STILL_MS = 30000;              // so lange ohne Frame = Bus schweigt

// Ruht die Regel, weil das Board selbst geschaltet hat? (eigener_befehl_ms 0 = nie)
inline bool eigen_ruht(uint32_t eigener_befehl_ms, uint32_t jetzt) {
  return eigener_befehl_ms != 0 && jetzt - eigener_befehl_ms < RUHE_EIGENER_BEFEHL_MS;
}

// Darf gelesen werden? (Bus lebt und Anlaufpause vorbei)
inline bool bus_bereit(uint32_t bus_seit, uint32_t letzter_frame, uint32_t pause_ms, uint32_t jetzt) {
  return !(bus_seit == 0 || jetzt - letzter_frame > BUS_STILL_MS || jetzt - bus_seit <= pause_ms);
}

inline uint16_t sdo_index(const std::vector<uint8_t> &x) { return (x[2] << 8) | x[1]; }

// Temperaturen der PDOs: int16 little-endian, Faktor 0,1
inline float pdo_temp(uint8_t lo, uint8_t hi) { return (int16_t(hi << 8 | lo)) * 0.1f; }

// R4 aus PDO 0x241 (Vorlaufsoll HZ): zurueckgerechnete Raumsoll-Stufe gegen die bekannte
// Betriebsart 6 Komfort (21), 7 Normal (20), 8 Absenk (18).
// RT = (VL - 1,4 + 1,1 * AT) / 2,1 (geeicht bei AT 12,9-15,2 C).
// r4_key merkt den letzten Widerspruch, damit derselbe nicht erneut ausloest.
// true = neuer Widerspruch.
struct R4Befund { float rt = 0; int stufe = 0; int soll = 0; };
inline bool r4_vorlauf(int &r4_key, float vl, float at, int hk, R4Befund &b) {
  if (vl <= 0 || std::isnan(at) || hk < 6 || hk > 8) { r4_key = -1; return false; }
  b.rt = (vl - 1.4f + 1.1f * at) / 2.1f;
  b.stufe = b.rt < 19.0f ? 18 : (b.rt < 20.5f ? 20 : 21);
  b.soll = hk == 6 ? 21 : (hk == 7 ? 20 : 18);
  if (b.stufe == b.soll) { r4_key = -1; return false; }
  int key = hk * 100 + b.stufe;
  if (key == r4_key) return false;
  r4_key = key;
  return true;
}

// Antwort von Knoten 1 (0x581) auf eine Lesung mit Subindex 2: Index und 1-Byte-Wert
inline bool antwort_lesen(const std::vector<uint8_t> &x, uint16_t &idx, int &wert) {
  if (x.size() < 5 || (x[0] & 0xF0) != 0x40 || x[3] != 2) return false;
  idx = sdo_index(x);
  wert = x[4];
  return true;
}

// Eigene Regeln: 0x601 (eigene Anfragen) und 0x581 (ihre Antworten) sind nie Ausloeser
inline bool gesperrte_id(uint32_t can_id) { return can_id == 0x601 || can_id == 0x581; }

// (Daten UND Maske) == Muster; fehlende Bytes zaehlen als 00
inline bool maske_passt(const uint8_t maske[8], const uint8_t muster[8], const std::vector<uint8_t> &x) {
  for (int i = 0; i < 8; i++) {
    uint8_t b = i < (int) x.size() ? x[i] : 0;
    if ((b & maske[i]) != muster[i]) return false;
  }
  return true;
}

// Gemeinsame Pruefung vor dem Nachlesen (Skript vd_regel). Aendert die Zustaende genau so,
// wie es die Firmware tut: Schattenmodus -> eigene Zeitliste; sonst Bus, Mindestabstand,
// Obergrenze pro Stunde (alle Regeln zusammen).
enum class Ergebnis {
  SCHATTEN_GEMELDET,     // Regel aus: "waere ausgeloest" melden, nicht lesen
  SCHATTEN_ABSTAND,      // Regel aus, Mindestabstand der Schattenmeldung laeuft noch
  BUS_NICHT_BEREIT,      // Bus schweigt oder Anlaufpause
  ABSTAND,               // Mindestabstand der Regel laeuft noch
  OBERGRENZE,            // Obergrenze pro Stunde erreicht
  AUSLOESEN,             // lesen
};

inline Ergebnis regel_pruefen(const std::string &regel, bool an, int abstand_min, int max_h, bool bus_ok, uint32_t jetzt,
                              std::map<std::string, uint32_t> &letzt, std::map<std::string, uint32_t> &letzt_schatten,
                              std::vector<uint32_t> &zeiten) {
  if (!an) {
    auto is = letzt_schatten.find(regel);
    if (is != letzt_schatten.end() && jetzt - is->second < (uint32_t) abstand_min * 60000UL) return Ergebnis::SCHATTEN_ABSTAND;
    letzt_schatten[regel] = jetzt;
    return Ergebnis::SCHATTEN_GEMELDET;
  }
  if (!bus_ok) return Ergebnis::BUS_NICHT_BEREIT;
  auto it = letzt.find(regel);
  if (it != letzt.end() && jetzt - it->second < (uint32_t) abstand_min * 60000UL) return Ergebnis::ABSTAND;
  zeiten.erase(std::remove_if(zeiten.begin(), zeiten.end(), [jetzt](uint32_t t) { return jetzt - t > 3600000UL; }), zeiten.end());
  if ((int) zeiten.size() >= max_h) return Ergebnis::OBERGRENZE;
  zeiten.push_back(jetzt);
  letzt[regel] = jetzt;
  return Ergebnis::AUSLOESEN;
}

}  // namespace rl
