// Kern des Pakets kraiz-per-can: Tabelle der zu lesenden CAN-Objekte, Planer, Decoder fuer SDO-Antworten.
// Reines C++ ohne ESPHome-Typen. Gelesen wird ausschliesslich per SDO-Upload (Kommandobyte 0x40).
// Die Eintraege kommen aus pakete/kraiz-per-can.yaml (erzeugt aus kraiz/hassio-weishaupt).
// Autor: cg089 (Fork cg089/weishaupt-wem-can-esphome, Zweig olimex-esp32-evb, 31.10.2026), MIT.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace kc {

struct Eintrag {
  uint8_t kn = 0;
  uint16_t idx = 0;
  uint8_t sub = 0;
  bool vorzeichen = false;
  float faktor = 1.0f;
  uint32_t periode_ms = 60000;
  uint32_t faellig = 0;
  uint32_t gesendet = 0;
  bool offen = false;      // Anfrage gesendet, Antwort steht aus
  bool fehlt = false;      // der Knoten lehnt das Objekt ab (Abbruch 0x80): bis zum Neustart nicht mehr fragen
  std::function<void(int32_t, float)> melden;   // (roher Wert, skalierter Wert)
};

inline std::vector<Eintrag> tabelle;
inline bool fehler_wartet = false;
inline bool schreiben_gesendet = false;

// Objekt eintragen. Die erste Anfrage kommt frueh (nach dem Anlauf des Boards), die weiteren versetzt.
inline void anlegen(uint8_t kn, uint16_t idx, uint8_t sub, bool vorzeichen, float faktor, uint32_t periode_s,
                    std::function<void(int32_t, float)> melden) {
  Eintrag e;
  e.kn = kn; e.idx = idx; e.sub = sub; e.vorzeichen = vorzeichen; e.faktor = faktor;
  e.periode_ms = periode_s * 1000u;
  e.faellig = 20000u + (uint32_t) tabelle.size() * 3000u;
  e.melden = std::move(melden);
  tabelle.push_back(std::move(e));
}

// naechste faellige Anfrage (die am laengsten ueberfaellige); false = nichts zu tun
inline bool naechste(uint32_t jetzt, uint8_t &kn, std::vector<uint8_t> &daten) {
  Eintrag *best = nullptr;
  for (auto &e : tabelle) {
    if (e.offen && (int32_t) (jetzt - e.gesendet) > 10000) e.offen = false;   // Antwort blieb aus
    if (e.fehlt || e.offen) continue;
    if ((int32_t) (jetzt - e.faellig) < 0) continue;
    if (best == nullptr || (int32_t) (best->faellig - e.faellig) > 0) best = &e;
  }
  if (best == nullptr) return false;
  best->offen = true;
  best->gesendet = jetzt;
  best->faellig = jetzt + best->periode_ms;
  kn = best->kn;
  daten = {0x40, (uint8_t) (best->idx & 0xFF), (uint8_t) (best->idx >> 8), best->sub, 0, 0, 0, 0};
  return true;
}

// ein Objekt in ein paar Sekunden noch einmal lesen (Kontrolle nach einem Schreibbefehl)
inline void bald(uint8_t kn, uint16_t idx, uint8_t sub) {
  for (auto &e : tabelle)
    if (e.kn == kn && e.idx == idx && e.sub == sub) { e.faellig = e.gesendet + 4000u; e.fehlt = false; e.offen = false; }
}

// jede SDO-Antwort von Knoten 1 (0x581) und 2 (0x582): gehoert sie zur Tabelle, wird sie veroeffentlicht
inline void antwort(uint32_t can_id, const std::vector<uint8_t> &x) {
  if (x.size() < 8 || can_id < 0x581 || can_id > 0x582) return;
  uint8_t kn = (uint8_t) (can_id - 0x580);
  uint8_t b = x[0];
  uint16_t idx = (uint16_t) (x[1] | (x[2] << 8));
  uint8_t sub = x[3];
  for (auto &e : tabelle) {
    if (e.kn != kn || e.idx != idx || e.sub != sub) continue;
    if (b == 0x80) {                         // Abbruch: nur werten, wenn wir selbst gefragt haben
      if (e.offen) { e.fehlt = true; e.offen = false; }
      return;
    }
    if ((b & 0xE0) != 0x40 || !(b & 0x02)) return;      // nur Upload-Antwort mit Wert in den Bytes 4-7
    int n = (b & 0x01) ? 4 - ((b >> 2) & 0x03) : 4;
    if (n < 1) return;
    uint32_t u = 0;
    for (int i = n - 1; i >= 0; i--) u = (u << 8) | x[4 + i];
    int32_t v = (int32_t) u;
    if (e.vorzeichen) {
      if (n == 1) v = (int8_t) u;
      else if (n == 2) v = (int16_t) u;
    }
    e.offen = false;
    bool ungueltig = e.vorzeichen && n == 2 && (u == 0x7FFF || u == 0x8000);   // "kein Fuehler"
    if (e.melden) e.melden(v, ungueltig ? NAN : v * e.faktor);
    return;
  }
}

inline std::string hex(const std::vector<uint8_t> &d) {
  std::string s;
  char t[4];
  for (size_t i = 0; i < d.size(); i++) {
    snprintf(t, sizeof(t), i ? " %02X" : "%02X", d[i]);
    s += t;
  }
  return s;
}

// Fehler/Warnung-Status aus Byte 2-3 des Fehlerblocks (Auswertung wie kraiz, parsing.py)
inline std::string fehler_text(uint16_t roh) {
  switch (roh & 0x0F) {
    case 1: return "Fehler";
    case 2: return "Warnung";
    case 3: return "Info";
    default: break;
  }
  if ((roh >> 8) > 0) return "Fehler aktiv";
  return "Keine Meldung";
}

}  // namespace kc
