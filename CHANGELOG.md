# Changelog – ESP-CAN-Brücke Weishaupt (WeAct CAN485, ESPHome)


## v15 – 2026-09-27
- **Schalter „Eigene CAN-Anfragen“** (Einstellungen, bleibt über Neustarts erhalten). Startzustand in der Hauptdatei über `can_anfragen_start`, Vorgabe **aus** (nur mithören). Aus = nur mithören: ausgewertet wird, was ohnehin auf dem Bus liegt (PDOs des Kessels, Antworten auf Fragen des WEM). Schalten von Heizkreis-Betriebsart und Warmwasser bleibt auch dann erlaubt, samt der einen Kontroll-Leseanfrage; ebenso der Lesebefehl von Hand.
- Beim Abschalten werden alle Werte, die nur durch eigene Abfragen aktuell blieben, auf „unbekannt“ gesetzt – kein veralteter Wert, der aktuell aussieht. Fragt der WEM einen davon selbst ab, kommt er über das Mithören zurück.

## v14 – 2026-09-27
- **Abfragetakt nach Weishaupt-Vorgabe**: kein Wert öfter als alle 40 s (kürzestes Intervall des Datenloggers im WEM-Portal; die Datenpunktliste des WEM-Modbus-Gateways nennt 30 s / 60 s / 10 min).
  Kessel: Temperatur, Abgas, Vorlauf, Leistung, Drehzahl, Brenner alle 40 s; Vorlauf-Soll, Volumenstrom, Druck alle 60 s.
  WEM: Vorlauf Heizkreis, Vorlaufsoll-Anforderung alle 60 s; Heizkreis- und Warmwasser-Betriebsart, Raumsoll, WW-Sollwerte alle 5 min (nach jedem Schaltbefehl wird gezielt nachgelesen).
- Statusbits 0x274D werden nicht mehr abgefragt, sie kommen als PDO 0x1C1 von selbst.
- **Anlaufpause**: nach Board-Start oder wenn der Bus wieder da ist (Heizung war stromlos), 10 min nur mithören. Anzeige „Eigene CAN-Anfragen“ unter Diagnose.
- Last auf den WEM (Knoten 1): von 12 auf etwa 2,6 Anfragen pro Minute.

## v13 – 2026-09-27
- **WEM-Lebenszeichen entfernt** – Automatik alle 6 h, Knopf „jetzt prüfen“ und Anzeige. Jede zusätzliche JSON-Anfrage ist ein Risiko: am 27.09. fiel die JSON-Schnittstelle des WEM nach vier Schaltbefehlen und einem Lebenszeichen binnen zehn Minuten aus (CM=05 auf alle Register).
- **Zähler für WEM-JSON-Fehler und CM=05 entfernt.** CM=05 steht weiterhin im Ergebnis des Schaltbefehls.
- Der WEM wird damit nur noch angesprochen, wenn jemand schaltet.

## v12 – 2026-09-27
- **Vorlauf** (Kessel, 0x2536/00) wird jetzt selbst abgefragt – der WEM fragt es nie ab, deshalb blieb das Feld bisher leer. Deutung bei laufendem Brenner noch zu prüfen.
- Schalten per MQTT (`cmd/heizkreis`, `cmd/warmwasser`) am echten Board getestet: Warteschlange, WEM, Bestätigung am Bus, Protokoll.

## v11 – 2026-09-27
- **Warteschlange sichtbar**: eigenes Feld „Warteschlange" (Ziel, Wert, Quelle, ab wann der nächste Befehl frei ist) und Knopf „Warteschlange leeren". Höchstens zwei Einträge, einer je Ziel (Heizkreis vor Warmwasser); ein neuer Wunsch für dasselbe Ziel ersetzt den alten.

## v10 – 2026-09-27
- **Pakete**: Kessel (Pflicht), Warmwasser, MQTT, Home-Assistant-API, WEM-Schalten, Warmwasser-Schalten, feste IP – einzeln abschaltbar.
- **Warteschlange**: ein Schaltwunsch in der Sperrminute wird vorgemerkt (neuester je Ziel gewinnt) und danach gesendet.
- **Schalten per MQTT**: `cmd/heizkreis` (1–8 oder Name), `cmd/warmwasser` (Ein/Aus).
- **Schaltprotokoll** der letzten zehn Befehle (Web und `schaltprotokoll` retained), mit Quelle und Ergebnis am Bus; Kontrolle erst nach der eigenen Leseanfrage (kein Fehlurteil durch eine zufällige Abfrage).
- **WEM-Lebenszeichen** alle 6 h ± 30 min Zufall, Vergleich mit dem Bus; Knopf zum sofortigen Prüfen.
- **CM=05-Erkennung** mit eigenem Zähler.
- **WLAN wechseln** im Web (Name, Passwort, „WLAN übernehmen"; bei Fehlschlag bleibt das alte WLAN).
- „Warmwasser aktiv" und „Brenner Status" als Text Ein/Aus; die Zahlen stehen unter Zahlencodes.
- Rohmitschnitt `canraw` abschaltbar (Vorgabe aus, beim Scan immer an).
- Text-Topics jetzt ESPHome-Standard (`text_sensor/...`), Zahlen-Topics unverändert (`sensor/...`).

## v9 – 2026-09-26
- **Schreibfeld über WEM** (Experten): „MI MX OX OS WERT" in hex plus Knopf „Schreiben". Nur MI 01/02/03 erlaubt, Kessel gesperrt. Antwort des WEM wird ausgewertet (CM=04 bestätigt, CM=05 abgelehnt).
- **Netzdiagnose**: Gateway, Netzmaske, DNS-Server, BSSID und MAC-Adresse.
- **Zähler für WEM-JSON-Fehler** (bleibt über Neustarts erhalten).
- „Schaltstatus" heißt jetzt „Ergebnis letzter Schaltbefehl".
- Changelog nicht mehr in der Weboberfläche.

## v8 – 2026-09-26
- **Schalten im Web**: Heizkreis-Betriebsart und Warmwasser Ein/Aus über die WEM-JSON-Schnittstelle, Kontrolle am Bus, höchstens ein Befehl pro Minute.
- Web-Passwort; WEM-Adresse und Wächterzeit im Web änderbar; Neustart-Knopf, Laufzeit, Alter des letzten CAN-Frames, IP und WLAN.

## v7 – 2026-09-26 (Endfassung)
- **Senden nur, wenn der Bus lebt** (letzter Frame < 30 s): Poller, Scan und Lesebefehl schweigen bei ausgeschalteter Heizung → kein Bus-off nach Stromzyklus. Wächter (5 min ohne Frame → Neustart) bleibt als zweite Sicherung.
- **Lesebefehl über die Weboberfläche**: Feld „Lesebefehl (KN IDX SUB hex)" plus Knopf „Lesen", Antwort in „Lesebefehl Antwort" (Wert dezimal/hex oder Abbruchcode). Nur Lesen (0x40). Objektscan-Knopf entfernt (Scan weiter per MQTT `cmd/scan`).
- **Oberfläche in Gruppen**: Kessel, Betriebsarten und Sollwerte, Energie, Lesebefehl, Zahlencodes, Diagnose, Firmware. Changelog eine Zeile je Version.
- Textsensoren (Firmware, Changelog, ESPHome-Version) unter `text/`, damit Telegraf sie nicht als Zahl liest. „Betriebsart Heizkreis Code" heißt jetzt „Heizkreis Status Code" (0x274D-Bitfeld).
- **Neue Sensoren (Knoten 1, alle 60 s)**: Heizkreis-Betriebsart (0x2933/2, Text + Code), Warmwasser-Betriebsart (0x2A20/2, Ein/Aus), Raumsoll aktuell (0x2958/2), WW-Soll aktuell (0x2A2C/2), WW-Soll normal (0x2A39/2), Vorlauf Heizkreis (0x2907/2).
- Firmware-Stand, ESPHome-Version und Changelog auf der Weboberfläche.
- ESP-Chiptemperatur entfernt (ESP32-D0WD hat keinen kalibrierten Fühler, Werte wurden verworfen).

## v6 – 2026-09-26
- MQTT-Lesekanal `cmd/lesen`, frei wählbarer Scan `cmd/scan`, `cmd/stop` (Kommandobyte fest 0x40).
- Bus-off-Wächter: 5 min kein Frame → Neustart.

## v5 – 2026-09-26
- 0x274D als Statusbitfeld (0x1000 HK Standby, 0x0040 Heizbetrieb, 0x0010 WW-Ladung).
- Sensor „Vorlaufsoll Anforderung" (0x2640/3). Objektscan-Knopf entfernt.

## v4 – 2026-09-26
- Aktiver Kessel-Poller (Knoten 2, 20 s), Textsensoren unter `text/`, Abgas statt „Rücklauf" für 0x2537.

## Abbildung JSON → CAN (Knoten 1), Grundlage der v7-Sensoren
MI 01 System +0x100 · MI 02 Heizkreis +0x400 · MI 03 Warmwasser +0x500 · Kessel MI 07 = gleich, MI 09/01 +0x100 (Knoten 2).
