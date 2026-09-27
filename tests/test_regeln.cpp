// Tests der Regellogik (pakete/regellogik.h) und der Datei-Parser (pakete/verdacht.h,
// pakete/zusatz.h) - laeuft auf dem PC, ohne ESPHome, ohne Board, ohne Bus.
// Alle Frames sind AUSGEDACHT nach den Mustern in PROTOKOLL.md, keine Mitschnitte.
//
// Bauen und starten: tests/run.sh
#include "../pakete/regellogik.h"
#include "../pakete/verdacht.h"
#include "../pakete/zusatz.h"
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
