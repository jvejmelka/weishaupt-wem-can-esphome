# Changelog – ESP-CAN-Brücke Weishaupt (WeAct CAN485, ESPHome)


## v23 – 2026-09-27
- **Kein gemeinsamer Hauptschalter mehr für R1–R4.** Jede feste Regel ist einzeln schaltbar (Weboberfläche, Datei, `cmd/regel`), mit eigenem Mindestabstand; die gemeinsame Obergrenze pro Stunde bleibt. **Vorgaben im Repository: nur R3 an** (= Verhalten bis v21: Heizkreis nachlesen, wenn sich die Statusbits ändern), R1, R2, R4 aus.
- **Eigene Regeln** sind ein eigener Block „Eigene Regeln (experimentell)“ mit **eigenem Hauptschalter** (Vorgabe aus), der nur für die eigenen Regeln gilt.
- Weboberfläche: Gruppen „Regeln (R1-R4)“ und „Eigene Regeln (experimentell)“ getrennt; „Regeln aktiv“ zeigt zusätzlich, ob die eigenen Regeln an sind. Umbenannt: „Regeln Protokoll“, „Regeln: hoechstens pro Stunde (alle zusammen)“.
- **Datei-Format `cmd/regeln` geändert:** `{"version":N, "R1":{"an":…,"abstand_min":…}, …, "max_pro_stunde":6, "eigene_regeln_an":false, "eigene":[…]}`. Das alte Feld `verdacht` wird **mit Meldung abgelehnt** (nicht umgedeutet – `verdacht: true` hieß „R1–R4 an“ und würde sonst still die eigenen Regeln einschalten). `version` wird im Stand zurückgemeldet.
- `cmd/regel`: Namen `R1`–`R4`, `eigene` (Hauptschalter der eigenen Regeln) und eigene Regeln; `verdacht` wird mit Hinweis abgelehnt. `regeln/stand` im neuen Format.
- `substitutions` umbenannt: `regel_r1_start` … `regel_r4_start`, `regel_abstand_r1` … `_r4`, `regeln_max_h`, `eigene_regeln_start` (bis v22 `verdacht_…`).

## v22 – 2026-09-27
- **Verdachts-Lesen (experimentell), neues Paket `verdacht.yaml`:** die Betriebsarten werden gelesen, wenn das Mithören eine Änderung vermuten lässt – ausschließlich SDO-Leseanfragen an Knoten 1, nie JSON, keine periodische Abfrage, nur bei lebendem Bus und außerhalb der Anlaufpause. **Hauptschalter, Vorgabe AUS.**
- Feste Regeln, einzeln schaltbar mit Mindestabstand: **R1** WEM-Abfragefolge `2101/0A, 2102/0D, 2102/01` (Warmwasser-Wechsel), **R2** Warmwasserbetrieb trotz bekanntem Aus, **R3** Statusbits `0x1C1` geändert (bisheriger Anlass, jetzt schaltbar, Zustand dauerhaft gespeichert), **R4** Heizanforderung passt nicht zur Betriebsart (Vorgabe aus). Obergrenze für alle zusammen pro Stunde (Vorgabe 6). R1–R4 ruhen 2 min nach eigenen Schaltbefehlen.
- **Eigene Regeln** (bis 8): CAN-ID + Maske + Muster → Heizkreis, Warmwasser oder beide lesen; im Flash gespeichert, schaltbar per Datei, per `cmd/regel` und in der Weboberfläche („Regel-Befehl“). `0x601`/`0x581` sind als Auslöser gesperrt.
- **Einstellen per Datei:** Startwerte als `substitutions` in der Hauptdatei; zur Laufzeit JSON an `<gerät>/cmd/regeln` (retained), ungültiges wird mit Meldung abgelehnt; aktiver Stand retained unter `<gerät>/regeln/stand`. Vorlage `regeln.json.example`.
- Weboberfläche: je Regel „was und warum“ und „zuletzt ausgelöst“, Protokoll der letzten zehn Auslösungen (Regel, gelesen, geändert ja/nein), auch retained unter `<gerät>/verdacht/protokoll`.
- **Geändert:** Mit Hauptschalter AUS löst eine Änderung der Statusbits kein Nachlesen mehr aus (bis v21 geschah das immer). Gelesen wird dann nur beim Start, nach eigenen Schaltbefehlen und auf „Status lesen“. Ohne Paket `verdacht` bleibt das alte Verhalten.

## v21 – 2026-09-27
- **Status lesen auf Wunsch:** Knopf „Status lesen“ in der Weboberfläche (Gruppe Betriebsarten) und MQTT-Befehl `<gerät>/cmd/status` (beliebiger Inhalt). Liest einmal die Heizkreis-Betriebsart (Knoten 1 0x2933/2) und – mit Paket warmwasser – Warmwasser-Betriebsart und -Sollwerte (0x2A20/0x2A2C/0x2A39). Nur CAN-Leseanfragen über das vorhandene Anlass-Nachlesen, keine WEM-JSON-Anfrage, keine periodische Abfrage.
- Die 2-min-Grenze gilt für diese eine Lesung nicht; Wünsche werden höchstens alle 10 s angenommen, gesendet wird nur bei lebendem Bus (in der Anlaufpause erst danach).
- Neue Anzeige „Status gelesen“: Zeitpunkt der letzten Lesung der Heizkreis-Betriebsart bzw. Hinweis „angefordert …“ / „Bus schweigt“.
- Handy-App: Knopf „STATUS LESEN“ neben der Überschrift Betriebsart.

## v20 – 2026-09-27
- **Brenner nach Betriebsphase** (0x2541: 0 aus, 1 Vorbelüftung, 2 Steuer-, 3 Regelbetrieb, 4 Nachbelüftung). „Brenner Status“ zeigt jetzt Aus / Vorlüften / Ein / Nachlüften; „Brenner“ (0/1) und der Startzähler zählen nur noch mit Flamme (Phase 2/3), nicht mehr schon beim Vorlüften. Neuer Zahlenwert „Brennerphase“.

## Handy-App – 2026-09-27
- Die Web-App liegt jetzt im Ordner `app/`. Ihre frühere Fassung fragte den WEM direkt über JSON ab und hat ihn damit mehrfach gesperrt; jetzt liest sie nur noch MQTT vom Board und schaltet über dessen `cmd`-Topics – kein Zugriff mehr auf den WEM. Neu: Warmwasser mit Vorgabe/Ist und Ein/Aus, Betriebsarten vierspaltig, echter Rücklauf, Datum statt Build-Nummer.

## v19 – 2026-09-27
- **Anlaufpause unterscheidet zwei Fälle:** nach einem Neustart des Boards (Flashen, Stromausfall am Board) 1 min – der Bus lief ja weiter; kommt der Bus dagegen nach einem Ausfall wieder (Heizung war stromlos, WEM fährt hoch), bleibt es bei 10 min (`anlaufpause_min`).

## v18 – 2026-09-27
- **Betriebsarten werden nicht mehr periodisch gelesen**, sondern nur bei Anlass: einmal nach der Anlaufpause, wenn sich die Statusbits ändern (PDO 0x1C1 – etwa bei Umstellung am Display, in der Weishaupt-App oder im Portal) und nach eigenen Schaltbefehlen. Heizkreis-Betriebsart höchstens alle 2 min. Warmwasser-Betriebsart und -Sollwerte hängen sich an denselben Anlass und werden nach einem Warmwasser-Schaltbefehl nachgelesen.
- Lücke, bewusst in Kauf genommen: ein Wechsel zwischen zwei Zeitprogrammen am Display ändert die Statusbits nicht und bleibt bis zum nächsten Anlass unbemerkt.
- Entfällt: 15-min-Abfrage aus v16, 5-min-Abfrage der Betriebsarten.

## v17 – 2026-09-27
- **Mehr Werte rein passiv**, ohne eine einzige Anfrage:
  - **Rücklauf** (Rücklauftemperatur VPT, 0x2699), **Vorlauf VPT** (0x2697) und **Sollleistung** (0x2698) aus den SDO-Telegrammen, die der Kessel alle ~5 s an Knoten 0x42 schickt (0x6C2).
  - **Kesseltemperatur** zusätzlich aus PDO 0x241 (Bytes 2–3) – alle paar Sekunden statt nur, wenn jemand fragt.
  - **Uhrzeit der Heizung** aus PDO 0x181 (Diagnose).

## v16 – 2026-09-27
- Auch bei ausgeschaltetem Schalter „Eigene CAN-Anfragen“ fragt das Board **Heizkreis- und Warmwasser-Betriebsart alle 15 min** ab (2 Anfragen je Viertelstunde an den WEM). Beide liegen im WEM und kämen sonst nie über den Bus – eine Umstellung am Kesseldisplay oder im Portal bliebe unbemerkt. Sie gehen beim Abschalten deshalb nicht mehr auf „unbekannt“.

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
