// Paket ZUSATZ (zusatz.yaml): Liste der Zusatzanzeigen fuer die Handy-App.
// Das Board liest die Werte NICHT selbst - es verwaltet nur die Liste (Name, Topic, Feld ...)
// und veroeffentlicht sie retained unter <geraet>/app/zusatz. Die App abonniert die Topics.
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
#include <cmath>
#include <strings.h>

namespace zs {

static const size_t MAX_EINTRAEGE = 6;
static const size_t MAX_NAME = 24;      // Bytes (UTF-8, Umlaute zaehlen doppelt)
static const size_t MAX_TOPIC = 100;
static const size_t MAX_FELD = 32;
static const size_t MAX_EINHEIT = 8;

struct Eintrag {
  std::string name;
  std::string topic;
  std::string feld;          // leer = Payload ist selbst die Zahl; "a.b" = verschachtelt
  std::string einheit;
  bool laeuft = false;       // art: false = "wert", true = "laeuft" (an, wenn Wert >= schwelle)
  float schwelle = 1.0f;
};

inline std::vector<Eintrag> &liste() { static std::vector<Eintrag> v; return v; }

inline bool name_ok(const std::string &n) {
  if (n.empty() || n.size() > MAX_NAME) return false;
  if (n.front() == ' ' || n.back() == ' ') return false;
  for (unsigned char c : n) if (c < 0x20 || c == '=' || c == '|' || c == '"' || c == '\\') return false;
  std::string k = n; for (auto &c : k) c = tolower(c);
  return k != "alle";
}
inline bool topic_ok(const std::string &t) {
  if (t.empty() || t.size() > MAX_TOPIC || t[0] == '$' || t[0] == '/') return false;
  for (unsigned char c : t) if (c <= 0x20 || c == '+' || c == '#' || c == '"' || c == '\\') return false;
  return true;
}
inline bool feld_ok(const std::string &f) {
  if (f.size() > MAX_FELD) return false;
  for (unsigned char c : f) if (!(isalnum(c) || c == '_' || c == '-' || c == '.' || c == ':')) return false;
  return true;
}
inline bool einheit_ok(const std::string &e) {
  if (e.size() > MAX_EINHEIT) return false;
  for (unsigned char c : e) if (c < 0x20 || c == '"' || c == '\\') return false;
  return true;
}

inline Eintrag *finden(const std::string &n) {
  for (auto &e : liste()) if (strcasecmp(e.name.c_str(), n.c_str()) == 0) return &e;
  return nullptr;
}

// Pruefung eines Eintrags (gemeinsam fuer JSON und Web-Befehl)
inline bool pruefen(const Eintrag &e, std::string &f) {
  if (!name_ok(e.name)) { f = "name: 1-24 Zeichen, ohne = | \" \\ (nicht 'alle')"; return false; }
  std::string p = e.name + ": ";
  if (!topic_ok(e.topic)) { f = p + "topic fehlt oder ungueltig (1-100 Zeichen, ohne Leerzeichen, ohne + #)"; return false; }
  if (!feld_ok(e.feld)) { f = p + "feld ungueltig (bis 32 Zeichen A-Z a-z 0-9 _ - . :)"; return false; }
  if (!einheit_ok(e.einheit)) { f = p + "einheit hoechstens 8 Zeichen"; return false; }
  if (!std::isfinite(e.schwelle)) { f = p + "schwelle muss eine Zahl sein"; return false; }
  return true;
}

inline bool eintrag_lesen(JsonObject o, Eintrag &e, std::string &f) {
  for (JsonPair kv : o) {
    std::string k = kv.key().c_str();
    if (k == "name" || k == "topic" || k == "feld" || k == "einheit" || k == "art" || k == "schwelle" ||
        (!k.empty() && k[0] == '_')) continue;
    f = "unbekanntes Feld '" + k + "'"; return false;
  }
  if (!o["name"].is<const char *>()) { f = "name fehlt"; return false; }
  e.name = o["name"].as<const char *>();
  std::string p = e.name + ": ";
  if (!o["topic"].is<const char *>()) { f = p + "topic fehlt"; return false; }
  e.topic = o["topic"].as<const char *>();
  if (!o["feld"].isNull()) { if (!o["feld"].is<const char *>()) { f = p + "feld muss Text sein"; return false; } e.feld = o["feld"].as<const char *>(); }
  if (!o["einheit"].isNull()) { if (!o["einheit"].is<const char *>()) { f = p + "einheit muss Text sein"; return false; } e.einheit = o["einheit"].as<const char *>(); }
  std::string art = o["art"].is<const char *>() ? o["art"].as<const char *>() : "wert";
  if (!o["art"].isNull() && !o["art"].is<const char *>()) { f = p + "art muss \"wert\" oder \"laeuft\" sein"; return false; }
  if (art == "wert") e.laeuft = false;
  else if (art == "laeuft" || art == "läuft") e.laeuft = true;
  else { f = p + "art muss \"wert\" oder \"laeuft\" sein"; return false; }
  if (!o["schwelle"].isNull()) {
    if (!o["schwelle"].is<float>()) { f = p + "schwelle muss eine Zahl sein"; return false; }
    e.schwelle = o["schwelle"].as<float>();
  }
  return pruefen(e, f);
}

inline bool liste_lesen(JsonArray a, std::vector<Eintrag> &v, std::string &f) {
  v.clear();
  if (a.size() > MAX_EINTRAEGE) { f = "hoechstens 6 Eintraege"; return false; }
  for (JsonVariant x : a) {
    if (!x.is<JsonObject>()) { f = "eintraege: jeder Eintrag muss ein Objekt sein"; return false; }
    Eintrag e;
    if (!eintrag_lesen(x.as<JsonObject>(), e, f)) return false;
    for (auto &y : v) if (strcasecmp(y.name.c_str(), e.name.c_str()) == 0) { f = "Name doppelt: " + e.name; return false; }
    v.push_back(e);
  }
  return true;
}

// Datei {"version":N,"eintraege":[...]}; version -1 = nicht angegeben
inline bool datei_lesen(const std::string &txt, int &version, std::vector<Eintrag> &v, std::string &f) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, txt);
  if (err) { f = std::string("kein gueltiges JSON (") + err.c_str() + ")"; return false; }
  if (!doc.is<JsonObject>()) { f = "JSON muss ein Objekt { ... } sein"; return false; }
  JsonObject root = doc.as<JsonObject>();
  bool hat = false;
  version = -1;
  for (JsonPair kv : root) {
    std::string k = kv.key().c_str();
    if (k == "version") {
      if (!kv.value().is<int>() || kv.value().as<int>() < 0) { f = "version muss eine ganze Zahl >= 0 sein"; return false; }
      version = kv.value().as<int>();
    } else if (k == "eintraege") {
      if (!kv.value().is<JsonArray>()) { f = "eintraege muss eine Liste [ ... ] sein"; return false; }
      if (!liste_lesen(kv.value().as<JsonArray>(), v, f)) return false;
      hat = true;
    } else if (k == "kommentar" || (!k.empty() && k[0] == '_')) {
      continue;
    } else { f = "unbekanntes Feld '" + k + "'"; return false; }
  }
  if (!hat) { f = "eintraege fehlt (leere Liste [] loescht alles)"; return false; }
  return true;
}

inline void eintrag_json(JsonObject o, const Eintrag &e) {
  o["name"] = e.name;
  o["topic"] = e.topic;
  if (!e.feld.empty()) o["feld"] = e.feld;
  if (!e.einheit.empty()) o["einheit"] = e.einheit;
  o["art"] = e.laeuft ? "laeuft" : "wert";
  if (e.laeuft) o["schwelle"] = e.schwelle;
}

inline std::string speicherform() {
  JsonDocument d;
  JsonArray a = d.to<JsonArray>();
  for (auto &e : liste()) eintrag_json(a.add<JsonObject>(), e);
  std::string s;
  serializeJson(d, s);
  return s;
}

inline bool aus_speicher(const char *txt, std::string &f) {
  liste().clear();
  if (txt == nullptr || txt[0] == 0) return true;
  JsonDocument d;
  if (deserializeJson(d, txt) || !d.is<JsonArray>()) { f = "Speicher unlesbar"; return false; }
  std::vector<Eintrag> v;
  if (!liste_lesen(d.as<JsonArray>(), v, f)) return false;
  liste() = v;
  return true;
}

inline std::string schwelle_text(float s) {
  char b[24];
  snprintf(b, sizeof(b), "%g", s);
  return b;
}

// Kurzanzeige fuer die Weboberflaeche
inline std::string anzeige() {
  if (liste().empty()) return "keine";
  std::string s;
  for (auto &e : liste()) {
    if (!s.empty()) s += " | ";
    s += e.name + ": " + e.topic;
    if (!e.feld.empty()) s += " [" + e.feld + "]";
    if (e.laeuft) s += " laeuft ab " + schwelle_text(e.schwelle);
    if (!e.einheit.empty()) s += " " + e.einheit;
  }
  return s;
}

// Web-Befehl:
//   NAME loeschen | alle loeschen
//   NAME topic=... [feld=...] [einheit=...] [art=wert|laeuft] [schwelle=...]
// NAME = alles vor dem ersten Wort mit "=" (darf Leerzeichen enthalten). Ein vorhandener
// Eintrag gleichen Namens wird ersetzt, sonst angehaengt. Rueckgabe: Meldung, ok = geaendert.
inline std::string befehl(const std::string &roh, bool &ok) {
  ok = false;
  std::string b = roh;
  while (!b.empty() && isspace((unsigned char) b.back())) b.pop_back();
  while (!b.empty() && isspace((unsigned char) b.front())) b.erase(0, 1);
  if (b.empty()) return "Format: NAME topic=... [feld=...] [einheit=...] [art=wert|laeuft] [schwelle=...]  |  NAME loeschen  |  alle loeschen";
  std::vector<std::string> w;
  size_t i = 0;
  while (i < b.size()) {
    while (i < b.size() && b[i] == ' ') i++;
    size_t j = i;
    while (j < b.size() && b[j] != ' ') j++;
    if (j > i) w.push_back(b.substr(i, j - i));
    i = j;
  }
  std::string letzt = w.back();
  for (auto &c : letzt) c = tolower(c);
  if (letzt == "loeschen" || letzt == "löschen") {
    std::string name;
    for (size_t k = 0; k + 1 < w.size(); k++) { if (!name.empty()) name += " "; name += w[k]; }
    std::string kl = name; for (auto &c : kl) c = tolower(c);
    if (kl == "alle") { liste().clear(); ok = true; return "alle Eintraege geloescht"; }
    auto &v = liste();
    for (size_t k = 0; k < v.size(); k++)
      if (strcasecmp(v[k].name.c_str(), name.c_str()) == 0) { v.erase(v.begin() + k); ok = true; return name + " geloescht"; }
    return "kein Eintrag '" + name + "'";
  }
  Eintrag e;
  size_t k = 0;
  for (; k < w.size() && w[k].find('=') == std::string::npos; k++) { if (!e.name.empty()) e.name += " "; e.name += w[k]; }
  if (k == w.size()) return "unbekannt - Format: NAME topic=... [feld=...] [einheit=...] [art=wert|laeuft] [schwelle=...]";
  bool hat_topic = false;
  for (; k < w.size(); k++) {
    size_t p = w[k].find('=');
    if (p == std::string::npos) return "'" + w[k] + "': erwartet schluessel=wert (keine Leerzeichen im Wert)";
    std::string s = w[k].substr(0, p), v = w[k].substr(p + 1);
    for (auto &c : s) c = tolower(c);
    if (s == "topic") { e.topic = v; hat_topic = true; }
    else if (s == "feld") e.feld = v;
    else if (s == "einheit") e.einheit = v;
    else if (s == "art") {
      if (v == "wert") e.laeuft = false; else if (v == "laeuft" || v == "läuft") e.laeuft = true;
      else return "art muss wert oder laeuft sein";
    } else if (s == "schwelle") {
      char *end; e.schwelle = strtof(v.c_str(), &end);
      if (v.empty() || *end) return "schwelle muss eine Zahl sein";
    } else return "unbekannter Schluessel '" + s + "' (topic, feld, einheit, art, schwelle)";
  }
  if (!hat_topic) return "topic=... fehlt";
  std::string f;
  if (!pruefen(e, f)) return f;
  if (Eintrag *alt = finden(e.name)) { *alt = e; ok = true; return e.name + " ersetzt"; }
  if (liste().size() >= MAX_EINTRAEGE) return "schon 6 Eintraege - erst einen loeschen";
  liste().push_back(e);
  ok = true;
  return e.name + " hinzugefuegt";
}

inline uint32_t fnv(const std::string &s) {
  uint32_t h = 2166136261u;
  for (unsigned char c : s) { h ^= c; h *= 16777619u; }
  return h | 1;
}

// <geraet>/app/zusatz (retained): die Liste fuer die Handy-App
inline std::string app_json(const std::string &firmware, int version) {
  JsonDocument d;
  JsonObject o = d.to<JsonObject>();
  o["firmware"] = firmware;
  if (version >= 0) o["version"] = version;
  JsonArray a = o["eintraege"].to<JsonArray>();
  for (auto &e : liste()) eintrag_json(a.add<JsonObject>(), e);
  std::string s;
  serializeJson(d, s);
  return s;
}

}  // namespace zs
