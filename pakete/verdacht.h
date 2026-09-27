// Paket VERDACHT (verdacht.yaml): Hilfsfunktionen fuer die eigenen Regeln.
// Reines C++ ohne ESPHome-IDs: Einlesen/Pruefen der JSON-Datei, Speicherform, Anzeige.
#pragma once
// Auf dem Board kommt ArduinoJson ueber ESPHome, in den Tests (tests/) direkt.
#if __has_include("esphome/components/json/json_util.h")
#include "esphome/components/json/json_util.h"
#else
#include <ArduinoJson.h>
#endif
#include <cstdint>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <strings.h>
#include <algorithm>

namespace vd {

static const size_t MAX_EIGENE = 8;
static const size_t MAX_NAME = 24;
static const size_t MAX_BESCHR = 120;

struct Regel {
  std::string name;
  std::string beschreibung;
  bool an = true;
  uint16_t can_id = 0;
  uint8_t maske[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t muster[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t lesen = 3;          // 1 = Heizkreis, 2 = Warmwasser, 3 = beide
  uint16_t abstand_min = 10;
  // Laufzeit (nicht gespeichert)
  uint32_t zuletzt_ms = 0;
  std::string zuletzt;
  uint32_t anzahl = 0;
};

inline std::vector<Regel> &eigene() { static std::vector<Regel> v; return v; }

// Inhalt der Datei (cmd/regeln). -1 = Feld fehlt, Wert bleibt wie er ist.
struct Datei {
  int version = -1;           // frei waehlbare Nummer, wird im Stand zurueckgemeldet
  int eigene_an = -1;         // Hauptschalter NUR fuer die eigenen Regeln
  int an[5] = {-1, -1, -1, -1, -1};
  int abstand[5] = {-1, -1, -1, -1, -1};
  int max_h = -1;
  bool hat_eigene = false;
  std::vector<Regel> eigene;
};

inline const char *lesen_text(uint8_t l) { return l == 1 ? "hk" : (l == 2 ? "ww" : "beide"); }

inline std::string hex8(const uint8_t *b) {
  char t[32];
  snprintf(t, sizeof(t), "%02X %02X %02X %02X %02X %02X %02X %02X", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
  return t;
}

// "40 3E 22" oder "403E22" -> bis zu 8 Bytes; n = Anzahl gelesener Bytes
inline bool bytes_lesen(const std::string &s, uint8_t out[8], int &n) {
  std::string h;
  for (char c : s) {
    if (c == ' ' || c == ':' || c == ',' || c == '-') continue;
    if (!isxdigit((unsigned char) c)) return false;
    h += c;
  }
  if (h.empty() || h.size() % 2 || h.size() > 16) return false;
  n = h.size() / 2;
  for (int i = 0; i < 8; i++) out[i] = 0;
  for (int i = 0; i < n; i++) out[i] = (uint8_t) strtoul(h.substr(i * 2, 2).c_str(), nullptr, 16);
  return true;
}

inline bool name_ok(const std::string &n) {
  if (n.empty() || n.size() > MAX_NAME) return false;
  for (char c : n)
    if (!(isalnum((unsigned char) c) || c == '-' || c == '_' || c == '.')) return false;
  std::string k = n;
  for (auto &c : k) c = tolower(c);
  // R1..R5, "eigene" (Hauptschalter der eigenen Regeln) und das alte "verdacht" sind reserviert
  return !(k == "r1" || k == "r2" || k == "r3" || k == "r4" || k == "r5" || k == "eigene" || k == "verdacht");
}

inline bool regel_lesen(JsonObject o, Regel &r, std::string &f) {
  for (JsonPair kv : o) {
    std::string k = kv.key().c_str();
    if (k == "name" || k == "an" || k == "can_id" || k == "maske" || k == "muster" || k == "lesen" ||
        k == "abstand_min" || k == "beschreibung" || (!k.empty() && k[0] == '_')) continue;
    f = "unbekanntes Feld '" + k + "'"; return false;
  }
  if (!o["name"].is<const char *>()) { f = "name fehlt"; return false; }
  r.name = o["name"].as<const char *>();
  if (!name_ok(r.name)) { f = "name '" + r.name + "': 1-24 Zeichen A-Z a-z 0-9 - _ . (nicht R1-R5/eigene)"; return false; }
  std::string p = "Regel " + r.name + ": ";
  if (!o["an"].isNull()) { if (!o["an"].is<bool>()) { f = p + "an muss true/false sein"; return false; } r.an = o["an"].as<bool>(); }
  // CAN-ID: "0x602" oder Zahl
  long id = -1;
  if (o["can_id"].is<int>()) id = o["can_id"].as<int>();
  else if (o["can_id"].is<const char *>()) { const char *s = o["can_id"].as<const char *>(); char *e; id = strtol(s, &e, 0); if (*e) id = -1; }
  if (id < 1 || id > 0x7FF) { f = p + "can_id fehlt oder ungueltig (z.B. \"0x602\")"; return false; }
  if (id == 0x601 || id == 0x581) { f = p + "can_id 0x601/0x581 gesperrt (eigene Anfragen und ihre Antworten)"; return false; }
  r.can_id = id;
  int nm = 0, nk = 0;
  if (!o["muster"].is<const char *>() || !bytes_lesen(o["muster"].as<const char *>(), r.muster, nm)) {
    f = p + "muster fehlt oder ungueltig (1-8 Bytes hex, z.B. \"40 3E 22\")"; return false;
  }
  if (o["maske"].isNull()) { for (int i = 0; i < 8; i++) r.maske[i] = i < nm ? 0xFF : 0x00; }
  else if (!o["maske"].is<const char *>() || !bytes_lesen(o["maske"].as<const char *>(), r.maske, nk)) {
    f = p + "maske ungueltig (1-8 Bytes hex)"; return false;
  }
  for (int i = 0; i < 8; i++) r.muster[i] &= r.maske[i];
  std::string l = o["lesen"].is<const char *>() ? o["lesen"].as<const char *>() : "beide";
  if (l == "hk") r.lesen = 1; else if (l == "ww") r.lesen = 2; else if (l == "beide") r.lesen = 3;
  else { f = p + "lesen muss hk, ww oder beide sein"; return false; }
  if (!o["abstand_min"].isNull()) {
    if (!o["abstand_min"].is<int>() || o["abstand_min"].as<int>() < 1 || o["abstand_min"].as<int>() > 1440) {
      f = p + "abstand_min muss 1-1440 sein"; return false;
    }
    r.abstand_min = o["abstand_min"].as<int>();
  }
  if (!o["beschreibung"].isNull()) {
    if (!o["beschreibung"].is<const char *>()) { f = p + "beschreibung muss Text sein"; return false; }
    r.beschreibung = o["beschreibung"].as<const char *>();
    if (r.beschreibung.size() > MAX_BESCHR) r.beschreibung.resize(MAX_BESCHR);
  }
  return true;
}

inline bool liste_lesen(JsonArray a, std::vector<Regel> &v, std::string &f) {
  v.clear();
  if (a.size() > MAX_EIGENE) { f = "hoechstens 8 eigene Regeln"; return false; }
  for (JsonVariant e : a) {
    if (!e.is<JsonObject>()) { f = "eigene: jeder Eintrag muss ein Objekt sein"; return false; }
    Regel r;
    if (!regel_lesen(e.as<JsonObject>(), r, f)) return false;
    for (auto &x : v) if (strcasecmp(x.name.c_str(), r.name.c_str()) == 0) { f = "Name doppelt: " + r.name; return false; }
    v.push_back(r);
  }
  return true;
}

inline bool datei_lesen(const std::string &txt, Datei &d, std::string &f) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, txt);
  if (e) { f = std::string("kein gueltiges JSON (") + e.c_str() + ")"; return false; }
  if (!doc.is<JsonObject>()) { f = "JSON muss ein Objekt { ... } sein"; return false; }
  JsonObject root = doc.as<JsonObject>();
  static const char *rn[] = {"R1", "R2", "R3", "R4", "R5"};
  for (JsonPair kv : root) {
    std::string k = kv.key().c_str();
    JsonVariant v = kv.value();
    if (k == "verdacht") {
      // bis v22 der gemeinsame Hauptschalter; seit v23 gibt es ihn nicht mehr. Bewusst NICHT
      // umgedeutet: true hiess "R1-R4 an", eigene_regeln_an bedeutet etwas anderes.
      f = "Feld 'verdacht' gibt es seit v23 nicht mehr: R1-R5 einzeln per {\"an\":...}, eigene Regeln per \"eigene_regeln_an\"";
      return false;
    } else if (k == "eigene_regeln_an") {
      if (!v.is<bool>()) { f = "eigene_regeln_an muss true/false sein"; return false; }
      d.eigene_an = v.as<bool>();
    } else if (k == "version") {
      if (!v.is<int>() || v.as<int>() < 0) { f = "version muss eine ganze Zahl >= 0 sein"; return false; }
      d.version = v.as<int>();
    } else if (k == "max_pro_stunde") {
      if (!v.is<int>() || v.as<int>() < 1 || v.as<int>() > 60) { f = "max_pro_stunde muss 1-60 sein"; return false; }
      d.max_h = v.as<int>();
    } else if (k == "eigene") {
      if (!v.is<JsonArray>()) { f = "eigene muss eine Liste [ ... ] sein"; return false; }
      if (!liste_lesen(v.as<JsonArray>(), d.eigene, f)) return false;
      d.hat_eigene = true;
    } else if (k == "kommentar" || (!k.empty() && k[0] == '_')) {
      continue;
    } else {
      int i = -1;
      for (int j = 0; j < 5; j++) if (k == rn[j]) i = j;
      if (i < 0) { f = "unbekanntes Feld '" + k + "'"; return false; }
      if (!v.is<JsonObject>()) { f = k + " muss ein Objekt sein, z.B. {\"an\":true,\"abstand_min\":10}"; return false; }
      JsonObject o = v.as<JsonObject>();
      for (JsonPair p : o) {
        std::string pk = p.key().c_str();
        if (pk != "an" && pk != "abstand_min" && !(pk.size() && pk[0] == '_')) { f = k + ": unbekanntes Feld '" + pk + "'"; return false; }
      }
      if (!o["an"].isNull()) { if (!o["an"].is<bool>()) { f = k + ".an muss true/false sein"; return false; } d.an[i] = o["an"].as<bool>(); }
      if (!o["abstand_min"].isNull()) {
        if (!o["abstand_min"].is<int>() || o["abstand_min"].as<int>() < 1 || o["abstand_min"].as<int>() > 1440) {
          f = k + ".abstand_min muss 1-1440 sein"; return false;
        }
        d.abstand[i] = o["abstand_min"].as<int>();
      }
    }
  }
  return true;
}

inline void regel_json(JsonObject o, const Regel &r, bool laufzeit) {
  char id[8];
  snprintf(id, sizeof(id), "0x%03X", r.can_id);
  o["name"] = r.name;
  o["an"] = r.an;
  o["can_id"] = id;
  o["maske"] = hex8(r.maske);
  o["muster"] = hex8(r.muster);
  o["lesen"] = lesen_text(r.lesen);
  o["abstand_min"] = r.abstand_min;
  if (!r.beschreibung.empty()) o["beschreibung"] = r.beschreibung;
  if (laufzeit) {
    o["zuletzt"] = r.zuletzt;
    o["anzahl"] = r.anzahl;
  }
}

// Speicherform (ohne Laufzeitwerte) - passt in den Flash-Speicher des Boards
inline std::string speicherform() {
  JsonDocument d;
  JsonArray a = d.to<JsonArray>();
  for (auto &r : eigene()) regel_json(a.add<JsonObject>(), r, false);
  std::string s;
  serializeJson(d, s);
  return s;
}

inline bool aus_speicher(const char *txt, std::string &f) {
  eigene().clear();
  if (txt == nullptr || txt[0] == 0) return true;
  JsonDocument d;
  if (deserializeJson(d, txt) || !d.is<JsonArray>()) { f = "Speicher unlesbar"; return false; }
  std::vector<Regel> v;
  if (!liste_lesen(d.as<JsonArray>(), v, f)) return false;
  eigene() = v;
  return true;
}

// Kurzanzeige fuer die Weboberflaeche
inline std::string anzeige() {
  if (eigene().empty()) return "keine";
  std::string s;
  for (auto &r : eigene()) {
    if (!s.empty()) s += " | ";
    char b[200];
    // Trigger kurz: CAN-ID und nur die Bytes, deren Maske nicht 00 ist
    std::string t;
    for (int i = 0; i < 8; i++) {
      char x[4];
      if (r.maske[i] == 0) snprintf(x, sizeof(x), "..");
      else if (r.maske[i] == 0xFF) snprintf(x, sizeof(x), "%02X", r.muster[i]);
      else snprintf(x, sizeof(x), "%02X", r.muster[i]);
      if (!t.empty()) t += " ";
      t += x;
    }
    while (t.size() >= 3 && t.compare(t.size() - 3, 3, " ..") == 0) t.resize(t.size() - 3);
    snprintf(b, sizeof(b), "%s %s: %03X [%s] -> %s, %u min, zuletzt %s, %u x", r.name.c_str(), r.an ? "an" : "AUS",
             r.can_id, t.c_str(), lesen_text(r.lesen), r.abstand_min, r.zuletzt.empty() ? "-" : r.zuletzt.c_str(),
             (unsigned) r.anzahl);
    s += b;
  }
  return s;
}

inline Regel *finden(const std::string &n) {
  for (auto &r : eigene()) if (strcasecmp(r.name.c_str(), n.c_str()) == 0) return &r;
  return nullptr;
}

inline uint32_t fnv(const std::string &s) {
  uint32_t h = 2166136261u;
  for (unsigned char c : s) { h ^= c; h *= 16777619u; }
  return h | 1;
}

// Befehl "NAME an|aus|loeschen" (MQTT cmd/regel, Web-Feld "Regel-Befehl"). Nur Zerlegen und
// Pruefen - das Umschalten macht die YAML. art: FEST = R1-R5 (nr 1-5), HAUPT = "eigene"
// (Hauptschalter der eigenen Regeln), EIGENE = eine eigene Regel mit diesem Namen,
// VERALTET = "verdacht"/"hauptschalter" (gibt es seit v23 nicht mehr), FEHLER = ungueltig.
enum class BefehlArt { FEST, HAUPT, EIGENE, VERALTET, FEHLER };
enum class Aktion { AN, AUS, LOESCHEN };
struct RegelBefehl {
  BefehlArt art = BefehlArt::FEHLER;
  Aktion aktion = Aktion::AN;
  int nr = 0;                 // bei FEST: 1-5
  std::string name;           // wie eingegeben (ohne Leerzeichen am Rand)
  std::string fehler;         // bei FEHLER/VERALTET: Meldung fuer die Anzeige
};

inline RegelBefehl befehl_zerlegen(const std::string &roh) {
  RegelBefehl r;
  std::string b = roh;
  while (!b.empty() && isspace((unsigned char) b.back())) b.pop_back();
  while (!b.empty() && isspace((unsigned char) b.front())) b.erase(0, 1);
  size_t sp = b.find_last_of(' ');
  if (sp == std::string::npos) { r.fehler = "Format: NAME an | NAME aus | NAME loeschen"; return r; }
  r.name = b.substr(0, sp);
  std::string cmd = b.substr(sp + 1);
  while (!r.name.empty() && isspace((unsigned char) r.name.back())) r.name.pop_back();
  for (auto &c : cmd) c = tolower((unsigned char) c);
  if (cmd == "an") r.aktion = Aktion::AN;
  else if (cmd == "aus") r.aktion = Aktion::AUS;
  else if (cmd == "loeschen" || cmd == "l\xc3\xb6schen" || cmd == "l\xc3\x96schen") r.aktion = Aktion::LOESCHEN;
  else { r.fehler = "unbekannt: '" + cmd + "' - erlaubt: an, aus, loeschen"; return r; }
  std::string k = r.name;
  for (auto &c : k) c = tolower((unsigned char) c);
  if (k == "verdacht" || k == "hauptschalter") {
    r.art = BefehlArt::VERALTET;
    r.fehler = "'" + r.name + "' gibt es seit v23 nicht mehr: R1-R5 einzeln schalten, eigene Regeln mit 'eigene an|aus'";
    return r;
  }
  if (k == "eigene") r.art = BefehlArt::HAUPT;
  else if (k.size() == 2 && k[0] == 'r' && k[1] >= '1' && k[1] <= '5') { r.art = BefehlArt::FEST; r.nr = k[1] - '0'; }
  else if (!name_ok(r.name)) { r.fehler = "keine Regel '" + r.name + "' (feste: R1-R5, eigene)"; return r; }
  else r.art = BefehlArt::EIGENE;
  if (r.art != BefehlArt::EIGENE && r.aktion == Aktion::LOESCHEN) {
    r.fehler = r.name + (r.art == BefehlArt::FEST ? " ist eine feste Regel - nur an/aus" : " - nur an/aus");
    r.art = BefehlArt::FEHLER;
  }
  return r;
}

}  // namespace vd
