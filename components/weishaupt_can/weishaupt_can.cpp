#include "weishaupt_can.h"
#include "esphome/core/log.h"

namespace esphome {
namespace weishaupt_can {

static const char *const TAG = "weishaupt_can";

void WeishauptCan::setup() {
  for (auto &s : this->speicher_) s->laden();
}

void WeishauptCan::loop() {
  // wie RestoringGlobalsComponent: hoechstens einmal pro Sekunde auf Aenderungen pruefen
  uint32_t jetzt = millis();
  if (jetzt - this->zuletzt_gesichert_ < 1000) return;
  this->zuletzt_gesichert_ = jetzt;
  for (auto &s : this->speicher_) s->sichern();
}

void WeishauptCan::on_shutdown() {
  for (auto &s : this->speicher_) s->sichern();
}

void WeishauptCan::dump_config() {
  ESP_LOGCONFIG(TAG, "Weishaupt WEM/WTC am CAN-Bus:\n"
                     "  Anlaufpause nach Busausfall: %u s\n"
                     "  Statusbits loesen Nachlesen aus: %s\n"
                     "  gespeicherte Werte: %u",
                (unsigned) (this->bus.anlaufpause_ms / 1000), YESNO(this->anlass.statusbits_anlass),
                (unsigned) this->speicher_.size());
}

std::string WeishauptCan::zeit(const char *format) {
#ifdef USE_TIME
  if (this->uhr_ != nullptr && this->uhr_->now().is_valid()) return this->uhr_->now().strftime(format);
#endif
  return "";
}

uint32_t WeishauptCan::unix_zeit() {
#ifdef USE_TIME
  if (this->uhr_ != nullptr && this->uhr_->now().is_valid()) return (uint32_t) this->uhr_->now().timestamp;
#endif
  return 0;
}

}  // namespace weishaupt_can
}  // namespace esphome
