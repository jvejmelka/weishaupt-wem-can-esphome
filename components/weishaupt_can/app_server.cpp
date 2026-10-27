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

// Schluessel des gespeicherten Sitzungs-Tokens (fest, wie die Globals der anderen Pakete)
static const uint32_t TOKEN_SCHLUESSEL = 0x41505054;   // "APPT"

void AppServer::setup() {
  // Sitzungs-Token: einmal erzeugt, im Flash gespeichert - ein Update meldet niemanden ab
  std::array<char, 65> t{};
  this->token_pref_ = global_preferences->make_preference<std::array<char, 65>>(TOKEN_SCHLUESSEL);
  if (!this->token_pref_.load(&t) || std::strlen(t.data()) != 64) {
    uint8_t z[32];
    random_bytes(z, sizeof(z));
    for (int i = 0; i < 32; i++) snprintf(&t[i * 2], 3, "%02x", z[i]);
    this->token_pref_.save(&t);
    ESP_LOGI(TAG, "neues Sitzungs-Token erzeugt");
  }
  this->token_ = t.data();

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
  std::string s = ap::status_antwort(l), e = ap::energie_antwort(l);
  std::lock_guard<std::mutex> g(this->sperre_);
  this->status_.swap(s);
  this->energie_.swap(e);
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
  if (this->body_.size() + len <= 1024) this->body_.append(reinterpret_cast<const char *>(data), len);
}

// Der IDF-Webserver von ESPHome kennt nur einige Statuscodes (sonst 500) - die uebrigen hier setzen.
// Die Antwort gehoert der Anfrage (wird mit ihr geloescht).
static void status_setzen(AsyncWebServerRequest *r, int code) {
  if (code == 304) httpd_resp_set_status(*r, "304 Not Modified");
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
  return t.has_value() && ap::gleich(t.value(), this->token_);
}

uint32_t AppServer::adresse_(AsyncWebServerRequest *r) const {
  // hinter dem Reverse Proxy zaehlt die erste Adresse aus X-Forwarded-For
  auto xf = r->get_header("X-Forwarded-For");
  if (xf.has_value() && !xf.value().empty()) {
    std::string a = xf.value().substr(0, xf.value().find(','));
    return (uint32_t) zs::fnv(bf::trimmen(a));
  }
  struct sockaddr_storage sa {};
  socklen_t n = sizeof(sa);
  int fd = httpd_req_to_sockfd(*r);
  if (fd < 0 || getpeername(fd, (struct sockaddr *) &sa, &n) != 0) return 0;
  if (sa.ss_family == AF_INET) return ((struct sockaddr_in *) &sa)->sin_addr.s_addr;
#if LWIP_IPV6
  if (sa.ss_family == AF_INET6) {
    auto *a6 = (struct sockaddr_in6 *) &sa;
    uint32_t h = 0;
    for (int i = 0; i < 16; i++) h = h * 31 + a6->sin6_addr.s6_addr[i];
    return h;
  }
#endif
  return 0;
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

void AppServer::login_(AsyncWebServerRequest *r) {
  uint32_t adr = this->adresse_(r), jetzt = millis();
  {
    std::lock_guard<std::mutex> g(this->sperre_);
    if (this->anmeldung_.gesperrt(adr, jetzt)) {
      this->antworten_(r, 429, "{\"error\":\"Zu viele Fehlversuche - bitte 15 Minuten warten\"}");
      return;
    }
  }
  JsonDocument d;
  std::string user, pass;
  if (!deserializeJson(d, this->body_)) {
    if (d["user"].is<const char *>()) user = d["user"].as<std::string>();
    if (d["pass"].is<const char *>()) pass = d["pass"].as<std::string>();
  }
  if (!this->passwort_.empty() && ap::gleich(user, this->benutzer_) && ap::gleich(pass, this->passwort_)) {
    ESP_LOGI(TAG, "Login erfolgreich");
    this->antworten_(r, 200, "{\"token\":\"" + this->token_ + "\"}");
    return;
  }
  bool gesperrt;
  uint32_t heute;
  {
    std::lock_guard<std::mutex> g(this->sperre_);
    gesperrt = this->anmeldung_.fehlversuch(adr, jetzt);
    heute = this->anmeldung_.heute;
  }
  ESP_LOGW(TAG, "Login fehlgeschlagen (%s, %u seit dem Start)", gesperrt ? "Adresse 15 min gesperrt" : "falsche Zugangsdaten",
           (unsigned) heute);
  this->antworten_(r, 401, "{\"error\":\"Falsche Zugangsdaten\"}");
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
  } else if (url == "/api/login" && post) {
    this->login_(request);
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
    this->antworten_(request, 200, ap::meta_antwort("/"));
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
