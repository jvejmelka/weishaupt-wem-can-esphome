// ESPHome-Bauteil weishaupt_can: haelt den Zustand des Boards (Klassen aus kern.h) und
// speichert, was den Neustart ueberleben muss. Die YAML-Pakete greifen mit id(<id>)-> darauf zu.
//
//   weishaupt_can:
//     id: wcan
//     canbus_id: my_can_bus
//     time_id: uhr
//
// Gespeichert wird unter DENSELBEN Schluesseln wie bis v28 als ESPHome-Globals
// (1944399030 ^ md5(<alter id>)[:8], gleicher Datentyp) - Einstellungen und Zaehler bleiben
// beim Update erhalten. Die Schluessel berechnet __init__.py aus den alten ids.
#pragma once
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/preferences.h"
#include "esphome/components/canbus/canbus.h"
#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif
#include "kern.h"

namespace esphome {
namespace weishaupt_can {

// ein gespeicherter Wert: verhaelt sich wie ein ESPHome-Global mit restore_value
// (laden beim Start, sichern bei Aenderung - geprueft jede Sekunde - und beim Herunterfahren)
class SpeicherBasis {
 public:
  virtual ~SpeicherBasis() = default;
  virtual void laden() = 0;
  virtual void sichern() = 0;
};

template<typename T> class Speicher : public SpeicherBasis {
 public:
  Speicher(T *wert, uint32_t name_hash) : wert_(wert), name_hash_(name_hash) {}
  void laden() override {
    this->rtc_ = global_preferences->make_preference<T>(1944399030U ^ this->name_hash_);
    this->rtc_.load(this->wert_);
    memcpy(&this->zuletzt_, this->wert_, sizeof(T));
  }
  void sichern() override {
    if (memcmp(this->wert_, &this->zuletzt_, sizeof(T)) != 0) {
      this->rtc_.save(this->wert_);
      memcpy(&this->zuletzt_, this->wert_, sizeof(T));
    }
  }

 protected:
  T *wert_;
  T zuletzt_{};
  uint32_t name_hash_;
  ESPPreferenceObject rtc_;
};

class WeishauptCan : public Component {
 public:
  // Zustand (kern.h) - oeffentlich, die YAML-Lambdas lesen und schreiben ihn direkt
  wc::Bus bus;
  wc::Anlass anlass;
  wc::Lesebefehl lesen;
  wc::Scan scan;
  wc::Brenner brenner;
  wc::Schalten schalten;
  wc::Verdacht vd;
  wc::Zusatz zs;

  void setup() override;
  void loop() override;
  void on_shutdown() override;
  void dump_config() override;
  // wie die frueheren Globals: gespeicherte Werte vor allen anderen Bauteilen laden
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  // Konfiguration (aus __init__.py)
  void set_canbus(canbus::Canbus *c) { this->canbus_ = c; }
#ifdef USE_TIME
  void set_uhr(time::RealTimeClock *u) { this->uhr_ = u; }
#endif
  void set_anlaufpause(uint32_t ms) { this->bus.anlaufpause_ms = ms; }
  void set_statusbits_anlass(bool b) { this->anlass.statusbits_anlass = b; }
  void speicher_brenner(uint32_t h) { this->speicher_.emplace_back(new Speicher<int>(&this->brenner.starts, h)); }
  void speicher_verdacht(uint32_t h_speicher, uint32_t h_hash, uint32_t h_version, uint32_t h_stby, uint32_t h_rest) {
    this->speicher_.emplace_back(new Speicher<std::array<char, 2400>>(&this->vd.speicher, h_speicher));
    this->speicher_.emplace_back(new Speicher<uint32_t>(&this->vd.datei_hash, h_hash));
    this->speicher_.emplace_back(new Speicher<int>(&this->vd.datei_version, h_version));
    this->speicher_.emplace_back(new Speicher<int>(&this->vd.stby_vorher, h_stby));
    this->speicher_.emplace_back(new Speicher<int>(&this->vd.rest_vorher, h_rest));
  }
  void speicher_zusatz(uint32_t h_speicher, uint32_t h_hash, uint32_t h_version) {
    this->speicher_.emplace_back(new Speicher<std::array<char, 1600>>(&this->zs.speicher, h_speicher));
    this->speicher_.emplace_back(new Speicher<uint32_t>(&this->zs.datei_hash, h_hash));
    this->speicher_.emplace_back(new Speicher<int>(&this->zs.version, h_version));
  }

  // --- Helfer fuer die Lambdas
  // jeder Frame: Lebenszeichen des Busses (neu da -> Betriebsarten nach der Pause neu lesen)
  void frame(uint32_t jetzt) {
    if (this->bus.frame(jetzt)) this->anlass.start_gelesen = false;
  }
  void senden(uint32_t can_id, const std::vector<uint8_t> &daten) {
    if (this->canbus_ != nullptr) this->canbus_->send_data(can_id, false, daten);
  }
  // Ortszeit als Text (leer, solange die Uhr nicht gestellt ist), Unix-Zeit (0 = unbekannt)
  std::string zeit(const char *format);
  uint32_t unix_zeit();

 protected:
  canbus::Canbus *canbus_{nullptr};
#ifdef USE_TIME
  time::RealTimeClock *uhr_{nullptr};
#endif
  std::vector<std::unique_ptr<SpeicherBasis>> speicher_;
  uint32_t zuletzt_gesichert_{0};
};

}  // namespace weishaupt_can
}  // namespace esphome
