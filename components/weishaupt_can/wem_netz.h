// WEM-Adresse per CAN lesen (ab v42): drei einzelne Leseanfragen an den WEM (Knoten 1, eigener SDO-Kanal
// 0x601/0x581 - den benutzt der WEM selbst nicht), im Abstand von 0,5 s:
//   0x2B08/0 aktuelle IP, 0x2B09/0 Netzmaske, 0x2B0A/0 Gateway (uint32, Bytes 4-7 = letztes..erstes Oktett)
// Gefunden beim Scan am 27.10.2026 (GATEWAY0 der WEM-Systemtabelle). Nie am Kessel, nie schreiben.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace wn {

struct WemNetz {
  static constexpr uint16_t OBJEKTE[3] = {0x2B08, 0x2B09, 0x2B0A};
  static constexpr uint32_t ABSTAND_MS = 500, WARTEN_MS = 2000;
  bool laeuft = false;
  int schritt = 0;              // 0..2: welches Objekt
  bool gesendet = false;
  uint32_t zeit = 0;            // millis() der letzten Anfrage bzw. Antwort
  std::string werte[3];         // ip, maske, gateway (leer = keine Antwort)
  std::string fehler;

  static std::string ip_text(const std::vector<uint8_t> &x) {
    char t[16];
    snprintf(t, sizeof(t), "%u.%u.%u.%u", x[7], x[6], x[5], x[4]);
    return t;
  }

  void starten(uint32_t jetzt) {
    laeuft = true; schritt = 0; gesendet = false; fehler.clear();
    for (auto &w : werte) w.clear();
    zeit = jetzt - ABSTAND_MS;
  }

  // in der Hauptschleife: true = diese Anfrage senden (an 0x601)
  bool weiter(uint32_t jetzt, std::vector<uint8_t> &daten) {
    if (!laeuft) return false;
    if (gesendet) {
      if (jetzt - zeit < WARTEN_MS) return false;
      if (schritt == 0) { laeuft = false; fehler = "keine Antwort vom WEM (Knoten 1)"; return false; }
      naechster(jetzt);   // Maske/Gateway fehlen: weiter, die IP zaehlt
      if (!laeuft) return false;
    }
    if (jetzt - zeit < ABSTAND_MS) return false;
    uint16_t o = OBJEKTE[schritt];
    daten = {0x40, (uint8_t) (o & 0xFF), (uint8_t) (o >> 8), 0, 0, 0, 0, 0};
    gesendet = true; zeit = jetzt;
    return true;
  }

  // jeder Frame; true = gehoerte zu uns
  bool antwort(uint32_t can_id, const std::vector<uint8_t> &x, uint32_t jetzt) {
    if (!laeuft || !gesendet || can_id != 0x581 || x.size() < 8) return false;
    uint16_t o = OBJEKTE[schritt];
    if (((x[2] << 8) | x[1]) != o || x[3] != 0) return false;
    if (x[0] == 0x43) werte[schritt] = ip_text(x);
    else if (schritt == 0) { laeuft = false; fehler = "WEM kennt das Objekt nicht (Abbruch)"; return true; }
    naechster(jetzt);
    return true;
  }

  void naechster(uint32_t jetzt) {
    gesendet = false; zeit = jetzt;
    if (++schritt >= 3) laeuft = false;
  }
};

}  // namespace wn
