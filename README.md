# Weishaupt WEM am CAN-Bus – ESPHome-Firmware

[![CI](https://github.com/jvejmelka/weishaupt-wem-can-esphome/actions/workflows/ci.yml/badge.svg)](https://github.com/jvejmelka/weishaupt-wem-can-esphome/actions/workflows/ci.yml)

Firmware für ein **WeAct CAN485 DevBoard V1** (ESP32, galvanisch getrennter CAN-Transceiver),
das am CAN-Bus einer Weishaupt-Brennwertheizung mit **WEM-Systemgerät** (z. B. WTC-GW 15-B) mithört.

- **Lesen am Bus**, ohne den WEM zu belasten: Kessel-, Vorlauf-, Abgas-, Warmwasser- und
  Außentemperatur, Druck, Leistung, Brennerstatus mit Startzähler, Betriebsarten, Wärmemengen.
- **Schalten über die JSON-Schnittstelle des WEM**, mit Kontrolle am Bus:
  Heizkreis-Betriebsart (Standby, Zeitprogramm 1–3, Sommer, Komfort, Normal, Absenk)
  und Warmwasser Ein/Aus – über Weboberfläche, Home Assistant oder MQTT.
  Höchstens ein Befehl pro Minute; was in der Sperrminute kommt, wird vorgemerkt
  (höchstens einer je Ziel, der neueste Wunsch gewinnt) und danach gesendet – sichtbar im Feld
  „Warteschlange“, mit Knopf zum Leeren.
- **Schaltprotokoll** der letzten zehn Befehle mit Quelle und Ergebnis am Bus.
- **Der WEM wird nie von selbst abgefragt** – nur dann, wenn jemand schaltet. Jede zusätzliche
  JSON-Anfrage ist ein Risiko für die empfindliche Schnittstelle.
- **Weboberfläche** mit Passwort, Lesefeld für beliebige Objekte, Schreibfeld für Experten,
  WLAN-Wechsel ohne neues Flashen.
- **MQTT** für Messwerte, Lese-, Scan- und Schaltbefehle; wahlweise native Home-Assistant-API.

> **Hinweis:** Privates Projekt ohne Verbindung zu Weishaupt. Nutzung auf eigene Gefahr –
> das Board spricht mit der Steuerung einer Gasheizung. Arbeiten an der Anlage selbst
> (Klemmen im Kessel, Fachmann-Parameter) gehören in fachkundige Hände.

## Sicherheitsregeln – bitte lesen

1. **Auf den Kessel (Knoten 2) wird nie geschrieben.** Die Firmware schickt am Bus nur
   Lese-Anfragen (SDO 0x40).
2. **Schreiben geht ausschließlich über den WEM** (JSON). Das Schreibfeld erlaubt nur die Module
   SYSTEM0 (MI 01), Heizkreis HZK0 (MI 02) und Warmwasser WW0 (MI 03).
3. **Der WEM ist empfindlich.** Häufige JSON-Anfragen, Anfragen an nicht vorhandene Objekte oder
   mehrere Clients gleichzeitig können seine JSON-Schnittstelle bis zum nächsten Stromlos-Machen
   lahmlegen. Deshalb: mindestens eine Minute zwischen zwei Befehlen, kein Polling über JSON.
   Einzelheiten: [PROTOKOLL.md – Wann sich der WEM aufhängt](PROTOKOLL.md#8-wann-sich-der-wem-aufhängt).
4. Der 120-Ω-Abschluss auf dem Board bleibt **aus**, das Board hängt mitten am Bus.

## Anschluss

| Board | Heizung (Klemme H L − +) |
|---|---|
| CAN H | H |
| CAN L | L |

H und L aus **demselben verdrillten Paar** nehmen. − und + bleiben frei.
Busgeschwindigkeit 50 kbit/s. Am WEM muss die **JSON-Schnittstelle** eingeschaltet sein
(Parameter 10.8.1, Fachmann-Ebene).

## Aufbau

```
weishaupt-wem-can.yaml        wählt die Pakete aus (substitutions, WLAN, Weboberfläche)
pakete/*.yaml                 Konfiguration und Verdrahtung: Entitäten, CAN-Takte, MQTT-Topics
components/weishaupt_can/     ESPHome-Bauteil (external component): Zustand und Logik in C++-Klassen
tests/                        Tests der Klassen auf dem PC (ohne ESPHome, ohne Board)
```

Seit v29 liegen Zustand und Logik im **Bauteil `weishaupt_can`**, die Pakete sind dünn: sie
legen Entitäten an, schicken die CAN-Anfragen im Takt und rufen im Übrigen nur Methoden des
Bauteils (`id(wcan)->…`). Die Klassen in [`kern.h`](components/weishaupt_can/kern.h):

| Klasse | Aufgabe |
|---|---|
| `wc::Bus` | Lebenszeichen des Busses, Anlaufpause, Sendesperre |
| `wc::Anlass` | wann die Betriebsarten nachgelesen werden (Start, Statusbits, Schaltbefehl, „Status lesen“, Regeln) |
| `wc::Lesebefehl`, `wc::Scan` | Lesebefehl von Hand und Objektscan (nur Leseanfragen) |
| `wc::Brenner` | Flankenerkennung, Brennerstarts (gespeichert) |
| `wc::Schalten`, `wc::Protokoll` | Sperrminute, laufender WEM-Aufruf, Schreibbefehl, Schaltprotokoll |
| `wc::Verdacht` | Laufzeitzustand der Regeln R1–R5 und der eigenen Regeln (teils gespeichert) |
| `wc::Zusatz` | Zusatzanzeigen: Speicherform und Datei-Prüfsumme (gespeichert) |

Dazu die reinen Logik-Dateien: [`regelwerk.h`](components/weishaupt_can/regelwerk.h)
(Auswerter aller Regeln; R1–R5 dort seit v30 als **Datenregeln** im selben Format wie die eigenen
Regeln, siehe [REGELN.md](REGELN.md#5-eigene-regeln-experimentell)),
[`regellogik.h`](components/weishaupt_can/regellogik.h) (gemeinsame Bausteine: Ruhezeit,
Masken, Rückrechnung R4, Prüfung vor dem Lesen), [`befehle.h`](components/weishaupt_can/befehle.h) (Warteschlange,
Befehlsphasen, Status-JSON), [`verdacht.h`](components/weishaupt_can/verdacht.h) und
[`zusatz.h`](components/weishaupt_can/zusatz.h) (Parser, JSON), `register_gen.h` (erzeugt aus
`register.yaml`). Das ESPHome-Gerüst [`weishaupt_can.h`](components/weishaupt_can/weishaupt_can.h)
hält je eine Instanz und speichert, was einen Neustart überleben muss – unter denselben
Schlüsseln wie bis v28, Einstellungen bleiben beim Update erhalten.

**Bewusst YAML geblieben:** alle Entitäten (Namen, Einheiten, Gruppen der Weboberfläche), die
CAN-Anfragen mit ihren Takten und Pausen (`interval` + `canbus.send`), die Zuordnung der
Frames zu den Paketen (`on_frame`), die MQTT-Abos und der WEM-Aufruf (`http_request`). Das ist
Konfiguration, die man beim Nachbau anpasst, und in ESPHome als YAML am lesbarsten; die
Lambdas dort sind kurz und rufen nur das Bauteil.

## Pakete

`weishaupt-wem-can.yaml` wählt nur aus; die Funktionen stehen in `pakete/`.
Nicht benötigte Zeilen unter `packages:` auskommentieren.

| Paket | Inhalt | Pflicht |
|---|---|---|
| `kessel.yaml` | Kesselwerte, Heizkreis-Zustand, Diagnose, Lesefeld, Bus-Wächter, WLAN-Wechsel | ja |
| `warmwasser.yaml` | Warmwasserwerte (nur mit Speicher am WTC) | nein |
| `mqtt.yaml` | MQTT-Broker, `cmd/lesen`, `cmd/scan`, `cmd/stop`, `cmd/status`, Rohmitschnitt | nein |
| `homeassistant-api.yaml` | native ESPHome-API (verschlüsselt) | nein |
| `wem-schalten.yaml` | Heizkreis schalten, Warteschlange, Protokoll | nein |
| `warmwasser-schalten.yaml` | Warmwasser Ein/Aus (braucht `wem-schalten` und `warmwasser`) | nein |
| `verdacht.yaml` | **Regeln:** Betriebsarten nur lesen, wenn das Mithören eine Änderung vermuten lässt; feste Regeln R1–R5 einzeln schaltbar (Vorgabe: R1–R3 an, R4/R5 aus; ausgeschaltete laufen im Schattenmodus weiter), dazu eigene Regeln (experimentell, eigener Hauptschalter, Vorgabe aus); einstellbar per Web, Datei und MQTT (braucht `warmwasser`). Details: [REGELN.md](REGELN.md) | nein |
| `zusatz.yaml` | **Zusatzanzeigen für die Handy-App:** Liste von bis zu 6 fremden MQTT-Topics (z. B. Raumtemperaturen, „Wärmepumpe läuft“), einstellbar per Web und Datei. Das Board liest diese Werte **nicht** selbst, es verwaltet nur die Liste (braucht `mqtt`). Details: [PROTOKOLL.md, Abschnitt 6b](PROTOKOLL.md#6b-zusatzanzeigen-für-die-handy-app) | nein |
| `feste-ip.yaml` | feste IP statt DHCP | nein |

Dazu optional die Handy-App im Ordner `app/` (siehe unten).

## Einrichten

**Ausführliche Schritt-für-Schritt-Anleitung: [INSTALL.md](INSTALL.md)** – Kurzfassung:

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
| `<gerät>/cmd/heizkreis` | `1`–`8`, Name (`Zeitprogramm 1`) oder `ZP1`; ab v27 auch JSON `{"id":17,"wert":"ZP2"}` | Heizkreis-Betriebsart (Warteschlange); jede Phase auf `befehl/status` |
| `<gerät>/cmd/warmwasser` | `Ein` / `Aus`; ab v27 auch JSON `{"id":18,"wert":"Aus"}` | Warmwasser (Warteschlange); jede Phase auf `befehl/status` |
| `<gerät>/cmd/lesen` | `01 2933 02` (Knoten Index Sub, hex) | ein Objekt lesen |
| `<gerät>/cmd/scan` | `01 2A00 2AFF 3` | Bereich lesen, Antworten in `<gerät>/canraw` |
| `<gerät>/cmd/stop` | beliebig | Scan abbrechen |
| `<gerät>/cmd/status` | beliebig | Heizkreis- und Warmwasser-Betriebsart einmal vom Bus lesen (höchstens alle 10 s, nur Leseanfragen); Zeitpunkt in „Status gelesen“ |
| `<gerät>/cmd/regeln` | JSON-Datei, am besten retained (`mosquitto_pub -r -f regeln.json`) | Regeln einstellen: R1–R5 je an/aus und Abstand, Obergrenze, `eigene_regeln_an`, eigene Regeln (Vorlage `regeln.json.example`, Erklärung [REGELN.md](REGELN.md#6-einstellen); das alte Feld `verdacht` wird abgelehnt) |
| `<gerät>/cmd/regel` | `NAME an` / `NAME aus` / `NAME loeschen` | eine Regel schalten (NAME = `R1`–`R5`, `eigene` = Hauptschalter der eigenen Regeln, oder eine eigene Regel), siehe [REGELN.md](REGELN.md#7-per-mqtt-schalten) |
| `<gerät>/cmd/zusatz` | JSON-Datei, am besten retained (`mosquitto_pub -r -f zusatz.json`) | Zusatzanzeigen der Handy-App einstellen: `{"version":N,"eintraege":[{name, topic, feld, einheit, art, schwelle}]}` (Vorlage `zusatz.json.example`); Ungültiges wird mit Meldung abgelehnt |
| `<gerät>/app/zusatz` | (retained, vom Board) | aktuelle Liste der Zusatzanzeigen (die App abonniert daraus die Topics) |
| `<gerät>/status/json` | (retained, vom Board, ab v27) | **strukturierter Zustand** mit festen Feldern: Betriebsarten (Vorgabe, Ist, Quelle, Zeit), Schaltbefehle (laufend, Warteschlange, letzte), Bus, Firmware – nur bei Änderung, höchstens alle 2 s. Aufbau: [TOPICS.md](TOPICS.md) |
| `<gerät>/befehl/status` | (retained, vom Board, ab v27) | jede Phase eines Schaltbefehls als JSON: angenommen → vorgemerkt → gesendet → pruefe_bus → bestaetigt / gescheitert (Grund); ersetzt, abgelehnt. [TOPICS.md](TOPICS.md#gerätbefehlstatus) |
| `<gerät>/schaltprotokoll` | (retained, vom Board) | **veraltet**, Ersatz `befehl/status` – letzte zehn Befehle, einer je Zeile |
| `<gerät>/regeln/stand` | (retained, vom Board) | aktiver Stand der Regeln als JSON (Format wie `cmd/regeln`, plus `firmware`), samt eigener Regeln |
| `<gerät>/verdacht/protokoll` | (retained, vom Board) | letzte zehn Auslösungen: Regel, gelesen, geändert ja/nein |
| `<gerät>/verdacht/ereignis` | (nicht retained, vom Board) | jedes Regel-Ereignis als JSON: `{"regel","phase":"ausgeloest"\|"waere"\|"ergebnis"\|"keine_antwort","ziel","wert","geaendert"}` – für Telegraf/Grafana, siehe [REGELN.md](REGELN.md#8-protokoll-log-und-ereignisse) |

**Wer `cmd/regeln` oder `cmd/regel` schreiben darf, kann Leseanfragen auslösen** – nur Lesen an
Knoten 1, gedrosselt durch Mindestabstand und Obergrenze pro Stunde, aber eben Busverkehr. Das
App-Konto bekommt diese Rechte bewusst **nicht**; im Broker nur einem Verwaltungskonto geben.

Codes Heizkreis: 1 Standby, 2–4 Zeitprogramm 1–3, 5 Sommer, 6 Komfort, 7 Normal, 8 Absenk.

**Neue Auswertungen bitte auf `status/json` und `befehl/status` aufbauen.** Die Text-Sensoren
„Ergebnis letzter Schaltbefehl“, „Warteschlange“, „Status gelesen“, „Heizkreis Status“ und das
Topic `schaltprotokoll` bleiben in der Übergangszeit erhalten, gelten aber als veraltet –
Gegenüberstellung alt/neu in [TOPICS.md](TOPICS.md#veraltete-text-topics).

## Abfragetakt

Mithören ist passiv und kostet nichts. Eigene Leseanfragen stellt das Board höchstens alle 40 s
je Wert (Weishaupts Vorgabe für den Datenlogger), Betriebsarten nur bei Anlass; nach einem
Stromzyklus der Heizung hört es zehn Minuten nur mit. Mit dem Schalter **„Eigene CAN-Anfragen“**
lassen sich die Abfragen ganz abschalten – Takte und Bedingungen:
[PROTOKOLL.md – Mithören und Lesen auf Anforderung](PROTOKOLL.md#2-mithören-und-lesen-auf-anforderung).

## Überwachung

Der Sensor **„Letzter CAN-Frame vor"** (Sekunden) eignet sich für eine Warnung: bleibt er
länger als 300 s darüber, schweigt der Bus (Heizung aus, Kabel ab, Controller im Bus-off).
Der Bus-Wächter startet das Board nach einstellbarer Zeit ohne Frame selbst neu.

## Protokoll

Was auf dem Bus mitgehört und was angefragt wird, wie der JSON-Befehl an den WEM aussieht, wie
JSON-Objekte auf CAN-Objekte abgebildet werden (z. B. Heizkreis-Betriebsart JSON `02 00 2533 02` =
CAN Knoten 1 `0x2933/2`) und wie neue Objekte gefunden werden: **[PROTOKOLL.md](PROTOKOLL.md)**.

Beispielanlage mit Wärmepumpe und PV (Sollwerte, Messungen, was man am Bus sieht):
**[ANLAGE.md](ANLAGE.md)**.

Die Dokumente im Überblick:

- [INSTALL.md](INSTALL.md) – Schritt für Schritt vom Board bis zur Handy-App
- [PROTOKOLL.md](PROTOKOLL.md) – CAN-Bus, WEM-JSON, Abbildung und Fehlerbilder
- [REGELN.md](REGELN.md) – die Regeln R1–R5 und eigene Regeln: wann das Board Betriebsarten nachliest
- [ANLAGE.md](ANLAGE.md) – Beispielanlage mit Wärmepumpe und PV
- [IDEEN.md](IDEEN.md) – was sich noch bauen ließe: Datenbank und Grafana, H2-Sperre,
  Raumsensoren, Raumgerät per Funk, Heizkörperventile, Anlage prüfen
- [CHANGELOG.md](CHANGELOG.md) – Änderungen je Firmware-Stand
- [LICENSE](LICENSE) – Lizenz

## Handy-App (optional, Ordner `app/`)

Eine schlanke Web-App (PWA) fürs Handy: eine schmale Spalte (höchstens 520 CSS-px), die die volle Höhe nutzt **ohne Scrollen** – auch auf quadratischen Displays. Die Grundschrift wird dafür nach dem Laden gemessen und angepasst (größte Schrift, bei der alles passt; neu bei Größenänderung oder wenn eine Fortschrittszeile erscheint). Mit `?diag` hinter der Adresse zeigt die Fußzeile Viewport, Pixelverhältnis, gewählte Schrift und Scrollhöhe.
Auf dem Startbildschirm installierbar („+ App“).

### Was sie zeigt

| Bereich | Inhalt | Quelle |
|---|---|---|
| Kopfzeile | grüner Punkt = letzter Abruf erfolgreich, rot = Problem; oranger Strich = Countdown bis zum nächsten Abruf (30 s) | App |
| ⚙ (Kopfzeile) | öffnet die Weboberfläche des Boards in einem neuen Tab – nur sichtbar, wenn `BOARD_URL` in der `.env` gesetzt ist. Die App reicht nichts durch; das Board fragt sein Passwort selbst ab und ist nur im LAN erreichbar | `.env`: `BOARD_URL` |
| Heizkreis | acht Knöpfe (Standby, ZP 1–3, Sommer, Komfort, Normal, Absenk). **Voll markiert = Ist**: die Betriebsart, die das Board am Bus gelesen hat (0x2933/2). Rechts vom Titel **„Zustand: …“** = Laufzustand aus den Statusbits (z. B. „Standby“, „Zeitprogramm, heizt“, „· WW lädt“) | Board: `status/json` → `heizkreis`; Knopf → `cmd/heizkreis` (JSON mit Kennung) |
| ↻ (beim Heizkreis) | lässt das Board Heizkreis- und Warmwasser-Betriebsart einmal vom Bus lesen (die liest es sonst nur bei Anlass); die Statuszeile zeigt „angefordert …“ und dann „Status HH:MM:SS gelesen“ | Knopf → `cmd/status`; Board: `status/json` → `heizkreis.zeit`, `bus` |
| Warmwasser | Temperatur (Fühler **unten** im Speicher), Knöpfe EIN/AUS – **voll markiert = Ist** am Bus (0x2A20/2); rechts **„Ladung: Gas“** (orange pulsierend, Kessel im Warmwasserbetrieb = Kesselstatus 15) bzw. **„Ladung: aus“** | Board: `warmwasser`, `warmwasser_betriebsart`, `kesselstatus_code` (ersatzweise `warmwasser_aktiv`); Knopf → `cmd/warmwasser` |
| Vorgabe (gestrichelt) | **Vorgabe = eigener Schaltbefehl** über das Board, solange er nicht am Bus bestätigt ist: **vorgemerkt** (Warteschlange bzw. eben gedrückt), **gesendet** (Befehl an den WEM raus, Bus-Kontrolle steht aus). Nach „OK“ verschwindet die Strichelung, der Knopf wird voll markiert. **Rot gestrichelt** mit Kurztext: nicht übernommen / abgelehnt (CM=05) / keine Rückmeldung. **„von außen geändert“** am Ist-Knopf: der Bus steht auf etwas anderem als der letzte eigene erfolgreiche Befehl, ohne dass ein Befehl läuft (Display, Portal) | Board: `status/json` → `schalten`, `befehl/status` |
| Fortschritt (unter den Knöpfen) | nach einem Schaltbefehl im betroffenen Block: **vorgemerkt (ab HH:MM) → an WEM gesendet (JSON) → Bus liest nach → bestätigt ✓** (grün); im Fehlerfall endet die Kette rot mit **✗ nicht übernommen (steht auf …) / abgelehnt (CM=05) / keine Rückmeldung / ungültig**. Allein aus der **Phase** des Befehls (keine Textauswertung). Der aktuelle Schritt pulsiert orange; nach dem Abschluss bleibt die Zeile 3 min stehen, ohne laufenden Befehl gibt es keine Zeile | Board: `status/json` → `schalten`, `befehl/status` |
| Kessel | Kesseltemperatur groß, daneben Vorlauf / Soll und Rücklauf; rechts **Brenner: aus / vorlüften / an / nachlüften** (an in Orange, nur mit Flamme), dahinter der **Zweck** aus dem Kesselstatus: „· Heizung“ bzw. „· Warmwasser“; „(Heizung wartet)“, wenn der Kessel Warmwasser macht und der Heizkreis gleichzeitig Wärme anfordert. **Kaminfeger** und **Wartung** stehen in Rot, auch bei Brenner aus | Board: `kesseltemperatur`, `vorlauf_vpt`, `vorlauf_soll`, `ruecklauf`, `brennerphase`, `kesselstatus_code`, `heizkreis_status_code` (Bit 0x0040), `heizanforderung` |
| Zusatz (unter Kessel) | eine kompakte Zeile aus der Liste des Boards, z. B. „Wohnzimmer 21,3 °C · Arbeitszimmer 22,1 °C · WP ◉ läuft“ – Art `laeuft` zeigt „läuft“ (orange, pulsierend wie der Brenner) bzw. „aus“ ab der Schwelle. Werte älter als 10 min erscheinen als „--“; leere Liste = keine Zeile. Tippen/Überfahren zeigt das Alter des Werts | Board: `<gerät>/app/zusatz` (Paket `zusatz`), Werte direkt aus den dort genannten Topics |
| Außen | Außentemperatur groß | Board: `aussentemperatur` |
| Statuszeile | „Aktualisiert HH:MM:SS · Status gelesen HH:MM“, dahinter „nächster Befehl ab …“, solange die Sperrminute läuft (höchstens zwei Zeilen); nach einem Knopfdruck 3 min lang der Fortschritt: vorgemerkt → gesendet → **OK** (grün) oder **NICHT übernommen / abgelehnt (CM=05) / keine Rückmeldung** (rot); Fehler wie „CAN-Board nicht erreichbar“ | Board: `befehl/status`, `status/json` → `schalten.frei_ab`, `heizkreis.zeit` |
| Fußzeile | Build-Stand als Datum | App |

Messwerte, die länger als 10 min nicht aktualisiert wurden, zeigt die App als „--“ statt eines
alten Werts. Betriebsarten und Sollwerte sind davon ausgenommen – das Board liest sie nur bei
Anlass (nach dem Start, bei geänderten Statusbits, nach Schaltbefehlen).

### Wie sie arbeitet

- **Liest nur MQTT** vom Board (`<gerät>/sensor/+/state`, retained) und spricht **nie mit dem WEM**.
- **Zusatzanzeigen:** die App abonniert `<gerät>/app/zusatz` und danach die dort genannten fremden
  Topics (bei Änderung der Liste neu abonniert bzw. abbestellt). Das Board selbst liest sie nicht.
- **Schalten** geht als MQTT-Befehl mit Kennung an das Board (`{"id":"app-…","wert":3,"quelle":"App"}`);
  das Board setzt ihn mit Sperrminute, Warteschlange und Kontrolle am Bus um und meldet jede Phase
  auf `befehl/status`. Die App liest nur diese festen Felder und `status/json` – **keinen Freitext**.
- Anmeldung mit Benutzer und Passwort aus der `.env` – **ohne beide startet die App nicht**.
  Die Sitzungen liegen in `data/` und überleben einen Neubau.
- Braucht Board-Firmware **ab v27** (`status/json`, Befehle mit Kennung). Mit älterer Firmware
  fehlen Fortschritt und Vorgabe-Markierung; Ist-Werte kommen dann ersatzweise aus den Zahlencodes.

### Einrichten

```
cd app
cp .env.example .env          # Broker, MQTT-Konto, App-Login eintragen
docker compose up -d --build  # danach http://<Host>:4000
```

Eigenes Broker-Konto, das nur lesen, die zwei Schaltbefehle und den Status-Lesebefehl senden darf:

```
user heizungsapp
topic read  weact-can485-weishaupt/#
topic write weact-can485-weishaupt/cmd/heizkreis
topic write weact-can485-weishaupt/cmd/warmwasser
topic write weact-can485-weishaupt/cmd/status
# Zusatzanzeigen: je eingetragenem Topic genau eine Leserecht-Zeile, z. B.
topic read  sensoren/wohnzimmer/temperatur
```

**Jedes Topic der Zusatzliste braucht eine eigene `topic read`-Zeile** – fehlt sie, bleibt die
Anzeige stumm auf „--“ (der Broker verweigert das Abo ohne Meldung an die App). Nur Leserecht
vergeben: bei manchen Geräten (z. B. Shelly) lässt sich über ein benachbartes Topic schalten.

### Hinweise

- **Von außen nur hinter einem Reverse Proxy mit TLS.** Die App selbst spricht HTTP.
- **Kein Forward-Auth-Portal davor** (Authelia & Co.): die App holt ihre Werte per `fetch`; läuft
  die Portal-Sitzung ab, bekommt sie eine Weiterleitung statt JSON und zeigt nur noch Fehler.
  Besser Basic Auth am Proxy oder Zugriff über VPN.
- **Keine Sicherungskopien in `app/public/` ablegen** – alles dort landet im Image und wird
  ausgeliefert, auch `*.bak`.
- **Cloudflare & Co. cachen Stil- und Skriptdateien** (bei uns 4 h). Deshalb hängt der Build
  an `style.css` und `app.js` eine Versionsnummer an – nach einem Update genügt einmal Neuladen.
- **Warum die App den WEM nicht selbst fragt:** Eine frühere Fassung las ihre Werte bei jedem
  Öffnen direkt über die JSON-Schnittstelle des WEM. Das verträgt die Schnittstelle nicht – sie
  wurde dadurch mehrfach gesperrt und kam erst nach Stromlos-Machen der Heizung wieder. Seitdem
  liest die App nur MQTT vom Board, und Schaltbefehle gehen über das Board mit Sperrminute.

## Was belegt ist und was nicht

Die Zuordnung der Objekte stammt teils aus fremden Vorlagen. Welcher Wert an einer echten Anlage
(WTC-GW 15-B) gegengeprüft ist und welcher nicht, steht je Objekt in
[PROTOKOLL.md, Abschnitt 3 und 4](PROTOKOLL.md#3-was-mitgehört-wird). Rückmeldungen von anderen
Anlagen sind willkommen.

Kurzübersicht aller Objekte, die die Firmware auswertet (Einzelheiten und Belege in
[PROTOKOLL.md](PROTOKOLL.md); Quelle ist [`register.yaml`](register.yaml)):

<!-- REGISTER:BEGIN uebersicht -->
<!-- erzeugt aus register.yaml von werkzeuge/register_erzeugen.py - nicht von Hand aendern -->

| Stelle | Name in der Firmware | Weg | Stand |
|---|---|---|---|
| PDO `0x201` B1–2 | Aussentemperatur | PDO | **belegt** |
| PDO `0x201` B0 | Systembetriebsart | PDO | plausibel |
| PDO `0x241` B0–1 | Heizanforderung *(Name historisch)* | PDO | plausibel |
| PDO `0x241` B2–3 | Kesseltemperatur | PDO | plausibel |
| PDO `0x241` B6–7 | Warmwasser | PDO | **belegt** |
| PDO `0x181` B0–5 | Uhrzeit Heizung | PDO | **belegt** |
| PDO `0x182` B0 | Kesselstatus, Warmwasser aktiv (Status 15) | PDO | **belegt** |
| PDO `0x1C1` B2–3 | Heizkreis Status | PDO | plausibel |
| Knoten 2 `0x252B`/0 | Regeln R2/R4 | WEM schreibt | plausibel |
| `0x6C2` `0x2699`/1 | Ruecklauf | Telegramm 0x6C2 | plausibel |
| `0x6C2` `0x2697`/1 | Vorlauf VPT | Telegramm 0x6C2 | plausibel |
| `0x6C2` `0x2698`/1 | Sollleistung | Telegramm 0x6C2 | plausibel |
| Knoten 2 `0x2532`/0 | Kesseltemperatur | eigene Anfrage, Antwort an den WEM | **belegt** |
| Knoten 2 `0x2537`/0 | Abgastemperatur | eigene Anfrage, Antwort an den WEM | **unbestätigt** |
| Knoten 2 `0x2536`/0 | Vorlauf | eigene Anfrage | **unbestätigt** |
| Knoten 2 `0x2534`/0 | Leistung | eigene Anfrage, Antwort an den WEM | **unbestätigt** |
| Knoten 2 `0x2540`/0 | Drehzahl | eigene Anfrage | **unbestätigt** |
| Knoten 2 `0x2541`/0 | Brenner Status, Brenner, Brennerphase, Brennerstarts | eigene Anfrage, Antwort an den WEM | **belegt** |
| Knoten 2 `0x2545`/0 | Vorlauf Soll | eigene Anfrage, Antwort an den WEM | **unbestätigt** |
| Knoten 2 `0x2713`/2 | Volumenstrom | eigene Anfrage, Antwort an den WEM | **unbestätigt** |
| Knoten 2 `0x2714`/2 | Anlagendruck | eigene Anfrage, Antwort an den WEM | **belegt** |
| Knoten 2 `0x2726`/2 | Wärmemenge Vortag Heizung | Antwort an den WEM | **belegt** |
| Knoten 2 `0x2727`/2 | Wärmemenge Vortag WW | Antwort an den WEM | **belegt** |
| Knoten 2 `0x2728`/2 | Wärmemenge Vortag Gesamt | Antwort an den WEM | **belegt** |
| Knoten 2 `0x2101`/0A | Regel R1 | Frage des WEM | **belegt** |
| Knoten 2 `0x2102`/0D | Regel R1 | Frage des WEM | **belegt** |
| Knoten 2 `0x2102`/1 | Regel R1 | Frage des WEM | **belegt** |
| Knoten 1 `0x2907`/2 | Vorlauf Heizkreis | eigene Anfrage | plausibel |
| Knoten 1 `0x2640`/3 | Vorlaufsoll Anforderung | eigene Anfrage | plausibel |
| Knoten 1 `0x2958`/2 | Raumsoll aktuell | eigene Anfrage | plausibel |
| Knoten 1 `0x2933`/2 | Heizkreis Betriebsart | eigene Anfrage | **belegt** |
| Knoten 1 `0x2A20`/2 | Warmwasser Betriebsart | eigene Anfrage | **belegt** |
| Knoten 1 `0x2A2C`/2 | Warmwasser Soll aktuell | eigene Anfrage | plausibel |
| Knoten 1 `0x2A39`/2 | Warmwasser Soll normal | eigene Anfrage | plausibel |

<!-- REGISTER:END uebersicht -->

## Sicherheit

- Die Weboberfläche nutzt Basic Auth über **unverschlüsseltes HTTP** – nur im LAN betreiben,
  von außen höchstens hinter einem Reverse Proxy mit TLS und eigener Anmeldung.
- **Wer auf `<gerät>/cmd/...` schreiben darf, kann die Heizung schalten.** Am Broker Zugriffsregeln
  (ACL) setzen: Schreibrecht auf `cmd/#` nur für Konten, die schalten sollen.
- Die WEM-Zugangsdaten sind die Werkseinstellung von Weishaupt und laut Anleitung nicht änderbar.
  Die WEM-Schnittstelle gehört deshalb nicht ins Internet.
- Das Schaltprotokoll liegt im Arbeitsspeicher und ist nach einem Neustart leer
  (retained im MQTT-Topic `schaltprotokoll` bleibt der letzte Stand).

## Hardware

**WeAct CAN485 DevBoard V1** – ESP32-D0WD-V3, 8 MB Flash, CAN-Transceiver mit 2,5 kV galvanischer
Trennung, RS485, USB-C; umschaltbarer 120-Ω-Abschluss (hier **aus**).

- Hersteller-Repository mit Schaltplan und Pinbelegung:
  [WeActStudio/WeActStudio.CAN485DevBoardV1_ESP32](https://github.com/WeActStudio/WeActStudio.CAN485DevBoardV1_ESP32)
- Pinbelegung auch bei Zephyr: [WeAct CAN485 DevBoard V1](https://docs.zephyrproject.org/latest/boards/weact/can485dbv1/doc/index.html)
- Vorstellung mit Preisen: [CNX Software, 14.01.2026](https://www.cnx-software.com/2026/01/14/weact-can485-a-low-cost-esp32-board-with-can-bus-and-rs485-interfaces/)
- **Kaufen:** AliExpress (WeAct-Shop, Link aus dem CNX-Artikel): https://a.aliexpress.com/_c4TXgZeJ –
  rund 9–17 $ plus Versand, je nach Land. Bei Amazon war das Board beim Schreiben nicht zu finden.
- Stromversorgung im Betrieb: beliebiges USB-Netzteil ab 1 A.

Alternative mit Einzelteilen (Aufbau von MenkeC): ESP32-C3 SuperMini plus Transceiver SN65HVD230 –
billiger, aber **ohne** galvanische Trennung.

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
- **Weishaupt-Unterlagen:**
  - [Handbuch WEM-Modbus TCP (Löbbeshop, PDF)](https://www.loebbeshop.de/media/67944/file/static/pdf/weishaupt/manual-wem-modbustcp.pdf) –
    Datenpunktliste mit Aktualisierungsklassen (30 s / 60 s / 10 min); zeigt, dass Weishaupt den
    WEM selbst regelmäßig abfragt, und dass Gateway und WEM-Portal sich ausschließen.
  - [Montage- und Betriebsanleitung WTC-GW 15–32-B (PDF)](https://www.intec-heizung.de/media/pdf/9f/c7/72/Weishaupt-Thermo-Condens-WTC-GW_15-32-B-Montage-u-Betriebsanleitung.pdf) –
    Parameter 10.8.1 (JSON-Schnittstelle), Werkszugang des WEM, Klemme H/L/−/+.
  - [WEM-Portal-FAQ (PDF)](https://www.wemportal.com/Web/Documents/FAQ/FAQ.de.pdf?lang=de)
- Weitere Projekte und Diskussionen:
  - [Varitras/weishaupt_modbus](https://github.com/Varitras/weishaupt_modbus) – Home Assistant über das WEM-Modbus-Gateway
  - [Home-Assistant-Forum: Weishaupt WTC, CAPI VG, CanApiJson](https://community.home-assistant.io/t/weishaupt-wtc-weishaupt-capi-vg-canapijson/997400) – Aufbau des VG-Felds (CM MI MX OX OS VS VA)
  - [geronet1/wem-python Issue #1](https://github.com/geronet1/wem-python/issues/1)
- ESPHome-Dokumentation: [CAN-Bus / esp32_can](https://esphome.io/components/canbus/esp32/),
  [Pakete](https://esphome.io/components/packages/),
  [WLAN inkl. `wifi.configure`](https://esphome.io/components/wifi/),
  [HTTP Request](https://esphome.io/components/http_request/),
  [MQTT](https://esphome.io/components/mqtt/), [Web Server](https://esphome.io/components/web_server/).

## Tests & CI

Die Regeln (Format und Auswerter) stehen in [`verdacht.h`](components/weishaupt_can/verdacht.h) und
[`regelwerk.h`](components/weishaupt_can/regelwerk.h), gemeinsame Bausteine in
[`regellogik.h`](components/weishaupt_can/regellogik.h), die
Schaltbefehle (Phasen, Warteschlange, Parser für `cmd/heizkreis`/`cmd/warmwasser`) und das
Status-JSON in [`befehle.h`](components/weishaupt_can/befehle.h), der Zustand des Boards in den
Klassen von [`kern.h`](components/weishaupt_can/kern.h) – reines C++ ohne ESPHome. Die Tests der
Klassen treiben den Code von v28 (als Vergleichsfassung im Test) und die Klassen mit denselben,
teils zufälligen Eingaben und verlangen gleiches Ergebnis; ebenso vergleicht ein Äquivalenztest die
Regeln von v29 (eingefroren in [`tests/referenz_v29/`](tests/referenz_v29/)) mit den Datenregeln
Frame für Frame – Logzeilen, Auslösungen, Ereignisse, gespeicherter Zustand. Getestet mit
ausgedachten Beispiel-Frames nach [PROTOKOLL.md](PROTOKOLL.md), zusammen mit den Parsern für
`cmd/regeln`, `cmd/regel` und `cmd/zusatz`. Lokal (braucht `g++` und
`curl`; ArduinoJson wird in der Board-Version 7.4.3 geladen und per SHA-256 geprüft):

```
tests/run.sh                                     # Regellogik, Klassen, Datei-Parser, Registertabelle
python3 werkzeuge/register_erzeugen.py --pruefen # erzeugte Dateien passen zu register.yaml
python3 tests/links.py                           # relative Links und Anker in allen .md-Dateien
```

**Registertabelle:** alle CAN-Objekte stehen nur in [`register.yaml`](register.yaml); daraus
erzeugt [`werkzeuge/register_erzeugen.py`](werkzeuge/register_erzeugen.py) die C++-Konstanten
(`components/weishaupt_can/register_gen.h`) und die Tabellen in PROTOKOLL.md und hier. Ablauf zum Ändern:
[INSTALL.md, Abschnitt 14](INSTALL.md#14-register-ändern).

Bei jedem Push laufen alle drei in [GitHub Actions](https://github.com/jvejmelka/weishaupt-wem-can-esphome/actions/workflows/ci.yml), dazu wird die
Firmware mit ESPHome 2026.9.0 und einer Dummy-`secrets.yaml` (aus `secrets.yaml.example`)
kompiliert – samt Bauteil `weishaupt_can`. Neue Regeln oder Änderungen an bestehenden bitte mit Test – der Fehler aus
v22–v24 (R1 kannte nur `0x40`, der WEM fragt mit `0xA4`) hätte so nicht passieren können.

## Mithelfen

Offene Punkte, die sich nur an anderen Anlagen oder zu anderer Jahreszeit klären lassen:

| Frage | was helfen würde |
|---|---|
| Heizkreis-Codes 6–8 (Komfort, Normal, Absenk) wurden nie am Bus gesehen | Mitschnitt einer Umschaltung auf diese Betriebsarten |
| Umschaltungen über WEM-Portal oder WEM-App wurden nie mitgeschnitten | Rohmitschnitt während einer Portal-Umschaltung (s. [PROTOKOLL.md](PROTOKOLL.md#passiv-erkennen-was-geht-und-was-nicht)) |
| Rücklauf-Kandidat `0x2533`/2 ist ungeprüft | Werte bei laufendem Brenner neben dem Display |
| Rückrechnung der Raumsoll-Stufe nur für AT 12,9–15,2 °C geeicht | Winterdaten (AT 0–8 °C, je Betriebsart 10–15 min stationär) |
| Was sendet ein Weishaupt-Raumgerät? | Mitschnitte, s. [IDEEN.md, Idee 4](IDEEN.md#gesucht-jemand-mit-weishaupt-raumgerät-am-bus) |

Bitte als [GitHub-Issue](https://github.com/jvejmelka/weishaupt-wem-can-esphome/issues), mit
Zusammenfassung statt Rohdateien (die können Seriennummern und Gerätekennungen enthalten).

## Kontakt

**Autor:** Juergen Vejmelka

Fragen, Fehlermeldungen und Rückmeldungen von anderen Anlagen bitte als
[GitHub-Issue](https://github.com/jvejmelka/weishaupt-wem-can-esphome/issues).

## Herkunft

Ausgangspunkt war [MenkeC/Weishaupt-C3supermini](https://github.com/MenkeC/Weishaupt-C3supermini)
(ohne Lizenz veröffentlicht). Übernommen sind Protokollangaben – Busgeschwindigkeit, Knotennummern,
PDOs, einzelne Register mit Faktoren; der Code dieses Repositorys ist neu geschrieben.
Registerwissen stammt außerdem aus den unter „Quellen“ genannten Projekten.

## Lizenz

[MIT](LICENSE) – © 2026 Juergen Vejmelka. Verwendung auf eigene Gefahr; keine Verbindung zu Weishaupt.
