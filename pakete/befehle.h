// Pakete WEM-SCHALTEN und MQTT: strukturierter Status und Schaltbefehle mit Kennung.
// Reines C++ ohne ESPHome-IDs (wie regellogik.h): die YAML-Lambdas rufen diese Funktionen
// auf und kuemmern sich um MQTT, Log und den eigentlichen WEM-Aufruf. Getestet in tests/.
//
//   <geraet>/status/json     (retained) fester Aufbau, nur bei Aenderung, hoechstens alle 2 s
//   <geraet>/befehl/status   (retained) jede Phase jedes Schaltbefehls:
//        angenommen -> vorgemerkt -> gesendet -> pruefe_bus -> bestaetigt
//                                                            \-> gescheitert (grund)
//        vorgemerkt -> ersetzt (neuerer Wunsch fuer dasselbe Ziel) | gescheitert (geleert)
//        abgelehnt (ungueltig, nie in der Warteschlange)
// Neu erzeugt wird hier NICHTS auf dem Bus und nichts am WEM - nur Buchfuehrung und Text.
#pragma once
#if __has_include("esphome/components/json/json_util.h")
#include "esphome/components/json/json_util.h"
#else
#include <ArduinoJson.h>
#endif
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <cctype>
#include <cstdlib>

namespace bf {

static const int ZIEL_HK = 1;
static const int ZIEL_WW = 2;
static const size_t MAX_ID = 32;
static const size_t MAX_QUELLE = 16;
static const uint32_t STATUS_MINDESTABSTAND_MS = 2000;

inline const char *ziel_name(int z) { return z == ZIEL_HK ? "heizkreis" : (z == ZIEL_WW ? "warmwasser" : "?"); }

inline const char *hk_text(int w) {
  static const char *n[] = {"?", "Standby", "Zeitprogramm 1", "Zeitprogramm 2", "Zeitprogramm 3",
                            "Sommer", "Komfort", "Normal", "Absenk"};
  return w >= 1 && w <= 8 ? n[w] : "unbekannt";
}
inline const char *ww_text(int w) { return w == 1 ? "Ein" : (w == 2 ? "Aus" : "unbekannt"); }
inline const char *wert_text(int ziel, int w) { return ziel == ZIEL_HK ? hk_text(w) : ww_text(w); }

inline std::string klein(std::string s) {
  for (auto &c : s) c = tolower((unsigned char) c);
  return s;
}
inline std::string trimmen(const std::string &s) {
  size_t a = 0, e = s.size();
  while (a < e && isspace((unsigned char) s[a])) a++;
  while (e > a && isspace((unsigned char) s[e - 1])) e--;
  return s.substr(a, e - a);
}

// "3", "Zeitprogramm 2", "zeitprogramm2", "ZP2", "ZP 2", "Standby" ... -> 1..8, sonst 0
inline int hk_wert(const std::string &roh) {
  std::string s = klein(trimmen(roh)), k;
  for (char c : s) if (c != ' ' && c != '_' && c != '-') k += c;
  if (k.size() == 1 && k[0] >= '1' && k[0] <= '8') return k[0] - '0';
  static const char *n[] = {"", "standby", "zeitprogramm1", "zeitprogramm2", "zeitprogramm3", "sommer", "komfort", "normal", "absenk"};
  for (int i = 1; i <= 8; i++) if (k == n[i]) return i;
  if (k == "zp1") return 2;
  if (k == "zp2") return 3;
  if (k == "zp3") return 4;
  return 0;
}
// "Ein"/"ein"/"1"/"on"/"an" -> 1, "Aus"/"aus"/"2"/"off" -> 2, sonst 0
inline int ww_wert(const std::string &roh) {
  std::string k = klein(trimmen(roh));
  if (k == "ein" || k == "1" || k == "on" || k == "an" || k == "true") return 1;
  if (k == "aus" || k == "2" || k == "off" || k == "false") return 2;
  return 0;
}

// Ein Schaltwunsch, wie er ankommt
struct Anfrage {
  std::string id;             // leer = das Board vergibt eine Kennung
  bool id_zahl = false;       // Kennung kam als Zahl (wird als Zahl zurueckgemeldet)
  int wert = 0;               // 1-8 Heizkreis, 1/2 Warmwasser; 0 = ungueltig
  std::string quelle = "MQTT";
};

// Nutzlast von cmd/heizkreis bzw. cmd/warmwasser lesen: Klartext ("ZP2", "Ein") oder JSON
// {"id":17,"wert":"ZP2","quelle":"App"}. false = ungueltig, f = Grund; eine mitgeschickte
// Kennung steht auch dann in a.id, damit die Ablehnung zugeordnet werden kann.
inline bool anfrage_lesen(int ziel, const std::string &nutzlast, Anfrage &a, std::string &f) {
  std::string p = trimmen(nutzlast);
  if (p.empty()) { f = "leer"; return false; }
  if (p[0] != '{') {
    a.wert = ziel == ZIEL_HK ? hk_wert(p) : ww_wert(p);
    if (!a.wert) { f = std::string("unbekannter Wert '") + p.substr(0, 40) + "'"; return false; }
    return true;
  }
  JsonDocument doc;
  if (deserializeJson(doc, p) || !doc.is<JsonObject>()) { f = "kein gueltiges JSON"; return false; }
  JsonObject o = doc.as<JsonObject>();
  // Kennung zuerst, damit auch eine spaetere Ablehnung sie traegt
  JsonVariant id = o["id"];
  if (!id.isNull()) {
    if (id.is<long>() && !id.is<bool>()) { a.id = std::to_string(id.as<long>()); a.id_zahl = true; }
    else if (id.is<const char *>()) {
      std::string s = id.as<const char *>();
      if (s.empty() || s.size() > MAX_ID) { f = "id: 1-32 Zeichen"; return false; }
      for (char c : s) if (!isprint((unsigned char) c) || c == '"' || c == '\\') { f = "id: nur druckbare Zeichen"; return false; }
      a.id = s;
    } else { f = "id muss Zahl oder Text sein"; return false; }
  }
  for (JsonPair kv : o) {
    std::string k = kv.key().c_str();
    if (k == "id" || k == "wert" || k == "quelle" || (!k.empty() && k[0] == '_')) continue;
    f = "unbekanntes Feld '" + k + "'"; return false;
  }
  JsonVariant q = o["quelle"];
  if (!q.isNull()) {
    if (!q.is<const char *>()) { f = "quelle muss Text sein"; return false; }
    std::string s = q.as<const char *>();
    if (s.empty() || s.size() > MAX_QUELLE) { f = "quelle: 1-16 Zeichen"; return false; }
    for (char c : s) if (!isalnum((unsigned char) c) && c != '-' && c != '_' && c != '/' && c != ' ') { f = "quelle: A-Z a-z 0-9 - _ /"; return false; }
    a.quelle = s;
  }
  JsonVariant w = o["wert"];
  if (w.isNull()) { f = "wert fehlt"; return false; }
  if (w.is<bool>()) {
    if (ziel != ZIEL_WW) { f = "wert true/false nur fuer Warmwasser"; return false; }
    a.wert = w.as<bool>() ? 1 : 2;
  } else if (w.is<long>()) {
    long n = w.as<long>();
    a.wert = ziel == ZIEL_HK ? (n >= 1 && n <= 8 ? (int) n : 0) : (n == 1 || n == 2 ? (int) n : 0);
    if (!a.wert) { f = "wert " + std::to_string(n) + " ausserhalb " + (ziel == ZIEL_HK ? "1-8" : "1-2"); return false; }
  } else if (w.is<const char *>()) {
    std::string s = w.as<const char *>();
    a.wert = ziel == ZIEL_HK ? hk_wert(s) : ww_wert(s);
    if (!a.wert) { f = "unbekannter Wert '" + s.substr(0, 40) + "'"; return false; }
  } else { f = "wert muss Zahl oder Text sein"; return false; }
  return true;
}

// Stand eines Befehls, so wie er auf <geraet>/befehl/status gemeldet wird
struct Befehl {
  std::string id;
  bool id_zahl = false;
  int ziel = 0, wert = 0;
  std::string quelle;
  std::string phase;          // angenommen vorgemerkt gesendet pruefe_bus bestaetigt gescheitert ersetzt abgelehnt
  std::string grund;          // ungueltig cm05 nicht_uebernommen keine_rueckmeldung geleert ersetzt
  std::string text;           // Klartext zum Grund
  std::string wem;            // Antwort des WEM ab pruefe_bus: bestaetigt | unklar | nicht_erreichbar
  int ist = -1;               // am Bus zurueckgelesener Wert
  int warten_s = -1;          // bei vorgemerkt: Sekunden bis zur Sperrminute (0 = sofort)
  std::string ersetzt_durch;  // bei ersetzt: Kennung des neueren Befehls
  uint32_t zeit = 0;          // Unix-Zeit dieser Phase (0 = Uhr noch nicht gestellt)
};

inline bool ist_ende(const std::string &phase) {
  return phase == "bestaetigt" || phase == "gescheitert" || phase == "ersetzt" || phase == "abgelehnt";
}

enum class Wem { BESTAETIGT, UNKLAR, NICHT_ERREICHBAR, CM05 };

// Warteschlange und laufender Befehl. Hoechstens ein Wunsch je Ziel; ein neuer Wunsch fuer
// dasselbe Ziel ersetzt den alten (der NEUESTE gewinnt). Heizkreis wird vor Warmwasser gesendet.
// Jede Phase landet in "ereignisse" - die YAML veroeffentlicht und leert die Liste.
struct Schaltung {
  Befehl wunsch[3];
  bool hat_wunsch[3] = {false, false, false};
  Befehl lauf;
  bool laeuft = false;
  Befehl letzte[3];
  bool hat_letzte[3] = {false, false, false};
  uint32_t zaehler = 0;
  uint32_t uhr = 0;           // aktuelle Unix-Zeit, setzt der Aufrufer (0 = unbekannt)
  std::vector<Befehl> ereignisse;

  std::string neue_id() { return "b" + std::to_string(++zaehler); }
  void melden(Befehl &b, const std::string &phase) {
    b.phase = phase;
    b.zeit = uhr;
    // "letzte" = zuletzt abgeschlossener Befehl je Ziel; ein ersetzter Wunsch zaehlt nicht
    if (ist_ende(phase) && phase != "ersetzt" && b.ziel >= 1 && b.ziel <= 2) { letzte[b.ziel] = b; hat_letzte[b.ziel] = true; }
    ereignisse.push_back(b);
  }

  // ungueltiger Wunsch: nur melden, nichts vormerken
  void ablehnen(int ziel, const Anfrage &a, const std::string &fehler) {
    Befehl b;
    b.id = a.id.empty() ? neue_id() : a.id;
    b.id_zahl = !a.id.empty() && a.id_zahl;
    b.ziel = ziel; b.wert = a.wert; b.quelle = a.quelle;
    b.grund = "ungueltig"; b.text = fehler;
    melden(b, "abgelehnt");
  }

  // gueltiger Wunsch: angenommen + vorgemerkt; warten_s = Sekunden bis gesendet werden darf
  Befehl annehmen(int ziel, const Anfrage &a, int warten_s) {
    Befehl b;
    b.id = a.id.empty() ? neue_id() : a.id;
    b.id_zahl = !a.id.empty() && a.id_zahl;
    b.ziel = ziel; b.wert = a.wert; b.quelle = a.quelle;
    melden(b, "angenommen");
    if (hat_wunsch[ziel]) {
      Befehl alt = wunsch[ziel];
      alt.grund = "ersetzt"; alt.ersetzt_durch = b.id; alt.warten_s = -1; alt.text = "neuerer Wunsch fuer dasselbe Ziel";
      melden(alt, "ersetzt");
    }
    b.warten_s = warten_s < 0 ? 0 : warten_s;
    melden(b, "vorgemerkt");
    wunsch[ziel] = b; hat_wunsch[ziel] = true;
    return b;
  }

  // Wunsch holen und als laufend markieren. Nur rufen, wenn gesendet werden darf.
  bool naechster(Befehl &b) {
    if (laeuft) return false;
    int z = hat_wunsch[ZIEL_HK] ? ZIEL_HK : (hat_wunsch[ZIEL_WW] ? ZIEL_WW : 0);
    if (!z) return false;
    lauf = wunsch[z]; hat_wunsch[z] = false; laeuft = true;
    lauf.warten_s = -1;
    melden(lauf, "gesendet");
    b = lauf;
    return true;
  }

  // Antwort des WEM. CM=05 beendet den Befehl, sonst entscheidet der Bus (pruefe_bus).
  void wem_antwort(Wem w, const std::string &text) {
    if (!laeuft) return;
    lauf.text = text;
    if (w == Wem::CM05) {
      lauf.wem = "cm05"; lauf.grund = "cm05";
      melden(lauf, "gescheitert"); laeuft = false;
      return;
    }
    lauf.wem = w == Wem::BESTAETIGT ? "bestaetigt" : (w == Wem::UNKLAR ? "unklar" : "nicht_erreichbar");
    melden(lauf, "pruefe_bus");
  }

  // Zurueckgelesener Wert am Bus. true = der laufende Befehl ist damit abgeschlossen.
  bool bus_wert(int ziel, int ist) {
    if (!laeuft || lauf.ziel != ziel || lauf.phase != "pruefe_bus") return false;
    lauf.ist = ist;
    if (ist == lauf.wert) { lauf.grund = ""; lauf.text = ""; melden(lauf, "bestaetigt"); }
    else {
      lauf.grund = "nicht_uebernommen";
      lauf.text = std::string("steht auf ") + wert_text(ziel, ist);
      melden(lauf, "gescheitert");
    }
    laeuft = false;
    return true;
  }

  // keine Antwort vom Bus innerhalb der Frist
  void keine_rueckmeldung() {
    if (!laeuft) return;
    lauf.grund = "keine_rueckmeldung"; lauf.text = "keine Rueckmeldung vom Bus - Ergebnis unbekannt";
    melden(lauf, "gescheitert");
    laeuft = false;
  }

  // Warteschlange von Hand leeren; Anzahl der verworfenen Wuensche
  int leeren() {
    int n = 0;
    for (int z = 1; z <= 2; z++) {
      if (!hat_wunsch[z]) continue;
      Befehl b = wunsch[z];
      b.grund = "geleert"; b.text = "Warteschlange von Hand geleert"; b.warten_s = -1;
      melden(b, "gescheitert");
      hat_wunsch[z] = false; n++;
    }
    return n;
  }
};

inline Schaltung &schaltung() { static Schaltung s; return s; }

// ---------------------------------------------------------------- JSON

inline void setze_id(JsonObject o, const Befehl &b) {
  if (b.id_zahl) o["id"] = atol(b.id.c_str());
  else o["id"] = b.id;
}
inline void setze_text(JsonObject o, const char *k, const std::string &v) {
  if (v.empty()) o[k] = nullptr; else o[k] = v;
}
inline void setze_zeit(JsonObject o, const char *k, uint32_t t) {
  if (t == 0) o[k] = nullptr; else o[k] = t;
}

inline void befehl_objekt(JsonObject o, const Befehl &b) {
  setze_id(o, b);
  o["ziel"] = ziel_name(b.ziel);
  if (b.wert) { o["wert"] = b.wert; o["wert_text"] = wert_text(b.ziel, b.wert); }
  else { o["wert"] = nullptr; o["wert_text"] = nullptr; }
  o["quelle"] = b.quelle;
  o["phase"] = b.phase;
  o["ende"] = ist_ende(b.phase);
  setze_text(o, "grund", b.grund);
  setze_text(o, "text", b.text);
  setze_text(o, "wem", b.wem);
  if (b.ist >= 0) { o["ist"] = b.ist; o["ist_text"] = wert_text(b.ziel, b.ist); }
  else { o["ist"] = nullptr; o["ist_text"] = nullptr; }
  if (b.warten_s >= 0) o["warten_s"] = b.warten_s; else o["warten_s"] = nullptr;
  setze_text(o, "ersetzt_durch", b.ersetzt_durch);
  setze_zeit(o, "zeit", b.zeit);
}

inline std::string befehl_json(const Befehl &b) {
  JsonDocument d;
  befehl_objekt(d.to<JsonObject>(), b);
  std::string s;
  serializeJson(d, s);
  return s;
}

// Zustand fuer <geraet>/status/json. Die Setter merken sich, ob sich etwas geaendert hat.
struct Status {
  int hk_vorgabe = -1;        // 0x2933/2 (1-8), -1 = noch nicht gelesen
  uint32_t hk_zeit = 0;
  std::string hk_quelle;
  int hk_bits = -1;           // Statusbits 0x274D (PDO 0x1C1), -1 = noch nicht gesehen
  int ww_vorgabe = -1;        // 0x2A20/2 (1 Ein, 2 Aus)
  uint32_t ww_zeit = 0;
  std::string ww_quelle;
  float ww_soll_akt = NAN, ww_soll_normal = NAN;
  int kesselstatus = -1;      // PDO 0x182
  bool bus_lebt = false, anlaufpause = false;
  uint32_t frei_ab = 0;       // Unix-Zeit, ab der der naechste Schaltbefehl gesendet werden darf
  // Anlass der naechsten Lesung (wer sie ausgeloest hat) - wird bei der Antwort uebernommen
  std::string grund_hk, grund_ww;
  bool geaendert = true;
  uint32_t zuletzt_ms = 0;    // millis() der letzten Veroeffentlichung

  template<typename T> void setze(T &feld, T v) { if (!(feld == v)) { feld = v; geaendert = true; } }
  void setze_f(float &feld, float v) {
    if (std::isnan(v) && std::isnan(feld)) return;
    if (!std::isnan(v) && !std::isnan(feld) && std::fabs(v - feld) < 0.05f) return;
    feld = v; geaendert = true;
  }
  // Lesung vom Bus: Wert, Zeit und Anlass setzen (auch bei gleichem Wert = neue Lesung)
  void hk_gelesen(int w, uint32_t zeit) {
    hk_vorgabe = w; hk_zeit = zeit; hk_quelle = grund_hk.empty() ? "lesung" : grund_hk; grund_hk.clear();
    geaendert = true;
  }
  void ww_gelesen(int w, uint32_t zeit) {
    ww_vorgabe = w; ww_zeit = zeit; ww_quelle = grund_ww.empty() ? "lesung" : grund_ww; grund_ww.clear();
    geaendert = true;
  }
  void anlass(const std::string &g, bool hk, bool ww) {
    if (hk) grund_hk = g;
    if (ww) grund_ww = g;
  }
};

inline Status &status() { static Status s; return s; }

// Darf status/json jetzt veroeffentlicht werden? (nur bei Aenderung, hoechstens alle 2 s)
inline bool senden_faellig(const Status &s, uint32_t jetzt, uint32_t mindest = STATUS_MINDESTABSTAND_MS) {
  return s.geaendert && (s.zuletzt_ms == 0 || jetzt - s.zuletzt_ms >= mindest);
}

inline const char *kessel_text(int st) {
  switch (st) {
    case 0: return "Standby";
    case 1: return "Aus";
    case 10: return "Heizbetrieb";
    case 15: return "Warmwasserbetrieb";
    case 101: return "Kaminfeger";
    case 104: return "Wartung";
    default: return "unbekannt";
  }
}

// jetzt = Unix-Zeit (0 = unbekannt), letzter_frame_s = Sekunden seit dem letzten CAN-Frame
// (-1 = noch nie). schalten.frei_ab nur, solange die Sperrminute noch laeuft (sonst null).
inline std::string status_json(const Status &s, const Schaltung &sch, const std::string &firmware,
                               uint32_t jetzt, long letzter_frame_s) {
  JsonDocument d;
  JsonObject o = d.to<JsonObject>();
  o["v"] = 1;
  o["firmware"] = firmware;
  setze_zeit(o, "zeit", jetzt);

  JsonObject hk = o["heizkreis"].to<JsonObject>();
  if (s.hk_vorgabe >= 1 && s.hk_vorgabe <= 8) { hk["vorgabe"] = s.hk_vorgabe; hk["vorgabe_text"] = hk_text(s.hk_vorgabe); }
  else { hk["vorgabe"] = nullptr; hk["vorgabe_text"] = nullptr; }
  if (s.hk_bits >= 0) {
    hk["ist"] = (s.hk_bits & 0x1000) ? "standby" : "zeitprogramm";
    hk["heizt"] = (s.hk_bits & 0x0040) != 0;
    hk["statusbits"] = s.hk_bits;
  } else { hk["ist"] = nullptr; hk["heizt"] = nullptr; hk["statusbits"] = nullptr; }
  setze_text(hk, "quelle", s.hk_quelle);
  setze_zeit(hk, "zeit", s.hk_zeit);

  JsonObject ww = o["warmwasser"].to<JsonObject>();
  if (s.ww_vorgabe == 1 || s.ww_vorgabe == 2) { ww["vorgabe"] = s.ww_vorgabe; ww["vorgabe_text"] = ww_text(s.ww_vorgabe); }
  else { ww["vorgabe"] = nullptr; ww["vorgabe_text"] = nullptr; }
  if (s.kesselstatus >= 0) ww["ladung"] = s.kesselstatus == 15; else ww["ladung"] = nullptr;
  if (!std::isnan(s.ww_soll_akt)) ww["soll_aktuell"] = std::round(s.ww_soll_akt * 10) / 10.0; else ww["soll_aktuell"] = nullptr;
  if (!std::isnan(s.ww_soll_normal)) ww["soll_normal"] = std::round(s.ww_soll_normal * 10) / 10.0; else ww["soll_normal"] = nullptr;
  setze_text(ww, "quelle", s.ww_quelle);
  setze_zeit(ww, "zeit", s.ww_zeit);

  JsonObject k = o["kessel"].to<JsonObject>();
  if (s.kesselstatus >= 0) { k["status"] = s.kesselstatus; k["status_text"] = kessel_text(s.kesselstatus); }
  else { k["status"] = nullptr; k["status_text"] = nullptr; }

  JsonObject sc = o["schalten"].to<JsonObject>();
  if (sch.laeuft) befehl_objekt(sc["laufend"].to<JsonObject>(), sch.lauf); else sc["laufend"] = nullptr;
  JsonArray wl = sc["warteschlange"].to<JsonArray>();
  for (int z = 1; z <= 2; z++) if (sch.hat_wunsch[z]) befehl_objekt(wl.add<JsonObject>(), sch.wunsch[z]);
  setze_zeit(sc, "frei_ab", (s.frei_ab != 0 && jetzt != 0 && s.frei_ab > jetzt) ? s.frei_ab : 0);
  JsonObject le = sc["letzte"].to<JsonObject>();
  for (int z = 1; z <= 2; z++) {
    if (sch.hat_letzte[z]) befehl_objekt(le[ziel_name(z)].to<JsonObject>(), sch.letzte[z]);
    else le[ziel_name(z)] = nullptr;
  }

  JsonObject b = o["bus"].to<JsonObject>();
  b["lebt"] = s.bus_lebt;
  b["anlaufpause"] = s.anlaufpause;
  if (letzter_frame_s >= 0) b["letzter_frame_s"] = letzter_frame_s; else b["letzter_frame_s"] = nullptr;

  std::string out;
  serializeJson(d, out);
  return out;
}

}  // namespace bf
