// Oberflaechen-Paket (.wui, ab v42): App und Einstellungsseite als Paket im Dateisystem, ohne neue Firmware.
//   Zeile 1: "WUI1"
//   Zeile 2: JSON-Kopf {"art":"weishaupt-wem-can-oberflaeche","api":N,"version":"...",
//                       "dateien":[{"pfad","typ","laenge","sha256"},...]}
//   danach:  die gzip-Daten aller Dateien hintereinander (Reihenfolge wie im Kopf)
// Das Board nimmt nur Pakete mit seiner API-Version an und prueft jede Datei per SHA-256; sonst bleibt
// die eingebaute Oberflaeche. Erzeugt von werkzeuge/app_einbetten.py --paket.
#pragma once
#include "zugang.h"
#include <cstdint>
#include <string>
#include <vector>
#if __has_include("esphome/components/json/json_util.h")
#include "esphome/components/json/json_util.h"
#else
#include <ArduinoJson.h>
#endif

namespace ob {

struct Datei {
  std::string pfad, typ, sha256;
  size_t start = 0, laenge = 0;   // Lage im Paket (ab Dateianfang)
};

struct Paket {
  int api = 0;
  std::string version;
  std::vector<Datei> dateien;
  size_t groesse = 0;             // erwartete Gesamtgroesse
  const Datei *finden(const std::string &pfad) const {
    for (auto &d : dateien)
      if (d.pfad == pfad) return &d;
    return nullptr;
  }
};

static const size_t KOPF_MAX = 4096;

// anfang = die ersten (bis zu KOPF_MAX) Bytes des Pakets
inline bool kopf_lesen(const std::string &anfang, int api, Paket &p, std::string &f) {
  if (anfang.compare(0, 5, "WUI1\n") != 0) { f = "kein Oberflaechen-Paket"; return false; }
  size_t ende = anfang.find('\n', 5);
  if (ende == std::string::npos) { f = "Kopf zu lang oder unvollstaendig"; return false; }
  JsonDocument d;
  if (deserializeJson(d, anfang.substr(5, ende - 5)) || d["art"] != "weishaupt-wem-can-oberflaeche") { f = "Kopf ungueltig"; return false; }
  Paket n;
  n.api = d["api"] | 0;
  n.version = d["version"] | "";
  if (n.api != api) { f = "passt nicht zur Firmware (API " + std::to_string(n.api) + ", Board " + std::to_string(api) + ")"; return false; }
  size_t pos = ende + 1;
  for (JsonObject e : d["dateien"].as<JsonArray>()) {
    Datei x;
    x.pfad = e["pfad"] | "";
    x.typ = e["typ"] | "";
    x.sha256 = e["sha256"] | "";
    x.laenge = e["laenge"] | 0;
    x.start = pos;
    if (x.pfad.empty() || x.pfad[0] != '/' || x.typ.empty() || x.sha256.size() != 64 || x.laenge == 0 || x.laenge > 256 * 1024) {
      f = "Eintrag ungueltig";
      return false;
    }
    pos += x.laenge;
    n.dateien.push_back(x);
  }
  if (n.dateien.empty() || !n.finden("/app")) { f = "Paket ohne App"; return false; }
  n.groesse = pos;
  p = n;
  return true;
}

inline std::string sha256_hex(const std::string &daten) {
  zg::Sha256 s;
  s.update((const uint8_t *) daten.data(), daten.size());
  uint8_t h[32];
  s.fertig(h);
  static const char *z = "0123456789abcdef";
  std::string out;
  for (auto b : h) { out += z[b >> 4]; out += z[b & 15]; }
  return out;
}

}  // namespace ob
