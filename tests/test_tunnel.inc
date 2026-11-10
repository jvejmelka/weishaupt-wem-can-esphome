// ---------------------------------------------------------------- CAN-Tunnel (can_tunnel.h, Versuch)
// Wird in test_regeln.cpp eingebunden. Geprueft wird nur, was ohne Board laeuft: Paketformat, Adresspruefung
// und die Sperre "nur senden, wenn der Bus lebt". Alle Frames und Adressen sind AUSGEDACHT
// (Adressen aus 192.0.2.0/24, dem Bereich fuer Dokumentation).

// Dieselbe Pruefung macht das Paket can-tunnel beim Bauen der Firmware
static_assert(can_tunnel::adresse_ok("192.0.2.54"), "gueltige Adresse");
static_assert(!can_tunnel::adresse_ok(""), "leer ist keine Gegenstelle");
static_assert(can_tunnel::adresse_ok_oder_leer(""), "leer = kein Mitschnitt");

TEST(tunnel_adresse) {
  for (const char *gut : {"192.0.2.54", "10.0.0.1", "0.0.0.1", "255.255.255.254", "1.2.3.4"})
    PRUEFE(can_tunnel::adresse_ok(gut));
  for (const char *schlecht : {"", " ", "192.0.2", "192.0.2.54.1", "192.0.2.", ".192.0.2.54", "192..2.54", "192.0.2.256",
                               "192.0.2.054", "00.0.0.1", "192.0.2.54 ", " 192.0.2.54", "192.0.2.5a", "board-b.local",
                               "${tunnel_ziel}", "0.0.0.0", "255.255.255.255", "1234.0.0.1", "192,0,2,54"})
    PRUEFE(!can_tunnel::adresse_ok(schlecht));
  PRUEFE(!can_tunnel::adresse_ok(nullptr));
  PRUEFE(can_tunnel::adresse_ok_oder_leer("") && can_tunnel::adresse_ok_oder_leer("192.0.2.30"));
  PRUEFE(!can_tunnel::adresse_ok_oder_leer("rechner") && !can_tunnel::adresse_ok_oder_leer(nullptr));
}

TEST(tunnel_paket) {
  // hin und zurueck: jede Laenge 0-8, mit und ohne RTR, kleinste und groesste Kennung
  const uint8_t daten[8] = {0x23, 0xF0, 0x20, 0x01, 0x01, 0x00, 0x00, 0x04};
  for (uint32_t id : {0x000u, 0x184u, 0x604u, 0x7FFu})
    for (int rtr = 0; rtr < 2; rtr++)
      for (size_t n = 0; n <= 8; n++) {
        uint8_t p[14], d[8] = {0}, laenge = 99, folge = 0;
        uint32_t id2 = 0xFFFF; bool rtr2 = !rtr;
        size_t l = can_tunnel::packen(p, 200, id, rtr, daten, n);
        PRUEFE(l == 6 + n && p[0] == 'W' && p[1] == 'B');
        PRUEFE(can_tunnel::entpacken(p, l, folge, id2, rtr2, d, laenge));
        PRUEFE(folge == 200 && id2 == id && rtr2 == (bool) rtr && laenge == n && memcmp(d, daten, n) == 0);
      }
  // mehr als 8 Datenbytes gibt es nicht: packen kuerzt
  uint8_t p[32] = {0}, d[8], laenge, folge; uint32_t id; bool rtr;
  const uint8_t lang[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  PRUEFE(can_tunnel::packen(p, 0, 0x184, false, lang, 12) == 14);
  // Fremdes und Kaputtes wird verworfen: falsche Kennbytes, zu kurz, Laenge passt nicht zum Paket, Laenge > 8
  size_t l = can_tunnel::packen(p, 1, 0x184, false, lang, 4);
  PRUEFE(can_tunnel::entpacken(p, l, folge, id, rtr, d, laenge));
  PRUEFE(!can_tunnel::entpacken(p, l - 1, folge, id, rtr, d, laenge));
  PRUEFE(!can_tunnel::entpacken(p, l + 1, folge, id, rtr, d, laenge));
  PRUEFE(!can_tunnel::entpacken(p, 5, folge, id, rtr, d, laenge));
  uint8_t q[32]; memcpy(q, p, sizeof(q)); q[0] = 'X';
  PRUEFE(!can_tunnel::entpacken(q, l, folge, id, rtr, d, laenge));
  memcpy(q, p, sizeof(q)); q[5] = 9;
  PRUEFE(!can_tunnel::entpacken(q, 15, folge, id, rtr, d, laenge));
  // nur 11-Bit-Kennungen
  PRUEFE(can_tunnel::gueltig(0x7FF) && !can_tunnel::gueltig(0x800) && !can_tunnel::gueltig(0x1FFFFFFF));
}

TEST(tunnel_nur_bei_bus) {
  can_tunnel::Zaehler &z = can_tunnel::zaehler();
  z = can_tunnel::Zaehler{};
  PRUEFE(!can_tunnel::bus_aktiv(0) && !can_tunnel::bus_aktiv(100000));   // nie etwas empfangen: gesperrt
  z.hat_empfang = true; z.letzter_empfang = 100000;
  PRUEFE(can_tunnel::bus_aktiv(100000) && can_tunnel::bus_aktiv(100000 + can_tunnel::BUS_AKTIV_MS - 1));
  PRUEFE(!can_tunnel::bus_aktiv(100000 + can_tunnel::BUS_AKTIV_MS));     // 3 s Stille: gesperrt
  z.letzter_empfang = 0xFFFFFF00u;                                       // Ueberlauf von millis()
  PRUEFE(can_tunnel::bus_aktiv(0x00000100u) && !can_tunnel::bus_aktiv(0x00001000u));
  z = can_tunnel::Zaehler{};
}
