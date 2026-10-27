// microSD-Karte im Steckplatz des WeAct CAN485 DevBoard (ab v42): einbinden (FAT, nur lesen und
// schreiben - NIE formatieren) und Groesse/frei melden. Pins laut WeAct: CS 13, SCK 14, MOSI 15, MISO 2.
// Hinweis WeAct: beim Flashen per USB die Karte herausnehmen (GPIO2 ist ein Strapping-Pin); OTA ist egal.
#pragma once
#ifdef USE_WC_SD
#include <cstdint>
#include <string>

namespace esphome {
namespace weishaupt_can {

struct SdKarte {
  bool eingebunden = false;
  std::string fehler;         // leer = ok
  std::string name;           // Kartenname aus dem CID
  uint64_t groesse = 0;       // Byte
  uint64_t frei = 0;          // Byte
  void einbinden();           // einmal beim Start
  void frei_messen();
};

}  // namespace weishaupt_can
}  // namespace esphome
#endif
