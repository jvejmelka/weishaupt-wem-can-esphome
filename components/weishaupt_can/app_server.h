// Paket APP (app.yaml, ab v38): Webserver-Teil der Heizungs-App auf dem Board.
// Haengt sich an den Webserver von ESPHome (dieselbe Adresse, Port 80) und liefert
//   /app, /app.js, /style.css, /manifest.json, /icon.svg, /sw.js   (gzip, aus app_dateien.h)
//   /api/login, /api/status, /api/energie, /api/meta, /api/mode, /api/warmwasser, /api/lesen
// mit denselben Antworten wie die Docker-App (app/src/server.js). Die Logik steht in app_logik.h.
//
// Faeden: Der Webserver laeuft in einer eigenen Aufgabe. Er liest nur die fertige Antwort, die
// loop() jede Sekunde zusammenstellt (unter Sperre), und gibt Schaltbefehle per defer() an die
// Hauptschleife weiter - dort laufen die Skripte wie bei einem MQTT-Befehl.
#pragma once
#ifdef USE_WC_APP
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
  AppServer(WeishauptCan *wc, std::string benutzer, std::string passwort, std::string firmware)
      : wc_(wc), benutzer_(std::move(benutzer)), passwort_(std::move(passwort)), firmware_(std::move(firmware)) {}

  void setup();
  void loop();

  // aus der YAML (Paket app): Schaltbefehl (1 Heizkreis, 2 Warmwasser; Nutzlast wie cmd/heizkreis)
  // und "Betriebsarten einmal lesen" (wie cmd/status)
  std::function<void(int, const std::string &)> befehl;
  std::function<void()> lesen;

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

  WeishauptCan *wc_;
  std::string benutzer_, passwort_, firmware_;
  std::string token_;
  ESPPreferenceObject token_pref_;
  std::vector<Quelle> quellen_;
  std::vector<Abo> abos_;
  std::string abo_stand_;          // Liste der Topics beim letzten Abonnieren
  uint32_t zuletzt_{0};
  uint32_t befehl_nr_{0};

  // nur unter sperre_: fertige Antworten fuer den Webserver-Faden
  std::mutex sperre_;
  std::string status_, energie_;
  ap::Anmeldung anmeldung_;

  std::string body_;               // POST-Inhalt der laufenden Anfrage (nur Webserver-Faden)
};

}  // namespace weishaupt_can
}  // namespace esphome
#endif
