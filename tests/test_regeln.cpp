// Tests des Bauteils weishaupt_can (components/weishaupt_can/): Regellogik (regellogik.h), Datei-Parser
// (verdacht.h, zusatz.h), Schaltbefehle/Status-JSON (befehle.h) und die Zustandsklassen (kern.h) -
// laeuft auf dem PC, ohne ESPHome, ohne Board, ohne Bus.
// Alle Frames sind AUSGEDACHT nach den Mustern in PROTOKOLL.md, keine Mitschnitte.
//
// Bauen und starten: tests/run.sh
#include "../components/weishaupt_can/regellogik.h"
#include "../components/weishaupt_can/verdacht.h"
#include "../components/weishaupt_can/zusatz.h"
#include "../components/weishaupt_can/befehle.h"
#include "../components/weishaupt_can/kern.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <functional>

static int g_fehler = 0, g_pruef = 0;
static const char *g_test = "";

#define PRUEFE(bed)                                                                   \
  do {                                                                                \
    g_pruef++;                                                                        \
    if (!(bed)) {                                                                     \
      g_fehler++;                                                                     \
      std::printf("  FEHLER in %s (Zeile %d): %s\n", g_test, __LINE__, #bed);        \
    }                                                                                 \
  } while (0)

struct Test { const char *name; std::function<void()> f; };
static std::vector<Test> &tests() { static std::vector<Test> v; return v; }
struct Reg { Reg(const char *n, std::function<void()> f) { tests().push_back({n, f}); } };
#define TEST(n) static void n(); static Reg reg_##n(#n, n); static void n()

using F = std::vector<uint8_t>;

// SDO-Anfrage des WEM an den Kessel (0x602): Kommandobyte, Index LE, Subindex
static F sdo(uint8_t kmd, uint16_t idx, uint8_t sub, uint8_t d0 = 0) {
  return F{kmd, (uint8_t) (idx & 0xFF), (uint8_t) (idx >> 8), sub, d0, 0, 0, 0};
}

static std::string datei(const char *pfad) {
  std::ifstream f(pfad);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}
static bool enthaelt(const std::string &s, const char *t) { return s.find(t) != std::string::npos; }

// Spielt eine Folge von (Zeit ms, Frame an 0x602) durch R1 und zaehlt die Ausloesungen.
struct Zeitframe { uint32_t t; F x; };
static int r1_zaehlen(const std::vector<Zeitframe> &folge) {
  uint32_t a = 0, b = 0;
  int n = 0;
  for (auto &z : folge) if (rl::r1_frame(a, b, z.x, z.t)) n++;
  return n;
}

// ---------------------------------------------------------------- R1 (Warmwasser umgeschaltet)

// Das ist der Test, der den Fehler aus v22-v24 gefunden haette: der WEM fragt mit 0xA4.
TEST(r1_erkennt_block_upload_A4) {
  std::vector<Zeitframe> ww = {
      {100000, sdo(0xA4, 0x2101, 0x0A)}, {100400, sdo(0xA4, 0x2102, 0x0D)}, {100800, sdo(0xA4, 0x2102, 0x01)},
      {101200, sdo(0xA4, 0x2101, 0x0A)}, {101600, sdo(0xA4, 0x273F, 0x01)},
      // 4-13 s spaeter die Kurzform
      {109000, sdo(0xA4, 0x2101, 0x0A)}, {109400, sdo(0xA4, 0x273F, 0x01)},
  };
  PRUEFE(r1_zaehlen(ww) == 1);
}

TEST(r1_erkennt_expedited_40) {
  std::vector<Zeitframe> ww = {
      {5000, sdo(0x40, 0x2101, 0x0A)}, {5300, sdo(0x40, 0x2102, 0x0D)}, {5600, sdo(0x40, 0x2102, 0x01)}};
  PRUEFE(r1_zaehlen(ww) == 1);
}

TEST(r1_gemischte_kommandobytes) {
  std::vector<Zeitframe> ww = {
      {5000, sdo(0xA4, 0x2101, 0x0A)}, {5300, sdo(0x40, 0x2102, 0x0D)}, {5600, sdo(0xA4, 0x2102, 0x01)}};
  PRUEFE(r1_zaehlen(ww) == 1);
}

TEST(r1_nicht_bei_heizkreis_umschaltung) {
  // Heizkreis Standby -> Zeitprogramm: WEM schreibt 0x252B/0x252C/0x2709 und liest Kesselwerte,
  // aber nie die Warmwasserfolge
  std::vector<Zeitframe> hk = {
      {1000, sdo(0x2F, 0x252B, 0x00, 0x0A)}, {1200, sdo(0x2B, 0x252C, 0x00, 0xC8)}, {1400, sdo(0x2F, 0x2709, 0x00, 0x64)},
      {1600, sdo(0xA4, 0x2532, 0x00)},        {1800, sdo(0xA4, 0x2537, 0x00)},        {2000, sdo(0xA4, 0x2541, 0x00)},
      {2200, sdo(0xA4, 0x2714, 0x02)},        {2400, sdo(0xA4, 0x2545, 0x00)},
  };
  PRUEFE(r1_zaehlen(hk) == 0);
}

TEST(r1_nicht_bei_kurzform_ladebeginn) {
  // Ladebeginn: nur 2101/0A + 273F/01, ohne 2102/0D + 2102/01
  std::vector<Zeitframe> kurz = {{1000, sdo(0xA4, 0x2101, 0x0A)}, {1400, sdo(0xA4, 0x273F, 0x01)}};
  PRUEFE(r1_zaehlen(kurz) == 0);
}

TEST(r1_nicht_ohne_2102_0D) {
  std::vector<Zeitframe> f = {{1000, sdo(0xA4, 0x2101, 0x0A)}, {1400, sdo(0xA4, 0x2102, 0x01)}};
  PRUEFE(r1_zaehlen(f) == 0);
}

TEST(r1_nicht_bei_falscher_reihenfolge) {
  std::vector<Zeitframe> f = {
      {1000, sdo(0xA4, 0x2102, 0x0D)}, {1400, sdo(0xA4, 0x2101, 0x0A)}, {1800, sdo(0xA4, 0x2102, 0x01)}};
  PRUEFE(r1_zaehlen(f) == 0);
}

TEST(r1_zeitfenster) {
  // 2102/0D -> 2102/01 laenger als 10 s: keine Folge
  std::vector<Zeitframe> zu_langsam = {
      {1000, sdo(0xA4, 0x2101, 0x0A)}, {2000, sdo(0xA4, 0x2102, 0x0D)}, {12500, sdo(0xA4, 0x2102, 0x01)}};
  PRUEFE(r1_zaehlen(zu_langsam) == 0);
  // 2101/0A -> 2102/01 laenger als 15 s: keine Folge
  std::vector<Zeitframe> a_zu_alt = {
      {1000, sdo(0xA4, 0x2101, 0x0A)}, {12000, sdo(0xA4, 0x2102, 0x0D)}, {16500, sdo(0xA4, 0x2102, 0x01)}};
  PRUEFE(r1_zaehlen(a_zu_alt) == 0);
  // knapp innerhalb
  std::vector<Zeitframe> knapp = {
      {1000, sdo(0xA4, 0x2101, 0x0A)}, {6000, sdo(0xA4, 0x2102, 0x0D)}, {15999, sdo(0xA4, 0x2102, 0x01)}};
  PRUEFE(r1_zaehlen(knapp) == 1);
}

TEST(r1_zustand_nach_abschluss_geleert) {
  uint32_t a = 0, b = 0;
  PRUEFE(!rl::r1_frame(a, b, sdo(0xA4, 0x2101, 0x0A), 1000));
  PRUEFE(!rl::r1_frame(a, b, sdo(0xA4, 0x2102, 0x0D), 1200));
  PRUEFE(rl::r1_frame(a, b, sdo(0xA4, 0x2102, 0x01), 1400));
  PRUEFE(a == 0 && b == 0);
  // ein zweites 2102/01 allein loest nicht erneut aus
  PRUEFE(!rl::r1_frame(a, b, sdo(0xA4, 0x2102, 0x01), 1600));
}

TEST(r1_ignoriert_antworten_und_kurze_frames) {
  uint32_t a = 0, b = 0;
  // Antworten des Kessels (0x43/0x4F) und Abbrueche (0x80) sind keine Anfragen
  PRUEFE(!rl::r1_frame(a, b, sdo(0x43, 0x2101, 0x0A), 1000));
  PRUEFE(a == 0);
  PRUEFE(!rl::r1_frame(a, b, F{0xA4, 0x01, 0x21, 0x0A}, 1000));   // nur 4 Bytes
  PRUEFE(a == 0);
}

TEST(r1_wem_neustart_und_eigener_befehl) {
  PRUEFE(!rl::wem_neu(0, 1000));
  PRUEFE(rl::wem_neu(1001, 1000 + 299999));
  PRUEFE(!rl::wem_neu(1001, 1001 + 300000));
  PRUEFE(!rl::eigen_ruht(0, 50000));
  PRUEFE(rl::eigen_ruht(10001, 10001 + 119999));
  PRUEFE(!rl::eigen_ruht(10001, 10001 + 120000));
}

// ---------------------------------------------------------------- R2/R4 ueber 0x252B und 0x182

TEST(w252b_schreiben_und_aenderung) {
  int vorher = -1, v = 0;
  PRUEFE(rl::w252b_frame(vorher, sdo(0x2F, 0x252B, 0x00, 0x0F), v) && v == 0x0F);
  PRUEFE(!rl::w252b_frame(vorher, sdo(0x2F, 0x252B, 0x00, 0x0F), v));     // gleicher Wert
  PRUEFE(rl::w252b_frame(vorher, sdo(0x2F, 0x252B, 0x00, 0x0A), v) && v == 0x0A);
  // Lesen von 0x252B ist kein Schreiben; anderer Index ebenso nicht
  int vorher2 = -1;
  PRUEFE(!rl::w252b_frame(vorher2, sdo(0xA4, 0x252B, 0x00), v));
  PRUEFE(!rl::w252b_frame(vorher2, sdo(0x2F, 0x252C, 0x00, 0x0F), v));
  PRUEFE(vorher2 == -1);
}

TEST(r2_r4_bedingungen) {
  PRUEFE(rl::r2_bei_252b(0x0F, 2));        // Warmwasserbetrieb, bekannt "Aus"
  PRUEFE(!rl::r2_bei_252b(0x0F, 1));       // bekannt "Ein": kein Widerspruch
  PRUEFE(!rl::r2_bei_252b(0x0F, -1));      // unbekannt
  PRUEFE(rl::r4_bei_252b(0x0A, 1));        // Heizbetrieb bei Standby
  PRUEFE(rl::r4_bei_252b(0x0A, 5));        // Heizbetrieb bei Sommer
  PRUEFE(!rl::r4_bei_252b(0x0A, 2));       // Zeitprogramm: erwartet
  PRUEFE(rl::r2_bei_kstatus(15, 2) && !rl::r2_bei_kstatus(10, 2));
  PRUEFE(rl::r4_bei_kstatus(10, 1) && !rl::r4_bei_kstatus(15, 1));
}

TEST(kesselstatus_flanke) {
  int vorher = -1, st = 0;
  PRUEFE(!rl::kstatus_flanke(vorher, F{10}, st));    // erster Wert: keine Flanke
  PRUEFE(vorher == 10);
  PRUEFE(!rl::kstatus_flanke(vorher, F{10}, st));
  PRUEFE(rl::kstatus_flanke(vorher, F{15}, st) && st == 15);
  PRUEFE(!rl::kstatus_flanke(vorher, F{}, st));      // leerer Frame
}

// ---------------------------------------------------------------- R3/R5 (Statusbits PDO 0x1C1)

TEST(statusbits_byte_reihenfolge) {
  // Bytes 2-3 little-endian: 00 00 00 10 = 0x1000 (Standby), 00 00 10 00 = 0x0010 (WW-Ladung)
  PRUEFE(rl::statusbits_m(F{0, 0, 0x00, 0x10}) == 0x1000);
  PRUEFE(rl::statusbits_m(F{0, 0, 0x10, 0x00}) == 0x0010);
}

TEST(r3_standby_bit) {
  int sv = -1, rv = -1;
  auto e = rl::statusbits(sv, rv, F{0, 0, 0x00, 0x10});      // erster Stand: Standby
  PRUEFE(e.geaendert && !e.r3 && !e.r5);                     // Vorwert unbekannt: nichts ausloesen
  e = rl::statusbits(sv, rv, F{0, 0, 0x00, 0x10});           // unveraendert
  PRUEFE(!e.geaendert);
  e = rl::statusbits(sv, rv, F{0, 0, 0x00, 0x00});           // Heizung ein
  PRUEFE(e.geaendert && e.r3 && !e.r5 && e.sv == 1 && e.stby == 0);
}

TEST(r3_nicht_bei_ww_ladung_und_heizbetrieb) {
  // 0x0010 (WW-Ladung) und 0x0040 (Heizbetrieb) zaehlen weder fuer R3 noch fuer R5.
  // Wer die Bytes vertauscht, liest 00 00 10 00 als Standby - dieser Test faengt das.
  int sv = -1, rv = -1;
  rl::statusbits(sv, rv, F{0, 0, 0x00, 0x00});
  auto e = rl::statusbits(sv, rv, F{0, 0, 0x10, 0x00});
  PRUEFE(!e.geaendert && !e.r3 && !e.r5);
  e = rl::statusbits(sv, rv, F{0, 0, 0x40, 0x00});
  PRUEFE(!e.geaendert);
  e = rl::statusbits(sv, rv, F{0, 0, 0x50, 0x00});
  PRUEFE(!e.geaendert);
}

TEST(r5_uebrige_bits) {
  int sv = -1, rv = -1;
  rl::statusbits(sv, rv, F{0, 0, 0x00, 0x10});
  auto e = rl::statusbits(sv, rv, F{0, 0, 0x04, 0x10});     // 0x1004: Heizbedarf-Bit dazu
  PRUEFE(e.geaendert && !e.r3 && e.r5 && e.rest == 0x0004 && e.rv == 0);
  e = rl::statusbits(sv, rv, F{0, 0, 0x04, 0x04});          // 0x0404: Standby weg, 0x0400 dazu
  PRUEFE(e.r3 && e.r5);
}

TEST(status_entscheiden) {
  rl::StatusAenderung beide;
  beide.geaendert = beide.r3 = beide.r5 = true;
  auto s = rl::status_entscheiden(beide, false, true);       // R3 an: R3 liest, R5 nicht zusaetzlich
  PRUEFE(s.r3 && s.r3_liest && !s.r5);
  s = rl::status_entscheiden(beide, false, false);           // R3 aus: R3 nur Schatten, R5 wird gerufen
  PRUEFE(s.r3 && !s.r3_liest && s.r5);
  s = rl::status_entscheiden(beide, true, true);             // eigener Befehl: alles ruht
  PRUEFE(!s.r3 && !s.r5);
  rl::StatusAenderung nur5;
  nur5.geaendert = nur5.r5 = true;
  s = rl::status_entscheiden(nur5, false, true);
  PRUEFE(!s.r3 && s.r5);
}

// ---------------------------------------------------------------- R4 (Vorlaufsoll gegen Raumsoll)

TEST(pdo_temperatur) {
  PRUEFE(std::fabs(rl::pdo_temp(0x2C, 0x01) - 30.0f) < 0.001f);
  PRUEFE(std::fabs(rl::pdo_temp(0xFF, 0xFF) + 0.1f) < 0.001f);
}

TEST(r4_vorlauf) {
  int key = -1;
  rl::R4Befund b;
  // AT 14 C, Normal (7, erwartet 20): Vorlaufsoll 30,1 -> RT 21 -> Stufe 21 = Widerspruch
  PRUEFE(rl::r4_vorlauf(key, 30.1f, 14.0f, 7, b) && b.stufe == 21 && b.soll == 20 && key == 721);
  PRUEFE(!rl::r4_vorlauf(key, 30.1f, 14.0f, 7, b));          // derselbe Widerspruch nicht erneut
  // passend: Vorlaufsoll 26,5 -> RT ~ 19,6 -> Stufe 20
  PRUEFE(!rl::r4_vorlauf(key, 26.5f, 14.0f, 7, b) && key == -1);
  PRUEFE(rl::r4_vorlauf(key, 30.1f, 14.0f, 7, b));           // danach wieder meldbar
  // keine Aussage bei Standby, ohne Aussentemperatur oder ohne Vorlaufsoll
  PRUEFE(!rl::r4_vorlauf(key, 30.1f, 14.0f, 1, b) && key == -1);
  PRUEFE(!rl::r4_vorlauf(key, 30.1f, NAN, 7, b));
  PRUEFE(!rl::r4_vorlauf(key, 0.0f, 14.0f, 7, b));
}

// ---------------------------------------------------------------- Antworten, eigene Regeln

TEST(antwort_lesen) {
  uint16_t idx = 0;
  int w = 0;
  PRUEFE(rl::antwort_lesen(F{0x4F, 0x33, 0x29, 0x02, 0x02, 0, 0, 0}, idx, w) && idx == 0x2933 && w == 2);
  PRUEFE(rl::antwort_lesen(F{0x4F, 0x20, 0x2A, 0x02, 0x01, 0, 0, 0}, idx, w) && idx == 0x2A20 && w == 1);
  PRUEFE(!rl::antwort_lesen(F{0x4F, 0x33, 0x29, 0x00, 0x02, 0, 0, 0}, idx, w));   // Subindex 0
  PRUEFE(!rl::antwort_lesen(F{0x80, 0x33, 0x29, 0x02, 0, 0, 2, 6}, idx, w));      // Abbruch
}

TEST(eigene_regel_maske) {
  uint8_t maske[8] = {0x00, 0xFF, 0xFF, 0, 0, 0, 0, 0};
  uint8_t muster[8] = {0x00, 0x3E, 0x22, 0, 0, 0, 0, 0};
  PRUEFE(rl::maske_passt(maske, muster, sdo(0xA4, 0x223E, 0x00)));
  PRUEFE(rl::maske_passt(maske, muster, sdo(0x40, 0x223E, 0x05)));      // Byte 0 und 3 egal
  PRUEFE(!rl::maske_passt(maske, muster, sdo(0xA4, 0x223F, 0x00)));
  PRUEFE(!rl::maske_passt(maske, muster, F{0xA4, 0x3E}));               // fehlende Bytes = 00
  PRUEFE(rl::gesperrte_id(0x601) && rl::gesperrte_id(0x581));
  PRUEFE(!rl::gesperrte_id(0x602) && !rl::gesperrte_id(0x582));
}

// ---------------------------------------------------------------- Pruefung vor dem Lesen

struct Stand {
  std::map<std::string, uint32_t> letzt, schatten;
  std::vector<uint32_t> zeiten;
  rl::Ergebnis p(const char *r, bool an, int ab, int max_h, bool bus, uint32_t t) {
    return rl::regel_pruefen(r, an, ab, max_h, bus, t, letzt, schatten, zeiten);
  }
};

TEST(bus_bereit) {
  PRUEFE(!rl::bus_bereit(0, 1000, 60000, 1000));             // Bus nie gesehen
  PRUEFE(!rl::bus_bereit(1, 1000, 60000, 50000));            // Anlaufpause
  PRUEFE(rl::bus_bereit(1, 70000, 60000, 70001));
  PRUEFE(!rl::bus_bereit(1, 70000, 60000, 100001));          // 30 s ohne Frame
  // ueber den Ueberlauf von millis() hinweg
  PRUEFE(rl::bus_bereit(0xFFFF0001u, 0x00000100u, 1000, 0x00000200u));
}

TEST(mindestabstand) {
  using E = rl::Ergebnis;
  Stand s;
  PRUEFE(s.p("R1", true, 1, 6, true, 1000000) == E::AUSLOESEN);
  PRUEFE(s.p("R1", true, 1, 6, true, 1059999) == E::ABSTAND);
  PRUEFE(s.p("R3", true, 1, 6, true, 1059999) == E::AUSLOESEN);        // Abstand gilt je Regel
  PRUEFE(s.p("R1", true, 1, 6, true, 1060000) == E::AUSLOESEN);
  PRUEFE(s.zeiten.size() == 3);
}

TEST(obergrenze_pro_stunde) {
  using E = rl::Ergebnis;
  Stand s;
  PRUEFE(s.p("R1", true, 1, 2, true, 1000000) == E::AUSLOESEN);
  PRUEFE(s.p("R2", true, 1, 2, true, 1100000) == E::AUSLOESEN);
  PRUEFE(s.p("R3", true, 1, 2, true, 1200000) == E::OBERGRENZE);
  PRUEFE(s.letzt.count("R3") == 0);                          // nichts vermerkt
  PRUEFE(s.p("R3", true, 1, 2, true, 1000000 + 3600001) == E::AUSLOESEN);   // R1 aus der Stunde gefallen
}

TEST(bus_nicht_bereit_vermerkt_nichts) {
  Stand s;
  PRUEFE(s.p("R1", true, 1, 6, false, 1000) == rl::Ergebnis::BUS_NICHT_BEREIT);
  PRUEFE(s.letzt.empty() && s.zeiten.empty());
  PRUEFE(s.p("R1", true, 1, 6, true, 1001) == rl::Ergebnis::AUSLOESEN);
}

TEST(schattenmodus) {
  using E = rl::Ergebnis;
  Stand s;
  PRUEFE(s.p("R4", false, 30, 1, false, 1000) == E::SCHATTEN_GEMELDET);    // auch ohne Bus
  PRUEFE(s.p("R4", false, 30, 1, true, 1000 + 29 * 60000) == E::SCHATTEN_ABSTAND);
  PRUEFE(s.p("R4", false, 30, 1, true, 1000 + 30 * 60000) == E::SCHATTEN_GEMELDET);
  // zaehlt nicht gegen die Obergrenze und nicht gegen den Abstand der eingeschalteten Regel
  PRUEFE(s.zeiten.empty() && s.letzt.empty());
  PRUEFE(s.p("R4", true, 30, 1, true, 1000 + 30 * 60000 + 1) == E::AUSLOESEN);
}

// ---------------------------------------------------------------- cmd/regeln (JSON-Datei)

TEST(regeln_beispieldatei) {
  vd::Datei d;
  std::string f;
  PRUEFE(vd::datei_lesen(datei("regeln.json.example"), d, f));
  PRUEFE(d.version == 1 && d.an[0] == 1 && d.an[3] == 0 && d.abstand[1] == 10 && d.max_h == 6);
  PRUEFE(d.eigene_an == 0 && d.hat_eigene && d.eigene.size() == 1);
  PRUEFE(d.eigene[0].can_id == 0x602 && d.eigene[0].lesen == 3 && d.eigene[0].abstand_min == 30);
}

TEST(regeln_altes_feld_verdacht_abgelehnt) {
  vd::Datei d;
  std::string f;
  PRUEFE(!vd::datei_lesen("{\"verdacht\":true}", d, f));
  PRUEFE(enthaelt(f, "verdacht"));
  PRUEFE(d.eigene_an == -1);                                 // bewusst NICHT umgedeutet
  PRUEFE(!vd::datei_lesen("{\"version\":2,\"verdacht\":false,\"R1\":{\"an\":true}}", d, f));
}

TEST(regeln_ungueltige_werte) {
  vd::Datei d;
  std::string f;
  PRUEFE(!vd::datei_lesen("kein json", d, f));
  PRUEFE(!vd::datei_lesen("[1,2]", d, f));
  PRUEFE(!vd::datei_lesen("{\"R6\":{\"an\":true}}", d, f));
  PRUEFE(!vd::datei_lesen("{\"R1\":{\"an\":1}}", d, f));
  PRUEFE(!vd::datei_lesen("{\"R1\":{\"abstand_min\":0}}", d, f));
  PRUEFE(!vd::datei_lesen("{\"R1\":{\"abstand_min\":1441}}", d, f));
  PRUEFE(!vd::datei_lesen("{\"R1\":{\"xyz\":1}}", d, f));
  PRUEFE(!vd::datei_lesen("{\"max_pro_stunde\":61}", d, f));
  PRUEFE(!vd::datei_lesen("{\"version\":-1}", d, f));
  PRUEFE(!vd::datei_lesen("{\"eigene_regeln_an\":\"ja\"}", d, f));
  vd::Datei ok;
  PRUEFE(vd::datei_lesen("{\"R5\":{\"an\":true,\"_notiz\":\"x\"},\"kommentar\":\"x\"}", ok, f));
  PRUEFE(ok.an[4] == 1 && ok.an[0] == -1 && !ok.hat_eigene);
}

static bool eigene_ok(const std::string &regel, std::string &f, vd::Datei &d) {
  return vd::datei_lesen("{\"eigene\":[" + regel + "]}", d, f);
}

TEST(regeln_eigene) {
  vd::Datei d;
  std::string f;
  PRUEFE(eigene_ok("{\"name\":\"t1\",\"can_id\":\"0x1C1\",\"muster\":\"00 00\",\"lesen\":\"hk\"}", f, d));
  PRUEFE(d.eigene[0].lesen == 1 && d.eigene[0].maske[1] == 0xFF && d.eigene[0].maske[2] == 0x00);
  PRUEFE(d.eigene[0].abstand_min == 10 && d.eigene[0].an);
  // Muster wird mit der Maske verundet
  PRUEFE(eigene_ok("{\"name\":\"t2\",\"can_id\":1538,\"maske\":\"F0\",\"muster\":\"FF\"}", f, d));
  PRUEFE(d.eigene[0].can_id == 0x602 && d.eigene[0].muster[0] == 0xF0);
  // gesperrte IDs, reservierte Namen, Grenzen
  PRUEFE(!eigene_ok("{\"name\":\"t\",\"can_id\":\"0x601\",\"muster\":\"40\"}", f, d) && enthaelt(f, "gesperrt"));
  PRUEFE(!eigene_ok("{\"name\":\"t\",\"can_id\":\"0x581\",\"muster\":\"40\"}", f, d) && enthaelt(f, "gesperrt"));
  PRUEFE(!eigene_ok("{\"name\":\"R3\",\"can_id\":\"0x602\",\"muster\":\"40\"}", f, d));
  PRUEFE(!eigene_ok("{\"name\":\"verdacht\",\"can_id\":\"0x602\",\"muster\":\"40\"}", f, d));
  PRUEFE(!eigene_ok("{\"name\":\"eigene\",\"can_id\":\"0x602\",\"muster\":\"40\"}", f, d));
  PRUEFE(!eigene_ok("{\"name\":\"t\",\"can_id\":\"0x800\",\"muster\":\"40\"}", f, d));
  PRUEFE(!eigene_ok("{\"name\":\"t\",\"can_id\":\"0x602\",\"muster\":\"4\"}", f, d));
  PRUEFE(!eigene_ok("{\"name\":\"t\",\"can_id\":\"0x602\",\"muster\":\"00 11 22 33 44 55 66 77 88\"}", f, d));
  PRUEFE(!eigene_ok("{\"name\":\"t\",\"can_id\":\"0x602\",\"muster\":\"40\",\"lesen\":\"x\"}", f, d));
  PRUEFE(!eigene_ok("{\"name\":\"t\",\"can_id\":\"0x602\",\"muster\":\"40\",\"abstand_min\":0}", f, d));
  PRUEFE(!eigene_ok("{\"name\":\"t\",\"can_id\":\"0x602\",\"muster\":\"40\",\"foo\":1}", f, d));
  PRUEFE(!eigene_ok("{\"name\":\"a\",\"can_id\":\"0x602\",\"muster\":\"40\"},{\"name\":\"A\",\"can_id\":\"0x602\",\"muster\":\"40\"}", f, d));
  std::string neun;
  for (int i = 0; i < 9; i++) neun += std::string(i ? "," : "") + "{\"name\":\"n" + std::to_string(i) + "\",\"can_id\":\"0x602\",\"muster\":\"40\"}";
  PRUEFE(!eigene_ok(neun, f, d));
}

TEST(regeln_speicher_rundreise) {
  vd::Datei d;
  std::string f;
  PRUEFE(vd::datei_lesen(datei("regeln.json.example"), d, f));
  vd::eigene() = d.eigene;
  std::string s = vd::speicherform();
  PRUEFE(s.size() < 2400);                                   // passt in vd_speicher
  vd::eigene().clear();
  PRUEFE(vd::aus_speicher(s.c_str(), f) && vd::eigene().size() == 1);
  PRUEFE(vd::speicherform() == s);
  PRUEFE(vd::aus_speicher("", f) && vd::eigene().empty());
  PRUEFE(!vd::aus_speicher("{kaputt", f));
  PRUEFE(vd::fnv("a") == vd::fnv("a") && vd::fnv("a") != vd::fnv("b") && (vd::fnv("") & 1));
}

// ---------------------------------------------------------------- cmd/zusatz

TEST(zusatz_beispieldatei) {
  int v = 0;
  std::vector<zs::Eintrag> l;
  std::string f;
  PRUEFE(zs::datei_lesen(datei("zusatz.json.example"), v, l, f));
  PRUEFE(v == 1 && l.size() == 3 && l[2].laeuft && l[2].schwelle == 50.0f && l[1].feld.empty());
}

TEST(zusatz_ungueltig) {
  int v = 0;
  std::vector<zs::Eintrag> l;
  std::string f;
  PRUEFE(!zs::datei_lesen("{\"version\":1}", v, l, f));                                   // eintraege fehlt
  PRUEFE(!zs::datei_lesen("{\"eintraege\":[{\"name\":\"a\"}]}", v, l, f));                 // topic fehlt
  PRUEFE(!zs::datei_lesen("{\"eintraege\":[{\"name\":\"a\",\"topic\":\"x/#\"}]}", v, l, f));
  PRUEFE(!zs::datei_lesen("{\"eintraege\":[{\"name\":\"alle\",\"topic\":\"x\"}]}", v, l, f));
  PRUEFE(!zs::datei_lesen("{\"eintraege\":[{\"name\":\"a\",\"topic\":\"x\",\"art\":\"an\"}]}", v, l, f));
  PRUEFE(!zs::datei_lesen("{\"eintraege\":[{\"name\":\"a\",\"topic\":\"x\",\"einheit\":\"123456789\"}]}", v, l, f));
  PRUEFE(!zs::datei_lesen("{\"eintraege\":[],\"foo\":1}", v, l, f));
  PRUEFE(zs::datei_lesen("{\"eintraege\":[]}", v, l, f) && l.empty() && v == -1);
}

TEST(zusatz_web_befehl) {
  bool ok = false;
  zs::liste().clear();
  PRUEFE(enthaelt(zs::befehl("Wohn Zimmer topic=a/b feld=tC einheit=°C", ok), "hinzugefuegt") && ok);
  PRUEFE(zs::liste().size() == 1 && zs::liste()[0].name == "Wohn Zimmer" && zs::liste()[0].feld == "tC");
  PRUEFE(enthaelt(zs::befehl("wohn zimmer topic=a/c", ok), "ersetzt") && ok && zs::liste()[0].topic == "a/c");
  PRUEFE(enthaelt(zs::befehl("P topic=w art=laeuft schwelle=50", ok), "hinzugefuegt") && zs::liste()[1].laeuft);
  zs::befehl("X feld=a", ok);
  PRUEFE(!ok);                                               // topic fehlt
  zs::befehl("X topic=a schwelle=abc", ok);
  PRUEFE(!ok);
  zs::befehl("X topic=a farbe=rot", ok);
  PRUEFE(!ok);
  for (int i = 0; i < 4; i++) zs::befehl("E" + std::to_string(i) + " topic=t", ok);
  PRUEFE(zs::liste().size() == 6);
  PRUEFE(enthaelt(zs::befehl("E9 topic=t", ok), "schon 6") && !ok);
  PRUEFE(enthaelt(zs::befehl("P loeschen", ok), "geloescht") && ok && zs::liste().size() == 5);
  PRUEFE(enthaelt(zs::befehl("gibtsnicht loeschen", ok), "kein Eintrag") && !ok);
  PRUEFE(enthaelt(zs::befehl("alle loeschen", ok), "alle") && ok && zs::liste().empty());
  zs::befehl("   ", ok);
  PRUEFE(!ok);
}

TEST(zusatz_speicher_rundreise) {
  int v = 0;
  std::vector<zs::Eintrag> l;
  std::string f;
  PRUEFE(zs::datei_lesen(datei("zusatz.json.example"), v, l, f));
  zs::liste() = l;
  std::string s = zs::speicherform();
  PRUEFE(s.size() < 1600);                                   // passt in zs-Speicher
  zs::liste().clear();
  PRUEFE(zs::aus_speicher(s.c_str(), f) && zs::liste().size() == 3 && zs::speicherform() == s);
}

// ---------------------------------------------------------------- cmd/regel (NAME an|aus|loeschen)

TEST(regel_befehl_feste_regeln) {
  auto r = vd::befehl_zerlegen("R3 aus");
  PRUEFE(r.art == vd::BefehlArt::FEST && r.nr == 3 && r.aktion == vd::Aktion::AUS);
  r = vd::befehl_zerlegen("  r1   AN  ");
  PRUEFE(r.art == vd::BefehlArt::FEST && r.nr == 1 && r.aktion == vd::Aktion::AN && r.name == "r1");
  r = vd::befehl_zerlegen("R5 loeschen");                       // feste Regel: nur an/aus
  PRUEFE(r.art == vd::BefehlArt::FEHLER && enthaelt(r.fehler, "nur an/aus"));
  r = vd::befehl_zerlegen("R6 an");                             // R6 gibt es nicht -> eigene Regel "R6"
  PRUEFE(r.art == vd::BefehlArt::EIGENE && r.name == "R6");
}

TEST(regel_befehl_hauptschalter_und_eigene) {
  auto r = vd::befehl_zerlegen("eigene an");
  PRUEFE(r.art == vd::BefehlArt::HAUPT && r.aktion == vd::Aktion::AN);
  r = vd::befehl_zerlegen("Eigene loeschen");
  PRUEFE(r.art == vd::BefehlArt::FEHLER);
  r = vd::befehl_zerlegen("meine-regel.1 L\xc3\xb6schen");
  PRUEFE(r.art == vd::BefehlArt::EIGENE && r.aktion == vd::Aktion::LOESCHEN && r.name == "meine-regel.1");
  r = vd::befehl_zerlegen("verdacht an");                       // gibt es seit v23 nicht mehr
  PRUEFE(r.art == vd::BefehlArt::VERALTET && enthaelt(r.fehler, "seit v23"));
  r = vd::befehl_zerlegen("Hauptschalter aus");
  PRUEFE(r.art == vd::BefehlArt::VERALTET);
}

TEST(regel_befehl_ungueltig) {
  PRUEFE(vd::befehl_zerlegen("").art == vd::BefehlArt::FEHLER);
  PRUEFE(enthaelt(vd::befehl_zerlegen("R1").fehler, "Format"));
  PRUEFE(enthaelt(vd::befehl_zerlegen("R1 umschalten").fehler, "erlaubt: an, aus, loeschen"));
  PRUEFE(vd::befehl_zerlegen("zwei worte an").art == vd::BefehlArt::FEHLER);   // Leerzeichen im Namen
  PRUEFE(vd::befehl_zerlegen("ein-sehr-langer-regelname-xyz an").art == vd::BefehlArt::FEHLER);
}

// ---------------------------------------------------------------- Schaltbefehle (befehle.h)

TEST(befehl_klartext_wie_bisher) {
  bf::Anfrage a; std::string f;
  PRUEFE(bf::anfrage_lesen(bf::ZIEL_HK, "3", a, f) && a.wert == 3 && a.id.empty() && a.quelle == "MQTT");
  bf::Anfrage b;
  PRUEFE(bf::anfrage_lesen(bf::ZIEL_HK, "Zeitprogramm 2", b, f) && b.wert == 3);
  bf::Anfrage c;
  PRUEFE(bf::anfrage_lesen(bf::ZIEL_HK, " zp 1 ", c, f) && c.wert == 2);
  bf::Anfrage d;
  PRUEFE(bf::anfrage_lesen(bf::ZIEL_HK, "Standby", d, f) && d.wert == 1);
  bf::Anfrage e;
  PRUEFE(bf::anfrage_lesen(bf::ZIEL_WW, "Ein", e, f) && e.wert == 1);
  bf::Anfrage g;
  PRUEFE(bf::anfrage_lesen(bf::ZIEL_WW, "off", g, f) && g.wert == 2);
  bf::Anfrage h;
  PRUEFE(!bf::anfrage_lesen(bf::ZIEL_HK, "9", h, f) && enthaelt(f, "unbekannt"));
  bf::Anfrage i;
  PRUEFE(!bf::anfrage_lesen(bf::ZIEL_WW, "vielleicht", i, f));
  bf::Anfrage j;
  PRUEFE(!bf::anfrage_lesen(bf::ZIEL_HK, "   ", j, f));
}

TEST(befehl_json_mit_kennung) {
  bf::Anfrage a; std::string f;
  PRUEFE(bf::anfrage_lesen(bf::ZIEL_HK, "{\"id\":17,\"wert\":\"ZP2\"}", a, f) && a.wert == 3 && a.id == "17" && a.id_zahl);
  bf::Anfrage b;
  PRUEFE(bf::anfrage_lesen(bf::ZIEL_HK, "{\"id\":\"app-4\",\"wert\":6,\"quelle\":\"App\"}", b, f) &&
         b.wert == 6 && b.id == "app-4" && !b.id_zahl && b.quelle == "App");
  bf::Anfrage c;
  PRUEFE(bf::anfrage_lesen(bf::ZIEL_WW, "{\"wert\":false}", c, f) && c.wert == 2 && c.id.empty());
  bf::Anfrage d;   // ungueltiger Wert: Kennung bleibt fuer die Ablehnung erhalten
  PRUEFE(!bf::anfrage_lesen(bf::ZIEL_HK, "{\"id\":5,\"wert\":12}", d, f) && d.id == "5" && enthaelt(f, "1-8"));
  bf::Anfrage e;
  PRUEFE(!bf::anfrage_lesen(bf::ZIEL_HK, "{\"id\":5,\"wert\":true}", e, f));
  bf::Anfrage g;
  PRUEFE(!bf::anfrage_lesen(bf::ZIEL_HK, "{\"id\":5}", g, f) && enthaelt(f, "wert fehlt"));
  bf::Anfrage h;
  PRUEFE(!bf::anfrage_lesen(bf::ZIEL_HK, "{\"wert\":3,\"farbe\":1}", h, f) && enthaelt(f, "unbekanntes Feld"));
  bf::Anfrage i;
  PRUEFE(!bf::anfrage_lesen(bf::ZIEL_HK, "{\"wert\":3", i, f));
  bf::Anfrage j;
  PRUEFE(!bf::anfrage_lesen(bf::ZIEL_HK, "{\"id\":\"\",\"wert\":3}", j, f));
  bf::Anfrage k;
  PRUEFE(!bf::anfrage_lesen(bf::ZIEL_HK, "{\"wert\":3,\"quelle\":\"a;b\"}", k, f));
  bf::Anfrage l;
  PRUEFE(bf::anfrage_lesen(bf::ZIEL_HK, "{\"wert\":3,\"_kommentar\":\"x\"}", l, f));
}

static std::vector<std::string> phasen(bf::Schaltung &s) {
  std::vector<std::string> v;
  for (auto &e : s.ereignisse) v.push_back(e.id + ":" + e.phase);
  s.ereignisse.clear();
  return v;
}

TEST(befehl_phasen_erfolg) {
  bf::Schaltung s;
  s.uhr = 1790000000;
  bf::Anfrage a; a.id = "17"; a.id_zahl = true; a.wert = 3;
  s.annehmen(bf::ZIEL_HK, a, 42);
  PRUEFE(s.ereignisse.size() == 2 && s.ereignisse[1].warten_s == 42);
  PRUEFE((phasen(s) == std::vector<std::string>{"17:angenommen", "17:vorgemerkt"}));
  bf::Befehl b;
  PRUEFE(s.naechster(b) && b.ziel == bf::ZIEL_HK && b.wert == 3 && s.laeuft);
  PRUEFE(!s.naechster(b));                                    // laeuft schon
  s.wem_antwort(bf::Wem::BESTAETIGT, "");
  PRUEFE(!s.bus_wert(bf::ZIEL_WW, 1));                        // anderes Ziel: nichts
  PRUEFE(s.bus_wert(bf::ZIEL_HK, 3) && !s.laeuft);
  auto p = phasen(s);
  PRUEFE((p == std::vector<std::string>{"17:gesendet", "17:pruefe_bus", "17:bestaetigt"}));
  PRUEFE(s.hat_letzte[bf::ZIEL_HK] && s.letzte[bf::ZIEL_HK].phase == "bestaetigt" && s.letzte[bf::ZIEL_HK].ist == 3);
  PRUEFE(s.letzte[bf::ZIEL_HK].wem == "bestaetigt" && s.letzte[bf::ZIEL_HK].zeit == 1790000000u);
}

TEST(befehl_phasen_fehlschlaege) {
  bf::Schaltung s;
  bf::Anfrage a; a.wert = 2;
  s.annehmen(bf::ZIEL_WW, a, 0);
  bf::Befehl b;
  s.naechster(b);
  s.wem_antwort(bf::Wem::CM05, "VG 05...");
  PRUEFE(!s.laeuft && s.letzte[bf::ZIEL_WW].phase == "gescheitert" && s.letzte[bf::ZIEL_WW].grund == "cm05");
  PRUEFE(!s.bus_wert(bf::ZIEL_WW, 2));                        // nach CM=05 keine Buspruefung mehr
  s.ereignisse.clear();
  s.annehmen(bf::ZIEL_WW, a, 0);
  s.naechster(b);
  s.wem_antwort(bf::Wem::NICHT_ERREICHBAR, "");
  PRUEFE(s.laeuft && s.lauf.phase == "pruefe_bus" && s.lauf.wem == "nicht_erreichbar");
  PRUEFE(s.bus_wert(bf::ZIEL_WW, 1));                         // steht auf Ein, gewuenscht Aus
  PRUEFE(s.letzte[bf::ZIEL_WW].grund == "nicht_uebernommen" && s.letzte[bf::ZIEL_WW].ist == 1 &&
         enthaelt(s.letzte[bf::ZIEL_WW].text, "Ein"));
  s.annehmen(bf::ZIEL_WW, a, 0);
  s.naechster(b);
  s.wem_antwort(bf::Wem::UNKLAR, "HTTP 200, VG leer");
  s.keine_rueckmeldung();
  PRUEFE(!s.laeuft && s.letzte[bf::ZIEL_WW].grund == "keine_rueckmeldung" && s.letzte[bf::ZIEL_WW].wem == "unklar");
  // Kennungen ohne mitgeschickte id sind fortlaufend
  PRUEFE(s.ereignisse.front().id == "b2" && s.ereignisse.back().id == "b3");
}

TEST(befehl_neuester_wunsch_gewinnt_und_reihenfolge) {
  bf::Schaltung s;
  bf::Anfrage ww; ww.id = "w1"; ww.wert = 1;
  bf::Anfrage h1; h1.id = "h1"; h1.wert = 2;
  bf::Anfrage h2; h2.id = "h2"; h2.wert = 4;
  s.annehmen(bf::ZIEL_WW, ww, 30);
  s.annehmen(bf::ZIEL_HK, h1, 30);
  s.ereignisse.clear();
  s.annehmen(bf::ZIEL_HK, h2, 30);
  auto p = phasen(s);
  PRUEFE((p == std::vector<std::string>{"h2:angenommen", "h1:ersetzt", "h2:vorgemerkt"}));
  PRUEFE(!s.hat_letzte[bf::ZIEL_HK]);                         // ersetzt zaehlt nicht als "letzte"
  bf::Befehl b;
  PRUEFE(s.naechster(b) && b.id == "h2");                     // Heizkreis zuerst
  s.wem_antwort(bf::Wem::BESTAETIGT, "");
  s.bus_wert(bf::ZIEL_HK, 4);
  PRUEFE(s.naechster(b) && b.id == "w1");
  s.ereignisse.clear();
  s.annehmen(bf::ZIEL_HK, h1, 60);
  PRUEFE(s.leeren() == 1 && !s.hat_wunsch[bf::ZIEL_HK]);
  PRUEFE(s.ereignisse.back().phase == "gescheitert" && s.ereignisse.back().grund == "geleert");
}

TEST(befehl_abgelehnt_vor_dem_senden) {
  bf::Schaltung s;
  bf::Anfrage a; std::string f;
  PRUEFE(!bf::anfrage_lesen(bf::ZIEL_HK, "{\"id\":99,\"wert\":\"Turbo\"}", a, f));
  s.ablehnen(bf::ZIEL_HK, a, f);
  PRUEFE(s.ereignisse.size() == 1 && s.ereignisse[0].phase == "abgelehnt" && s.ereignisse[0].id == "99");
  bf::Befehl b;
  PRUEFE(!s.hat_wunsch[bf::ZIEL_HK] && !s.naechster(b));     // nichts vorgemerkt -> nichts an den WEM
  std::string j = bf::befehl_json(s.ereignisse[0]);
  JsonDocument d;
  PRUEFE(!deserializeJson(d, j));
  PRUEFE(d["id"].is<long>() && d["id"].as<long>() == 99 && d["phase"] == "abgelehnt" && d["grund"] == "ungueltig");
  PRUEFE(d["ziel"] == "heizkreis" && d["wert"].isNull() && d["ende"] == true && d["zeit"].isNull());
}

TEST(befehl_json_felder_fest) {
  bf::Schaltung s;
  s.uhr = 1790000100;
  bf::Anfrage a; a.id = "x-1"; a.wert = 7; a.quelle = "App";
  s.annehmen(bf::ZIEL_HK, a, 12);
  JsonDocument d;
  PRUEFE(!deserializeJson(d, bf::befehl_json(s.ereignisse.back())));
  const char *felder[] = {"id", "ziel", "wert", "wert_text", "quelle", "phase", "ende", "grund", "text", "wem",
                          "ist", "ist_text", "warten_s", "ersetzt_durch", "zeit"};
  for (auto k : felder) PRUEFE(!d[k].isUnbound());
  PRUEFE(d["id"] == "x-1" && d["wert_text"] == "Normal" && d["quelle"] == "App" && d["warten_s"] == 12);
  PRUEFE(d["phase"] == "vorgemerkt" && d["ende"] == false && d["zeit"] == 1790000100u);
}

TEST(status_json_aufbau) {
  bf::Status st;
  bf::Schaltung s;
  JsonDocument d;
  PRUEFE(!deserializeJson(d, bf::status_json(st, s, "v27", 0, -1)));
  PRUEFE(d["v"] == 1 && d["firmware"] == "v27" && d["zeit"].isNull());
  PRUEFE(d["heizkreis"]["vorgabe"].isNull() && d["heizkreis"]["ist"].isNull() && !d["heizkreis"]["quelle"].isUnbound());
  PRUEFE(d["warmwasser"]["ladung"].isNull() && d["bus"]["lebt"] == false && d["bus"]["letzter_frame_s"].isNull());
  PRUEFE(d["schalten"]["laufend"].isNull() && d["schalten"]["warteschlange"].size() == 0);
  PRUEFE(d["schalten"]["letzte"]["heizkreis"].isNull() && d["schalten"]["frei_ab"].isNull());

  st.anlass("regel R3", true, false);
  st.hk_gelesen(3, 1790000000);
  st.hk_bits = 0x0040;
  st.ww_gelesen(2, 1790000005);
  st.ww_soll_akt = 8.04f;
  st.kesselstatus = 15;
  st.bus_lebt = true;
  st.frei_ab = 1790000030;
  bf::Anfrage a; a.id = "17"; a.id_zahl = true; a.wert = 1;
  s.annehmen(bf::ZIEL_WW, a, 20);
  JsonDocument e;
  PRUEFE(!deserializeJson(e, bf::status_json(st, s, "v27", 1790000010, 1)));
  PRUEFE(e["heizkreis"]["vorgabe"] == 3 && e["heizkreis"]["vorgabe_text"] == "Zeitprogramm 2");
  PRUEFE(e["heizkreis"]["ist"] == "zeitprogramm" && e["heizkreis"]["heizt"] == true && e["heizkreis"]["quelle"] == "regel R3");
  PRUEFE(e["warmwasser"]["vorgabe_text"] == "Aus" && e["warmwasser"]["quelle"] == "lesung" && e["warmwasser"]["ladung"] == true);
  PRUEFE(std::fabs(e["warmwasser"]["soll_aktuell"].as<double>() - 8.0) < 1e-9);
  PRUEFE(e["kessel"]["status_text"] == "Warmwasserbetrieb" && e["bus"]["letzter_frame_s"] == 1);
  PRUEFE(e["schalten"]["warteschlange"].size() == 1 && e["schalten"]["warteschlange"][0]["id"] == 17);
  PRUEFE(e["schalten"]["frei_ab"] == 1790000030u);
  PRUEFE(st.grund_hk.empty());                                // Anlass bei der Lesung verbraucht
  JsonDocument g;                                             // Sperrminute vorbei: frei_ab null
  PRUEFE(!deserializeJson(g, bf::status_json(st, s, "v27", 1790000031, 1)) && g["schalten"]["frei_ab"].isNull());
}

TEST(status_json_drossel) {
  bf::Status st;
  PRUEFE(bf::senden_faellig(st, 1000));                       // Start: einmal senden
  st.geaendert = false; st.zuletzt_ms = 1000;
  PRUEFE(!bf::senden_faellig(st, 1500));                      // nichts geaendert
  st.setze(st.kesselstatus, 10);
  PRUEFE(st.geaendert && !bf::senden_faellig(st, 2500));      // geaendert, aber < 2 s
  PRUEFE(bf::senden_faellig(st, 3000));
  st.geaendert = false;
  st.setze(st.kesselstatus, 10);                              // gleicher Wert: keine Aenderung
  PRUEFE(!st.geaendert);
  st.setze_f(st.ww_soll_akt, 50.0f);
  PRUEFE(st.geaendert);
  st.geaendert = false;
  st.setze_f(st.ww_soll_akt, 50.01f);                         // Rauschen unter 0,05 K
  PRUEFE(!st.geaendert);
  PRUEFE(bf::senden_faellig(bf::Status(), 0xFFFFFFF0u));      // millis()-Ueberlauf egal
}

// ---------------------------------------------------------------- Registertabelle (v28)
// Die Werte aus register.yaml (pakete/register_gen.h) muessen EXAKT dem entsprechen, was bis v27
// fest in den YAML-Lambdas stand - sonst hat sich das Verhalten der Firmware geaendert.
// Die erwarteten Bytes sind bewusst hier noch einmal von Hand hingeschrieben (Stand v27).

static bool bytes_gleich(const std::vector<uint8_t> &a, std::vector<uint8_t> b) { return a == b; }

TEST(register_leseanfragen_wie_v27) {
  // kessel.yaml, 40 s / 60 s / 5 min und Anlass (can_id 0x602 bzw. 0x601)
  PRUEFE(bytes_gleich(reg::lese(reg::KESSEL_TEMP),    {0x40, 0x32, 0x25, 0x00, 0, 0, 0, 0}));
  PRUEFE(bytes_gleich(reg::lese(reg::ABGAS),          {0x40, 0x37, 0x25, 0x00, 0, 0, 0, 0}));
  PRUEFE(bytes_gleich(reg::lese(reg::VORLAUF),        {0x40, 0x36, 0x25, 0x00, 0, 0, 0, 0}));
  PRUEFE(bytes_gleich(reg::lese(reg::LEISTUNG),       {0x40, 0x34, 0x25, 0x00, 0, 0, 0, 0}));
  PRUEFE(bytes_gleich(reg::lese(reg::DREHZAHL),       {0x40, 0x40, 0x25, 0x00, 0, 0, 0, 0}));
  PRUEFE(bytes_gleich(reg::lese(reg::BRENNER),        {0x40, 0x41, 0x25, 0x00, 0, 0, 0, 0}));
  PRUEFE(bytes_gleich(reg::lese(reg::VORLAUF_SOLL),   {0x40, 0x45, 0x25, 0x00, 0, 0, 0, 0}));
  PRUEFE(bytes_gleich(reg::lese(reg::VOLUMENSTROM),   {0x40, 0x13, 0x27, 0x02, 0, 0, 0, 0}));
  PRUEFE(bytes_gleich(reg::lese(reg::DRUCK),          {0x40, 0x14, 0x27, 0x02, 0, 0, 0, 0}));
  PRUEFE(bytes_gleich(reg::lese(reg::HK_VORLAUF),     {0x40, 0x07, 0x29, 0x02, 0, 0, 0, 0}));
  PRUEFE(bytes_gleich(reg::lese(reg::VL_SOLL_ANF),    {0x40, 0x40, 0x26, 0x03, 0, 0, 0, 0}));
  PRUEFE(bytes_gleich(reg::lese(reg::RAUMSOLL),       {0x40, 0x58, 0x29, 0x02, 0, 0, 0, 0}));
  PRUEFE(bytes_gleich(reg::lese(reg::HK_BETRIEBSART), {0x40, 0x33, 0x29, 0x02, 0, 0, 0, 0}));
  // warmwasser.yaml
  PRUEFE(bytes_gleich(reg::lese(reg::WW_BETRIEBSART), {0x40, 0x20, 0x2A, 0x02, 0, 0, 0, 0}));
  PRUEFE(bytes_gleich(reg::lese(reg::WW_SOLL_AKT),    {0x40, 0x2C, 0x2A, 0x02, 0, 0, 0, 0}));
  PRUEFE(bytes_gleich(reg::lese(reg::WW_SOLL_NORMAL), {0x40, 0x39, 0x2A, 0x02, 0, 0, 0, 0}));
  // Knoten: 0x602 Kessel, 0x601 WEM - nie ein anderes Ziel, nie Knoten 2 beschreiben
  for (const reg::Sdo *o : {&reg::KESSEL_TEMP, &reg::ABGAS, &reg::VORLAUF, &reg::LEISTUNG, &reg::DREHZAHL,
                            &reg::BRENNER, &reg::VORLAUF_SOLL, &reg::VOLUMENSTROM, &reg::DRUCK})
    PRUEFE(reg::anfrage_id(*o) == 0x602 && reg::antwort_id(*o) == 0x582);
  for (const reg::Sdo *o : {&reg::HK_VORLAUF, &reg::VL_SOLL_ANF, &reg::RAUMSOLL, &reg::HK_BETRIEBSART,
                            &reg::WW_BETRIEBSART, &reg::WW_SOLL_AKT, &reg::WW_SOLL_NORMAL})
    PRUEFE(reg::anfrage_id(*o) == 0x601 && reg::antwort_id(*o) == 0x581);
}

TEST(register_faktoren_wie_v27) {
  // exakt dieselben float-Konstanten wie bisher in den Lambdas
  PRUEFE(reg::DRUCK.faktor == 0.01f && reg::LEISTUNG.faktor == 0.01f && reg::SOLLLEISTUNG.faktor == 0.01f);
  PRUEFE(reg::WM_HEIZUNG.faktor == 0.01f && reg::WM_WW.faktor == 0.01f && reg::WM_GESAMT.faktor == 0.01f);
  for (const reg::Sdo *o : {&reg::VORLAUF, &reg::ABGAS, &reg::KESSEL_TEMP, &reg::VORLAUF_SOLL, &reg::RUECKLAUF_VPT,
                            &reg::VORLAUF_VPT, &reg::VL_SOLL_ANF, &reg::RAUMSOLL, &reg::HK_VORLAUF,
                            &reg::WW_SOLL_AKT, &reg::WW_SOLL_NORMAL})
    PRUEFE(o->faktor == 0.1f);
  // (float) v wie bisher
  for (int32_t v : {0, 1, 1234, -5, 65535, 2147483000})
    PRUEFE(reg::wert(reg::DREHZAHL, v) == (float) v && reg::wert(reg::VOLUMENSTROM, v) == (float) v);
  for (int32_t v : {0, 243, -71, 30000, 123456})
    PRUEFE(reg::wert(reg::DRUCK, v) == v * 0.01f && reg::wert(reg::KESSEL_TEMP, v) == v * 0.1f);
  // Objekte, die die Lambdas vergleichen
  PRUEFE(reg::ist(reg::DRUCK, 0x2714, 2) && !reg::ist(reg::DRUCK, 0x2714, 0));
  PRUEFE(reg::ist(reg::BRENNER, 0x2541, 0) && reg::ist(reg::STATUSBITS, 0x274D, 0));
  PRUEFE(reg::ist(reg::WM_HEIZUNG, 0x2726, 2) && reg::ist(reg::WM_WW, 0x2727, 2) && reg::ist(reg::WM_GESAMT, 0x2728, 2));
  PRUEFE(reg::ist(reg::RUECKLAUF_VPT, 0x2699, 1) && reg::ist(reg::VORLAUF_VPT, 0x2697, 1) && reg::ist(reg::SOLLLEISTUNG, 0x2698, 1));
  PRUEFE(reg::ist(reg::R1_A, 0x2101, 0x0A) && reg::ist(reg::R1_B, 0x2102, 0x0D) && reg::ist(reg::R1_C, 0x2102, 0x01));
  PRUEFE(reg::W_252B.index == 0x252B && reg::HK_BETRIEBSART.index == 0x2933 && reg::WW_BETRIEBSART.index == 0x2A20);
}

TEST(register_pdos_wie_v27) {
  std::vector<uint8_t> x = {0x03, 0x2C, 0x01, 0xAB, 0xFF, 0x12, 0x34, 0x02};
  PRUEFE(reg::pdo_wert(reg::AUSSENTEMP, x) == (int16_t(x[2] << 8 | x[1])) * 0.1f);
  PRUEFE(reg::pdo_wert(reg::VORLAUFSOLL_HZ, x) == (int16_t(x[1] << 8 | x[0])) * 0.1f);
  PRUEFE(reg::pdo_wert(reg::KESSEL_TEMP_PDO, x) == (int16_t(x[3] << 8 | x[2])) * 0.1f);
  PRUEFE(reg::pdo_wert(reg::WARMWASSER, x) == (int16_t(x[7] << 8 | x[6])) * 0.1f);
  PRUEFE(reg::pdo_wert(reg::AUSSENTEMP, x) == rl::pdo_temp(x[1], x[2]));
  PRUEFE(reg::SYSTEMBETRIEBSART.can_id == 0x201 && reg::SYSTEMBETRIEBSART.byte == 0);
  PRUEFE(reg::KESSELSTATUS.can_id == 0x182 && reg::KESSELSTATUS.byte == 0);
  PRUEFE(reg::STATUSBITS_PDO.can_id == 0x1C1 && reg::STATUSBITS_PDO.byte == 2);
  PRUEFE(reg::AUSSENTEMP.can_id == 0x201 && reg::WARMWASSER.can_id == 0x241 && reg::VORLAUFSOLL_HZ.can_id == 0x241);
}

TEST(register_json_schreiben_wie_v27) {
  char b[24], alt[24];
  for (unsigned w = 0; w <= 9; w++) {
    reg::json_vg_schreiben(b, sizeof(b), reg::HK_BETRIEBSART_JSON, w);
    snprintf(alt, sizeof(alt), "0302002533020001%02X", w);
    PRUEFE(std::string(b) == alt);
    reg::json_vg_schreiben(b, sizeof(b), reg::WW_BETRIEBSART_JSON, w);
    snprintf(alt, sizeof(alt), "0303002520020001%02X", w);
    PRUEFE(std::string(b) == alt);
  }
  // geschrieben wird nur im Systemgeraet (MI 01-03), nie der Kessel
  PRUEFE(reg::HK_BETRIEBSART_JSON.mi == 0x02 && reg::WW_BETRIEBSART_JSON.mi == 0x03);
}

#include "test_kern.inc"

int main() {
  for (auto &t : tests()) {
    g_test = t.name;
    int vorher = g_fehler;
    t.f();
    std::printf("%s %s\n", g_fehler == vorher ? "ok  " : "ROT ", t.name);
  }
  std::printf("\n%zu Tests, %d Pruefungen, %d Fehler\n", tests().size(), g_pruef, g_fehler);
  return g_fehler ? 1 : 0;
}
