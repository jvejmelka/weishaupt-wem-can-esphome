// ERZEUGT von werkzeuge/register_erzeugen.py aus register.yaml - NICHT VON HAND AENDERN.
// Aenderungen in register.yaml, dann: python3 werkzeuge/register_erzeugen.py
//
// Objekte am CAN-Bus der Weishaupt WTC-GW (Knoten 1 = WEM-Systemgeraet, Knoten 2 = Kessel).
// Reines C++ ohne ESPHome, damit auch die Tests es einbinden koennen.
#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>

namespace reg {

// SDO-Objekt: Knoten (0 = nur als Telegramm mitgelesen), Index, Subindex, Faktor
struct Sdo { uint8_t knoten; uint16_t index; uint8_t sub; float faktor; };
// Wert in einem PDO/Telegramm: CAN-ID, erstes Byte, Laenge in Byte, Faktor
struct Pdo { uint16_t can_id; uint8_t byte; uint8_t laenge; float faktor; };
// dasselbe Objekt ueber die WEM-JSON-Schnittstelle
struct Json { uint8_t mi; uint8_t mx; uint16_t ox; uint8_t os; uint8_t vs; };

constexpr uint16_t anfrage_id(const Sdo &o) { return 0x600 + o.knoten; }
constexpr uint16_t antwort_id(const Sdo &o) { return 0x580 + o.knoten; }
// Lese-Anfrage (SDO-Upload, Kommandobyte 0x40) - das Einzige, was die Firmware auf den Bus schickt
inline std::vector<uint8_t> lese(const Sdo &o) {
  return {0x40, (uint8_t) (o.index & 0xFF), (uint8_t) (o.index >> 8), o.sub, 0, 0, 0, 0};
}
constexpr bool ist(const Sdo &o, uint16_t idx, uint8_t sub) { return idx == o.index && sub == o.sub; }
inline float wert(const Sdo &o, int32_t roh) { return roh * o.faktor; }
// int16 little-endian an der Stelle des PDO-Werts
inline int16_t pdo_i16(const Pdo &p, const std::vector<uint8_t> &x) {
  return int16_t(x[p.byte + 1] << 8 | x[p.byte]);
}
inline float pdo_wert(const Pdo &p, const std::vector<uint8_t> &x) { return pdo_i16(p, x) * p.faktor; }
// VG-Feld eines JSON-Schreibbefehls (CM 03), z.B. 0302002533020001 + Wert
inline void json_vg_schreiben(char *b, size_t n, const Json &j, unsigned wert) {
  snprintf(b, n, "03%02X%02X%04X%02X%04X%02X", j.mi, j.mx, j.ox, j.os, j.vs, wert);
}

constexpr Pdo AUSSENTEMP{0x201, 1, 2, 0.1f};  // Außentemperatur, int16
constexpr Pdo SYSTEMBETRIEBSART{0x201, 0, 1, 1.0f};  // Systembetriebsart
constexpr Pdo VORLAUFSOLL_HZ{0x241, 0, 2, 0.1f};  // Vorlaufsoll Heizkreis, int16 – derselbe Wert, den der WEM in 0x252C schreibt (s. 0x602); 0 = keine Anforderung
constexpr Pdo KESSEL_TEMP_PDO{0x241, 2, 2, 0.1f};  // Kesseltemperatur (gleich 0x2532)
constexpr Pdo WARMWASSER{0x241, 6, 2, 0.1f};  // Warmwassertemperatur
constexpr Pdo UHRZEIT{0x181, 0, 6, 1.0f};  // Stunde, Minute, Jahr − 2000, Monat, Tag, Wochentag
constexpr Pdo KESSELSTATUS{0x182, 0, 1, 1.0f};  // Kesselstatus (Objekt 0x2530)
constexpr Pdo STATUSBITS_PDO{0x1C1, 2, 2, 1.0f};  // Statusbits Knoten 1 (Objekt 0x274D): 0x1000 Heizkreis Standby, 0x0040 Heizbetrieb, 0x0010 Warmwasser-Ladung ¹
constexpr Sdo W_252B{2, 0x252B, 0x00, 1.0f};  // Anforderung an den Kessel
constexpr Sdo W_252C{2, 0x252C, 0x00, 1.0f};  // Vorlaufsoll Heizkreis (erscheint auch in PDO 0x241 B0–1)
constexpr Sdo W_252D{2, 0x252D, 0x00, 1.0f};  // steigt beim Warmwasserladen rampenförmig (auf 50 °C beobachtet)
constexpr Sdo W_2709{2, 0x2709, 0x00, 1.0f};  // 0x64 Heizanforderung aktiv, 0x32 Nachlauf
constexpr Sdo RUECKLAUF_VPT{0, 0x2699, 0x01, 0.1f};  // Rücklauftemperatur VPT
constexpr Sdo VORLAUF_VPT{0, 0x2697, 0x01, 0.1f};  // Vorlauftemperatur VPT
constexpr Sdo SOLLLEISTUNG{0, 0x2698, 0x01, 0.01f};  // Sollleistung
constexpr Sdo KESSEL_TEMP{2, 0x2532, 0x00, 0.1f};  // Kesseltemperatur
constexpr Json KESSEL_TEMP_JSON{0x07, 0x00, 0x2532, 0x00, 2};
constexpr Sdo ABGAS{2, 0x2537, 0x00, 0.1f};  // Abgastemperatur
constexpr Sdo VORLAUF{2, 0x2536, 0x00, 0.1f};  // Vorlauf
constexpr Sdo LEISTUNG{2, 0x2534, 0x00, 0.01f};  // Leistung
constexpr Sdo DREHZAHL{2, 0x2540, 0x00, 1.0f};  // Drehzahl
constexpr Sdo BRENNER{2, 0x2541, 0x00, 1.0f};  // Betriebsphase Brenner
constexpr Json BRENNER_JSON{0x07, 0x00, 0x2541, 0x00, 1};
constexpr Sdo VORLAUF_SOLL{2, 0x2545, 0x00, 0.1f};  // Vorlauf Soll
constexpr Json VORLAUF_SOLL_JSON{0x07, 0x00, 0x2545, 0x00, 2};
constexpr Sdo VOLUMENSTROM{2, 0x2713, 0x02, 1.0f};  // Volumenstrom
constexpr Json VOLUMENSTROM_JSON{0x09, 0x01, 0x2613, 0x02, 2};
constexpr Sdo DRUCK{2, 0x2714, 0x02, 0.01f};  // Anlagendruck
constexpr Json DRUCK_JSON{0x09, 0x01, 0x2614, 0x02, 2};
constexpr Sdo WM_HEIZUNG{2, 0x2726, 0x02, 0.01f};  // Wärmemenge Vortag Heizung
constexpr Json WM_HEIZUNG_JSON{0x09, 0x01, 0x2626, 0x02, 4};
constexpr Sdo WM_WW{2, 0x2727, 0x02, 0.01f};  // Wärmemenge Vortag Warmwasser
constexpr Json WM_WW_JSON{0x09, 0x01, 0x2627, 0x02, 4};
constexpr Sdo WM_GESAMT{2, 0x2728, 0x02, 0.01f};  // Wärmemenge Vortag gesamt
constexpr Json WM_GESAMT_JSON{0x09, 0x01, 0x2628, 0x02, 4};
constexpr Sdo WAERMELEISTUNG{2, 0x2731, 0x02, 0.01f};  // Wärmeleistung aktuell
constexpr Json WAERMELEISTUNG_JSON{0x09, 0x01, 0x2631, 0x02, 4};
constexpr Sdo RUECKLAUF_KANDIDAT{2, 0x2533, 0x02, 0.1f};  // Rücklauftemperatur VPT, Kandidat
constexpr Sdo R1_A{2, 0x2101, 0x0A, 1.0f};  // 1. Glied der Warmwasser-Folge
constexpr Sdo R1_B{2, 0x2102, 0x0D, 1.0f};  // 2. Glied der Warmwasser-Folge
constexpr Sdo R1_C{2, 0x2102, 0x01, 1.0f};  // 3. Glied der Warmwasser-Folge
constexpr Sdo HK_VORLAUF{1, 0x2907, 0x02, 0.1f};  // Vorlauf Heizkreis
constexpr Json HK_VORLAUF_JSON{0x02, 0x00, 0x2507, 0x02, 2};
constexpr Sdo VL_SOLL_ANF{1, 0x2640, 0x03, 0.1f};  // Vorlaufsoll Anforderung
constexpr Sdo RAUMSOLL{1, 0x2958, 0x02, 0.1f};  // Raumsoll aktuell
constexpr Json RAUMSOLL_JSON{0x02, 0x00, 0x2558, 0x02, 2};
constexpr Sdo STATUSBITS{1, 0x274D, 0x00, 1.0f};  // Statusbits Knoten 1 (Objekt; ausgewertet wird der PDO 0x1C1, s. STATUSBITS_PDO)
constexpr Sdo HK_BETRIEBSART{1, 0x2933, 0x02, 1.0f};  // Heizkreis-Betriebsart
constexpr Json HK_BETRIEBSART_JSON{0x02, 0x00, 0x2533, 0x02, 1};
constexpr Sdo WW_BETRIEBSART{1, 0x2A20, 0x02, 1.0f};  // Warmwasser-Betriebsart
constexpr Json WW_BETRIEBSART_JSON{0x03, 0x00, 0x2520, 0x02, 1};
constexpr Sdo WW_SOLL_AKT{1, 0x2A2C, 0x02, 0.1f};  // Warmwasser Soll aktuell
constexpr Json WW_SOLL_AKT_JSON{0x03, 0x00, 0x252C, 0x02, 2};
constexpr Sdo WW_SOLL_NORMAL{1, 0x2A39, 0x02, 0.1f};  // Warmwasser Soll normal
constexpr Json WW_SOLL_NORMAL_JSON{0x03, 0x00, 0x2539, 0x02, 2};

}  // namespace reg
