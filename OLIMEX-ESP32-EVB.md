# Olimex ESP32-EVB (LAN) – Variante für die Weishaupt WTC-GW

> 🇬🇧 English version: [OLIMEX-ESP32-EVB.en.md](OLIMEX-ESP32-EVB.en.md)

Diese Variante stammt von **cg089** ([Fork cg089/weishaupt-wem-can-esphome](https://github.com/cg089/weishaupt-wem-can-esphome),
Zweig `olimex-esp32-evb`, 31.10.2026): eine fertige Konfiguration für das **Olimex ESP32-EVB** (Ethernet statt WLAN,
CAN-Transceiver onboard) und Funktionen, die eine Home-Assistant-Integration über die WEM-JSON-Schnittstelle bietet –
hier **direkt per CAN-Bus**. Im Fork steht alles in einer Datei; hier ist es in die Pakete des Projekts eingebaut,
damit beide Boards aus demselben Stand gebaut werden.

| Datei | Inhalt |
|---|---|
| [`olimex-weishaupt.yaml`](olimex-weishaupt.yaml) | Haupt-YAML für das Olimex ESP32-EVB: Board, Ethernet, Auswahl der Pakete, Dallas-Fühler |
| [`pakete/netz-lan.yaml`](pakete/netz-lan.yaml) | Netz-Anzeigen für Ethernet (Gegenstück zu `netz-wlan.yaml`) |
| [`pakete/kraiz-per-can.yaml`](pakete/kraiz-per-can.yaml) | weitere Werte per CAN, Warmwasser-Push, Fehlerblock des WEM – **erzeugt**, nicht von Hand ändern |
| [`pakete/kraiz-per-can.h`](pakete/kraiz-per-can.h), [`pakete/kraiz-per-can-texte.h`](pakete/kraiz-per-can-texte.h) | C++-Teil des Pakets: Abfrageplan und Decoder; Wertetexte (erzeugt) |
| [`pakete/dallas.yaml`](pakete/dallas.yaml) | 1-Wire-Bus für DS18B20-Fühler |
| [`secrets-olimex.yaml.example`](secrets-olimex.yaml.example) | Vorlage für `secrets.yaml` |
| [`werkzeuge/kraiz-per-can/`](werkzeuge/kraiz-per-can/) | Skripte (Node.js): kraiz-Objekte in CAN-Adressen umrechnen, Paket erzeugen |

Die Pakete `kraiz-per-can` und `dallas` laufen auch auf dem WeAct CAN485 (in `weishaupt-wem-can.yaml` einkommentieren).

## Unterschiede zum WeAct-Board

- **Ethernet** (LAN8720: MDC GPIO23, MDIO GPIO18, Takt extern auf GPIO0, PHY-Power GPIO12) statt WLAN. Paket
  `netz-lan` statt `netz-wlan`; WLAN-Wechsel, SSID und Signal entfallen.
- **CAN-Pins** des Olimex ESP32-EVB: TX **GPIO5**, RX **GPIO35** (laut [Schaltplan Rev I](https://github.com/OLIMEX/ESP32-EVB/files/11994875/ESP32-EVB_Rev_I_Schematic.pdf)),
  eingestellt über `can_tx_pin` / `can_rx_pin`.
- **Kein SD-Kartenleser und kein LittleFS** (`sd_karte: false`, `dateisystem: false`; das Board hat nur 4 MB Flash).
- Eingebunden: `kessel`, `netz-lan`, `warmwasser`, `wem-schalten` (Schalten per CAN ist ab Werk an),
  `warmwasser-schalten`, `kraiz-per-can`, `dallas`. Nicht eingebunden: `mqtt`, `app`, `verdacht`, `zusatz`, `json-lesen`.
- Verbindung zu Home Assistant über die ESPHome-API (kein MQTT).

## Ergänzungen

### Alle Werte der kraiz-Integration per CAN (`kraiz-per-can`)
Die JSON-Objekte der Integration [kraiz/hassio-weishaupt](https://github.com/kraiz/hassio-weishaupt) liegen am CAN-Bus
je Modul um einen festen Betrag verschoben (siehe [PROTOKOLL.md](PROTOKOLL.md)):

| JSON-Modul | CAN-Knoten | Versatz |
|---|---|---|
| MI 01 (SYSTEM0) | 1 | +0x100 |
| MI 02 (Heizkreis HZK0) | 1 | +0x400 |
| MI 03 (Warmwasser WW0) | 1 | +0x500 |
| MI 07 (Kessel WE0) | 2 | gleich |
| MI 09 / MX 01 (Kessel-Gerätemodul) | 2 | +0x100 |

Darüber werden 29 Werte gelesen, die das Paket `kessel` nicht liefert (Raum-/Vorlaufsollwerte Komfort/Normal/Absenk,
Status Heizkreis und Warmwasser, Betriebsphase WTC, Kaskade u. a.). Module, die die Anlage nicht hat (Heizkreis 2/3, Solar),
sind nicht enthalten. Die Abfrage läuft nur bei eingeschaltetem Schalter „Eigene CAN-Anfragen", höchstens eine Anfrage
alle 3 s, jedes Objekt höchstens alle 1 bis 10 Minuten; vom Knoten abgelehnte Objekte werden nicht erneut gefragt.

Das Paket ist erzeugt. Neu erzeugen (z. B. nach einer Änderung an der Auswahl in `kraiz_erzeugen.js`):

```
git clone https://github.com/kraiz/hassio-weishaupt
node werkzeuge/kraiz-per-can/kraiz_erzeugen.js hassio-weishaupt/custom_components/weishaupt_wtc/sensors.py .
```

### Warmwasser-Push
Ein Knopf schreibt `1` auf `0x2A49/2` des WEM (JSON `MI 03 / 0x2549/2` wie bei kraiz). Mindestabstand und Kontrolle am Bus
aus dem Paket `wem-schalten` gelten auch hier; der Schalter „Freies CAN-Schreiben“ gilt für diesen Knopf nicht
(festes Ziel, fester Wert). Nur an einer Anlage erprobt.

### Fehlerblock und Fehler-Klartext
Der 8-Byte-Fehlerblock des WEM (`0x273F`) wird alle 2 Minuten gelesen: Status, Fehlernummer, Modul und ein Klartext.
Die Emergency-Telegramme (`0x081`/`0x082`) wertet schon das Paket `kessel` aus („Meldung WEM“, „Meldung Kessel“).

Der Klartext kommt aus [`fehlercodes.yaml`](fehlercodes.yaml) – **eigene Kurztexte** des Projekts. Die Texte aus
Kapitel 10 der Weishaupt-Anleitung, die der Fork abgetippt enthält, sind hier **nicht** übernommen: Die Rechte daran
liegen bei Weishaupt. Wer die Originaltexte sehen will, erzeugt sie aus der eigenen Anleitung und baut dann selbst:

```
python3 werkzeuge/fehlertexte_aus_handbuch.py --header 83307201.pdf
esphome run olimex-weishaupt.yaml
```

Das schreibt `components/weishaupt_can/fehlertexte_original.h` (steht in `.gitignore`, gehört nicht ins Repo); die
Firmware zeigt dann die Originaltexte statt der eigenen. Der andere Weg – Texte auf der Einstellungsseite hochladen –
braucht das Dateisystem und geht auf diesem Board nicht.

**Ungeprüft:** ob die Nummer im Fehlerblock der Handbuchnummer entspricht und ob das Typ-Bit „Fehler" von „Warnung"
trennt; eine echte Störung lag noch nicht vor.

### Dallas-Temperaturfühler (DS18B20)
Fünf Fühler parallel an **GPIO16** (UEXT Pin 5, I²C-SCL): Umgebung, Warmwasser-Speicher oben, Vorlauf, Rücklauf
Heizkreis 1 und 2. **Die Adressen in `olimex-weishaupt.yaml` sind die des Autors – ersetzen!** Ein Tisch-Test mit Fühlern
ohne Adresse (`index: 0 …`) schreibt die Adressen beim Start ins Log; Zuordnung durch Anwärmen. Empfohlen: externer
4,7-kΩ-Pull-up gegen 3,3 V.

## Installation

1. Dieses Repository klonen (oder `olimex-weishaupt.yaml` und die Ordner `pakete/` und `components/` in das
   ESPHome-Verzeichnis kopieren).
2. `secrets-olimex.yaml.example` nach `secrets.yaml` kopieren und ausfüllen (`ota_password`, `web_password`,
   `wem_ip`, `wem_user`, `wem_pass`). Im Fork hieß `web_password` noch `web_server_password`.
3. Dallas-Adressen in `olimex-weishaupt.yaml` anpassen oder den Abschnitt `sensor:` und das Paket `dallas` entfernen.
4. `esphome run olimex-weishaupt.yaml` (erstes Mal per USB, dann OTA).
5. In der Weboberfläche oder in Home Assistant „Eigene CAN-Anfragen" einschalten, wenn das Mithören stabil läuft.

## Vergleich mit dem Fork

Die Fassung hier soll dasselbe ergeben wie `olimex-weishaupt.yaml` im Fork. Verglichen ist deshalb nicht der YAML-Text
(dort eine Datei, hier Pakete), sondern was ESPHome daraus macht – die Ausgabe von `esphome config`
(Fork `211bd9b` gegen dieses Repository, ESPHome 2026.9.1):

- **Alle 124 Entitäten des Forks bis auf eine kommen hier unverändert heraus** – 123 sind in jedem Feld gleich, auch im
  Programmteil (Lambdas): Name, ID, Einheit, Gruppe, Abfrage. Namen und IDs in Home Assistant bleiben also dieselben.
- Board, Ethernet, CAN-Bus-Pins, API, OTA, Logger, 1-Wire, Zeit, Intervalle und globale Variablen sind gleich.

Mit Absicht anders:

| Im Fork | Hier | Grund |
|---|---|---|
| Textsensor „Letzte Emergency-Meldung“ | „Meldung WEM“ und „Meldung Kessel“ | die Telegramme `0x081`/`0x082` wertet das Paket `kessel` aus |
| Fehler-Klartext aus `weishaupt-helfer.h` (Handbuchtexte) | aus `fehlercodes.yaml` (eigene Kurztexte) | Rechte an den Handbuchtexten, siehe oben |
| Secret `web_server_password` | `web_password` | wie in der WeAct-Variante |
| `weishaupt-helfer.h` | `pakete/kraiz-per-can.h`, `pakete/kraiz-per-can-texte.h` | Teil des Pakets |

[`tests/olimex-fork-soll.txt`](tests/olimex-fork-soll.txt) ist die Liste der Entitäten des Forks. Die CI prüft bei jedem
Stand, dass `olimex-weishaupt.yaml` sie weiterhin alle liefert. Selbst nachprüfen (mit dem Python, in dem ESPHome
installiert ist – das Skript braucht PyYAML):

```
cp secrets-olimex.yaml.example secrets.yaml      # falls noch keine secrets.yaml da ist
esphome config olimex-weishaupt.yaml > hier.cfg
python3 werkzeuge/olimex_vergleich.py --soll tests/olimex-fork-soll.txt hier.cfg

# voller Vergleich gegen den Fork, alle Felder und Abschnitte
git clone -b olimex-esp32-evb https://github.com/cg089/weishaupt-wem-can-esphome fork
cp fork/secrets-olimex.yaml.example fork/secrets.yaml
esphome config fork/olimex-weishaupt.yaml > fork.cfg
python3 werkzeuge/olimex_vergleich.py fork.cfg hier.cfg
```

## Stand der Prüfung

Am Olimex ESP32-EVB geprüft ist der **Stand des Forks** (cg089, Anlage mit WTC-GW …-B, ESPHome 2026.10):

- Die per CAN gelesenen kraiz-Werte stimmen mit der laufenden kraiz-Integration überein (Raum-/Vorlaufsollwerte,
  Betriebsart und Status HK1, Warmwasserstatus, Betriebsphase WTC, Druck, Volumenstrom).
- Warmwasser-Push über CAN wurde ausgelöst und lud den Speicher.
- Abgas- und Rücklauftemperatur: Das Projekt benennt `K2 0x2537` als Abgas und `K2 0x2533/2` als Rücklauf. kraiz
  nennt beide umgekehrt. Am Kesseldisplay (Rücklauf 68,4 °C, Abgas 67 °C) stimmt die Benennung des Projekts.

Die **Fassung in diesem Repository** (aus Paketen zusammengesetzt, Fehler-Klartext aus `fehlercodes.yaml`) wird in der
CI mit ESPHome 2026.9.1 kompiliert, ist auf einem Olimex ESP32-EVB aber **noch nicht gelaufen**.

## Lizenz und Quellen

- Projekt: MIT, © 2026 Juergen Vejmelka; die Olimex-Variante, `kraiz-per-can` und `dallas`: MIT, © 2026 cg089
  (siehe [`LICENSE`](LICENSE)).
- Objektnamen und Wertetexte sind aus kraiz/hassio-weishaupt abgeleitet: MIT, © 2026 Lars Kreisz
  (Lizenztext in [`LIZENZEN-DRITTER.md`](LIZENZEN-DRITTER.md)).
