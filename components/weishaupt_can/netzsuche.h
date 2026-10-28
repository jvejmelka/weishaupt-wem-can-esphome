// WEM im Netz suchen (ab v42), Rueckfall ohne CAN-Kabel: das eigene /24-Netz des Boards, je Adresse genau
// EINE Verbindung auf Port 80 und ein "GET /". Der WEM antwortet ohne Anmeldung mit 401 und
// 'WWW-Authenticate: Basic realm="WEM-SG HTTP-Server"' - daran wird er erkannt. Laeuft in einer eigenen
// Aufgabe (4 Verbindungen gleichzeitig, 0,3 s Wartezeit), dauert etwa 20-30 s. Kein Abfragen, nur Finden.
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace esphome {
namespace weishaupt_can {

class Netzsuche {
 public:
  // eigene Adresse und Maske (Netzreihenfolge wie lwIP: erstes Oktett im niedrigsten Byte)
  bool starten(uint32_t eigene_ip, uint32_t maske);
  bool laeuft() const { return this->laeuft_; }
  int fortschritt() const { return this->fertig_; }   // gepruefte Adressen (von 254)
  int uebersprungen() const { return this->fehlschlaege_; }
  std::vector<std::string> gefunden();

  // nur fuer die Aufgabe
  void lauf_();

 private:
  std::atomic<bool> laeuft_{false};
  std::atomic<int> fertig_{0}, fehlschlaege_{0};
  uint32_t netz_{0}, eigene_{0};
  std::mutex sperre_;
  std::vector<std::string> gefunden_;
};

// erkennt die Antwort des WEM-Webservers (auch fuer die Tests)
inline bool ist_wem(const std::string &antwort) { return antwort.find("WEM-SG") != std::string::npos; }

}  // namespace weishaupt_can
}  // namespace esphome
