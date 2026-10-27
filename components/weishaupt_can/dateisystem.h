// Dateisystem im freien Flash (ab v42): Das WeAct-Board hat 8 MB Flash, die Firmware ist fuer 4 MB gebaut.
// Die oberen 4 MB stehen in keiner Partitionstabelle - das Bauteil meldet sie beim Start selbst als
// Partition "dateien" an und bindet dort LittleFS unter /fs ein. So geht das ohne neue Partitionstabelle
// und ohne USB-Kabel. Auf Boards mit nur 4 MB bleibt das Dateisystem einfach aus.
#pragma once
#ifdef USE_WC_DATEIEN
#include <cstddef>
#include <string>

namespace esphome {
namespace weishaupt_can {

struct Dateisystem {
  bool bereit = false;
  std::string fehler;       // leer = ok
  size_t gesamt = 0, belegt = 0;
  void einbinden();
  void messen();
  // ganze Datei lesen (klein: Oberflaeche, JSON); false = nicht vorhanden
  static bool lesen(const std::string &pfad, std::string &inhalt, size_t max = 128 * 1024);
  static bool schreiben(const std::string &pfad, const std::string &inhalt);
};

}  // namespace weishaupt_can
}  // namespace esphome
#endif
