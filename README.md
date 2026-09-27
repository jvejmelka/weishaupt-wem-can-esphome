# Weishaupt WEM am CAN-Bus – ESPHome-Firmware

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
| `verdacht.yaml` | **Verdachts-Lesen (experimentell):** Betriebsarten nur lesen, wenn das Mithören eine Änderung vermuten lässt; feste Regeln R1–R4 und eigene Regeln, einstellbar per Web und Datei (braucht `warmwasser`). Details: [PROTOKOLL.md, Abschnitt 6a](PROTOKOLL.md#6a-verdachts-lesen-experimentell) | nein |
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
| `<gerät>/cmd/heizkreis` | `1`–`8` oder Name, z. B. `Zeitprogramm 1` | Heizkreis-Betriebsart (Warteschlange) |
| `<gerät>/cmd/warmwasser` | `Ein` / `Aus` | Warmwasser (Warteschlange) |
| `<gerät>/cmd/lesen` | `01 2933 02` (Knoten Index Sub, hex) | ein Objekt lesen |
| `<gerät>/cmd/scan` | `01 2A00 2AFF 3` | Bereich lesen, Antworten in `<gerät>/canraw` |
| `<gerät>/cmd/stop` | beliebig | Scan abbrechen |
| `<gerät>/cmd/status` | beliebig | Heizkreis- und Warmwasser-Betriebsart einmal vom Bus lesen (höchstens alle 10 s, nur Leseanfragen); Zeitpunkt in „Status gelesen“ |
| `<gerät>/cmd/regeln` | JSON-Datei, am besten retained (`mosquitto_pub -r -f regeln.json`) | Verdachts-Lesen einstellen: Hauptschalter, R1–R4, Obergrenze, eigene Regeln (Vorlage `regeln.json.example`) |
| `<gerät>/cmd/regel` | `NAME an` / `NAME aus` / `NAME loeschen` | eine Regel schalten (NAME = `verdacht`, `R1`–`R4` oder eine eigene Regel) |
| `<gerät>/schaltprotokoll` | (retained, vom Board) | letzte zehn Befehle, einer je Zeile |
| `<gerät>/regeln/stand` | (retained, vom Board) | aktiver Stand des Verdachts-Lesens als JSON, samt eigener Regeln |
| `<gerät>/verdacht/protokoll` | (retained, vom Board) | letzte zehn Auslösungen: Regel, gelesen, geändert ja/nein |

**Wer `cmd/regeln` oder `cmd/regel` schreiben darf, kann Leseanfragen auslösen** – nur Lesen an
Knoten 1, gedrosselt durch Mindestabstand und Obergrenze pro Stunde, aber eben Busverkehr. Das
App-Konto bekommt diese Rechte bewusst **nicht**; im Broker nur einem Verwaltungskonto geben.

Codes Heizkreis: 1 Standby, 2–4 Zeitprogramm 1–3, 5 Sommer, 6 Komfort, 7 Normal, 8 Absenk.

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
- [ANLAGE.md](ANLAGE.md) – Beispielanlage mit Wärmepumpe und PV
- [IDEEN.md](IDEEN.md) – was sich noch bauen ließe: Datenbank und Grafana, H2-Sperre,
  Raumsensoren, Raumgerät per Funk, Heizkörperventile, Anlage prüfen
- [CHANGELOG.md](CHANGELOG.md) – Änderungen je Firmware-Stand
- [LICENSE](LICENSE) – Lizenz

## Handy-App (optional, Ordner `app/`)

Eine schlanke Web-App (PWA) fürs Handy: eine schmale Spalte (höchstens 460 CSS-px), die die volle Höhe nutzt **ohne Scrollen** – auch auf quadratischen Displays.
Auf dem Startbildschirm installierbar („+ App“).

### Was sie zeigt

| Bereich | Inhalt | Quelle |
|---|---|---|
| Kopfzeile | grüner Punkt = letzter Abruf erfolgreich, rot = Problem; oranger Strich = Countdown bis zum nächsten Abruf (30 s) | App |
| ⚙ (Kopfzeile) | öffnet die Weboberfläche des Boards in einem neuen Tab – nur sichtbar, wenn `BOARD_URL` in der `.env` gesetzt ist. Die App reicht nichts durch; das Board fragt sein Passwort selbst ab und ist nur im LAN erreichbar | `.env`: `BOARD_URL` |
| Heizkreis | acht Knöpfe (Standby, ZP 1–3, Sommer, Komfort, Normal, Absenk). **Voll markiert = Ist**: die Betriebsart, die das Board am Bus gelesen hat (0x2933/2). Rechts vom Titel **„Zustand: …“** = Laufzustand aus den Statusbits (z. B. „Standby“, „Zeitprogramm, heizt“, „· WW lädt“) | Board: `heizkreis_betriebsart_code`, `heizkreis_status`; Knopf → `cmd/heizkreis` |
| ↻ (beim Heizkreis) | lässt das Board Heizkreis- und Warmwasser-Betriebsart einmal vom Bus lesen (die liest es sonst nur bei Anlass); die Statuszeile zeigt „angefordert …“ und dann „Status HH:MM:SS gelesen“ | Knopf → `cmd/status`; Board: `status_gelesen` |
| Warmwasser | Temperatur (Fühler **unten** im Speicher), Knöpfe EIN/AUS – **voll markiert = Ist** am Bus (0x2A20/2); rechts **„Zustand: lädt gerade / keine Ladung“** | Board: `warmwasser`, `warmwasser_betriebsart`, `warmwasser_aktiv`; Knopf → `cmd/warmwasser` |
| Vorgabe (gestrichelt) | **Vorgabe = eigener Schaltbefehl** über das Board, solange er nicht am Bus bestätigt ist: **vorgemerkt** (Warteschlange bzw. eben gedrückt), **gesendet** (Befehl an den WEM raus, Bus-Kontrolle steht aus). Nach „OK“ verschwindet die Strichelung, der Knopf wird voll markiert. **Rot gestrichelt** mit Kurztext: nicht übernommen / abgelehnt (CM=05) / keine Rückmeldung. **„von außen geändert“** am Ist-Knopf: der Bus steht auf etwas anderem als der letzte eigene erfolgreiche Befehl, ohne dass ein Befehl läuft (Display, Portal) | Board: `warteschlange`, `ergebnis_letzter_schaltbefehl`, `schaltprotokoll` |
| Kessel | Kesseltemperatur groß, daneben Vorlauf / Soll und Rücklauf; rechts **Brenner: aus / Vorlüften / an / Nachlüften** (an in Orange, nur mit Flamme) | Board: `kesseltemperatur`, `vorlauf_vpt`, `vorlauf_soll`, `ruecklauf`, `brennerphase` |
| Außen | Außentemperatur groß | Board: `aussentemperatur` |
| Statuszeile | „Aktualisiert HH:MM:SS · Status gelesen HH:MM“, dahinter „nächster Befehl ab …“, solange die Sperrminute läuft (höchstens zwei Zeilen); nach einem Knopfdruck 3 min lang der Fortschritt: vorgemerkt → gesendet → **OK** (grün) oder **NICHT übernommen / abgelehnt (CM=05) / keine Rückmeldung** (rot); Fehler wie „CAN-Board nicht erreichbar“ | Board: `ergebnis_letzter_schaltbefehl`, `warteschlange`, `status_gelesen` |
| Fußzeile | Build-Stand als Datum | App |

Messwerte, die länger als 10 min nicht aktualisiert wurden, zeigt die App als „--“ statt eines
alten Werts. Betriebsarten und Sollwerte sind davon ausgenommen – das Board liest sie nur bei
Anlass (nach dem Start, bei geänderten Statusbits, nach Schaltbefehlen).

### Wie sie arbeitet

- **Liest nur MQTT** vom Board (`<gerät>/sensor/+/state`, retained) und spricht **nie mit dem WEM**.
- **Schalten** geht als MQTT-Befehl an das Board; das Board setzt ihn mit Sperrminute,
  Warteschlange und Kontrolle am Bus um. Die App zeigt „vorgemerkt“, „gesendet“ und danach das Ergebnis der Bus-Kontrolle.
- Anmeldung mit Benutzer und Passwort aus der `.env` – **ohne beide startet die App nicht**.
  Die Sitzungen liegen in `data/` und überleben einen Neubau.
- Braucht Board-Firmware **ab v20** (Brennerphase). Ältere Firmware: der Brenner erscheint nur als An/Aus.

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
```

### Hinweise

- **Von außen nur hinter einem Reverse Proxy mit TLS.** Die App selbst spricht HTTP.
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
