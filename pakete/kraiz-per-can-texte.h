// ERZEUGT von werkzeuge/kraiz-per-can/kraiz_erzeugen.js aus kraiz/hassio-weishaupt (sensors.py) - NICHT VON HAND AENDERN.
// Wertetexte aus kraiz/hassio-weishaupt: MIT, (c) 2026 Lars Kreisz (LIZENZEN-DRITTER.md).
#pragma once
#include <cstdio>
#include <string>

namespace kc {

inline std::string text_BETRIEBSART_AKTUELL_MAP(int v) {
  switch (v) {
    case 1: return "System Standby";
    case 2: return "System Sommer";
    case 3: return "Estrich: Funktionsheizen";
    case 4: return "Estrich: Belegreifheizen";
    case 5: return "Urlaub";
    case 6: return "Zeitprogramm 1";
    case 7: return "Zeitprogramm 2";
    case 8: return "Zeitprogramm 3";
    case 9: return "Sommer";
    case 10: return "Absenk";
    case 11: return "Normal";
    case 12: return "Komfort";
    case 13: return "Standby";
    default: { char b[32]; snprintf(b, sizeof(b), "Unbekannt (%d)", v); return b; }
  }
}

inline std::string text_STATUS_HK_MAP(int v) {
  switch (v) {
    case 1: return "Raumfrostschutz";
    case 2: return "Anlagenfrostschutz";
    case 16: return "Eingang H1: Sonder";
    case 17: return "Eingang H1: Komfort";
    case 18: return "Eingang H1: Normal";
    case 19: return "Eingang H1: Absenk";
    case 20: return "Eingang H1: Standby";
    case 21: return "Eingang H1: Not-Aus";
    case 24: return "Party";
    case 32: return "Optimierung";
    case 33: return "Niveauanhebung Außentemperatur";
    case 48: return "ext. Notabnahme";
    case 49: return "ext. Überangebot";
    case 50: return "ext. Heizvorgabe";
    case 51: return "Warmwasservorrang";
    case 64: return "Sommerbetrieb witterungsgeführt";
    case 65: return "Heizgrenze Raum";
    case 66: return "Heizgrenze VL";
    case 67: return "Raumthermostat";
    case 80: return "Komfort";
    case 81: return "Normal";
    case 82: return "Absenken";
    case 83: return "Frost";
    case 84: return "Frostschutz aktiv";
    default: { char b[32]; snprintf(b, sizeof(b), "Unbekannt (%d)", v); return b; }
  }
}

inline std::string text_SOWI_UMSCHALTUNG_MAP(int v) {
  switch (v) {
    case 0: return "Aus";
    case 1: return "Ein";
    default: { char b[32]; snprintf(b, sizeof(b), "Unbekannt (%d)", v); return b; }
  }
}

inline std::string text_STATUS_WW_MAP(int v) {
  switch (v) {
    case 0: return "Standby: inaktiv";
    case 1: return "Zeitprogramm – Normal";
    case 2: return "Zeitprogramm – Absenk";
    case 3: return "Warmwasserladung aktiv";
    case 4: return "Service";
    case 5: return "Übertemperatur Alternativenergie";
    case 6: return "Überschuss Alternativenergie";
    case 7: return "Akzeptanz WW Alternativenergie";
    case 8: return "Frostschutz";
    case 9: return "Standby durch Systemprogrammschalter";
    case 10: return "Standby durch Eingang H2";
    case 12: return "Normal durch Eingang H2";
    case 13: return "Absenk durch Eingang H2";
    default: { char b[32]; snprintf(b, sizeof(b), "Unbekannt (%d)", v); return b; }
  }
}

inline std::string text_PUMPE_MAP(int v) {
  switch (v) {
    case 0: return "Aus";
    case 1: return "Ein";
    default: { char b[32]; snprintf(b, sizeof(b), "Unbekannt (%d)", v); return b; }
  }
}

inline std::string text_FOLGEWECHSEL_MAP(int v) {
  switch (v) {
    case 0: return "normal";
    case 1: return "invertiert";
    case 2: return "zeitlich";
    case 3: return "Betriebsstunden";
    default: { char b[32]; snprintf(b, sizeof(b), "Unbekannt (%d)", v); return b; }
  }
}

inline std::string text_BETRIEBSPHASE_WTC_MAP(int v) {
  switch (v) {
    case 0: return "Normalbetrieb";
    case 10: return "Pumpennachlauf";
    case 15: return "Brennertaktsperre Heizen";
    case 20: return "Sperre Mindestheizleistung";
    case 24: return "Adaption Gasstellglied läuft";
    case 25: return "Verzögerter Heizbetrieb";
    case 30: return "Softstart Warmwasser";
    case 35: return "Abregelfunktion Fernsteuerung";
    case 40: return "Spreizung Vorlauf/Abgas";
    case 45: return "Spreizung Vorlauf/Rücklauf";
    case 50: return "Abregelfunktion Abgastemperatur";
    case 55: return "Abschaltung Fernsteuerung";
    case 60: return "Abschaltung Mindestumlauf";
    case 70: return "SCOT Kalibration läuft";
    default: { char b[32]; snprintf(b, sizeof(b), "Unbekannt (%d)", v); return b; }
  }
}

}  // namespace kc
