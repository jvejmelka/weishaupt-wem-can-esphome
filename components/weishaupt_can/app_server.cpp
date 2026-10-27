#include "weishaupt_can.h"
#ifdef USE_WC_APP
#include "app_server.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_MQTT
#include "esphome/components/mqtt/mqtt_client.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif
#ifdef USE_LOGGER
#include "esphome/components/logger/logger.h"
#endif
#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <lwip/sockets.h>
#include <cstring>

namespace esphome {
namespace weishaupt_can {

static const char *const TAG = "app";

// Werte des Boards, die /api/status und /api/energie brauchen (object_id wie im MQTT-Topic)
static const char *const ZAHLEN[] = {
    "aussentemperatur", "kesseltemperatur", "abgastemperatur", "ruecklauf", "vorlauf_vpt", "vorlauf_heizkreis",
    "vorlauf_soll", "warmwasser", "brenner", "brennerphase", "heizkreis_betriebsart_code", "kesselstatus_code",
    "heizkreis_status_code", "heizanforderung", "letzter_can-frame_vor", "w__rmemenge_vortag_heizung",
    "w__rmemenge_vortag_ww", "w__rmemenge_vortag_gesamt"};
static const char *const TEXTE[] = {"warmwasser_aktiv", "warmwasser_betriebsart"};

// Schluessel im Flash (fest, wie die Globals der anderen Pakete)
static const uint32_t TOKEN_SCHLUESSEL = 0x41505054;   // "APPT" Sitzungs-Token der App
static const uint32_t ADMIN_SCHLUESSEL = 0x41505041;   // "APPA" Sitzungs-Token der Einstellungsseite
static const uint32_t ZUGANG_SCHLUESSEL = 0x4150505A;  // "APPZ" Zugangsdaten

// Einstellungen der Einstellungsseite: Gruppe, Name des Schalters bzw. der Zahl auf dem Board.
// Was es auf dem Board nicht gibt (Paket nicht eingebunden), fehlt einfach.
static const char *const EINSTELLUNGEN[][2] = {
    {"Schalten", "Schalten per CAN statt JSON (Heizkreis, Warmwasser)"},
    {"Schalten", "Abstand zwischen Befehlen per CAN (s)"},
    {"Schalten", "Abstand zwischen Befehlen per JSON (s)"},
    {"Lesen", "Eigene CAN-Anfragen"},
    {"Lesen", "Lange Lesebefehle auch am Kessel"},
    {"Lesen", "SD-Dateien abrufen"},
    {"Regeln", "R1 Warmwasser am Display oder Portal umgeschaltet"},
    {"Regeln", "R2 Warmwasserbetrieb trotz Aus"},
    {"Regeln", "R3 Heizung ein oder Standby"},
    {"Regeln", "R4 Heizanforderung passt nicht"},
    {"Regeln", "R5 uebrige Statusbits geaendert"},
    {"Regeln", "Eigene Regeln (Hauptschalter)"},
    {"Experten", "Freies CAN-Schreiben (Experten)"},
    {"Experten", "CAN-Rohmitschnitt nach MQTT"},
    {"Experten", "Waechter: Neustart nach Minuten ohne Bus (0 = aus)"},
};

// zufaelliges Token (64 Hex-Zeichen), gespeichert; neu = immer ein neues erzeugen (Passwort geaendert)
std::string AppServer::token_laden_(ESPPreferenceObject &pref, uint32_t schluessel, bool neu) {
  std::array<char, 65> t{};
  pref = global_preferences->make_preference<std::array<char, 65>>(schluessel);
  if (neu || !pref.load(&t) || std::strlen(t.data()) != 64) {
    uint8_t z[32];
    random_bytes(z, sizeof(z));
    for (int i = 0; i < 32; i++) snprintf(&t[i * 2], 3, "%02x", z[i]);
    pref.save(&t);
  }
  return t.data();
}

void AppServer::zugang_speichern_() {
  ap::Zugang z;
  { std::lock_guard<std::mutex> g(this->sperre_); z = this->zugang_; }
  this->zugang_pref_.save(&z);
  global_preferences->sync();
}

void AppServer::web_passwort_setzen_() {
#ifdef USE_WEBSERVER_AUTH
  std::string pw;
  {
    std::lock_guard<std::mutex> g(this->sperre_);
    pw = this->zugang_.admin_pw;
  }
  if (pw.empty()) {
    // noch nicht eingerichtet: Weboberflaeche mit Zufallspasswort sperren (kein bekanntes Standardpasswort)
    uint8_t z[16];
    random_bytes(z, sizeof(z));
    for (int i = 0; i < 16; i++) snprintf(&this->web_pw_[i * 2], 3, "%02x", z[i]);
  } else {
    ap::kopieren(this->web_pw_, sizeof(this->web_pw_), pw);
  }
  web_server_base::global_web_server_base->set_auth_username("admin");
  web_server_base::global_web_server_base->set_auth_password(this->web_pw_);
#endif
}

void AppServer::setup() {
  // Sitzungs-Token: einmal erzeugt, im Flash gespeichert - ein Update meldet niemanden ab
  this->token_ = this->token_laden_(this->token_pref_, TOKEN_SCHLUESSEL, false);
  this->admin_token_ = this->token_laden_(this->admin_pref_, ADMIN_SCHLUESSEL, false);

  // Zugangsdaten: gespeicherte gelten; sonst die Vorgaben aus der YAML; fehlen auch die -> Ersteinrichtung
  this->zugang_pref_ = global_preferences->make_preference<ap::Zugang>(ZUGANG_SCHLUESSEL);
  ap::Zugang z;
  if (!this->zugang_pref_.load(&z) || !ap::vollstaendig(z)) {
    z = ap::Zugang{};
    ap::kopieren(z.benutzer, sizeof(z.benutzer), this->benutzer_.substr(0, 32));
    ap::kopieren(z.app_pw, sizeof(z.app_pw), this->passwort_.substr(0, 64));
    ap::kopieren(z.admin_pw, sizeof(z.admin_pw), this->admin_.substr(0, 64));
    if (ap::vollstaendig(z)) {
      this->zugang_pref_.save(&z);
      ESP_LOGI(TAG, "Zugangsdaten aus der Konfiguration uebernommen");
    }
  }
  this->zugang_ = z;
  this->einrichtung_noetig_ = !ap::vollstaendig(z);
  if (this->einrichtung_noetig_) ESP_LOGW(TAG, "keine Zugangsdaten - Ersteinrichtung unter /app");
  this->web_passwort_setzen_();
  this->einstellungen_sammeln_();
#ifdef USE_LOGGER
  if (logger::global_logger != nullptr)
    logger::global_logger->add_log_callback(this, [](void *self, uint8_t stufe, const char *tag, const char *text, size_t n) {
      static_cast<AppServer *>(self)->log_zeile(stufe, text, n);
    });
#endif

  char buf[OBJECT_ID_MAX_LEN];
#ifdef USE_SENSOR
  for (auto *s : App.get_sensors()) {
    std::string id = s->get_object_id_to(buf).str();
    for (auto *n : ZAHLEN) {
      if (id != n) continue;
      size_t i = this->quellen_.size();
      this->quellen_.push_back(Quelle{id, false});
      s->add_on_state_callback([this, i](float v) {
        auto &q = this->quellen_[i];
        q.wert.da = true; q.wert.v = v; q.zeit = millis();
      });
    }
  }
#endif
#ifdef USE_TEXT_SENSOR
  for (auto *s : App.get_text_sensors()) {
    std::string id = s->get_object_id_to(buf).str();
    for (auto *n : TEXTE) {
      if (id != n) continue;
      size_t i = this->quellen_.size();
      this->quellen_.push_back(Quelle{id, true});
      s->add_on_state_callback([this, i](const std::string &v) {
        auto &q = this->quellen_[i];
        q.t = v; q.wert.da = true; q.zeit = millis();
      });
    }
  }
#endif
  // vor dem Handler der Weboberflaeche einhaengen ist nicht noetig: der kennt /app und /api/ nicht
  web_server_base::global_web_server_base->add_handler_without_auth(this);
  ESP_LOGI(TAG, "Heizungs-App unter /app (%u Werte, Version %s)", (unsigned) this->quellen_.size(), ap::APP_VERSION);
}

void AppServer::loop() {
  uint32_t jetzt = millis();
  if (jetzt - this->zuletzt_ < 1000 && this->zuletzt_ != 0) return;
  this->zuletzt_ = jetzt | 1;
  this->zusatz_abonnieren_();
  this->zusammenstellen_();
}

void AppServer::zusatz_abonnieren_() {
#ifdef USE_MQTT
  std::string stand;
  for (auto &e : zs::liste()) stand += e.topic + "\n";
  if (stand == this->abo_stand_) return;
  for (auto &a : this->abos_) mqtt::global_mqtt_client->unsubscribe(a.topic);
  this->abos_.clear();
  for (auto &e : zs::liste()) {
    bool doppelt = false;
    for (auto &a : this->abos_) doppelt |= a.topic == e.topic;
    if (doppelt) continue;
    this->abos_.push_back(Abo{e.topic});
  }
  for (size_t i = 0; i < this->abos_.size(); i++) {
    mqtt::global_mqtt_client->subscribe(this->abos_[i].topic, [this, i](const std::string &, const std::string &p) {
      if (i >= this->abos_.size()) return;
      auto &a = this->abos_[i];
      a.nutzlast = p.substr(0, 512); a.da = true; a.zeit = millis();
    });
  }
  this->abo_stand_ = stand;
  ESP_LOGI(TAG, "%u Zusatz-Topic(s) abonniert", (unsigned) this->abos_.size());
#endif
}

void AppServer::zusammenstellen_() {
  uint32_t jetzt = millis();
  ap::Lage l;
  for (auto &q : this->quellen_) {
    if (q.text) {
      if (q.wert.da) l.texte[q.name] = q.t;
    } else {
      ap::Messwert m = q.wert;
      m.alter_ms = jetzt - q.zeit;
      l.zahlen[q.name] = m;
    }
  }
  auto &bus = this->wc_->bus;
  long lf = bus.letzter_frame == 0 ? -1 : (long) (bus.still(jetzt) / 1000);
  l.status_json = bf::status_json(bf::status(), bf::schaltung(), this->firmware_, this->wc_->unix_zeit(), lf);
  l.firmware = this->firmware_;
  for (auto &e : zs::liste()) {
    ap::Zusatzwert z;
    z.e = e;
    for (auto &a : this->abos_) {
      if (a.topic != e.topic || !a.da) continue;
      z.da = true; z.nutzlast = a.nutzlast; z.alter_ms = jetzt - a.zeit;
    }
    l.zusatz.push_back(z);
  }
  std::string s = ap::status_antwort(l), e = ap::energie_antwort(l), st = this->einstellungen_json_();
  std::string in = this->info_json_(), si = this->sicherung_json_();
  std::lock_guard<std::mutex> g(this->sperre_);
  this->status_.swap(s);
  this->energie_.swap(e);
  this->einstellungen_stand_.swap(st);
  this->info_.swap(in);
  this->sicherung_.swap(si);
}

void AppServer::einstellungen_sammeln_() {
  char buf[OBJECT_ID_MAX_LEN];
  for (auto &e : EINSTELLUNGEN) {
#ifdef USE_SWITCH
    for (auto *s : App.get_switches()) {
      if (s->get_name() != e[1]) continue;
      this->einstellungen_.push_back(Einstellung{s->get_object_id_to(buf).str(), e[1], e[0], s, false});
    }
#endif
#ifdef USE_NUMBER
    for (auto *n : App.get_numbers()) {
      if (n->get_name() != e[1]) continue;
      this->einstellungen_.push_back(Einstellung{n->get_object_id_to(buf).str(), e[1], e[0], n, true});
    }
#endif
  }
}

// Stand aller Einstellungen (Hauptschleife, jede Sekunde) - der Webserver liefert ihn nur aus
std::string AppServer::einstellungen_json_() {
  JsonDocument d;
  JsonObject o = d.to<JsonObject>();
  {
    std::lock_guard<std::mutex> g(this->sperre_);
    o["benutzer"] = this->zugang_.benutzer;
    o["darstellung"] = ap::theme(this->zugang_);
  }
  o["firmware"] = this->firmware_;
  o["app_version"] = ap::APP_VERSION;
  JsonArray a = o["liste"].to<JsonArray>();
  for (auto &e : this->einstellungen_) {
    JsonObject x = a.add<JsonObject>();
    x["id"] = e.id;
    x["name"] = e.name;
    x["gruppe"] = e.gruppe;
#ifdef USE_NUMBER
    if (e.zahl) {
      auto *n = static_cast<number::Number *>(e.obj);
      x["art"] = "zahl";
      if (n->has_state()) x["wert"] = n->state; else x["wert"] = nullptr;
      x["min"] = n->traits.get_min_value();
      x["max"] = n->traits.get_max_value();
      x["schritt"] = n->traits.get_step();
      continue;
    }
#endif
#ifdef USE_SWITCH
    auto *s = static_cast<switch_::Switch *>(e.obj);
    x["art"] = "schalter";
    x["wert"] = s->state;
#endif
  }
  std::string out;
  serializeJson(d, out);
  return out;
}

// Log: nur Info, Warnung, Fehler; Farbcodes entfernen; die letzten 80 Zeilen (Text ab "[I][tag:...]")
void AppServer::log_zeile(uint8_t stufe, const char *text, size_t laenge) {
  if (stufe > ESPHOME_LOG_LEVEL_INFO) return;
  std::string z;
  z.reserve(laenge < 200 ? laenge : 200);
  for (size_t i = 0; i < laenge && z.size() < 200; i++) {
    char c = text[i];
    if (c == '\033') {                      // ESC [ ... m ueberspringen
      while (i < laenge && text[i] != 'm') i++;
      continue;
    }
    if (c == '\n' || c == '\r') c = ' ';
    z.push_back(c);
  }
  uint32_t s = millis() / 1000;
  char t[16];
  snprintf(t, sizeof(t), "%02u:%02u:%02u ", (unsigned) (s / 3600 % 100), (unsigned) (s / 60 % 60), (unsigned) (s % 60));
  std::lock_guard<std::mutex> g(this->log_sperre_);
  this->log_.push_back(t + z);
  while (this->log_.size() > 80) this->log_.pop_front();
}

static const char *neustart_grund() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "Strom an";
    case ESP_RST_SW: return "Neustart per Software (Update, Knopf, Waechter)";
    case ESP_RST_PANIC: return "Absturz (Panic)";
    case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "Watchdog";
    case ESP_RST_BROWNOUT: return "Unterspannung";
    case ESP_RST_DEEPSLEEP: return "Tiefschlaf";
    case ESP_RST_EXT: return "Reset-Taste";
    default: return "unbekannt";
  }
}

std::string AppServer::info_json_() {
  JsonDocument d;
  JsonObject o = d.to<JsonObject>();
  o["firmware"] = this->firmware_;
  o["app_version"] = ap::APP_VERSION;
  o["laufzeit_s"] = millis() / 1000;
  o["neustart_grund"] = neustart_grund();
  o["speicher_frei"] = esp_get_free_heap_size();
  o["speicher_min"] = esp_get_minimum_free_heap_size();
  o["groesster_block"] = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#ifdef USE_WIFI
  if (wifi::global_wifi_component != nullptr && wifi::global_wifi_component->is_connected()) {
    char ssid[wifi::SSID_BUFFER_SIZE];
    o["wlan"] = wifi::global_wifi_component->wifi_ssid_to(ssid);
    o["wlan_rssi"] = wifi::global_wifi_component->wifi_rssi();
    auto ips = wifi::global_wifi_component->get_ip_addresses();
    char ip[network::IP_ADDRESS_BUFFER_SIZE];
    if (ips[0].is_set()) o["ip"] = ips[0].str_to(ip);
  }
#endif
  auto &bus = this->wc_->bus;
  uint32_t jetzt = millis();
  o["bus_lebt"] = bus.lebt(jetzt);
  if (bus.letzter_frame != 0) o["letzter_frame_s"] = bus.still(jetzt) / 1000; else o["letzter_frame_s"] = nullptr;
  o["uhr"] = this->wc_->zeit("%d.%m.%Y %H:%M:%S");
  std::string out;
  serializeJson(d, out);
  return out;
}

// Sicherung: alle Einstellungen der Einstellungsseite und die Darstellung - OHNE Passwoerter
std::string AppServer::sicherung_json_() {
  JsonDocument d;
  JsonObject o = d.to<JsonObject>();
  o["art"] = "weishaupt-wem-can-einstellungen";
  o["version"] = 1;
  o["firmware"] = this->firmware_;
  o["uhr"] = this->wc_->zeit("%Y-%m-%d %H:%M");
  JsonObject e = o["einstellungen"].to<JsonObject>();
  {
    std::lock_guard<std::mutex> g(this->sperre_);
    e["darstellung"] = ap::theme(this->zugang_);
  }
  for (auto &x : this->einstellungen_) {
#ifdef USE_NUMBER
    if (x.zahl) {
      auto *n = static_cast<number::Number *>(x.obj);
      if (n->has_state()) e[x.id] = n->state;
      continue;
    }
#endif
#ifdef USE_SWITCH
    e[x.id] = static_cast<switch_::Switch *>(x.obj)->state;
#endif
  }
  std::string out;
  serializeJson(d, out);
  return out;
}

// ---------------------------------------------------------------- Webserver-Faden

static bool beginnt(const char *s, const char *p) { return std::strncmp(s, p, std::strlen(p)) == 0; }

bool AppServer::canHandle(AsyncWebServerRequest *request) const {
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  StringRef u = request->url_to(buf);
  return beginnt(u.c_str(), "/api/") || ap::datei(u.c_str(), u.size()) != nullptr;
}

void AppServer::handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (index == 0) this->body_.clear();
  if (this->body_.size() + len <= 4096) this->body_.append(reinterpret_cast<const char *>(data), len);
}

// Der IDF-Webserver von ESPHome kennt nur einige Statuscodes (sonst 500) - die uebrigen hier setzen.
// Die Antwort gehoert der Anfrage (wird mit ihr geloescht).
static void status_setzen(AsyncWebServerRequest *r, int code) {
  if (code == 304) httpd_resp_set_status(*r, "304 Not Modified");
  else if (code == 409) httpd_resp_set_status(*r, "409 Conflict");
  else if (code == 429) httpd_resp_set_status(*r, "429 Too Many Requests");
  else if (code == 503) httpd_resp_set_status(*r, "503 Service Unavailable");
}

void AppServer::antworten_(AsyncWebServerRequest *r, int code, const std::string &json) {
  auto *res = r->beginResponse(code, "application/json", json);
  status_setzen(r, code);
  res->addHeader("Cache-Control", "no-store");
  r->send(res);
}

bool AppServer::angemeldet_(AsyncWebServerRequest *r) const {
  auto t = r->get_header("x-app-token");
  std::lock_guard<std::mutex> g(this->sperre_);
  return t.has_value() && !this->einrichtung_noetig_ && ap::gleich(t.value(), this->token_);
}

bool AppServer::admin_angemeldet_(AsyncWebServerRequest *r) const {
  auto t = r->get_header("x-admin-token");
  std::lock_guard<std::mutex> g(this->sperre_);
  return t.has_value() && !this->einrichtung_noetig_ && ap::gleich(t.value(), this->admin_token_);
}

uint32_t AppServer::adresse_(AsyncWebServerRequest *r) const {
  // Adresse der Gegenstelle; X-Forwarded-For (erste Adresse) zaehlt NUR, wenn die Anfrage vom
  // eingetragenen Reverse Proxy kommt - sonst koennte jeder Client die Login-Sperre per Header umgehen
  struct sockaddr_storage sa {};
  socklen_t n = sizeof(sa);
  uint32_t peer = 0;
  char text[48] = {0};
  int fd = httpd_req_to_sockfd(*r);
  if (fd >= 0 && getpeername(fd, (struct sockaddr *) &sa, &n) == 0) {
    if (sa.ss_family == AF_INET) {
      auto *a4 = (struct sockaddr_in *) &sa;
      peer = a4->sin_addr.s_addr;
      inet_ntop(AF_INET, &a4->sin_addr, text, sizeof(text));
    }
#if LWIP_IPV6
    else if (sa.ss_family == AF_INET6) {
      auto *a6 = (struct sockaddr_in6 *) &sa;
      for (int i = 0; i < 16; i++) peer = peer * 31 + a6->sin6_addr.s6_addr[i];
      inet_ntop(AF_INET6, &a6->sin6_addr, text, sizeof(text));
      // IPv4 als IPv6 (::ffff:a.b.c.d) wie IPv4 behandeln
      if (std::strncmp(text, "::ffff:", 7) == 0 && std::strchr(text + 7, '.')) std::memmove(text, text + 7, std::strlen(text + 7) + 1);
    }
#endif
  }
  if (!this->proxy_.empty() && this->proxy_ == text) {
    auto xf = r->get_header("X-Forwarded-For");
    if (xf.has_value() && !xf.value().empty()) {
      std::string a = xf.value().substr(0, xf.value().find(','));
      return (uint32_t) zs::fnv(bf::trimmen(a));
    }
  }
  return peer;
}

void AppServer::datei_(AsyncWebServerRequest *r, const ap::Datei *d) {
  auto inm = r->get_header("If-None-Match");
  std::string etag = std::string("\"") + ap::APP_VERSION + "\"";
  if (inm.has_value() && inm.value() == etag) {
    auto *res = r->beginResponse(304, d->typ);
    status_setzen(r, 304);
    res->addHeader("ETag", etag.c_str());
    r->send(res);
    return;
  }
  auto *res = r->beginResponse(200, d->typ, d->daten, d->laenge);
  res->addHeader("Content-Encoding", "gzip");
  res->addHeader("Cache-Control", "no-cache");
  res->addHeader("ETag", etag.c_str());
  r->send(res);
}

// gemeinsamer Teil von App- und Admin-Login: Sperre pruefen, Fehlversuch zaehlen
static const char *const ZU_VIELE = "{\"error\":\"Zu viele Fehlversuche - bitte 15 Minuten warten\"}";

void AppServer::login_(AsyncWebServerRequest *r) {
  uint32_t adr = this->adresse_(r), jetzt = millis();
  JsonDocument d;
  std::string user, pass;
  if (!deserializeJson(d, this->body_)) {
    if (d["user"].is<const char *>()) user = d["user"].as<std::string>();
    if (d["pass"].is<const char *>()) pass = d["pass"].as<std::string>();
  }
  bool ok, gesperrt;
  uint32_t heute;
  std::string token;
  {
    std::lock_guard<std::mutex> g(this->sperre_);
    if (this->anmeldung_.gesperrt(adr, jetzt)) { this->antworten_(r, 429, ZU_VIELE); return; }
    ok = !this->einrichtung_noetig_ && ap::gleich(user, this->zugang_.benutzer) && ap::gleich(pass, this->zugang_.app_pw);
    token = this->token_;
    gesperrt = !ok && this->anmeldung_.fehlversuch(adr, jetzt);
    heute = this->anmeldung_.heute;
  }
  if (ok) {
    ESP_LOGI(TAG, "Login erfolgreich");
    this->antworten_(r, 200, "{\"token\":\"" + token + "\"}");
    return;
  }
  ESP_LOGW(TAG, "Login fehlgeschlagen (%s, %u seit dem Start)", gesperrt ? "Adresse 15 min gesperrt" : "falsche Zugangsdaten",
           (unsigned) heute);
  this->antworten_(r, 401, "{\"error\":\"Falsche Zugangsdaten\"}");
}

void AppServer::admin_login_(AsyncWebServerRequest *r) {
  uint32_t adr = this->adresse_(r), jetzt = millis();
  JsonDocument d;
  std::string pass;
  if (!deserializeJson(d, this->body_) && d["pass"].is<const char *>()) pass = d["pass"].as<std::string>();
  bool ok, gesperrt;
  std::string token;
  {
    std::lock_guard<std::mutex> g(this->sperre_);
    if (this->anmeldung_.gesperrt(adr, jetzt)) { this->antworten_(r, 429, ZU_VIELE); return; }
    ok = !this->einrichtung_noetig_ && ap::gleich(pass, this->zugang_.admin_pw);
    token = this->admin_token_;
    gesperrt = !ok && this->anmeldung_.fehlversuch(adr, jetzt);
  }
  if (ok) {
    ESP_LOGI(TAG, "Einstellungen: Login erfolgreich");
    this->antworten_(r, 200, "{\"token\":\"" + token + "\"}");
    return;
  }
  ESP_LOGW(TAG, "Einstellungen: Login fehlgeschlagen%s", gesperrt ? " - Adresse 15 min gesperrt" : "");
  this->antworten_(r, 401, "{\"error\":\"Falsches Passwort\"}");
}

static std::string neues_token() {
  uint8_t z[32];
  char t[65];
  random_bytes(z, sizeof(z));
  for (int i = 0; i < 32; i++) snprintf(&t[i * 2], 3, "%02x", z[i]);
  return std::string(t, 64);
}

// Ersteinrichtung: nur solange keine Zugangsdaten gespeichert sind (wie bei einem neuen Router)
void AppServer::einrichtung_(AsyncWebServerRequest *r) {
  ap::Zugang z;
  std::string f, token = neues_token(), admin = neues_token();
  {
    std::lock_guard<std::mutex> g(this->sperre_);
    if (!this->einrichtung_noetig_) { this->antworten_(r, 409, "{\"error\":\"Zugang ist schon eingerichtet\"}"); return; }
    z = this->zugang_;
  }
  if (!ap::einrichtung_lesen(this->body_, z, f)) {
    JsonDocument e;
    e["error"] = f;
    std::string out;
    serializeJson(e, out);
    this->antworten_(r, 400, out);
    return;
  }
  {
    std::lock_guard<std::mutex> g(this->sperre_);
    this->zugang_ = z;
    this->token_ = token;
    this->admin_token_ = admin;
    this->einrichtung_noetig_ = false;
  }
  ESP_LOGI(TAG, "Ersteinrichtung: Zugangsdaten gesetzt");
  this->wc_->spaeter([this]() {
    this->zugang_speichern_();
    std::array<char, 65> t{};
    { std::lock_guard<std::mutex> g(this->sperre_); ap::kopieren(t.data(), t.size(), this->token_); }
    this->token_pref_.save(&t);
    { std::lock_guard<std::mutex> g(this->sperre_); ap::kopieren(t.data(), t.size(), this->admin_token_); }
    this->admin_pref_.save(&t);
    global_preferences->sync();
    this->web_passwort_setzen_();
  });
  this->antworten_(r, 200, "{\"token\":\"" + token + "\"}");
}

// eine Einstellung pruefen und (in der Hauptschleife) setzen; false + fehler, wenn ungueltig
bool AppServer::einstellung_setzen_(const std::string &id, JsonVariant wert, std::string &fehler) {
  if (id == "darstellung") {
    std::string t = wert.is<const char *>() ? wert.as<std::string>() : "";
    if (!ap::theme_ok(t)) { fehler = "auto, hell oder dunkel"; return false; }
    { std::lock_guard<std::mutex> g(this->sperre_); ap::kopieren(this->zugang_.theme, sizeof(this->zugang_.theme), t); }
    this->wc_->spaeter([this]() { this->zugang_speichern_(); });
    return true;
  }
  for (auto &e : this->einstellungen_) {
    if (e.id != id) continue;
#ifdef USE_NUMBER
    if (e.zahl) {
      if (!wert.is<float>()) { fehler = "Zahl erwartet"; return false; }
      float v = wert.as<float>();
      auto *n = static_cast<number::Number *>(e.obj);
      if (v < n->traits.get_min_value() || v > n->traits.get_max_value()) { fehler = "ausserhalb des erlaubten Bereichs"; return false; }
      ESP_LOGI(TAG, "Einstellung %s = %.1f", e.name.c_str(), v);
      this->wc_->spaeter([n, v]() { n->make_call().set_value(v).perform(); });
      return true;
    }
#endif
#ifdef USE_SWITCH
    if (!wert.is<bool>()) { fehler = "an/aus erwartet"; return false; }
    bool an = wert.as<bool>();
    auto *s = static_cast<switch_::Switch *>(e.obj);
    ESP_LOGI(TAG, "Einstellung %s = %s", e.name.c_str(), an ? "an" : "aus");
    this->wc_->spaeter([s, an]() { if (an) s->turn_on(); else s->turn_off(); });
    return true;
#endif
  }
  fehler = "unbekannte Einstellung";
  return false;
}

// eine Einstellung aendern: {"id": "<object_id>" | "darstellung", "wert": true/false/Zahl/"hell"}
void AppServer::einstellen_(AsyncWebServerRequest *r) {
  JsonDocument d;
  if (deserializeJson(d, this->body_) || !d["id"].is<const char *>()) {
    this->antworten_(r, 400, "{\"error\":\"ungueltige Anfrage\"}");
    return;
  }
  std::string f;
  if (this->einstellung_setzen_(d["id"].as<std::string>(), d["wert"], f)) { this->antworten_(r, 200, "{\"ok\":true}"); return; }
  JsonDocument e;
  e["error"] = f;
  std::string out;
  serializeJson(e, out);
  this->antworten_(r, f == "unbekannte Einstellung" ? 404 : 400, out);
}

// Sicherung einspielen: jede bekannte Einstellung wird geprueft und gesetzt, unbekannte uebersprungen
void AppServer::sicherung_einspielen_(AsyncWebServerRequest *r) {
  JsonDocument d;
  if (deserializeJson(d, this->body_) || d["art"] != "weishaupt-wem-can-einstellungen" || !d["einstellungen"].is<JsonObject>()) {
    this->antworten_(r, 400, "{\"error\":\"keine Sicherungsdatei dieses Boards\"}");
    return;
  }
  int ok = 0;
  JsonDocument a;
  JsonArray fehler = a["fehler"].to<JsonArray>();
  for (JsonPair p : d["einstellungen"].as<JsonObject>()) {
    std::string f, id = p.key().c_str();
    if (this->einstellung_setzen_(id, p.value(), f)) ok++;
    else fehler.add(id + ": " + f);
  }
  a["ok"] = true;
  a["gesetzt"] = ok;
  ESP_LOGI(TAG, "Sicherung eingespielt: %d Einstellungen, %u uebersprungen", ok, (unsigned) fehler.size());
  std::string out;
  serializeJson(a, out);
  this->antworten_(r, 200, out);
}

// Passwort aendern: {"art": "app" | "admin", "benutzer" (nur app, optional), "passwort"}.
// Danach sind alle Geraete dieser Art abgemeldet (neues Token); die Einstellungsseite bekommt ihr neues.
void AppServer::passwort_aendern_(AsyncWebServerRequest *r) {
  JsonDocument d;
  std::string f;
  if (deserializeJson(d, this->body_) || !d["art"].is<const char *>() || !d["passwort"].is<const char *>()) {
    this->antworten_(r, 400, "{\"error\":\"ungueltige Anfrage\"}");
    return;
  }
  std::string art = d["art"].as<std::string>(), pw = d["passwort"].as<std::string>();
  std::string benutzer = d["benutzer"].is<const char *>() ? d["benutzer"].as<std::string>() : "";
  bool app = art == "app";
  if ((!app && art != "admin") || !ap::passwort_ok(pw, f) || (app && !benutzer.empty() && !ap::benutzer_ok(benutzer, f))) {
    JsonDocument e;
    e["error"] = f.empty() ? "art: app oder admin" : f;
    std::string out;
    serializeJson(e, out);
    this->antworten_(r, 400, out);
    return;
  }
  std::string token = neues_token(), antwort = "{\"ok\":true}";
  {
    std::lock_guard<std::mutex> g(this->sperre_);
    if ((app && pw == this->zugang_.admin_pw) || (!app && pw == this->zugang_.app_pw)) {
      this->antworten_(r, 400, "{\"error\":\"App- und Admin-Passwort muessen verschieden sein\"}");
      return;
    }
    if (app) {
      ap::kopieren(this->zugang_.app_pw, sizeof(this->zugang_.app_pw), pw);
      if (!benutzer.empty()) ap::kopieren(this->zugang_.benutzer, sizeof(this->zugang_.benutzer), benutzer);
      this->token_ = token;
    } else {
      ap::kopieren(this->zugang_.admin_pw, sizeof(this->zugang_.admin_pw), pw);
      this->admin_token_ = token;
      antwort = "{\"ok\":true,\"token\":\"" + token + "\"}";
    }
  }
  ESP_LOGI(TAG, "%s-Passwort geaendert", app ? "App" : "Admin");
  this->wc_->spaeter([this, app]() {
    this->zugang_speichern_();
    std::array<char, 65> t{};
    { std::lock_guard<std::mutex> g(this->sperre_); ap::kopieren(t.data(), t.size(), app ? this->token_ : this->admin_token_); }
    (app ? this->token_pref_ : this->admin_pref_).save(&t);
    global_preferences->sync();
    if (!app) this->web_passwort_setzen_();
  });
  this->antworten_(r, 200, antwort);
}

void AppServer::schalten_(AsyncWebServerRequest *r, int ziel) {
  JsonDocument d;
  bool ok = !deserializeJson(d, this->body_);
  JsonDocument n;
  JsonObject o = n.to<JsonObject>();
  char id[24];
  snprintf(id, sizeof(id), "app-%x-%x", (unsigned) millis(), (unsigned) ++this->befehl_nr_);
  o["id"] = id;
  o["quelle"] = "App";
  JsonDocument a;
  JsonObject ao = a.to<JsonObject>();
  ao["ok"] = true;
  ao["queued"] = true;
  ao["id"] = id;
  if (ziel == bf::ZIEL_HK) {
    int code = ok && d["modeCode"].is<int>() ? d["modeCode"].as<int>() : 0;
    if (code < 1 || code > 8) { this->antworten_(r, 400, "{\"error\":\"Unbekannte Betriebsart\"}"); return; }
    o["wert"] = code;
    ao["modeCode"] = code;
    ao["modeLabel"] = bf::hk_text(code);
  } else {
    if (!ok || !d["an"].is<bool>()) { this->antworten_(r, 400, "{\"error\":\"an: true oder false\"}"); return; }
    bool an = d["an"].as<bool>();
    o["wert"] = an ? "Ein" : "Aus";
    ao["an"] = an;
  }
  std::string nutzlast, antwort;
  serializeJson(n, nutzlast);
  serializeJson(a, antwort);
  ESP_LOGI(TAG, "Schaltbefehl aus der App: %s", nutzlast.c_str());
  this->wc_->spaeter([this, ziel, nutzlast]() {
    if (this->befehl) this->befehl(ziel, nutzlast);
  });
  this->antworten_(r, 200, antwort);
}

void AppServer::handleRequest(AsyncWebServerRequest *request) {
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  StringRef u = request->url_to(buf);
  std::string url = u.str();
  bool post = request->method() == HTTP_POST;

  if (const ap::Datei *d = ap::datei(url.c_str(), url.size())) {
    this->datei_(request, d);
  } else if (url == "/api/einrichtung") {
    bool noetig;
    { std::lock_guard<std::mutex> g(this->sperre_); noetig = this->einrichtung_noetig_; }
    if (post) this->einrichtung_(request);
    else this->antworten_(request, 200, noetig ? "{\"noetig\":true}" : "{\"noetig\":false}");
  } else if (url == "/api/login" && post) {
    this->login_(request);
  } else if (url == "/api/admin/login" && post) {
    this->admin_login_(request);
  } else if (beginnt(url.c_str(), "/api/admin/")) {
    if (!this->admin_angemeldet_(request)) {
      if (post) ESP_LOGW(TAG, "POST %s ohne gueltiges Admin-Token", url.c_str());
      this->antworten_(request, 401, "{\"error\":\"Nicht autorisiert\"}");
    } else if (url == "/api/admin/einstellungen" && !post) {
      std::string s;
      { std::lock_guard<std::mutex> g(this->sperre_); s = this->einstellungen_stand_; }
      this->antworten_(request, s.empty() ? 503 : 200, s.empty() ? "{\"error\":\"Board startet noch\"}" : s);
    } else if (url == "/api/admin/einstellungen" && post) {
      this->einstellen_(request);
    } else if (url == "/api/admin/passwort" && post) {
      this->passwort_aendern_(request);
    } else if ((url == "/api/admin/info" || url == "/api/admin/sicherung") && !post) {
      std::string s;
      { std::lock_guard<std::mutex> g(this->sperre_); s = url == "/api/admin/info" ? this->info_ : this->sicherung_; }
      this->antworten_(request, s.empty() ? 503 : 200, s.empty() ? "{\"error\":\"Board startet noch\"}" : s);
    } else if (url == "/api/admin/sicherung" && post) {
      this->sicherung_einspielen_(request);
    } else if (url == "/api/admin/log" && !post) {
      JsonDocument d;
      JsonArray a = d.to<JsonArray>();
      { std::lock_guard<std::mutex> g(this->log_sperre_); for (auto &z : this->log_) a.add(z); }
      std::string out;
      serializeJson(d, out);
      this->antworten_(request, 200, out);
    } else if (url == "/api/admin/neustart" && post) {
      ESP_LOGW(TAG, "Neustart ueber die Einstellungsseite");
      this->wc_->spaeter([]() { App.safe_reboot(); });
      this->antworten_(request, 200, "{\"ok\":true}");
    } else if (url == "/api/admin/werksreset" && post) {
      JsonDocument d;
      if (deserializeJson(d, this->body_) || d["bestaetigung"] != "WERKSRESET" || !this->werksreset) {
        this->antworten_(request, 400, "{\"error\":\"zur Bestaetigung WERKSRESET eingeben\"}");
      } else {
        ESP_LOGW(TAG, "Werksreset ueber die Einstellungsseite");
        this->wc_->spaeter([this]() { this->werksreset(); });
        this->antworten_(request, 200, "{\"ok\":true}");
      }
    } else {
      this->antworten_(request, 404, "{\"error\":\"Unbekannt\"}");
    }
  } else if (!this->angemeldet_(request)) {
    if (post) ESP_LOGW(TAG, "POST %s ohne gueltiges Token", url.c_str());
    this->antworten_(request, 401, "{\"error\":\"Nicht autorisiert\"}");
  } else if (url == "/api/status" && !post) {
    std::string s;
    { std::lock_guard<std::mutex> g(this->sperre_); s = this->status_; }
    if (s.empty()) this->antworten_(request, 503, "{\"error\":\"Board startet noch\"}");
    else this->antworten_(request, 200, s);
  } else if (url == "/api/energie" && !post) {
    std::string s;
    { std::lock_guard<std::mutex> g(this->sperre_); s = this->energie_; }
    this->antworten_(request, 200, s.empty() ? "{}" : s);
  } else if (url == "/api/meta" && !post) {
    std::string t;
    { std::lock_guard<std::mutex> g(this->sperre_); t = ap::theme(this->zugang_); }
    this->antworten_(request, 200, ap::meta_antwort("/einstellungen", t));
  } else if (url == "/api/mode" && post) {
    this->schalten_(request, bf::ZIEL_HK);
  } else if (url == "/api/warmwasser" && post) {
    this->schalten_(request, bf::ZIEL_WW);
  } else if (url == "/api/lesen" && post) {
    this->wc_->spaeter([this]() {
      if (this->lesen) this->lesen();
    });
    this->antworten_(request, 200, "{\"ok\":true}");
  } else {
    this->antworten_(request, 404, "{\"error\":\"Unbekannt\"}");
  }
  this->body_.clear();
}

}  // namespace weishaupt_can
}  // namespace esphome
#endif
