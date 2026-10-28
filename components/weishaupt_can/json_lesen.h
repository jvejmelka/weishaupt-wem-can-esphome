// Werte per WEM-JSON abrufen - nur von Hand (Knopf "Aktualisieren"), ab v42. Fuer Boards ohne CAN-Kabel
// oder wenn der Bus ausfaellt. EINE Anfrage mit 7 Objekten (wie die Heizungs-App bis 09/2026, dort am
// lebenden WEM geprueft; hoechstens 7 Objekte, s. u.); der WEM verkraftet kein Dauerabfragen - deshalb nie automatisch, Mindestabstand.
// VG lesen:  01 MI MX OX OS VS + VS Nullbytes;  Antwort: 02 MI MX OX OS VS + Wert (hex, hoechstes Byte zuerst)
// Der WEM liefert manchmal Antworten in anderer Reihenfolge oder aus seinem Zwischenspeicher - deshalb wird
// jede Antwort am Anfang (02 MI MX OX OS) erkannt, nicht an ihrer Position.
#pragma once
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace jl {

// Der WEM beantwortet hoechstens 7 Objekte je Anfrage - das achte lehnt er immer mit CM 05 ab (am 28.10.2026
// geprueft: egal welches Objekt auf Platz 8 steht). Der Vorlauf Heizkreis (MI 02 0x2507) fehlt deshalb; er war
// bei den Proben gleich der Kesseltemperatur.
enum Was { AUSSEN, HK_BETRIEBSART, KESSEL, BRENNERPHASE, WW_BETRIEBSART, WW_IST, WW_SOLL, ANZAHL };

struct Objekt {
  const char *name;
  uint8_t mi, mx;
  uint16_t ox;
  uint8_t os, vs;
  bool temperatur;   // vorzeichenbehaftet, 0,1 °C, 0x8000 = kein Wert
};

// Quellen: register.yaml (json), BorgNumberOne/Weishaupt_CanApiJson, Heizungs-App 2026-09
static const Objekt OBJEKTE[ANZAHL] = {
    {"Aussentemperatur", 0x01, 0x00, 0x26B1, 0x02, 2, true},
    {"Heizkreis-Betriebsart", 0x02, 0x00, 0x2533, 0x02, 1, false},
    {"Kesseltemperatur", 0x07, 0x00, 0x2532, 0x00, 2, true},
    {"Brennerphase", 0x07, 0x00, 0x2541, 0x00, 1, false},
    {"Warmwasser-Betriebsart", 0x03, 0x00, 0x2520, 0x02, 1, false},
    {"Warmwassertemperatur", 0x03, 0x00, 0x2529, 0x02, 2, true},
    {"Warmwasser-Soll", 0x03, 0x00, 0x252C, 0x02, 2, true},
};

inline std::string kopf(const Objekt &o, const char *cm) {
  char b[16];
  snprintf(b, sizeof(b), "%s%02X%02X%04X%02X", cm, o.mi, o.mx, o.ox, o.os);
  return b;
}

inline std::string vg_lesen(const Objekt &o) {
  char b[8];
  snprintf(b, sizeof(b), "%04X", o.vs);
  std::string s = kopf(o, "01") + b;
  for (int i = 0; i < o.vs; i++) s += "00";
  return s;
}

// Rumpf der Anfrage mit allen Objekten
inline std::string rumpf(uint32_t id) {
  std::string s = "{\"ID\":\"" + std::to_string(id % 100000000) + "\",\"SRC\":\"DDC\",\"CAPI\":{\"NN\":" + std::to_string(ANZAHL);
  for (int i = 0; i < ANZAHL; i++) {
    char n[8];
    snprintf(n, sizeof(n), "N%02d", i + 1);
    s += std::string(",\"") + n + "\":{\"VG\":\"" + vg_lesen(OBJEKTE[i]) + "\"}";
  }
  return s + "}}";
}

inline bool gleich_anfang(const std::string &vg, const std::string &k) {
  if (vg.size() < k.size()) return false;
  for (size_t i = 0; i < k.size(); i++)
    if (toupper((unsigned char) vg[i]) != toupper((unsigned char) k[i])) return false;
  return true;
}

// Wert eines Objekts aus der Antwort; false = fehlt, abgelehnt (CM 05) oder "kein Wert" (0x8000)
inline bool wert(const std::string &body, const Objekt &o, float &v) {
  std::string k = kopf(o, "02");
  size_t p = 0;
  while ((p = body.find("\"VG\":\"", p)) != std::string::npos) {
    p += 6;
    size_t e = body.find('"', p);
    if (e == std::string::npos) return false;
    std::string vg = body.substr(p, e - p);
    p = e;
    if (!gleich_anfang(vg, k) || vg.size() < 16) continue;
    int vs = (int) strtol(vg.substr(12, 4).c_str(), nullptr, 16);
    if (vs < 1 || vs > 4 || (int) vg.size() < 16 + 2 * vs) return false;
    uint32_t roh = (uint32_t) strtoul(vg.substr(16, 2 * vs).c_str(), nullptr, 16);
    if (o.temperatur) {
      if (vs == 2 && roh == 0x8000) return false;
      int32_t s = vs == 2 ? (int16_t) roh : vs == 1 ? (int8_t) roh : (int32_t) roh;
      v = s / 10.0f;
    } else {
      v = (float) roh;
    }
    return true;
  }
  return false;
}

}  // namespace jl
