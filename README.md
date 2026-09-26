# Weishaupt WEM am CAN-Bus – ESPHome-Firmware

Firmware für ein **WeAct CAN485 DevBoard V1** (ESP32, galvanisch getrennter CAN-Transceiver),
das am CAN-Bus einer Weishaupt-Brennwertheizung mit **WEM-Systemgerät** (z. B. WTC-GW 15-B) mithört.

- **Lesen am Bus**, ohne den WEM zu belasten: Kessel-, Vorlauf-, Abgas-, Warmwasser- und
  Außentemperatur, Druck, Leistung, Brennerstatus mit Startzähler, Betriebsarten, Wärmemengen.
- **Schalten über die JSON-Schnittstelle des WEM**, mit Kontrolle am Bus:
  Heizkreis-Betriebsart (Standby, Zeitprogramm 1–3, Sommer, Komfort, Normal, Absenk)
  und Warmwasser Ein/Aus – über Weboberfläche, Home Assistant oder MQTT.
  Höchstens ein Befehl pro Minute; was in der Sperrminute kommt, wird vorgemerkt
  (der neueste Wunsch gewinnt) und danach gesendet.
- **Schaltprotokoll** der letzten zehn Befehle mit Quelle und Ergebnis am Bus.
- **Lebenszeichen des WEM** alle sechs Stunden (± 30 min Zufall) und Zähler für
  Fehler und CM=05-Ablehnungen – ein toter WEM fällt auf, bevor man schalten will.
- **Weboberfläche** mit Passwort, Lesefeld für beliebige Objekte, Schreibfeld für Experten,
  WLAN-Wechsel ohne neues Flashen.
- **MQTT** für Messwerte, Lese-, Scan- und Schaltbefehle; wahlweise native Home-Assistant-API.

## Sicherheitsregeln – bitte lesen

1. **Auf den Kessel (Knoten 2) wird nie geschrieben.** Die Firmware schickt am Bus nur
   Lese-Anfragen (SDO 0x40).
2. **Schreiben geht ausschließlich über den WEM** (JSON). Das Schreibfeld erlaubt nur die Module
   SYSTEM0 (MI 01), Heizkreis HZK0 (MI 02) und Warmwasser WW0 (MI 03).
3. **Der WEM ist empfindlich.** Häufige JSON-Anfragen, Anfragen an nicht vorhandene Objekte oder
   mehrere Clients gleichzeitig können seine JSON-Schnittstelle bis zum nächsten Stromlos-Machen
   lahmlegen. Deshalb: mindestens eine Minute zwischen zwei Befehlen, kein Polling über JSON.
4. Der 120-Ω-Abschluss auf dem Board bleibt **aus**, das Board hängt mitten am Bus.

## Anschluss

| Board | Heizung (Klemme H L − +) |
|---|---|
| CAN H | H |
| CAN L | L |

H und L aus **demselben verdrillten Paar** nehmen. − und + bleiben frei.
Busgeschwindigkeit 50 kbit/s. Am WEM muss die **JSON-Schnittstelle** eingeschaltet sein
(Parameter 10.8.1, Fachmann-Ebene).

## Pakete

`weishaupt-wem-can.yaml` wählt nur aus; die Funktionen stehen in `pakete/`.
Nicht benötigte Zeilen unter `packages:` auskommentieren.

| Paket | Inhalt | Pflicht |
|---|---|---|
| `kessel.yaml` | Kesselwerte, Heizkreis-Zustand, Diagnose, Lesefeld, Bus-Wächter, WLAN-Wechsel | ja |
| `warmwasser.yaml` | Warmwasserwerte (nur mit Speicher am WTC) | nein |
| `mqtt.yaml` | MQTT-Broker, `cmd/lesen`, `cmd/scan`, `cmd/stop`, Rohmitschnitt | nein |
| `homeassistant-api.yaml` | native ESPHome-API (verschlüsselt) | nein |
| `wem-schalten.yaml` | Heizkreis schalten, Warteschlange, Lebenszeichen, Protokoll | nein |
| `warmwasser-schalten.yaml` | Warmwasser Ein/Aus (braucht `wem-schalten` und `warmwasser`) | nein |
| `feste-ip.yaml` | feste IP statt DHCP | nein |

## Einrichten

```
cp secrets.yaml.example secrets.yaml     # Werte eintragen
esphome run weishaupt-wem-can.yaml       # erstes Mal per USB, danach per OTA
```

**Netz:** am besten DHCP und im Router eine feste Zuordnung (Reservierung) für das Board.
Nur wenn das nicht geht, das Paket `feste-ip.yaml` einschalten. Findet OTA das Board nicht
per mDNS: `esphome upload weishaupt-wem-can.yaml --device <IP>`.

**WLAN wechseln** ohne Flashen: in der Weboberfläche unter *Einstellungen* Name und Passwort
eintragen, dann *WLAN übernehmen*. Klappt die Verbindung binnen 30 s, wird gespeichert; sonst
bleibt das bisherige WLAN. Letzter Rückweg ist der Notfall-Hotspot mit Captive Portal.

## MQTT-Befehle

| Topic | Inhalt | Wirkung |
|---|---|---|
| `<gerät>/cmd/heizkreis` | `1`–`8` oder Name, z. B. `Zeitprogramm 1` | Heizkreis-Betriebsart (Warteschlange) |
| `<gerät>/cmd/warmwasser` | `Ein` / `Aus` | Warmwasser (Warteschlange) |
| `<gerät>/cmd/lesen` | `01 2933 02` (Knoten Index Sub, hex) | ein Objekt lesen |
| `<gerät>/cmd/scan` | `01 2A00 2AFF 3` | Bereich lesen, Antworten in `<gerät>/canraw` |
| `<gerät>/cmd/stop` | beliebig | Scan abbrechen |
| `<gerät>/schaltprotokoll` | (retained, vom Board) | letzte zehn Befehle, einer je Zeile |

Codes Heizkreis: 1 Standby, 2–4 Zeitprogramm 1–3, 5 Sommer, 6 Komfort, 7 Normal, 8 Absenk.

## Überwachung

Der Sensor **„Letzter CAN-Frame vor"** (Sekunden) eignet sich für eine Warnung: bleibt er
länger als 300 s darüber, schweigt der Bus (Heizung aus, Kabel ab, Controller im Bus-off).
Der Bus-Wächter startet das Board nach einstellbarer Zeit ohne Frame selbst neu.

## Zuordnung JSON-Objekt → CAN-Objekt (gemessen)

| JSON-Modul | CAN-Knoten | Versatz |
|---|---|---|
| MI 07 (Kessel) | 2 | gleich |
| MI 01 (System) | 1 | +0x100 |
| MI 02 (Heizkreis HZK0) | 1 | +0x400 |
| MI 03 (Warmwasser WW0) | 1 | +0x500 |

Beispiele: Heizkreis-Betriebsart JSON `02 00 2533 02` = CAN Knoten 1 `0x2933/2`;
Warmwasser-Betriebsart JSON `03 00 2520 02` (1 = Ein, 2 = Aus) = CAN Knoten 1 `0x2A20/2`.

## Quellen – was bei der Entwicklung geholfen hat

- [MenkeC/Weishaupt-C3supermini](https://github.com/MenkeC/Weishaupt-C3supermini) – ESPHome am
  Weishaupt-CAN-Bus (ESP32-C3 + SN65HVD230), Ausgangspunkt dieser Firmware: 50 kbit/s,
  Kessel = CANopen-Knoten 2, SDO `0x602`/`0x582`, PDOs `0x201`/`0x241`.
- [geronet1/wem-python](https://github.com/geronet1/wem-python) – Python-Zugriff auf den WEM
  per CAN; Objektbedeutungen (u. a. Systembetriebsart `0x28BE`). In
  [Issue #2](https://github.com/geronet1/wem-python/issues/2) entstand der Hinweis auf den ESP-Weg.
- [BorgNumberOne/Weishaupt_CanApiJson](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) –
  Registertabelle der WEM-JSON-Schnittstelle (`/ajax/CanApiJson.json`) mit MI/MX/OX/OS,
  Faktoren und Aktualisierungsklassen (Datenpunktliste des Weishaupt-Gateways WEM-Modbus).
- [kraiz/hassio-weishaupt](https://github.com/kraiz/hassio-weishaupt) – Home-Assistant-Integration
  über die JSON-Schnittstelle. Die Issues erklären, warum der WEM so vorsichtig behandelt werden muss:
  - [#15](https://github.com/kraiz/hassio-weishaupt/issues/15) – zehn Anfragen an nicht vorhandene
    Objekte (CM=05) sperren die Schnittstelle bis zum Stromlos-Machen
  - [#13](https://github.com/kraiz/hassio-weishaupt/issues/13) – lokale JSON-Schnittstelle erst
    nutzbar, wenn das WEM-Portal aus ist
  - [#9](https://github.com/kraiz/hassio-weishaupt/issues/9) – zwei Clients gleichzeitig führen zur Sperre
- ESPHome-Dokumentation: [CAN-Bus / esp32_can](https://esphome.io/components/canbus/esp32/),
  [Pakete](https://esphome.io/components/packages/),
  [WLAN inkl. `wifi.configure`](https://esphome.io/components/wifi/),
  [HTTP Request](https://esphome.io/components/http_request/),
  [MQTT](https://esphome.io/components/mqtt/), [Web Server](https://esphome.io/components/web_server/).

## Herkunft

Ausgangspunkt war [MenkeC/Weishaupt-C3supermini](https://github.com/MenkeC/Weishaupt-C3supermini).
Verwendung auf eigene Gefahr; keine Verbindung zu Weishaupt.
