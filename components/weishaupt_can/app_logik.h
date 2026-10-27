// Paket APP (app.yaml, ab v38): die Heizungs-App laeuft direkt auf dem Board.
// Hier steht alles, was ohne ESPHome geht (getestet in tests/): Antwort von /api/status im
// selben Aufbau wie die Docker-App (app/src/server.js), Zusatzwerte aus fremden MQTT-Topics,
// Login-Sperre nach Fehlversuchen, Verlauf der Schaltbefehle. Den Webserver-Teil hat app_server.h.
#pragma once
#include "befehle.h"
#include "zusatz.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace ap {

// Werte, die laenger nicht aktualisiert wurden, gelten als veraltet (wie VERALTET_MS der Docker-App)
static const uint32_t VERALTET_MS = 10UL * 60 * 1000;
// Zusatzwerte aelter als das blendet die App aus (wie ZUSATZ_MAX_ALTER_MS)
static const uint32_t ZUSATZ_MAX_ALTER_MS = 2UL * 60 * 60 * 1000;

// Login-Schutz wie die Docker-App: 5 Fehlversuche je Adresse in 15 min -> 15 min Sperre
static const uint8_t FEHL_MAX = 5;
static const uint32_t FEHL_FENSTER_MS = 15UL * 60 * 1000;
static const uint32_t SPERRE_MS = 15UL * 60 * 1000;
static const size_t MAX_ADRESSEN = 16;   // aelteste Adresse faellt heraus

// ein Sensorwert des Boards, wie er beim Zusammenstellen gerade steht
struct Messwert {
  bool da = false;
  double v = NAN;
  uint32_t alter_ms = 0;    // seit der letzten Aktualisierung
};

// ein Zusatzwert: Eintrag der Liste (zusatz.h) plus letzte Nutzlast seines Topics
struct Zusatzwert {
  zs::Eintrag e;
  bool da = false;
  std::string nutzlast;
  uint32_t alter_ms = 0;
};

// alles, woraus /api/status entsteht
struct Lage {
  std::map<std::string, Messwert> zahlen;     // object_id -> Wert
  std::map<std::string, std::string> texte;   // object_id -> Text (Textsensoren)
  std::string status_json;                    // bf::status_json(...), leer = noch keiner
  std::string firmware;
  std::vector<Zusatzwert> zusatz;
};

// Einstellungen (Betriebsart, Sollwerte) liest das Board nur bei Anlass - sie veralten nicht.
inline bool ohne_altersgrenze(const std::string &n) {
  return n == "aussentemperatur" || n == "heizkreis_betriebsart_code" || n == "warmwasser_betriebsart_code" ||
         n == "warmwasser_soll_normal" || n == "warmwasser_soll_aktuell";
}

inline bool zahl(const Lage &l, const char *name, double &v) {
  auto it = l.zahlen.find(name);
  if (it == l.zahlen.end() || !it->second.da || std::isnan(it->second.v)) return false;
  if (it->second.alter_ms > VERALTET_MS && !ohne_altersgrenze(name)) return false;
  v = it->second.v;
  return true;
}

inline bool text(const Lage &l, const char *name, std::string &t) {
  auto it = l.texte.find(name);
  if (it == l.texte.end() || it->second.empty() || it->second == "unbekannt") return false;
  t = it->second;
  return true;
}

// Zahl aus einer MQTT-Nutzlast: ohne Feld ist die Nutzlast selbst die Zahl, mit Feld "a.b"
// ein verschachteltes JSON-Feld; true/false zaehlen als 1/0 (wie zusatzWert() der Docker-App).
inline bool zusatz_zahl(const std::string &nutzlast, const std::string &feld, double &v) {
  std::string p = bf::trimmen(nutzlast);
  if (feld.empty()) {
    if (p.empty() || p[0] == '{') return false;
    if (p == "true") { v = 1; return true; }
    if (p == "false") { v = 0; return true; }
    char *ende = nullptr;
    double x = std::strtod(p.c_str(), &ende);
    if (ende == p.c_str() || *ende != '\0' || !std::isfinite(x)) return false;
    v = x;
    return true;
  }
  JsonDocument d;
  if (deserializeJson(d, p)) return false;
  JsonVariant o = d.as<JsonVariant>();
  size_t a = 0;
  while (a <= feld.size()) {
    size_t b = feld.find('.', a);
    if (b == std::string::npos) b = feld.size();
    std::string k = feld.substr(a, b - a);
    if (!o.is<JsonObject>()) return false;
    o = o[k];
    a = b + 1;
  }
  if (o.is<bool>()) { v = o.as<bool>() ? 1 : 0; return true; }
  if (o.is<double>() || o.is<long>()) { v = o.as<double>(); return std::isfinite(v); }
  if (o.is<const char *>()) return zusatz_zahl(o.as<std::string>(), "", v);
  return false;
}

// Verlauf der Schaltbefehle (jede gemeldete Phase, neueste zuletzt; je Kennung nur die letzte)
inline std::deque<bf::Befehl> &verlauf() { static std::deque<bf::Befehl> v; return v; }
inline void befehl_merken(const bf::Befehl &b) {
  auto &v = verlauf();
  for (auto it = v.begin(); it != v.end(); ++it)
    if (it->id == b.id) { v.erase(it); break; }
  v.push_back(b);
  while (v.size() > 10) v.pop_front();
}

// Antwort von GET /api/status - Felder und Bedeutung wie in app/src/server.js
inline std::string status_antwort(const Lage &l) {
  JsonDocument d;
  JsonObject o = d.to<JsonObject>();
  JsonDocument sj;
  bool sj_ok = !l.status_json.empty() && !deserializeJson(sj, l.status_json);
  JsonVariant hk, ww;
  if (sj_ok) { hk = sj["heizkreis"]; ww = sj["warmwasser"]; }

  auto setze = [&](const char *k, const char *name) {
    double v;
    if (zahl(l, name, v)) o[k] = v; else o[k] = nullptr;
  };
  setze("outsideTempC", "aussentemperatur");
  setze("kesselTempC", "kesseltemperatur");
  setze("abgasTempC", "abgastemperatur");
  setze("ruecklaufTempC", "ruecklauf");
  {
    double v;
    if (zahl(l, "vorlauf_vpt", v) || zahl(l, "vorlauf_heizkreis", v)) o["vorlaufIstC"] = v; else o["vorlaufIstC"] = nullptr;
  }
  setze("vorlaufSollC", "vorlauf_soll");
  setze("warmwasserC", "warmwasser");

  std::string t;
  if (!ww.isNull() && ww["vorgabe_text"].is<const char *>()) o["warmwasser"] = ww["vorgabe_text"].as<const char *>();
  else if (text(l, "warmwasser_betriebsart", t)) o["warmwasser"] = t;
  else o["warmwasser"] = nullptr;
  std::string ww_aktiv;
  bool ww_aktiv_da = text(l, "warmwasser_aktiv", ww_aktiv);
  if (ww_aktiv_da) o["warmwasserAktiv"] = ww_aktiv; else o["warmwasserAktiv"] = nullptr;

  // Kesselstatus (PDO 0x182): 10 Heizung, 15 Warmwasser, 101 Kaminfeger, 104 Wartung
  double ks;
  bool ks_da = zahl(l, "kesselstatus_code", ks);
  int k = ks_da ? (int) std::lround(ks) : -1;
  const char *zweck = k == 10 ? "Heizung" : k == 15 ? "Warmwasser" : k == 101 ? "Kaminfeger" : k == 104 ? "Wartung" : nullptr;
  JsonObject ladung = o["ladung"].to<JsonObject>();
  if (ks_da) ladung["gas"] = k == 15;
  else if (ww_aktiv_da && (ww_aktiv == "Ein" || ww_aktiv == "Aus")) ladung["gas"] = ww_aktiv == "Ein";
  else ladung["gas"] = nullptr;
  if (zweck) o["kesselZweck"] = zweck; else o["kesselZweck"] = nullptr;

  // Heizkreis fordert an: Statusbit 0x0040 oder Heizanforderung > 0
  long bits = -1;
  if (!hk.isNull() && hk["statusbits"].is<long>()) bits = hk["statusbits"].as<long>();
  else {
    auto it = l.zahlen.find("heizkreis_status_code");   // ohne Altersgrenze (nur bei Aenderung gesendet)
    if (it != l.zahlen.end() && it->second.da && !std::isnan(it->second.v)) bits = std::lround(it->second.v);
  }
  double ha;
  bool fordert = (bits >= 0 && (bits & 0x0040)) || (zahl(l, "heizanforderung", ha) && ha > 0);
  o["heizungWartet"] = zweck && std::string(zweck) == "Warmwasser" && fordert;
  o["kesselZweckWarnung"] = k == 101 || k == 104;

  double fl, ph;
  bool fl_da = zahl(l, "brenner", fl), ph_da = zahl(l, "brennerphase", ph);
  if (fl_da) o["brennerAn"] = std::lround(fl) == 1; else o["brennerAn"] = nullptr;
  static const char *PHASE[] = {"Aus", "Vorlüften", "An", "An", "Nachlüften"};
  long p = ph_da ? std::lround(ph) : -1;
  if (p >= 0 && p <= 4) o["brennerText"] = PHASE[p];
  else if (fl_da) o["brennerText"] = std::lround(fl) == 1 ? "An" : "Aus";
  else o["brennerText"] = nullptr;

  double mc;
  if (!hk.isNull() && hk["vorgabe"].is<long>()) o["modeCode"] = hk["vorgabe"].as<long>();
  else if (zahl(l, "heizkreis_betriebsart_code", mc)) o["modeCode"] = std::lround(mc);
  else o["modeCode"] = nullptr;

  if (!hk.isNull()) o["heizkreis"] = hk; else o["heizkreis"] = nullptr;
  if (bits >= 0) o["heizkreisBits"] = bits; else o["heizkreisBits"] = nullptr;
  if (!ww.isNull()) o["warmwasserStatus"] = ww; else o["warmwasserStatus"] = nullptr;
  if (sj_ok && !sj["schalten"].isNull()) o["schalten"] = sj["schalten"]; else o["schalten"] = nullptr;
  if (sj_ok && !sj["bus"].isNull()) o["bus"] = sj["bus"]; else o["bus"] = nullptr;
  if (!l.firmware.empty()) o["boardFirmware"] = l.firmware; else o["boardFirmware"] = nullptr;
  if (sj_ok) o["statusJsonAlterS"] = 0; else o["statusJsonAlterS"] = nullptr;   // auf dem Board immer frisch

  JsonArray be = o["befehle"].to<JsonArray>();
  for (auto &b : verlauf()) bf::befehl_objekt(be.add<JsonObject>(), b);

  double lf;
  if (zahl(l, "letzter_can-frame_vor", lf)) o["busAlterS"] = lf; else o["busAlterS"] = nullptr;

  JsonArray za = o["zusatz"].to<JsonArray>();
  for (auto &z : l.zusatz) {
    JsonObject e = za.add<JsonObject>();
    e["name"] = z.e.name;
    e["einheit"] = z.e.einheit;
    bool laeuft = z.e.laeuft;
    e["art"] = laeuft ? "laeuft" : "wert";
    double v;
    bool ok = z.da && z.alter_ms <= ZUSATZ_MAX_ALTER_MS && zusatz_zahl(z.nutzlast, z.e.feld, v);
    if (ok) e["wert"] = v; else e["wert"] = nullptr;
    if (laeuft && ok) e["laeuft"] = v >= (std::isfinite(z.e.schwelle) ? (double) z.e.schwelle : 1.0);
    else e["laeuft"] = nullptr;
    if (z.da) e["alter_s"] = z.alter_ms / 1000; else e["alter_s"] = nullptr;
  }
  o["source"] = "board";

  std::string out;
  serializeJson(d, out);
  return out;
}

// Antwort von GET /api/energie (Vortageswerte des WEM)
inline std::string energie_antwort(const Lage &l) {
  JsonDocument d;
  JsonObject o = d.to<JsonObject>();
  double v;
  if (zahl(l, "w__rmemenge_vortag_heizung", v)) o["heizungKwh"] = v; else o["heizungKwh"] = nullptr;
  if (zahl(l, "w__rmemenge_vortag_ww", v)) o["warmwasserKwh"] = v; else o["warmwasserKwh"] = nullptr;
  if (zahl(l, "w__rmemenge_vortag_gesamt", v)) o["gesamtKwh"] = v; else o["gesamtKwh"] = nullptr;
  o["source"] = "board";
  std::string out;
  serializeJson(d, out);
  return out;
}

inline bool theme_ok(const std::string &t);

// Antwort von GET /api/meta (theme = Vorgabe der Darstellung, leer = keine)
inline std::string meta_antwort(const std::string &board_url, const std::string &theme = "") {
  JsonDocument d;
  JsonObject o = d.to<JsonObject>();
  o["app"] = "weishaupt-heizungsapp";
  o["api"] = 1;   // API-Version (wie APP_API in app_dateien.h)
  if (board_url.empty()) o["boardUrl"] = nullptr; else o["boardUrl"] = board_url;
  if (theme_ok(theme)) o["theme"] = theme;
  JsonArray m = o["modes"].to<JsonArray>();
  for (int c = 1; c <= 8; c++) {
    JsonObject e = m.add<JsonObject>();
    e["value"] = c;
    e["label"] = bf::hk_text(c);
  }
  std::string out;
  serializeJson(d, out);
  return out;
}

// Login-Sperre je Adresse (IPv4 als Zahl; 0 = unbekannt, zaehlt als eine Adresse)
class Anmeldung {
 public:
  bool gesperrt(uint32_t adr, uint32_t jetzt) const {
    for (auto &e : this->e_)
      if (e.adr == adr) return e.bis_aktiv && (int32_t) (e.bis - jetzt) > 0;
    return false;
  }
  // true = diese Adresse ist ab jetzt gesperrt
  bool fehlversuch(uint32_t adr, uint32_t jetzt) {
    this->heute++;
    Eintrag *e = nullptr;
    for (auto &x : this->e_)
      if (x.adr == adr) { e = &x; break; }
    if (e == nullptr) {
      if (this->e_.size() >= MAX_ADRESSEN) this->e_.erase(this->e_.begin());
      this->e_.push_back(Eintrag{});
      e = &this->e_.back();
      e->adr = adr;
    }
    std::vector<uint32_t> neu;
    for (auto t : e->zeiten)
      if (jetzt - t < FEHL_FENSTER_MS) neu.push_back(t);
    neu.push_back(jetzt);
    e->zeiten = neu;
    if (e->zeiten.size() >= FEHL_MAX) {
      e->bis = jetzt + SPERRE_MS;
      e->bis_aktiv = true;
      e->zeiten.clear();
      return true;
    }
    return false;
  }
  uint32_t heute = 0;   // Fehlversuche seit dem Start (fuer Anzeige und MQTT-Meldung)

 private:
  struct Eintrag {
    uint32_t adr = 0;
    std::vector<uint32_t> zeiten;
    uint32_t bis = 0;
    bool bis_aktiv = false;
  };
  std::vector<Eintrag> e_;
};

// ---------------------------------------------------------------- Zugangsdaten
// v39-v41 im Klartext gespeichert (Schluessel APPZ) - ab v42 nur noch zum Uebernehmen in zugang.h (APZ2)
struct Zugang {
  char benutzer[33] = {0};
  char app_pw[65] = {0};
  char admin_pw[65] = {0};
  char theme[8] = {0};      // Vorgabe der Darstellung: auto | hell | dunkel
  uint8_t version = 1;
};

inline bool vollstaendig(const Zugang &z) { return z.benutzer[0] && z.app_pw[0] && z.admin_pw[0]; }

inline bool kopieren(char *ziel, size_t n, const std::string &q) {
  if (q.size() >= n) return false;
  std::memset(ziel, 0, n);
  std::memcpy(ziel, q.data(), q.size());
  return true;
}

inline bool benutzer_ok(const std::string &b, std::string &f) {
  if (b.empty() || b.size() > 32) { f = "Benutzer: 1 bis 32 Zeichen"; return false; }
  for (unsigned char c : b)
    if (c <= ' ' || c == 0x7F || c == '"' || c == '\\') { f = "Benutzer: keine Leer- oder Sonderzeichen wie \" oder \\"; return false; }
  return true;
}

// neue Passwoerter: 8 bis 64 Zeichen, keine Steuerzeichen
inline bool passwort_ok(const std::string &p, std::string &f) {
  if (p.size() < 8 || p.size() > 64) { f = "Passwort: 8 bis 64 Zeichen"; return false; }
  for (unsigned char c : p)
    if (c < ' ' || c == 0x7F) { f = "Passwort: keine Steuerzeichen"; return false; }
  return true;
}

inline bool theme_ok(const std::string &t) { return t == "auto" || t == "hell" || t == "dunkel"; }
inline std::string theme(const char *t) { return theme_ok(t) ? std::string(t) : std::string("auto"); }
inline std::string theme(const Zugang &z) { return theme(z.theme); }

// Ersteinrichtung: {"benutzer","app_passwort","admin_passwort"[,"experte_passwort"]} - alle verschieden;
// ohne Experten-Passwort bleibt die Expertenstufe gesperrt
struct Einrichtung {
  std::string benutzer, app, admin, experte;
};
inline bool einrichtung_lesen(const std::string &body, Einrichtung &e, std::string &f) {
  JsonDocument d;
  if (deserializeJson(d, body)) { f = "ungueltige Anfrage"; return false; }
  auto feld = [&](const char *k) { return d[k].is<const char *>() ? d[k].as<std::string>() : std::string(); };
  Einrichtung n{feld("benutzer"), feld("app_passwort"), feld("admin_passwort"), feld("experte_passwort")};
  if (!benutzer_ok(n.benutzer, f) || !passwort_ok(n.app, f) || !passwort_ok(n.admin, f)) return false;
  if (!n.experte.empty() && !passwort_ok(n.experte, f)) return false;
  if (n.app == n.admin || (!n.experte.empty() && (n.experte == n.app || n.experte == n.admin))) {
    f = "App-, Admin- und Experten-Passwort muessen verschieden sein";
    return false;
  }
  e = n;
  return true;
}

// Was darf ueber den Reverse Proxy (aus dem Internet) kommen? App und Einstellungsseite (beide mit
// eigener Login-Sperre). Alles andere gehoert zur Expertenstufe (ESPHome-Oberflaeche, Werte, /update).
inline bool extern_erlaubt(const std::string &url) {
  static const char *const PFADE[] = {"/", "/app", "/app.js", "/style.css", "/manifest.json", "/icon.svg", "/sw.js",
                                      "/einstellungen", "/einstellungen.js"};
  for (auto *p : PFADE)
    if (url == p) return true;
  if (url.compare(0, 5, "/api/") != 0) return false;
  return url != "/api/admin/werksreset";   // Werksreset nur im LAN
}

// Vergleich ohne fruehen Abbruch (die Laenge verraet er trotzdem - Passwoerter sind kurz genug)
inline bool gleich(const std::string &a, const std::string &b) {
  uint8_t x = a.size() == b.size() ? 0 : 1;
  size_t n = a.size() > b.size() ? a.size() : b.size();
  for (size_t i = 0; i < n; i++) x |= (uint8_t) ((i < a.size() ? a[i] : 0) ^ (i < b.size() ? b[i] : 0));
  return x == 0;
}

}  // namespace ap
