// Paket VERDACHT (verdacht.yaml): reine Entscheidungslogik der Regeln R1-R5 und der
// eigenen Regeln. Bewusst OHNE ESPHome-Abhaengigkeiten: die Zeit (millis) und alle
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

namespace rl {

static const uint32_t RUHE_EIGENER_BEFEHL_MS = 120000;   // R1-R5 und eigene Regeln ruhen 2 min
static const uint32_t BUS_STILL_MS = 30000;              // so lange ohne Frame = Bus schweigt
static const uint32_t R1_FENSTER_B_MS = 10000;           // 2102/0D -> 2102/01
static const uint32_t R1_FENSTER_A_MS = 15000;           // 2101/0A -> 2102/01
static const uint32_t WEM_NEUSTART_RUHE_MS = 300000;     // R1 ruht 5 min nach Bootup des WEM

// Ruht die Regel, weil das Board selbst geschaltet hat? (eigener_befehl_ms 0 = nie)
inline bool eigen_ruht(uint32_t eigener_befehl_ms, uint32_t jetzt) {
  return eigener_befehl_ms != 0 && jetzt - eigener_befehl_ms < RUHE_EIGENER_BEFEHL_MS;
}

// Darf gelesen werden? (Bus lebt und Anlaufpause vorbei)
inline bool bus_bereit(uint32_t bus_seit, uint32_t letzter_frame, uint32_t pause_ms, uint32_t jetzt) {
  return !(bus_seit == 0 || jetzt - letzter_frame > BUS_STILL_MS || jetzt - bus_seit <= pause_ms);
}

// Lese-Anfrage des WEM an den Kessel: SDO Block-Upload (0xA4, der Normalfall) oder
// Expedited-Upload (0x40, selten). Bis v24 kannte R1 nur 0x40 und loeste deshalb nie aus.
inline bool ist_leseanfrage(uint8_t kommando) { return kommando == 0xA4 || kommando == 0x40; }

inline uint16_t sdo_index(const std::vector<uint8_t> &x) { return (x[2] << 8) | x[1]; }

// R1: WEM fragt den Kessel (0x602) 2101/0A, 2102/0D, 2102/01 ab. Aufruf fuer jeden Frame
// an 0x602; a/b sind die Zeitpunkte der ersten beiden Glieder (0 = nicht gesehen).
// true = die Folge ist mit diesem Frame vollstaendig.
inline bool r1_frame(uint32_t &a, uint32_t &b, const std::vector<uint8_t> &x, uint32_t j) {
  if (x.size() < 5 || !ist_leseanfrage(x[0])) return false;
  uint16_t idx = sdo_index(x);
  uint8_t sub = x[3];
  if (idx == 0x2101 && sub == 0x0A) { a = j | 1; return false; }
  if (idx == 0x2102 && sub == 0x0D) { b = j | 1; return false; }
  if (idx == 0x2102 && sub == 0x01) {
    bool folge = a != 0 && b != 0 && j - b < R1_FENSTER_B_MS && j - a < R1_FENSTER_A_MS && (int32_t)(b - a) >= 0;
    a = 0; b = 0;
    return folge;
  }
  return false;
}

// Liegt der Bootup des WEM (Heartbeat 0x701 = 00) weniger als 5 min zurueck?
inline bool wem_neu(uint32_t wem_start, uint32_t jetzt) {
  return wem_start != 0 && jetzt - wem_start < WEM_NEUSTART_RUHE_MS;
}

// Schreibt der WEM 0x252B (Kommandobyte 0x2x an 0x602)? true = geschrieben UND geaendert.
inline bool w252b_frame(int &vorher, const std::vector<uint8_t> &x, int &wert) {
  if (x.size() < 5 || ist_leseanfrage(x[0])) return false;
  if ((x[0] & 0xE0) != 0x20 || sdo_index(x) != 0x252B) return false;
  wert = x[4];
  int alt = vorher;
  vorher = wert;
  return wert != alt;
}

// Heizkreis-Betriebsart 1 Standby, 5 Sommer = keine Heizanforderung erwartet
inline bool hk_ohne_heizung(int hk) { return hk == 1 || hk == 5; }

// R2/R4 aus 0x252B: 0F = Warmwasserbetrieb, 0A = Heizbetrieb
inline bool r2_bei_252b(int wert, int ww_bekannt) { return wert == 0x0F && ww_bekannt == 2; }
inline bool r4_bei_252b(int wert, int hk_bekannt) { return wert == 0x0A && hk_ohne_heizung(hk_bekannt); }

// Kesselstatus (PDO 0x182, Byte 0). true = Flanke (gueltiger Vorwert und geaendert).
inline bool kstatus_flanke(int &vorher, const std::vector<uint8_t> &x, int &st) {
  if (x.size() < 1) return false;
  st = x[0];
  int alt = vorher;
  vorher = st;
  return !(alt < 0 || st == alt);
}
inline bool r2_bei_kstatus(int st, int ww_bekannt) { return st == 15 && ww_bekannt == 2; }
inline bool r4_bei_kstatus(int st, int hk_bekannt) { return st == 10 && hk_ohne_heizung(hk_bekannt); }

// Statusbits des WEM (PDO 0x1C1, Objekt 0x274D). Bytes 2-3 little-endian:
// Rohbytes 00 00 00 10 -> m = 0x1000 (Standby), Rohbytes 00 00 10 00 -> m = 0x0010 (WW-Ladung).
inline uint16_t statusbits_m(const std::vector<uint8_t> &x) { return (x[3] << 8) | x[2]; }

struct StatusAenderung {
  bool geaendert = false;     // Standby-Bit oder uebrige Bits anders als vorher
  bool r3 = false;            // Bedingung R3: Standby-Bit geaendert (und Vorwert bekannt)
  bool r5 = false;            // Bedingung R5: uebrige Bits geaendert (und Vorwert bekannt)
  int stby = 0, rest = 0;     // neuer Stand
  int sv = -1, rv = -1;       // alter Stand (-1 = unbekannt)
};

// R3 nur Standby-Bit 0x1000; R5 alle uebrigen Bits ohne 0x1000, WW-Ladung 0x0010, Heizbetrieb 0x0040
inline StatusAenderung statusbits(int &stby_vorher, int &rest_vorher, const std::vector<uint8_t> &x) {
  StatusAenderung e;
  if (x.size() < 4) return e;
  uint16_t m = statusbits_m(x);
  e.stby = (m & 0x1000) ? 1 : 0;
  e.rest = m & ~0x1050;
  e.sv = stby_vorher;
  e.rv = rest_vorher;
  bool s_neu = e.stby != e.sv, r_neu = e.rest != e.rv;
  if (!s_neu && !r_neu) return e;
  e.geaendert = true;
  stby_vorher = e.stby;
  rest_vorher = e.rest;
  e.r3 = s_neu && e.sv >= 0;
  e.r5 = r_neu && e.rv >= 0;
  return e;
}

// Was aus einer Statusbit-Aenderung wird. R3 wird ausgefuehrt (auch ausgeschaltet, dann im
// Schattenmodus); liest R3 wirklich (r3_an), wird R5 im selben Frame nicht zusaetzlich gerufen.
struct StatusEntscheid {
  bool r3 = false;            // R3 ausfuehren
  bool r5 = false;            // R5 ausfuehren
  bool r3_liest = false;      // R3 ausgefuehrt und eingeschaltet
};
inline StatusEntscheid status_entscheiden(const StatusAenderung &e, bool eigen, bool r3_an) {
  StatusEntscheid s;
  if (e.r3 && !eigen) { s.r3 = true; s.r3_liest = r3_an; }
  if (e.r5 && !eigen && !s.r3_liest) s.r5 = true;
  return s;
}

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
