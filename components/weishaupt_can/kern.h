// Bauteil weishaupt_can: Zustand und Logik des Boards in Klassen - reines C++ ohne ESPHome.
// Die YAML-Pakete verdrahten nur noch Entitaeten, MQTT und Bus mit diesen Klassen; das
// ESPHome-Bauteil (weishaupt_can.h) haelt je eine Instanz und speichert, was den Neustart
// ueberleben muss. Getestet auf dem PC in tests/ (ohne Board, ohne Bus).
//
//   Bus         Lebenszeichen des Busses, Anlaufpause, Sendesperre
//   Anlass      wann die Betriebsarten (Heizkreis/Warmwasser) nachgelesen werden
//   Lesebefehl  Lesebefehl von Hand ("KN IDX SUB") und die Zuordnung der Antwort
//   Scan        Objektscan (cmd/scan)
//   Brenner     Flankenerkennung, Brennerstarts
//   Protokoll   Schalt- und Regelprotokoll (10 Eintraege)
//   Schalten    Sperrminute, laufender WEM-Aufruf, Schreibbefehl
//   Verdacht    Laufzeitzustand der Regeln R1-R5 (Datenregeln, regelwerk.h) und der eigenen Regeln
//   Zusatz      Zusatzanzeigen (Speicherform, Datei-Pruefsumme)
//
// Was hier steht, sendet nichts: die Klassen liefern nur, OB und WAS gesendet wird.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include "register_gen.h"
#include "regellogik.h"
#include "befehle.h"
#include "verdacht.h"
#include "regelwerk.h"

namespace wc {

static const uint32_t BUS_STILL_MS = 30000;        // so lange ohne Frame = Bus schweigt
static const uint32_t PAUSE_START_MS = 60000;      // Anlaufpause nach Board-Start
static const uint32_t HK_MINDESTABSTAND_MS = 120000;
static const uint32_t STATUS_MANUELL_MS = 10000;
static const uint32_t SPERRMINUTE_MS = 60000;
static const uint32_t REGEL_ANTWORT_FRIST_MS = 180000;

// ---------------------------------------------------------------- Texte

inline const char *hk_name(int b, const char *sonst) {
  static const char *n[] = {"?", "Standby", "Zeitprogramm 1", "Zeitprogramm 2", "Zeitprogramm 3",
                            "Sommer", "Komfort", "Normal", "Absenk"};
  return b >= 0 && b <= 8 ? n[b] : sonst;
}
inline const char *system_text(int sb) {
  static const char *n[] = {"Aus", "Standby", "Sommer", "Automatik"};
  return sb >= 0 && sb <= 3 ? n[sb] : "unbekannt";
}
// Betriebsphase Brenner (Weishaupt-Registertabelle): 0 aus, 1 Vorbelueftung,
// 2 Steuerbetrieb, 3 Regelbetrieb, 4 Nachbelueftung
inline const char *brenner_text(int ph) {
  static const char *n[] = {"Aus", "Vorlueften", "Ein", "Ein", "Nachlueften"};
  return ph >= 0 && ph <= 4 ? n[ph] : "unbekannt";
}
inline const char *ww_name(int b, const char *sonst) { return b == 1 ? "Ein" : (b == 2 ? "Aus" : sonst); }
// Statusbits 0x274D: 0x1000 Heizkreis Standby, 0x0010 WW-Ladung, Rest als Hex
inline std::string hk_status_text(uint16_t m) {
  std::string s = (m & 0x1000) ? "Standby" : "Zeitprogramm";
  if (m & 0x0010) s += " + WW-Ladung";
  uint16_t rest = m & ~0x1010;
  if (rest) { char b[12]; snprintf(b, sizeof(b), " (0x%04X)", rest); s += b; }
  return s;
}
// PDO 0x181: Stunde, Minute, Jahr-2000, Monat, Tag, Wochentag -> "TT.MM.JJJJ hh:mm"; leer = zu kurz
inline std::string uhr_text(const std::vector<uint8_t> &x) {
  if (x.size() < 6) return "";
  char b[24];
  snprintf(b, sizeof(b), "%02u.%02u.%04u %02u:%02u", x[4], x[3], 2000 + x[2], x[0], x[1]);
  return b;
}

// ---------------------------------------------------------------- Bus

struct Bus {
  uint32_t letzter_frame = 0;         // millis() des letzten Frames
  uint32_t seit = 0;                  // seit wann der Bus (wieder) lebt; 0 = noch nie
  uint32_t pause_ms = PAUSE_START_MS; // aktuelle Anlaufpause
  uint32_t anlaufpause_ms = 600000;   // nach einem Busausfall (Heizung war stromlos)

  // Frame gesehen. true = der Bus ist neu da (Board-Start oder nach > 30 s Stille)
  bool frame(uint32_t j) {
    bool neu = false;
    if (seit == 0) { seit = j | 1; pause_ms = PAUSE_START_MS; neu = true; }
    else if (j - letzter_frame > BUS_STILL_MS) { seit = j | 1; pause_ms = anlaufpause_ms; neu = true; }
    letzter_frame = j;
    return neu;
  }
  uint32_t still(uint32_t j) const { return j - letzter_frame; }
  bool tot(uint32_t j) const { return still(j) > BUS_STILL_MS; }
  bool lebt(uint32_t j) const { return seit != 0 && still(j) <= BUS_STILL_MS; }
  bool in_pause(uint32_t j) const { return j - seit <= pause_ms; }
  // eigene Anfragen im Takt / Anlass-Lesen: Bus lebt und Anlaufpause vorbei
  bool senden_erlaubt(uint32_t j) const { return still(j) < BUS_STILL_MS && seit != 0 && j - seit > pause_ms; }
  // dasselbe fuer die Regeln (Grenze 30 s einschliesslich, wie rl::bus_bereit)
  bool bereit(uint32_t j) const { return rl::bus_bereit(seit, letzter_frame, pause_ms, j); }
};

// Text "Eigene CAN-Anfragen" (an = Stand des Schalters)
inline std::string anfragen_text(bool an, const Bus &b, uint32_t j) {
  if (!an) return "aus - nur mithoeren (Betriebsarten bei Anlass)";
  if (b.seit == 0 || j - b.letzter_frame >= BUS_STILL_MS) return "aus - Bus schweigt";
  uint32_t pause = b.pause_ms, seit = j - b.seit;
  if (seit <= pause) return "Anlaufpause, noch " + std::to_string((pause - seit) / 60000 + 1) + " min nur mithoeren";
  return "aktiv (40 s / 60 s / 5 min)";
}

// ---------------------------------------------------------------- Anlass-Nachlesen

// Betriebsarten liegen im WEM und werden nur bei Anlass gelesen: nach dem Start, bei
// geaenderten Statusbits, nach Schaltbefehlen, auf Wunsch ("Status lesen") und durch Regeln.
// Die Zeitpunkte sind millis() | 1 (0 = kein Anlass).
struct Anlass {
  uint32_t anlass_ms = 0;             // Anlass fuer beide Betriebsarten
  uint32_t hk_ms = 0, ww_ms = 0;      // Anlass nur Heizkreis / nur Warmwasser (Regeln)
  uint32_t eigener_befehl_ms = 0;     // letzter eigener Schaltbefehl ueber den WEM
  bool statusbits_anlass = true;      // Paket verdacht schaltet das ab (Regel R3 statt dessen)
  bool start_gelesen = false;
  uint32_t hk_erledigt = 0, hk_letzte_lesung = 0;
  uint32_t ww_erledigt = 0;
  uint32_t status_manuell_ms = 0;
  int statusbits_vorher = -1;
  bool hk_jetzt = false, ww_jetzt = false;   // in diesem 5-s-Takt senden?

  // 5-s-Takt Heizkreis (Paket kessel). hk_jetzt wird IMMER zuerst zurueckgesetzt - bis v28
  // blieb es bei nicht bereitem Bus stehen, und die Leseanfrage 0x2933/2 ging dann alle 5 s
  // erneut hinaus (auch in der Anlaufpause). Behoben in v29.
  void hk_takt(bool bus_ok, uint32_t jetzt) {
    hk_jetzt = false;
    if (!bus_ok) return;
    if (!start_gelesen) { start_gelesen = true; anlass_ms = (jetzt - 5000) | 1; bf::status().anlass("start", true, true); }
    uint32_t a = anlass_ms;
    if (hk_ms != 0 && (a == 0 || (int32_t) (hk_ms - a) > 0)) a = hk_ms;
    if (a == 0 || a == hk_erledigt) return;
    if (jetzt - a < 5000) return;
    if (hk_letzte_lesung != 0 && jetzt - hk_letzte_lesung < HK_MINDESTABSTAND_MS) return;
    hk_erledigt = a; hk_letzte_lesung = jetzt | 1; hk_jetzt = true;
  }
  // 5-s-Takt Warmwasser (Paket warmwasser): 8 s nach dem Anlass, nur bei lebendem Bus
  void ww_takt(bool bus_tot, uint32_t jetzt) {
    ww_jetzt = false;
    uint32_t a = anlass_ms;
    if (ww_ms != 0 && (a == 0 || (int32_t) (ww_ms - a) > 0)) a = ww_ms;
    if (a == 0 || a == ww_erledigt) return;
    if (jetzt - a < 8000) return;
    if (bus_tot) return;
    ww_erledigt = a; ww_jetzt = true;
  }

  enum class Status { SCHWEIGT, VERWORFEN, PAUSE, ANGEFORDERT };
  // "Status lesen": einmal beide Betriebsarten, hebt die 2-min-Grenze auf, hoechstens alle 10 s
  Status anfordern(const Bus &b, uint32_t jetzt) {
    if (b.seit == 0 || jetzt - b.letzter_frame > BUS_STILL_MS) return Status::SCHWEIGT;
    if (status_manuell_ms != 0 && jetzt - status_manuell_ms < STATUS_MANUELL_MS) return Status::VERWORFEN;
    status_manuell_ms = jetzt | 1;
    hk_letzte_lesung = 0;
    anlass_ms = (jetzt - 5000) | 1;
    bf::status().anlass("status", true, true);
    return jetzt - b.seit <= b.pause_ms ? Status::PAUSE : Status::ANGEFORDERT;
  }

  // PDO 0x1C1: geaenderte Statusbits (ohne WW-Ladung und Heizbetrieb) loesen das Nachlesen aus
  void statusbits(uint16_t m, uint32_t jetzt) {
    int kern = m & ~0x0050;
    if (statusbits_anlass && statusbits_vorher >= 0 && kern != statusbits_vorher) {
      anlass_ms = jetzt | 1; bf::status().anlass("statusbits", true, true);
    }
    bf::status().setze(bf::status().hk_bits, (int) m);
    statusbits_vorher = kern;
  }

  // Regel hat ausgeloest: Heizkreis sofort im naechsten Takt (eigener Abstand statt 2-min-Grenze)
  void regel_hk(uint32_t jetzt) { hk_letzte_lesung = 0; hk_ms = (jetzt - 5000) | 1; }
  void regel_ww(uint32_t jetzt) { ww_ms = (jetzt - 8000) | 1; }
};

// ---------------------------------------------------------------- Lesebefehl von Hand

// "KNOTEN INDEX SUB" hex, z.B. "01 2933 02". NUR LESEN (Kommandobyte fest 0x40).
struct Lesebefehl {
  bool aktiv = false;
  int kn = 0, idx = 0, sub = 0;

  // true = senden (can_id, daten gesetzt); meldung immer gesetzt
  bool starten(const std::string &eingabe, bool bus_tot, std::string &meldung, uint32_t &can_id,
               std::vector<uint8_t> &daten) {
    unsigned k = 0, i = 0, s = 0;
    if (sscanf(eingabe.c_str(), "%x %x %x", &k, &i, &s) != 3 || k < 1 || k > 127 || i > 0xFFFF || s > 0xFF) {
      meldung = "ungueltig - Format: 01 2933 02"; return false;
    }
    if (bus_tot) { meldung = "Bus tot - nicht gesendet"; return false; }
    kn = k; idx = i; sub = s; aktiv = true;
    meldung = "gesendet, warte ...";
    can_id = 0x600 + k;
    daten = {0x40, (uint8_t) (i & 0xFF), (uint8_t) (i >> 8), (uint8_t) s, 0, 0, 0, 0};
    return true;
  }

  // jeder Frame: Antwort auf den offenen Lesebefehl? true = text gesetzt, Befehl erledigt
  bool antwort(uint32_t can_id, const std::vector<uint8_t> &x, std::string &text) {
    if (!aktiv || can_id != (uint32_t) (0x580 + kn) || x.size() < 8) return false;
    if (((x[2] << 8) | x[1]) != idx || x[3] != sub) return false;
    char t[80];
    if (x[0] == 0x80) {
      snprintf(t, sizeof(t), "%02X %04X/%02X: Abbruch %02X%02X%02X%02X (Objekt fehlt?)", kn, idx, sub, x[7], x[6], x[5], x[4]);
    } else {
      int n = (x[0] == 0x4F) ? 1 : (x[0] == 0x4B) ? 2 : (x[0] == 0x47) ? 3 : 4;
      uint32_t u = 0;
      for (int i = n - 1; i >= 0; i--) u = (u << 8) | x[4 + i];
      long v = (n == 2) ? (long) (int16_t) u : (n == 1) ? (long) u : (long) (int32_t) u;
      snprintf(t, sizeof(t), "%02X %04X/%02X = %ld (0x%0*lX, %d Byte)", kn, idx, sub, v, n * 2, (unsigned long) u, n);
    }
    text = t;
    aktiv = false;
    return true;
  }
};

// ---------------------------------------------------------------- Objektscan

// "KNOTEN START ENDE MAXSUB" hex, z.B. "01 2A00 2AFF 3": eine Leseanfrage je Schritt (25 ms)
struct Scan {
  bool aktiv = false;
  int knoten = 1, idx = 0, ende = 0, sub = 0, maxsub = 0;

  bool starten(const std::string &eingabe) {
    unsigned kn = 0, a = 0, e = 0, ms = 0;
    if (sscanf(eingabe.c_str(), "%x %x %x %x", &kn, &a, &e, &ms) != 4 || kn < 1 || kn > 127 || a > e || e > 0xFFFF || ms > 10)
      return false;
    knoten = kn; idx = a; ende = e; maxsub = ms; sub = 0; aktiv = true;
    return true;
  }
  // true = diese Anfrage senden; fertig = der Scan ist damit abgeschlossen
  bool schritt(bool bus_tot, uint32_t &can_id, std::vector<uint8_t> &daten, bool &fertig) {
    fertig = false;
    if (!aktiv || bus_tot) return false;
    int i = idx, s = sub;
    daten = {0x40, (uint8_t) (i & 0xFF), (uint8_t) (i >> 8), (uint8_t) s, 0, 0, 0, 0};
    can_id = 0x600 + knoten;
    if (++s > maxsub) { s = 0; i++; }
    if (i > ende) { aktiv = false; fertig = true; }
    idx = i; sub = s;
    return true;
  }
};

// ---------------------------------------------------------------- Brenner

struct Brenner {
  int vorher = -1;       // -1 = unbekannt (nach dem Start oder "Eigene CAN-Anfragen" aus)
  int starts = 0;        // ab Inbetriebnahme des Boards, gespeichert (frueher Global brenner_starts)
  // Phase (0-4) -> 1 = Flamme an (2/3), sonst 0. true = Start gezaehlt (Flanke 0 -> 1)
  bool phase(int ph, int &an) {
    an = (ph == 2 || ph == 3) ? 1 : 0;
    bool gezaehlt = false;
    if (an == 1 && vorher == 0) { starts += 1; gezaehlt = true; }
    vorher = an;
    return gezaehlt;
  }
};

// ---------------------------------------------------------------- Protokoll

// Neueste zuerst, 10 Eintraege seit dem letzten Neustart
struct Protokoll {
  std::vector<std::string> eintraege;
  void neu(const std::string &zeile) {
    eintraege.insert(eintraege.begin(), zeile);
    if (eintraege.size() > 10) eintraege.resize(10);
  }
  // kurz: fuer den Text-Sensor (hoechstens 250 Zeichen, " | "), lang: je Zeile ein Eintrag (MQTT)
  std::string kurz() const {
    std::string k;
    for (auto &e : eintraege)
      if (k.size() + e.size() + 3 <= 250) { if (!k.empty()) k += " | "; k += e; }
    return k;
  }
  std::string lang() const {
    std::string l;
    for (auto &e : eintraege) { if (!l.empty()) l += "\n"; l += e; }
    return l;
  }
};

// ---------------------------------------------------------------- Schalten ueber den WEM

struct Schalten {
  uint32_t letzter_befehl = 0;   // millis() des letzten WEM-Aufrufs (Sperrminute)
  int ziel = 0;                  // laufender Aufruf: 0 keiner, 1 Heizkreis, 2 Warmwasser, 3 Schreibbefehl
  std::string vg;                // VG des laufenden Aufrufs
  bool senden = false;           // in diesem Durchlauf an den WEM senden
  bool pruefen = false;          // Kontroll-Leseanfrage am Bus ist raus
  Protokoll protokoll;
  uint32_t letzter_gesehen = 0;  // Anzeige: wann die Sperrminute endet
  std::string frei_ab;

  bool sperrminute(uint32_t j) const { return letzter_befehl != 0 && j - letzter_befehl < SPERRMINUTE_MS; }
  // Sekunden, bis ein neuer Wunsch gesendet werden darf
  int warten_s(uint32_t j, bool wem_laeuft) const {
    uint32_t seit = j - letzter_befehl;
    return wem_laeuft ? 60 : (letzter_befehl != 0 && seit < SPERRMINUTE_MS) ? (int) ((SPERRMINUTE_MS - seit) / 1000) + 1 : 0;
  }
};

// Anzeige "Warteschlange"
inline std::string warteschlange_text(const bf::Schaltung &s, bool gesperrt, const std::string &frei_ab) {
  std::string w;
  if (s.hat_wunsch[1]) w = std::string("1. Heizkreis -> ") + bf::hk_text(s.wunsch[1].wert) + " (" + s.wunsch[1].quelle + ")";
  if (s.hat_wunsch[2]) {
    if (!w.empty()) w += " | 2. "; else w = "1. ";
    w += std::string("Warmwasser -> ") + bf::ww_text(s.wunsch[2].wert) + " (" + s.wunsch[2].quelle + ")";
  }
  if (w.empty()) w = "leer";
  else if (gesperrt) w += frei_ab.empty() ? " - wartet auf die Sperrminute" : " - naechster Befehl ab " + frei_ab;
  return w;
}

// Schreibbefehl von Hand "MI MX OX OS WERT" (hex) -> VG (CM 03). Nur MI 01/02/03 (Systemgeraet).
inline bool schreib_vg(const std::string &eingabe, std::string &vg, std::string &fehler) {
  unsigned mi = 0, mx = 0, ox = 0, os = 0;
  char w[12] = {0};
  if (sscanf(eingabe.c_str(), "%x %x %x %x %10s", &mi, &mx, &ox, &os, w) != 5 || mx > 0xFF || ox > 0xFFFF || os > 0xFF) {
    fehler = "ungueltig - Format: MI MX OX OS WERT, z.B. 03 00 2520 02 02"; return false;
  }
  if (mi != 1 && mi != 2 && mi != 3) { fehler = "gesperrt: nur MI 01, 02, 03 erlaubt - der Kessel wird nie beschrieben"; return false; }
  std::string v = w;
  if (v.empty() || v.size() > 4 || v.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
    fehler = "Wert: 1-4 Hexziffern"; return false;
  }
  if (v.size() % 2) v = "0" + v;
  char b[40];
  snprintf(b, sizeof(b), "03%02X%02X%04X%02X%04X%s", mi, mx, ox, os, (unsigned) (v.size() / 2), v.c_str());
  vg = b;
  return true;
}

// VG aus der Antwort des WEM ({"...","VG":"04..."}); leer = keins gefunden
inline std::string wem_vg(const std::string &body) {
  size_t p = body.find("\"VG\":\"");
  return p == std::string::npos ? std::string("") : body.substr(p + 6, body.find('"', p + 6) - p - 6);
}

// Rumpf eines WEM-Aufrufs (JSON, eine VG)
inline std::string wem_rumpf(uint32_t millis_jetzt, const std::string &vg) {
  return std::string("{\"ID\":\"") + std::to_string(millis_jetzt % 100000000) +
         "\",\"SRC\":\"DDC\",\"CAPI\":{\"NN\":1,\"N01\":{\"VG\":\"" + vg + "\"}}}";
}

// ---------------------------------------------------------------- Regeln (Laufzeit)

struct Verdacht {
  // offene Lesung einer Regel (fuer "geaendert ja/nein" im Protokoll)
  struct Offen { bool aktiv = false; std::string regel; int alt = -1; uint32_t ms = 0; };
  // Ergebnis einer Antwort bzw. eines Zeitablaufs -> Protokoll und Ereignis
  struct Ergebnis {
    std::string eintrag;       // Protokollzeile (ohne Zeit)
    std::string regel, phase, ziel, wert;
    int geaendert = -1;        // -1 null, 0 nein, 1 ja
  };

  int hk_bekannt = -1, ww_bekannt = -1;   // zuletzt gelesene Betriebsarten
  std::vector<uint32_t> zeiten;           // Ausloesezeitpunkte der letzten Stunde
  std::map<std::string, uint32_t> letzt, letzt_schatten;
  Protokoll protokoll;
  Offen offen_hk, offen_ww;
  rw::Werk werk{rw::feste_regeln()};     // R1-R5 als Datenregeln (regelwerk.h) samt Laufzeitzustand
  bool stand_gesendet = false;
  // gespeichert (frueher Globals vd_*): ueberleben den Neustart
  int stby_vorher = -1, rest_vorher = -1;
  uint32_t datei_hash = 0;
  int datei_version = -1;
  std::array<char, 2400> speicher{};      // eigene Regeln in Speicherform

  // Regeln fuer einen Frame auswerten (feste, dann eigene). k.hk/ww/plaetze setzt diese Funktion.
  std::vector<rw::Aktion> frame(uint32_t can_id, const std::vector<uint8_t> &x, rw::Kontext k) {
    k.hk = hk_bekannt;
    k.ww = ww_bekannt;
    k.plaetze[0] = &stby_vorher;
    k.plaetze[1] = &rest_vorher;
    return rw::frame(werk, vd::eigene(), can_id, x, k);
  }

  // Regel hat ausgeloest: offene Lesung merken, Anlass setzen. ziel 1 HK, 2 WW, 3 beide
  void ausgeloest(const std::string &regel, int ziel, uint32_t jetzt, Anlass &a) {
    if (ziel & 1) {
      if (offen_hk.aktiv) offen_hk.regel += "+" + regel;
      else { offen_hk.aktiv = true; offen_hk.regel = regel; offen_hk.alt = hk_bekannt; offen_hk.ms = jetzt; }
      a.regel_hk(jetzt);
      bf::status().anlass("regel " + regel, true, false);
    }
    if (ziel & 2) {
      if (offen_ww.aktiv) offen_ww.regel += "+" + regel;
      else { offen_ww.aktiv = true; offen_ww.regel = regel; offen_ww.alt = ww_bekannt; offen_ww.ms = jetzt; }
      a.regel_ww(jetzt);
      bf::status().anlass("regel " + regel, false, true);
    }
  }

  // Antwort auf eine Lesung (Knoten 1, 0x581). true = offene Regel-Lesung abgeschlossen (e gesetzt)
  bool antwort(const std::vector<uint8_t> &x, Ergebnis &e) {
    uint16_t idx = 0;
    int neu = 0;
    if (!rl::antwort_lesen(x, idx, neu)) return false;
    bool fertig = false;
    if (idx == reg::HK_BETRIEBSART.index) {
      if (offen_hk.aktiv) {
        offen_hk.aktiv = false;
        int alt = offen_hk.alt;
        std::string t = offen_hk.regel + ": Heizkreis " + hk_name(neu, "?");
        if (alt < 0) t += " (vorher unbekannt), geaendert: ?";
        else if (alt == neu) t += ", geaendert: nein";
        else t += std::string(" (vorher ") + hk_name(alt, "?") + "), geaendert: JA";
        e.eintrag = t; e.regel = offen_hk.regel; e.phase = "ergebnis"; e.ziel = "hk"; e.wert = hk_name(neu, "?");
        e.geaendert = alt < 0 ? -1 : (alt == neu ? 0 : 1);
        fertig = true;
      }
      hk_bekannt = neu;
    } else if (idx == reg::WW_BETRIEBSART.index) {
      if (offen_ww.aktiv) {
        offen_ww.aktiv = false;
        int alt = offen_ww.alt;
        std::string t = offen_ww.regel + ": Warmwasser " + ww_name(neu, "?");
        if (alt < 0) t += " (vorher unbekannt), geaendert: ?";
        else if (alt == neu) t += ", geaendert: nein";
        else t += std::string(" (vorher ") + ww_name(alt, "?") + "), geaendert: JA";
        e.eintrag = t; e.regel = offen_ww.regel; e.phase = "ergebnis"; e.ziel = "ww"; e.wert = ww_name(neu, "?");
        e.geaendert = alt < 0 ? -1 : (alt == neu ? 0 : 1);
        fertig = true;
      }
      ww_bekannt = neu;
    }
    return fertig;
  }

  // offene Lesung ohne Antwort nach 3 min abschliessen. true = e gesetzt
  bool abgelaufen(Offen &o, const char *ziel, const char *ziel_text, uint32_t jetzt, Ergebnis &e) {
    if (!o.aktiv || jetzt - o.ms <= REGEL_ANTWORT_FRIST_MS) return false;
    o.aktiv = false;
    e.eintrag = o.regel + ": " + ziel_text + " - keine Antwort";
    e.regel = o.regel; e.phase = "keine_antwort"; e.ziel = ziel; e.wert = ""; e.geaendert = -1;
    return true;
  }

  // Ausloesungen der letzten Stunde (Anzeige)
  int letzte_stunde(uint32_t jetzt) const {
    int h = 0;
    for (auto t : zeiten) if (jetzt - t <= 3600000UL) h++;
    return h;
  }
};

// Anzeige "Regeln aktiv": an/ab = Schalter und Mindestabstaende R1-R5
inline std::string regeln_aktiv_text(const bool an[5], const int ab[5], bool eigene_haupt, int max_h, const Verdacht &v,
                                     const Bus &b, uint32_t jetzt) {
  std::string s;
  for (int i = 0; i < 5; i++) {
    if (!an[i]) continue;
    if (!s.empty()) s += ", ";
    s += "R" + std::to_string(i + 1) + " " + std::to_string(ab[i]) + " min";
  }
  if (s.empty()) s = "R1-R5 aus (gelesen wird nur bei Start, Schaltbefehl und 'Status lesen')";
  int n = 0;
  for (auto &r : vd::eigene()) if (r.an) n++;
  if (!eigene_haupt) s += " | eigene Regeln aus";
  else s += " | eigene Regeln an: " + std::to_string(n) + " aktiv";
  s += " | max " + std::to_string(max_h) + "/h, letzte Stunde " + std::to_string(v.letzte_stunde(jetzt));
  if (!b.lebt(jetzt)) s += " | Bus schweigt";
  else if (b.in_pause(jetzt)) s += " | Anlaufpause";
  return s;
}

// ---------------------------------------------------------------- Zusatzanzeigen

struct Zusatz {
  // gespeichert (frueher Globals zs_*)
  std::array<char, 1600> speicher{};   // Liste in Speicherform (JSON)
  uint32_t datei_hash = 0;             // Pruefsumme der zuletzt uebernommenen Datei
  int version = -1;                    // "version" dieser Datei (-1 = keine)
  bool gesendet = false;               // app/zusatz seit der letzten Aenderung/Verbindung veroeffentlicht
};

// Speicherform in ein festes Feld schreiben; false = zu gross (dann bleibt das Feld unveraendert)
template<size_t N> inline bool speicher_schreiben(std::array<char, N> &m, const std::string &s) {
  if (s.size() >= m.size()) return false;
  m.fill(0);
  for (size_t i = 0; i < s.size(); i++) m[i] = s[i];
  return true;
}

}  // namespace wc
