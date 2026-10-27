// Paket APP (app.yaml, ab v38): Webserver-Teil der Heizungs-App auf dem Board.
// Haengt sich an den Webserver von ESPHome (dieselbe Adresse, Port 80) und liefert
//   /app, /app.js, /style.css, /manifest.json, /icon.svg, /sw.js   (gzip, aus app_dateien.h)
//   /api/login, /api/status, /api/energie, /api/meta, /api/mode, /api/warmwasser, /api/lesen
// mit denselben Antworten wie die Docker-App (app/src/server.js). Die Logik steht in app_logik.h.
// Ab v39 zusaetzlich:
//   /einstellungen, /einstellungen.js   Einstellungsseite im Design der App (eigenes Admin-Passwort)
//   /api/einrichtung                    Ersteinrichtung der Zugangsdaten (nur solange keine gespeichert sind)
//   /api/admin/login, /api/admin/einstellungen, /api/admin/passwort
//
// Faeden: Der Webserver laeuft in einer eigenen Aufgabe. Er liest nur die fertige Antwort, die
// loop() jede Sekunde zusammenstellt (unter Sperre), und gibt Schaltbefehle per defer() an die
// Hauptschleife weiter - dort laufen die Skripte wie bei einem MQTT-Befehl.
#pragma once
#ifdef USE_WC_APP
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "app_logik.h"
#include "app_dateien.h"

namespace esphome {
namespace weishaupt_can {

class WeishauptCan;

class AppServer : public AsyncWebHandler {
 public:
  // benutzer/passwort/admin: Vorgaben aus der YAML (secrets.yaml) fuer den ersten Start; leer = Ersteinrichtung
  // proxy: IP des Reverse Proxys - nur dessen X-Forwarded-For zaehlt fuer die Login-Sperre (leer = nie)
  AppServer(WeishauptCan *wc, std::string benutzer, std::string passwort, std::string admin, std::string firmware,
            std::string proxy)
      : wc_(wc), benutzer_(std::move(benutzer)), passwort_(std::move(passwort)), admin_(std::move(admin)),
        firmware_(std::move(firmware)), proxy_(std::move(proxy)) {}

  void setup();
  void loop();

  // aus der YAML (Paket app): Schaltbefehl (1 Heizkreis, 2 Warmwasser; Nutzlast wie cmd/heizkreis)
  // und "Betriebsarten einmal lesen" (wie cmd/status)
  std::function<void(int, const std::string &)> befehl;
  std::function<void()> lesen;
  std::function<void()> werksreset;   // ab v41: Knopf factory_reset der YAML

  // Log-Zeile vom Logger (beliebiger Faden) - die letzten Zeilen fuer die Einstellungsseite
  void log_zeile(uint8_t stufe, const char *text, size_t laenge);

  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
  void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) override;
  bool isRequestHandlerTrivial() const override { return false; }

 protected:
  struct Quelle {
    std::string name;     // object_id
    bool text;
    ap::Messwert wert;
    std::string t;
    uint32_t zeit = 0;    // millis() der letzten Aktualisierung
  };
  // eine Einstellung der Einstellungsseite (Schalter oder Zahl des Boards, per Name gefunden)
  struct Einstellung {
    std::string id, name, gruppe;
    void *obj;          // switch_::Switch* oder number::Number*
    bool zahl;
  };
  struct Abo {
    std::string topic;
    std::string nutzlast;
    bool da = false;
    uint32_t zeit = 0;
  };

  void zusammenstellen_();
  void zusatz_abonnieren_();
  void antworten_(AsyncWebServerRequest *r, int code, const std::string &json);
  bool angemeldet_(AsyncWebServerRequest *r) const;
  uint32_t adresse_(AsyncWebServerRequest *r) const;
  void datei_(AsyncWebServerRequest *r, const ap::Datei *d);
  void login_(AsyncWebServerRequest *r);
  void schalten_(AsyncWebServerRequest *r, int ziel);
  bool admin_angemeldet_(AsyncWebServerRequest *r) const;
  void einrichtung_(AsyncWebServerRequest *r);
  void admin_login_(AsyncWebServerRequest *r);
  void einstellen_(AsyncWebServerRequest *r);
  void passwort_aendern_(AsyncWebServerRequest *r);
  void einstellungen_sammeln_();
  std::string einstellungen_json_();
  void zugang_speichern_();          // nur in der Hauptschleife
  void web_passwort_setzen_();       // Weboberflaeche des Boards: Admin-Passwort (bzw. gesperrt)
  std::string token_laden_(ESPPreferenceObject &pref, uint32_t schluessel, bool neu);
  std::string info_json_();          // Hauptschleife
  std::string sicherung_json_();     // Hauptschleife
  void sicherung_einspielen_(AsyncWebServerRequest *r);
  bool einstellung_setzen_(const std::string &id, JsonVariant wert, std::string &fehler);

  WeishauptCan *wc_;
  std::string benutzer_, passwort_, admin_, firmware_, proxy_;
  std::string token_, admin_token_;
  ESPPreferenceObject token_pref_, admin_pref_, zugang_pref_;
  std::vector<Einstellung> einstellungen_;
  char web_pw_[65] = {0};          // feste Adresse: der Webserver haelt nur den Zeiger
  std::vector<Quelle> quellen_;
  std::vector<Abo> abos_;
  std::string abo_stand_;          // Liste der Topics beim letzten Abonnieren
  uint32_t zuletzt_{0};
  uint32_t befehl_nr_{0};

  // nur unter sperre_: fertige Antworten fuer den Webserver-Faden
  mutable std::mutex sperre_;
  std::string status_, energie_, einstellungen_stand_, info_, sicherung_;
  ap::Anmeldung anmeldung_;
  ap::Zugang zugang_;
  bool einrichtung_noetig_{false};

  std::mutex log_sperre_;
  std::deque<std::string> log_;

  std::string body_;               // POST-Inhalt der laufenden Anfrage (nur Webserver-Faden)
};

}  // namespace weishaupt_can
}  // namespace esphome
#endif
