# Weishaupt WEM am CAN-Bus – ESPHome-Firmware

Firmware für ein **WeAct CAN485 DevBoard V1** (ESP32, galvanisch getrennter CAN-Transceiver),
das am CAN-Bus einer Weishaupt-Brennwertheizung mit **WEM-Systemgerät** (z. B. WTC-GW 15-B) mithört.

- **Lesen am Bus**, ohne den WEM zu belasten: Kessel-, Vorlauf-, Abgas-, Warmwasser- und
  Außentemperatur, Druck, Leistung, Brennerstatus mit Startzähler, Betriebsarten, Wärmemengen.
- **Schalten über die JSON-Schnittstelle des WEM**, mit Kontrolle am Bus:
  Heizkreis-Betriebsart (Standby, Zeitprogramm 1–3, Sommer, Komfort, Normal, Absenk)
  und Warmwasser Ein/Aus. Höchstens ein Befehl pro Minute.
- **Weboberfläche** mit Passwort, Lesefeld für beliebige Objekte, Schreibfeld für Experten.
- **MQTT** für Messwerte, Lese- und Scanbefehle.

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

## Einrichten

```
cp secrets.yaml.example secrets.yaml     # Werte eintragen
esphome run weishaupt-wem-can.yaml       # erstes Mal per USB, danach per OTA
```

Netz: DHCP und im Router eine feste Zuordnung für das Board. Kommt das WLAN nicht zustande,
öffnet das Board einen Notfall-Hotspot.

## Zuordnung JSON-Objekt → CAN-Objekt (gemessen)

| JSON-Modul | CAN-Knoten | Versatz |
|---|---|---|
| MI 07 (Kessel) | 2 | gleich |
| MI 01 (System) | 1 | +0x100 |
| MI 02 (Heizkreis HZK0) | 1 | +0x400 |
| MI 03 (Warmwasser WW0) | 1 | +0x500 |

Beispiele: Heizkreis-Betriebsart JSON `02 00 2533 02` = CAN Knoten 1 `0x2933/2`;
Warmwasser-Betriebsart JSON `03 00 2520 02` (1 = Ein, 2 = Aus) = CAN Knoten 1 `0x2A20/2`.

## Geplant

- Pakete: MQTT, WEM-Schalten und Warmwasser wahlweise abschaltbar
- WLAN-Zugangsdaten im Web änderbar, Warteschlange für Befehle, Schalten per MQTT
- Lebenszeichen des WEM alle sechs Stunden, Schaltprotokoll der letzten zehn Befehle

## Herkunft

Ausgangspunkt war [MenkeC/Weishaupt-C3supermini](https://github.com/MenkeC/Weishaupt-C3supermini).
Verwendung auf eigene Gefahr; keine Verbindung zu Weishaupt.
