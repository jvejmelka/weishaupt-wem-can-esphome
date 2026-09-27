// Paket VERDACHT (verdacht.yaml): Regelformat (feste und eigene Regeln), Einlesen/Pruefen der
// JSON-Datei, Speicherform, Anzeige. Reines C++ ohne ESPHome-IDs. Ausgewertet werden die Regeln
// in regelwerk.h.
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
#include <array>
#include <cstdlib>

namespace vd {

static const size_t MAX_EIGENE = 8;
static const size_t MAX_NAME = 24;
static const size_t MAX_BESCHR = 120;

// ---- Regelformat (ab v30): feste Regeln R1-R5 und eigene Regeln sind dieselben Daten ----
//
// Eine Regel hat einen oder mehrere AUSLOESER. Trifft einer zu, wird (nach Ruhezeiten,
// Mindestabstand und Obergrenze) die Betriebsart gelesen, die in "lesen" steht.
//   FRAME     ein Frame: CAN-ID, Mindestlaenge, (Daten UND Maske) == eines der Muster
//   FOLGE     2-4 Frames in dieser Reihenfolge; jedes Glied (ausser dem letzten) darf beim
//             letzten hoechstens fenster_ms alt sein
//   FLANKE    ein Wert im Frame (1-2 Bytes little-endian, UND bits) hat sich geaendert;
//             bits mit genau einem gesetzten Bit ergeben 0/1
//   BAUSTEIN  benannte Rechnung im Code, die sich nicht als Muster ausdruecken laesst
//             (bisher nur "r4_vorlauf": Raumsoll-Stufe aus Vorlaufsoll und Aussentemperatur)
// Jeder Ausloeser kann zusaetzlich den bekannten Zustand verlangen ("wenn": hk/ww in Liste).
// Auf Regelebene: "ruhe_nach" (nach einem bestimmten Frame ruht die Regel ms lang) und
// "unterdrueckt_durch" (liest die genannte Regel im selben Frame schon, wird nicht zusaetzlich gelesen).

using Bytes8 = std::array<uint8_t, 8>;

struct Glied {
  uint16_t can_id = 0;
  uint8_t laenge_min = 0;           // Frame muss mindestens so viele Datenbytes haben
  Bytes8 maske{};                   // gemeinsame Maske aller Muster
  std::vector<Bytes8> muster;       // eines muss passen ("oder"); leer = jeder Frame dieser ID
  uint32_t fenster_ms = 0;          // nur in einer FOLGE: hoechstes Alter beim letzten Glied (0 = egal)
};

enum class Art : uint8_t { FRAME, FOLGE, FLANKE, BAUSTEIN };

struct Ausloeser {
  Art art = Art::FRAME;
  std::vector<Glied> glieder;       // FRAME/FLANKE/BAUSTEIN: 1, FOLGE: 2-4
  // FLANKE
  uint8_t byte = 0, bytes = 1;      // Wert: 'bytes' Bytes ab 'byte', little-endian
  uint16_t bits = 0xFFFF;           // UND-Maske
  bool erster = false;              // true: schon der erste gesehene Wert gilt als Aenderung
  int8_t platz = -1;                // Vorwert im Flash (nur feste Regeln R3/R5: 0/1)
  std::vector<int> gleich;          // nur bei diesen neuen Werten (leer = jede Aenderung)
  // alle Arten
  std::vector<int> hk, ww;          // bekannter Zustand muss in der Liste sein (leer = egal)
  std::string baustein;             // BAUSTEIN: Name
  std::string text;                 // Kennung fuer das Log, z.B. "Kesselstatus 15"
  std::string meldung_ruht;         // Logtext (leer = Vorgabe), Platzhalter siehe regelwerk.h
  std::string meldung_unterdrueckt;
  // Laufzeit (nicht gespeichert)
  std::vector<uint32_t> t;          // FOLGE: Zeitpunkte der Glieder (0 = nicht gesehen)
  int vorher = -1;                  // FLANKE: Vorwert; BAUSTEIN: eigener Zustand
};

struct Regel {
  std::string name;
  std::string beschreibung;
  bool an = true;
  uint8_t nr = 0;                   // 1-5 = feste Regel (Schalter/Abstand aus der Weboberflaeche), 0 = eigene
  std::vector<Ausloeser> ausloeser;
  Glied ruhe;                       // ruhe_nach: dieser Frame ...
  uint32_t ruhe_ms = 0;             // ... laesst die Regel so lange ruhen (0 = keine)
  std::string ruhe_grund;
  std::string unterdrueckt_durch;
  uint8_t lesen = 3;                // 1 = Heizkreis, 2 = Warmwasser, 3 = beide
  uint16_t abstand_min = 10;
  // Laufzeit (nicht gespeichert)
  uint32_t ruhe_seit = 0;
  uint32_t zuletzt_ms = 0;
  std::string zuletzt;
  uint32_t anzahl = 0;
};

// Kurzform (Format bis v29): genau ein FRAME-Ausloeser mit einem Muster, sonst nichts.
// So gespeicherte/angezeigte Regeln sehen byteweise aus wie bisher.
inline bool kurzform(const Regel &r) {
  if (r.ausloeser.size() != 1 || r.ruhe_ms != 0 || !r.unterdrueckt_durch.empty()) return false;
  const Ausloeser &a = r.ausloeser[0];
  return a.art == Art::FRAME && a.glieder.size() == 1 && a.glieder[0].muster.size() == 1 && a.glieder[0].laenge_min == 0 &&
         a.hk.empty() && a.ww.empty() && a.text.empty() && a.meldung_ruht.empty() && a.meldung_unterdrueckt.empty();
}

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

inline bool id_lesen(JsonVariant v, uint16_t &id, const std::string &p, std::string &f) {
  long x = -1;
  if (v.is<int>()) x = v.as<int>();
  else if (v.is<const char *>()) { const char *s = v.as<const char *>(); char *e; x = strtol(s, &e, 0); if (*e || e == s) x = -1; }
  if (x < 1 || x > 0x7FF) { f = p + "can_id fehlt oder ungueltig (z.B. \"0x602\")"; return false; }
  if (x == 0x601 || x == 0x581) { f = p + "can_id 0x601/0x581 gesperrt (eigene Anfragen und ihre Antworten)"; return false; }
  id = (uint16_t) x;
  return true;
}

// "muster" (Text oder Liste mit 1-4 Texten) und "maske". Fehlt die Maske, zaehlen genau die
// Bytes, die im Muster stehen. Das Muster wird mit der Maske verundet.
inline bool muster_lesen(JsonVariant vm, JsonVariant vk, Glied &g, const std::string &p, bool pflicht, std::string &f) {
  static const char *falsch = "muster fehlt oder ungueltig (1-8 Bytes hex, z.B. \"40 3E 22\")";
  std::vector<std::string> ms;
  if (vm.is<const char *>()) ms.push_back(vm.as<const char *>());
  else if (vm.is<JsonArray>()) {
    JsonArray a = vm.as<JsonArray>();
    if (a.size() < 1 || a.size() > 4) { f = p + "muster: Liste mit 1-4 Mustern"; return false; }
    for (JsonVariant e : a) { if (!e.is<const char *>()) { f = p + falsch; return false; } ms.push_back(e.as<const char *>()); }
  } else if (!vm.isNull() || pflicht) { f = p + falsch; return false; }
  g.muster.clear();
  int n0 = -1;
  for (auto &s : ms) {
    Bytes8 b{};
    int n = 0;
    if (!bytes_lesen(s, b.data(), n)) { f = p + falsch; return false; }
    if (n0 < 0) n0 = n;
    else if (n != n0 && vk.isNull()) { f = p + "Muster unterschiedlich lang - maske angeben"; return false; }
    g.muster.push_back(b);
  }
  if (vk.isNull()) { for (int i = 0; i < 8; i++) g.maske[i] = i < n0 ? 0xFF : 0x00; }
  else {
    int nk = 0;
    if (!vk.is<const char *>() || !bytes_lesen(vk.as<const char *>(), g.maske.data(), nk)) { f = p + "maske ungueltig (1-8 Bytes hex)"; return false; }
  }
  for (auto &b : g.muster) for (int i = 0; i < 8; i++) b[i] &= g.maske[i];
  return true;
}

inline bool zahl_lesen(JsonVariant v, long lo, long hi, long &x) {
  if (v.is<int>()) x = v.as<long>();
  else if (v.is<const char *>()) { const char *s = v.as<const char *>(); char *e; x = strtol(s, &e, 0); if (*e || e == s) return false; }
  else return false;
  return x >= lo && x <= hi;
}

inline bool liste_zahlen(JsonVariant v, std::vector<int> &l, long lo, long hi) {
  l.clear();
  if (!v.is<JsonArray>()) return false;
  JsonArray a = v.as<JsonArray>();
  if (a.size() < 1 || a.size() > 16) return false;
  for (JsonVariant e : a) { long x = 0; if (!zahl_lesen(e, lo, hi, x)) return false; l.push_back((int) x); }
  return true;
}

inline bool nur_felder(JsonObject o, const std::vector<const char *> &erlaubt, const std::string &p, std::string &f) {
  for (JsonPair kv : o) {
    std::string k = kv.key().c_str();
    if (!k.empty() && k[0] == '_') continue;
    bool ok = false;
    for (auto e : erlaubt) if (k == e) ok = true;
    if (!ok) { f = p + "unbekanntes Feld '" + k + "'"; return false; }
  }
  return true;
}

// Frame-Muster {can_id, maske, muster, laenge_min[, fenster_ms]}
// ruhe = true: zusaetzlich "ms" und "grund" erlaubt (ruhe_nach, dort ausgewertet)
inline bool glied_lesen(JsonVariant v, Glied &g, const std::string &p, bool fenster, std::string &f, bool ruhe = false) {
  if (!v.is<JsonObject>()) { f = p + "muss ein Objekt {\"can_id\":..., \"muster\":...} sein"; return false; }
  JsonObject o = v.as<JsonObject>();
  std::vector<const char *> erlaubt = {"can_id", "maske", "muster", "laenge_min"};
  if (fenster) erlaubt.push_back("fenster_ms");
  if (ruhe) { erlaubt.push_back("ms"); erlaubt.push_back("grund"); }
  if (!nur_felder(o, erlaubt, p, f)) return false;
  if (!id_lesen(o["can_id"], g.can_id, p, f)) return false;
  if (!muster_lesen(o["muster"], o["maske"], g, p, false, f)) return false;
  long x = 0;
  if (!o["laenge_min"].isNull()) { if (!zahl_lesen(o["laenge_min"], 0, 8, x)) { f = p + "laenge_min muss 0-8 sein"; return false; } g.laenge_min = x; }
  if (fenster && !o["fenster_ms"].isNull()) {
    if (!zahl_lesen(o["fenster_ms"], 1, 3600000, x)) { f = p + "fenster_ms muss 1-3600000 sein"; return false; }
    g.fenster_ms = x;
  }
  return true;
}

inline bool wenn_lesen(JsonVariant v, Ausloeser &a, const std::string &p, std::string &f) {
  if (v.isNull()) return true;
  if (!v.is<JsonObject>()) { f = p + "wenn muss ein Objekt sein, z.B. {\"ww\":[2]}"; return false; }
  JsonObject o = v.as<JsonObject>();
  if (!nur_felder(o, {"hk", "ww"}, p + "wenn: ", f)) return false;
  if (!o["hk"].isNull() && !liste_zahlen(o["hk"], a.hk, 0, 255)) { f = p + "wenn.hk: Liste mit 1-16 Zahlen 0-255"; return false; }
  if (!o["ww"].isNull() && !liste_zahlen(o["ww"], a.ww, 0, 255)) { f = p + "wenn.ww: Liste mit 1-16 Zahlen 0-255"; return false; }
  return true;
}

inline bool text_lesen(JsonVariant v, std::string &t, size_t max, const std::string &p, const char *feld, std::string &f) {
  if (v.isNull()) return true;
  if (!v.is<const char *>()) { f = p + feld + " muss Text sein"; return false; }
  t = v.as<const char *>();
  if (t.size() > max) { f = p + feld + " zu lang (hoechstens " + std::to_string(max) + " Zeichen)"; return false; }
  return true;
}

// Bekannte Bausteine (Rechnungen im Code, regelwerk.h)
inline bool baustein_bekannt(const std::string &b) { return b == "r4_vorlauf"; }

inline bool ausloeser_lesen(JsonVariant v, Ausloeser &a, const std::string &p, std::string &f) {
  if (!v.is<JsonObject>()) { f = p + "jeder Ausloeser muss ein Objekt sein"; return false; }
  JsonObject o = v.as<JsonObject>();
  if (!nur_felder(o, {"frame", "folge", "flanke", "baustein", "byte", "bytes", "bits", "erster", "gleich", "wenn", "text",
                      "meldung_ruht", "meldung_unterdrueckt"}, p, f)) return false;
  int arten = !o["frame"].isNull() + !o["folge"].isNull() + !o["flanke"].isNull() + !o["baustein"].isNull();
  if (arten != 1) { f = p + "genau eines von frame, folge, flanke, baustein angeben"; return false; }
  bool flanke = !o["flanke"].isNull();
  if (!flanke && (!o["byte"].isNull() || !o["bytes"].isNull() || !o["bits"].isNull() || !o["erster"].isNull() || !o["gleich"].isNull())) {
    f = p + "byte, bytes, bits, erster, gleich nur bei flanke"; return false;
  }
  if (!o["frame"].isNull()) {
    a.art = Art::FRAME;
    Glied g;
    if (!glied_lesen(o["frame"], g, p + "frame: ", false, f)) return false;
    a.glieder = {g};
  } else if (!o["folge"].isNull()) {
    a.art = Art::FOLGE;
    if (!o["folge"].is<JsonArray>() || o["folge"].size() < 2 || o["folge"].size() > 4) { f = p + "folge: Liste mit 2-4 Frame-Mustern"; return false; }
    int i = 0;
    JsonArray fl = o["folge"].as<JsonArray>();
    for (JsonVariant e : fl) {
      Glied g;
      if (!glied_lesen(e, g, p + "folge[" + std::to_string(i++) + "]: ", true, f)) return false;
      a.glieder.push_back(g);
    }
    a.glieder.back().fenster_ms = 0;   // das letzte Glied ist der Zeitpunkt der Ausloesung
  } else if (flanke) {
    a.art = Art::FLANKE;
    Glied g;
    if (!glied_lesen(o["flanke"], g, p + "flanke: ", false, f)) return false;
    a.glieder = {g};
    long x = 0;
    if (!o["byte"].isNull()) { if (!zahl_lesen(o["byte"], 0, 7, x)) { f = p + "byte muss 0-7 sein"; return false; } a.byte = x; }
    if (!o["bytes"].isNull()) { if (!zahl_lesen(o["bytes"], 1, 2, x)) { f = p + "bytes muss 1 oder 2 sein"; return false; } a.bytes = x; }
    if (a.byte + a.bytes > 8) { f = p + "byte + bytes hoechstens 8"; return false; }
    if (!o["bits"].isNull()) { if (!zahl_lesen(o["bits"], 1, 0xFFFF, x)) { f = p + "bits muss 1-0xFFFF sein, z.B. \"0x1000\""; return false; } a.bits = x; }
    if (!o["erster"].isNull()) { if (!o["erster"].is<bool>()) { f = p + "erster muss true/false sein"; return false; } a.erster = o["erster"].as<bool>(); }
    if (!o["gleich"].isNull() && !liste_zahlen(o["gleich"], a.gleich, 0, 0xFFFF)) { f = p + "gleich: Liste mit 1-16 Zahlen"; return false; }
  } else {
    a.art = Art::BAUSTEIN;
    if (!o["baustein"].is<const char *>() || !baustein_bekannt(o["baustein"].as<const char *>())) {
      f = p + "baustein unbekannt (vorhanden: r4_vorlauf)"; return false;
    }
    a.baustein = o["baustein"].as<const char *>();
  }
  if (!wenn_lesen(o["wenn"], a, p, f)) return false;
  if (!text_lesen(o["text"], a.text, 40, p, "text", f)) return false;
  if (!text_lesen(o["meldung_ruht"], a.meldung_ruht, 80, p, "meldung_ruht", f)) return false;
  if (!text_lesen(o["meldung_unterdrueckt"], a.meldung_unterdrueckt, 80, p, "meldung_unterdrueckt", f)) return false;
  return true;
}

// intern = true: auch die reservierten Namen R1-R5 (nur fuer Tests der festen Regeln)
inline bool regel_lesen(JsonObject o, Regel &r, std::string &f, bool intern = false) {
  if (!nur_felder(o, {"name", "an", "can_id", "maske", "muster", "laenge_min", "wenn", "ausloeser", "ruhe_nach",
                      "unterdrueckt_durch", "lesen", "abstand_min", "beschreibung"}, "", f)) return false;
  if (!o["name"].is<const char *>()) { f = "name fehlt"; return false; }
  r.name = o["name"].as<const char *>();
  if (!(intern ? !r.name.empty() && r.name.size() <= MAX_NAME : name_ok(r.name))) {
    f = "name '" + r.name + "': 1-24 Zeichen A-Z a-z 0-9 - _ . (nicht R1-R5/eigene)"; return false;
  }
  std::string p = "Regel " + r.name + ": ";
  if (!o["an"].isNull()) { if (!o["an"].is<bool>()) { f = p + "an muss true/false sein"; return false; } r.an = o["an"].as<bool>(); }
  r.ausloeser.clear();
  if (!o["ausloeser"].isNull()) {
    if (!o["can_id"].isNull() || !o["maske"].isNull() || !o["muster"].isNull() || !o["laenge_min"].isNull() || !o["wenn"].isNull()) {
      f = p + "entweder Kurzform (can_id, maske, muster, laenge_min, wenn) oder ausloeser"; return false;
    }
    if (!o["ausloeser"].is<JsonArray>() || o["ausloeser"].size() < 1 || o["ausloeser"].size() > 4) {
      f = p + "ausloeser: Liste mit 1-4 Ausloesern"; return false;
    }
    int i = 0;
    JsonArray al = o["ausloeser"].as<JsonArray>();
    for (JsonVariant e : al) {
      Ausloeser a;
      if (!ausloeser_lesen(e, a, p + "ausloeser[" + std::to_string(i++) + "]: ", f)) return false;
      r.ausloeser.push_back(a);
    }
  } else {
    // Kurzform (bis v29): ein Frame mit CAN-ID, Maske und Muster
    Ausloeser a;
    Glied g;
    if (!id_lesen(o["can_id"], g.can_id, p, f)) return false;
    if (!muster_lesen(o["muster"], o["maske"], g, p, true, f)) return false;
    long x = 0;
    if (!o["laenge_min"].isNull()) { if (!zahl_lesen(o["laenge_min"], 0, 8, x)) { f = p + "laenge_min muss 0-8 sein"; return false; } g.laenge_min = x; }
    if (!wenn_lesen(o["wenn"], a, p, f)) return false;
    a.glieder = {g};
    r.ausloeser.push_back(a);
  }
  if (!o["ruhe_nach"].isNull()) {
    JsonVariant v = o["ruhe_nach"];
    if (!v.is<JsonObject>()) { f = p + "ruhe_nach muss ein Objekt sein"; return false; }
    JsonObject ro = v.as<JsonObject>();
    if (!glied_lesen(v, r.ruhe, p + "ruhe_nach: ", false, f, true)) return false;
    long x = 0;
    if (!zahl_lesen(ro["ms"], 1, 86400000L, x)) { f = p + "ruhe_nach.ms muss 1-86400000 sein"; return false; }
    r.ruhe_ms = x;
    r.ruhe_grund = "ruhe_nach";
    if (!text_lesen(ro["grund"], r.ruhe_grund, 40, p + "ruhe_nach.", "grund", f)) return false;
  }
  if (!o["unterdrueckt_durch"].isNull()) {
    if (!o["unterdrueckt_durch"].is<const char *>() || o["unterdrueckt_durch"].as<std::string>().empty() ||
        o["unterdrueckt_durch"].as<std::string>().size() > MAX_NAME) {
      f = p + "unterdrueckt_durch muss ein Regelname sein"; return false;
    }
    r.unterdrueckt_durch = o["unterdrueckt_durch"].as<const char *>();
  }
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

inline std::string id_text(uint16_t id) {
  char b[8];
  snprintf(b, sizeof(b), "0x%03X", id);
  return b;
}

inline void glied_json(JsonObject o, const Glied &g) {
  o["can_id"] = id_text(g.can_id);
  if (!g.muster.empty()) {
    o["maske"] = hex8(g.maske.data());
    if (g.muster.size() == 1) o["muster"] = hex8(g.muster[0].data());
    else { JsonArray a = o["muster"].to<JsonArray>(); for (auto &m : g.muster) a.add(hex8(m.data())); }
  }
  if (g.laenge_min) o["laenge_min"] = g.laenge_min;
  if (g.fenster_ms) o["fenster_ms"] = g.fenster_ms;
}

inline void ausloeser_json(JsonObject o, const Ausloeser &a) {
  if (a.art == Art::FRAME) glied_json(o["frame"].to<JsonObject>(), a.glieder[0]);
  else if (a.art == Art::FOLGE) { JsonArray l = o["folge"].to<JsonArray>(); for (auto &g : a.glieder) glied_json(l.add<JsonObject>(), g); }
  else if (a.art == Art::FLANKE) {
    glied_json(o["flanke"].to<JsonObject>(), a.glieder[0]);
    o["byte"] = a.byte;
    o["bytes"] = a.bytes;
    char b[8];
    snprintf(b, sizeof(b), "0x%04X", a.bits);
    o["bits"] = b;
    if (a.erster) o["erster"] = true;
    if (!a.gleich.empty()) { JsonArray l = o["gleich"].to<JsonArray>(); for (int x : a.gleich) l.add(x); }
  } else o["baustein"] = a.baustein;
  if (!a.hk.empty() || !a.ww.empty()) {
    JsonObject w = o["wenn"].to<JsonObject>();
    if (!a.hk.empty()) { JsonArray l = w["hk"].to<JsonArray>(); for (int x : a.hk) l.add(x); }
    if (!a.ww.empty()) { JsonArray l = w["ww"].to<JsonArray>(); for (int x : a.ww) l.add(x); }
  }
  if (!a.text.empty()) o["text"] = a.text;
  if (!a.meldung_ruht.empty()) o["meldung_ruht"] = a.meldung_ruht;
  if (!a.meldung_unterdrueckt.empty()) o["meldung_unterdrueckt"] = a.meldung_unterdrueckt;
}

// Kurzform wie bis v29 (gleiche Felder, gleiche Reihenfolge); sonst Langform mit "ausloeser"
inline void regel_json(JsonObject o, const Regel &r, bool laufzeit) {
  o["name"] = r.name;
  o["an"] = r.an;
  if (kurzform(r)) {
    const Glied &g = r.ausloeser[0].glieder[0];
    o["can_id"] = id_text(g.can_id);
    o["maske"] = hex8(g.maske.data());
    o["muster"] = hex8(g.muster[0].data());
  } else {
    JsonArray a = o["ausloeser"].to<JsonArray>();
    for (auto &x : r.ausloeser) ausloeser_json(a.add<JsonObject>(), x);
    if (r.ruhe_ms) {
      JsonObject ro = o["ruhe_nach"].to<JsonObject>();
      glied_json(ro, r.ruhe);
      ro["ms"] = r.ruhe_ms;
      ro["grund"] = r.ruhe_grund;
    }
    if (!r.unterdrueckt_durch.empty()) o["unterdrueckt_durch"] = r.unterdrueckt_durch;
  }
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
    if (kurzform(r)) {
      const Glied &g = r.ausloeser[0].glieder[0];
      for (int i = 0; i < 8; i++) {
        char x[4];
        if (g.maske[i] == 0) snprintf(x, sizeof(x), "..");
        else snprintf(x, sizeof(x), "%02X", g.muster[0][i]);
        if (!t.empty()) t += " ";
        t += x;
      }
      while (t.size() >= 3 && t.compare(t.size() - 3, 3, " ..") == 0) t.resize(t.size() - 3);
      snprintf(b, sizeof(b), "%s %s: %03X [%s] -> %s, %u min, zuletzt %s, %u x", r.name.c_str(), r.an ? "an" : "AUS",
               g.can_id, t.c_str(), lesen_text(r.lesen), r.abstand_min, r.zuletzt.empty() ? "-" : r.zuletzt.c_str(),
               (unsigned) r.anzahl);
    } else {
      static const char *art[] = {"frame", "folge", "flanke", "baustein"};
      for (auto &a : r.ausloeser) {
        if (!t.empty()) t += " + ";
        char x[40];
        if (a.art == Art::BAUSTEIN) snprintf(x, sizeof(x), "baustein %s", a.baustein.c_str());
        else if (a.art == Art::FOLGE) snprintf(x, sizeof(x), "folge %03X x%u", a.glieder[0].can_id, (unsigned) a.glieder.size());
        else snprintf(x, sizeof(x), "%s %03X", art[(int) a.art], a.glieder[0].can_id);
        t += x;
      }
      snprintf(b, sizeof(b), "%s %s: %s -> %s, %u min, zuletzt %s, %u x", r.name.c_str(), r.an ? "an" : "AUS", t.c_str(),
               lesen_text(r.lesen), r.abstand_min, r.zuletzt.empty() ? "-" : r.zuletzt.c_str(), (unsigned) r.anzahl);
    }
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

// <geraet>/regeln/stand (retained): aktiver Stand der Regeln. an/ab = Schalter und
// Mindestabstaende R1-R5, version = zuletzt uebernommene Datei (-1 = keine)
inline std::string stand_json(const std::string &firmware, int version, const bool an[5], const int ab[5], int max_h,
                              bool eigene_an) {
  static const char *rn[] = {"R1", "R2", "R3", "R4", "R5"};
  JsonDocument d;
  JsonObject o = d.to<JsonObject>();
  o["firmware"] = firmware;
  if (version >= 0) o["version"] = version;
  for (int i = 0; i < 5; i++) {
    JsonObject r = o[rn[i]].to<JsonObject>();
    r["an"] = an[i];
    r["abstand_min"] = ab[i];
  }
  o["max_pro_stunde"] = max_h;
  o["eigene_regeln_an"] = eigene_an;
  JsonArray a = o["eigene"].to<JsonArray>();
  for (auto &r : eigene()) regel_json(a.add<JsonObject>(), r, true);
  std::string s;
  serializeJson(d, s);
  return s;
}

// <geraet>/verdacht/ereignis (NICHT retained). phase: waere | ausgeloest | ergebnis | keine_antwort;
// geaendert nur bei ergebnis (-1 = null); zeit leer = Uhr nicht gestellt (Feld fehlt dann)
inline std::string ereignis_json(const std::string &regel, const std::string &phase, const std::string &ziel,
                                 const std::string &wert, int geaendert, const std::string &zeit) {
  JsonDocument d;
  JsonObject o = d.to<JsonObject>();
  o["regel"] = regel;
  o["phase"] = phase;
  o["ziel"] = ziel;
  if (!wert.empty()) o["wert"] = wert;
  if (phase == "ergebnis") { if (geaendert < 0) o["geaendert"] = nullptr; else o["geaendert"] = geaendert == 1; }
  if (!zeit.empty()) o["zeit"] = zeit;
  std::string s;
  serializeJson(d, s);
  return s;
}

}  // namespace vd
