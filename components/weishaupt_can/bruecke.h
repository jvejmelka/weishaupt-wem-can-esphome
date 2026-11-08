#pragma once
// CAN-Bruecke ueber WLAN (UDP) zwischen zwei Boards: echtes Raumgeraet am Tisch, WEM an der Therme.
// Beide Richtungen leiten alles weiter (Entscheidung 05.11.2026). Sicherung ist die Herkunft: nur Frames, die das Board
// auf seinem eigenen Bus empfangen hat, nur von der eingetragenen Gegenstelle. Dazu Zaehler und Mitschnitt.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#ifndef BRUECKE_NUR_FILTER
#include <driver/twai.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include "esphome/core/helpers.h"
#endif

namespace bruecke {

constexpr uint16_t PORT = 47011;      // Frames zwischen den Boards
constexpr uint16_t PORT_LOG = 47012;  // Mitschnitt an einen Rechner im Heimnetz

inline bool gueltig(uint32_t id) { return id <= 0x7FF; }

// Paket: 'W' 'B' folge id_lo id_hi(+0x80 = RTR) len d0..d7
inline size_t packen(uint8_t *p, uint8_t folge, uint32_t id, bool rtr, const uint8_t *d, size_t n) {
  if (n > 8) n = 8;
  p[0] = 'W'; p[1] = 'B'; p[2] = folge; p[3] = id & 0xFF; p[4] = ((id >> 8) & 0x07) | (rtr ? 0x80 : 0); p[5] = (uint8_t) n;
  memcpy(p + 6, d, n);
  return 6 + n;
}
inline bool entpacken(const uint8_t *p, size_t l, uint8_t &folge, uint32_t &id, bool &rtr, uint8_t *d, uint8_t &n) {
  if (l < 6 || p[0] != 'W' || p[1] != 'B' || p[5] > 8 || l != (size_t) (6 + p[5])) return false;
  folge = p[2]; id = p[3] | ((uint32_t) (p[4] & 0x07) << 8); rtr = p[4] & 0x80; n = p[5];
  memcpy(d, p + 6, n);
  return true;
}

struct Zaehler {
  uint32_t vom_bus{0};       // auf dem eigenen Bus empfangen und an die Gegenstelle geschickt
  uint32_t zum_bus{0};       // von der Gegenstelle erhalten und auf den eigenen Bus gesendet
  uint32_t udp_verloren{0};  // Luecken in der Folgenummer
  uint32_t udp_fehler{0};    // sendto fehlgeschlagen
  uint32_t can_fehler{0};    // CAN-Senden abgelehnt
  uint32_t gesperrt{0};      // nicht gesendet, weil der Bus still war
  uint32_t bus_off{0};       // Bus-off erkannt und Wiederanlauf versucht
  uint32_t letzter_empfang{0};
  bool hat_empfang{false};
};
inline Zaehler &zaehler() { static Zaehler z; return z; }

constexpr uint32_t BUS_AKTIV_MS = 3000;
inline bool bus_aktiv(uint32_t jetzt) {
  Zaehler &z = zaehler();
  return z.hat_empfang && (uint32_t) (jetzt - z.letzter_empfang) < BUS_AKTIV_MS;
}

#ifndef BRUECKE_NUR_FILTER
inline esphome::HighFrequencyLoopRequester &schnell() { static esphome::HighFrequencyLoopRequester h; return h; }

class Kanal {
 public:
  bool oeffnen(const char *gegenstelle) {
    if (fd_ >= 0) return true;
    int f = ::lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (f < 0) return false;
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(PORT); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::lwip_bind(f, (sockaddr *) &a, sizeof(a)) < 0) { ::lwip_close(f); return false; }
    int fl = ::lwip_fcntl(f, F_GETFL, 0); ::lwip_fcntl(f, F_SETFL, fl | O_NONBLOCK);
    memset(&ziel_, 0, sizeof(ziel_)); ziel_.sin_family = AF_INET; ziel_.sin_port = htons(PORT);
    ziel_.sin_addr.s_addr = ::inet_addr(gegenstelle);
    fd_ = f;
    return true;
  }
  void senden(uint32_t id, bool rtr, const std::vector<uint8_t> &x) {
    if (fd_ < 0) return;
    uint8_t p[14]; size_t l = packen(p, folge_tx_++, id, rtr, x.data(), x.size());
    if (::lwip_sendto(fd_, p, l, 0, (sockaddr *) &ziel_, sizeof(ziel_)) < 0) zaehler().udp_fehler++;
  }
  // nur Pakete der eingetragenen Gegenstelle
  bool holen(uint32_t &id, bool &rtr, std::vector<uint8_t> &x) {
    if (fd_ < 0) return false;
    uint8_t p[32], d[8], n, folge; sockaddr_in q{}; socklen_t ql = sizeof(q);
    for (int i = 0; i < 16; i++) {
      int l = ::lwip_recvfrom(fd_, p, sizeof(p), 0, (sockaddr *) &q, &ql);
      if (l <= 0) return false;
      if (q.sin_addr.s_addr != ziel_.sin_addr.s_addr) continue;
      if (!entpacken(p, l, folge, id, rtr, d, n)) continue;
      if (hat_rx_) zaehler().udp_verloren += (uint8_t) (folge - folge_rx_ - 1);
      hat_rx_ = true; folge_rx_ = folge;
      x.assign(d, d + n);
      return true;
    }
    return false;
  }
  void neu() { hat_rx_ = false; }
 private:
  int fd_{-1};
  sockaddr_in ziel_{};
  uint8_t folge_tx_{0}, folge_rx_{0};
  bool hat_rx_{false};
};
inline Kanal *kanal(const char *gegenstelle) {
  static Kanal k;
  k.oeffnen(gegenstelle);
  return &k;
}

// Mitschnitt als Textzeilen per UDP, gebuendelt: "<name> <ms> <R|T|X> <ID>:<DATEN>"
// R = auf dem eigenen Bus empfangen, T = auf den eigenen Bus gesendet, X = Senden abgelehnt
class Mitschnitt {
 public:
  void oeffnen(const char *ziel, const char *name) {
    if (fd_ >= 0 || !ziel[0]) return;
    int f = ::lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (f < 0) return;
    int fl = ::lwip_fcntl(f, F_GETFL, 0); ::lwip_fcntl(f, F_SETFL, fl | O_NONBLOCK);
    memset(&ziel_, 0, sizeof(ziel_)); ziel_.sin_family = AF_INET; ziel_.sin_port = htons(PORT_LOG);
    ziel_.sin_addr.s_addr = ::inet_addr(ziel);
    name_ = name; fd_ = f;
  }
  void frame(char richtung, uint32_t id, bool rtr, const std::vector<uint8_t> &x, uint32_t ms) {
    char b[64];
    int n = snprintf(b, sizeof(b), "%s %lu %c %03X%s:", name_, (unsigned long) ms, richtung, (unsigned) id, rtr ? "r" : "");
    for (size_t i = 0; i < x.size() && i < 8; i++) n += snprintf(b + n, sizeof(b) - n, "%02X", x[i]);
    text(b, ms);
  }
  void text(const char *t, uint32_t ms) {
    if (fd_ < 0) return;
    size_t n = strlen(t);
    if (len_ + n + 1 > sizeof(buf_)) leeren();
    if (n + 1 > sizeof(buf_)) return;
    memcpy(buf_ + len_, t, n); len_ += n; buf_[len_++] = '\n';
    if (!laeuft_) { laeuft_ = true; seit_ = ms; }
  }
  void zeile(const char *t, uint32_t ms) { char b[300]; snprintf(b, sizeof(b), "%s %s", name_, t); text(b, ms); }
  void takt(uint32_t ms) { if (laeuft_ && ms - seit_ >= 20) leeren(); }
  void leeren() {
    if (fd_ >= 0 && len_) ::lwip_sendto(fd_, buf_, len_, 0, (sockaddr *) &ziel_, sizeof(ziel_));
    len_ = 0; laeuft_ = false;
  }
 private:
  int fd_{-1};
  sockaddr_in ziel_{};
  const char *name_{""};
  char buf_[1300];
  size_t len_{0};
  uint32_t seit_{0};
  bool laeuft_{false};
};
inline Mitschnitt *mitschnitt(const char *ziel, const char *name) {
  static Mitschnitt m;
  m.oeffnen(ziel, name);
  return &m;
}

// Firmware-Update laeuft: CAN-Baustein ist absichtlich gestoppt (sonst blockiert canbus beim Flash-Schreiben
// die Hauptschleife > 5 s und das ESPHome-OTA bricht ab, 08.11.2026). Nicht wieder anwerfen.
// Versuch: die Warnung "canbus took a long time" blieb danach (3-4 s); Wirkung unbelegt, Upload im Browser (/update) klappt.
inline bool &ota_pause() { static bool p = false; return p; }
inline void ota_beginn() { ota_pause() = true; twai_stop(); }
inline void ota_fehler() { ota_pause() = false; twai_start(); }

// Bus-off: der CAN-Baustein hat sich abgeschaltet und ist taub. Wiederanlauf statt auf den Neustart zu warten.
inline void bus_off_beheben(uint32_t ms) {
  if (ota_pause()) return;
  twai_status_info_t st{};
  if (twai_get_status_info(&st) != ESP_OK) return;
  if (st.state == TWAI_STATE_BUS_OFF) { zaehler().bus_off++; twai_initiate_recovery(); }
  else if (st.state == TWAI_STATE_STOPPED) { twai_start(); }
}

inline void statistik(Mitschnitt *m, uint32_t ms) {
  twai_status_info_t st{};
  bool ok = twai_get_status_info(&st) == ESP_OK;
  Zaehler &z = zaehler();
  char t[260];
  snprintf(t, sizeof(t), "STAT %lu vom_bus=%lu zum_bus=%lu udp_verloren=%lu udp_fehler=%lu can_fehler=%lu rx_verpasst=%ld rx_ueberlauf=%ld tx_fehler=%ld busfehler=%ld gesperrt=%lu bus_off=%lu",
           (unsigned long) ms, (unsigned long) z.vom_bus, (unsigned long) z.zum_bus, (unsigned long) z.udp_verloren,
           (unsigned long) z.udp_fehler, (unsigned long) z.can_fehler, ok ? (long) st.rx_missed_count : -1L,
           ok ? (long) st.rx_overrun_count : -1L, ok ? (long) st.tx_failed_count : -1L, ok ? (long) st.bus_error_count : -1L, (unsigned long) z.gesperrt, (unsigned long) z.bus_off);
  m->zeile(t, ms); m->leeren();
}
#endif
}  // namespace bruecke
