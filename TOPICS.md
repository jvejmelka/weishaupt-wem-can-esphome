# MQTT-Topics – strukturiert (ab v27) und alt

Ab Firmware **v27** gibt es zwei Topics mit **festem JSON-Aufbau**. App, Grafana und Home Assistant
müssen damit keinen Freitext des Boards mehr deuten. Die bisherigen Text-Topics bleiben parallel
bestehen (Übergangszeit), gelten aber als **veraltet** – siehe [unten](#veraltete-text-topics).

Beide Topics sind reine Buchführung aus dem, was das Board ohnehin mithört oder liest:
**keine zusätzliche CAN-Anfrage, keine periodische Abfrage der Betriebsarten, kein WEM-Aufruf.**
Die Logik steht in [`befehle.h`](components/weishaupt_can/befehle.h) und ist in [`tests/`](tests/) getestet.

| Topic | Richtung | retained | Inhalt |
|---|---|---|---|
| `<gerät>/status/json` | Board → | ja | Zustand: Betriebsarten, Schaltbefehle, Bus, Firmware |
| `<gerät>/befehl/status` | Board → | ja | jede Phase jedes Schaltbefehls (letzte Meldung bleibt stehen) |
| `<gerät>/cmd/heizkreis` | → Board | nein | Klartext wie bisher **oder** JSON mit Kennung |
| `<gerät>/cmd/warmwasser` | → Board | nein | Klartext wie bisher **oder** JSON mit Kennung |

## `<gerät>/status/json`

Wird **nur bei Änderung** veröffentlicht, **höchstens alle 2 s**, und nach jeder
MQTT-Wiederverbindung einmal. Alle Felder sind immer vorhanden; unbekannt = `null`.
Zeiten sind Unix-Sekunden der Board-Uhr (SNTP), `null` solange die Uhr nicht gestellt ist.

```json
{
  "v": 1,
  "firmware": "v27 vom 27.09.2026",
  "zeit": 1790510646,
  "heizkreis": {
    "vorgabe": 1, "vorgabe_text": "Standby",
    "ist": "standby", "heizt": false, "statusbits": 4096,
    "quelle": "start", "zeit": 1790510643
  },
  "warmwasser": {
    "vorgabe": 2, "vorgabe_text": "Aus", "ladung": false,
    "soll_aktuell": 8.0, "soll_normal": 35.0,
    "quelle": "start", "zeit": 1790510645
  },
  "kessel": { "status": 0, "status_text": "Standby" },
  "schalten": {
    "laufend": null,
    "warteschlange": [],
    "frei_ab": null,
    "letzte": { "heizkreis": null, "warmwasser": null }
  },
  "bus": { "lebt": true, "anlaufpause": false, "letzter_frame_s": 0 }
}
```

| Feld | Bedeutung | Quelle am Bus |
|---|---|---|
| `heizkreis.vorgabe` | Heizkreis-Betriebsart 1–8 (1 Standby, 2–4 Zeitprogramm 1–3, 5 Sommer, 6 Komfort, 7 Normal, 8 Absenk) | Knoten 1 `0x2933`/2, gelesen nur bei Anlass |
| `heizkreis.ist` | `standby` / `zeitprogramm` aus dem Standby-Bit `0x1000` | PDO `0x1C1` (nur bei Änderung – nach einem Board-Start `null` bis zur ersten Änderung) |
| `heizkreis.heizt` | Bit `0x0040` der Statusbits | PDO `0x1C1` |
| `heizkreis.statusbits` | Statusbits `0x274D` als Zahl | PDO `0x1C1` |
| `*.quelle` | Anlass der letzten Lesung: `start`, `status` (cmd/status), `statusbits`, `regel R1` … `regel R5` / eigene Regel, `schaltbefehl`, `lesung` (sonst, z. B. Lesebefehl) | – |
| `*.zeit` | Zeitpunkt der letzten Lesung | – |
| `warmwasser.vorgabe` | 1 Ein, 2 Aus | Knoten 1 `0x2A20`/2 |
| `warmwasser.ladung` | Kessel im Warmwasserbetrieb (Kesselstatus 15) | PDO `0x182` |
| `warmwasser.soll_*` | Warmwasser-Soll aktuell / normal in °C | Knoten 1 `0x2A2C`/2, `0x2A39`/2 |
| `kessel.status` | Kesselstatus (0 Standby, 1 Aus, 10 Heizbetrieb, 15 Warmwasserbetrieb, 101 Kaminfeger, 104 Wartung) | PDO `0x182` |
| `schalten.laufend` | Befehl, der gerade am WEM bzw. in der Bus-Kontrolle ist (Aufbau wie unten) | – |
| `schalten.warteschlange` | vorgemerkte Befehle (höchstens einer je Ziel, Heizkreis zuerst) | – |
| `schalten.frei_ab` | ab wann der nächste Befehl gesendet werden darf (Sperrminute), sonst `null` | – |
| `schalten.letzte.heizkreis` / `.warmwasser` | zuletzt abgeschlossener Befehl je Ziel (bestätigt, gescheitert oder abgelehnt; ein ersetzter zählt nicht) | – |
| `bus.lebt` | letzter Frame < 30 s | – |
| `bus.anlaufpause` | Bus lebt, eigene Leseanfragen warten aber noch (nach Start 1 min, nach Busausfall 10 min) | – |
| `bus.letzter_frame_s` | Sekunden seit dem letzten Frame **zum Zeitpunkt der Veröffentlichung** (löst selbst keine Veröffentlichung aus) | – |

Messwerte (Temperaturen, Drücke …) stehen **nicht** hier, sondern wie bisher unter
`<gerät>/sensor/<name>/state` – sie ändern sich laufend und würden das Topic fluten.

## Schaltbefehle mit Kennung

```
<gerät>/cmd/heizkreis   {"id":17,"wert":"ZP2","quelle":"App"}
<gerät>/cmd/warmwasser  {"id":"app-18","wert":"Aus"}
```

| Feld | Pflicht | Werte |
|---|---|---|
| `wert` | ja | Heizkreis: `1`–`8`, Name (`"Zeitprogramm 2"`, `"Standby"` …) oder Kurzform `"ZP1"`–`"ZP3"`; Warmwasser: `1`/`2`, `"Ein"`/`"Aus"`, `true`/`false` |
| `id` | nein | Zahl oder Text (1–32 Zeichen). Wird unverändert zurückgemeldet (Zahl bleibt Zahl). Fehlt sie, vergibt das Board `b1`, `b2`, … |
| `quelle` | nein | Text bis 16 Zeichen (A–Z a–z 0–9 - _ / Leerzeichen), Vorgabe `MQTT` |

Felder mit `_` am Anfang werden ignoriert, alle anderen unbekannten Felder führen zur Ablehnung.
**Die alte Klartextform bleibt gültig** (`2`, `Zeitprogramm 1`, `Ein`, `Aus` …) und bekommt intern
eine Kennung `b…`. Auch Befehle aus der Weboberfläche und Home Assistant bekommen eine Kennung
(Quelle `Web/HA`).

## `<gerät>/befehl/status`

Jede Phase eines Befehls als eigene Meldung (retained – stehen bleibt die zuletzt gemeldete Phase
irgendeines Befehls; für den Zustand je Ziel `status/json` → `schalten` lesen):

```json
{"id":17,"ziel":"heizkreis","wert":3,"wert_text":"Zeitprogramm 2","quelle":"App",
 "phase":"pruefe_bus","ende":false,"grund":null,"text":null,"wem":"bestaetigt",
 "ist":null,"ist_text":null,"warten_s":null,"ersetzt_durch":null,"zeit":1790510700}
```

```
angenommen -> vorgemerkt -> gesendet -> pruefe_bus -> bestaetigt
                   |            |             \-> gescheitert  (nicht_uebernommen | keine_rueckmeldung)
                   |            \-> gescheitert (cm05)
                   \-> ersetzt   (neuerer Wunsch fuer dasselbe Ziel, ersetzt_durch = dessen id)
                   \-> gescheitert (geleert: Knopf "Warteschlange leeren")
abgelehnt (ungueltig) - nie in der Warteschlange, nie am WEM
```

| Phase | Bedeutung |
|---|---|
| `angenommen` | Befehl gültig, Kennung vergeben |
| `vorgemerkt` | in der Warteschlange; `warten_s` = Sekunden bis zur Sperrminute (0 = beim nächsten Sekundentakt) |
| `gesendet` | JSON an den WEM unterwegs |
| `pruefe_bus` | WEM hat geantwortet; `wem` = `bestaetigt` / `unklar` / `nicht_erreichbar`. **Der Bus entscheidet**, nicht die WEM-Antwort |
| `bestaetigt` | Kontroll-Lesung am Bus zeigt den gewünschten Wert (`ist`) |
| `gescheitert` | `grund`: `cm05` (WEM lehnt ab, keine Bus-Kontrolle), `nicht_uebernommen` (Bus zeigt anderen Wert, siehe `ist`/`ist_text`), `keine_rueckmeldung` (keine Antwort am Bus binnen 10 s), `geleert` |
| `ersetzt` | ein neuerer Wunsch für dasselbe Ziel hat diesen verdrängt (der neueste gewinnt) |
| `abgelehnt` | `grund` = `ungueltig`, `text` nennt den Fehler – z. B. unbekannter Wert |

`ende` ist `true` für `bestaetigt`, `gescheitert`, `ersetzt`, `abgelehnt`.
Warteschlange, Sperrminute (höchstens ein Befehl pro Minute) und Kontrolle am Bus sind unverändert,
siehe [PROTOKOLL.md](PROTOKOLL.md#ablauf-in-der-firmware).

**Home Assistant** (MQTT-Sensor mit JSON-Attributen), Beispiel:

```yaml
mqtt:
  sensor:
    - name: Heizkreis Vorgabe
      state_topic: weact-can485-weishaupt/status/json
      value_template: "{{ value_json.heizkreis.vorgabe_text }}"
      json_attributes_topic: weact-can485-weishaupt/status/json
      json_attributes_template: "{{ value_json.schalten | tojson }}"
```

## Broker-Rechte

Wer `weact-can485-weishaupt/#` lesen darf, liest auch die beiden neuen Topics – das App-Konto aus
dem [README](README.md#einrichten-1) braucht keine neue Zeile. Schreiben weiterhin nur auf
`cmd/heizkreis`, `cmd/warmwasser`, `cmd/status`.

## Veraltete Text-Topics

Bleiben in der Übergangszeit unverändert erhalten, werden aber nicht mehr erweitert. Neue
Auswertungen bitte auf `status/json` und `befehl/status` aufbauen.

| alt (Freitext) | Ersatz |
|---|---|
| `…/text_sensor/ergebnis_letzter_schaltbefehl/state` | `befehl/status` bzw. `status/json` → `schalten.laufend` / `schalten.letzte` |
| `…/text_sensor/warteschlange/state` | `status/json` → `schalten.warteschlange`, `schalten.frei_ab` |
| `<gerät>/schaltprotokoll` | `befehl/status` (jede Phase) |
| `…/text_sensor/status_gelesen/state` | `status/json` → `heizkreis.zeit`, `warmwasser.zeit`, `*.quelle`, `bus` |
| `…/text_sensor/heizkreis_status/state` | `status/json` → `heizkreis.ist`, `heizt`, `statusbits` |
| `…/text_sensor/heizkreis_betriebsart/state`, `…/warmwasser_betriebsart/state` | `status/json` → `heizkreis.vorgabe`, `warmwasser.vorgabe` |

Die Zahlencodes unter `…/sensor/*_code/state` (für Grafana/InfluxDB) bleiben ohne Ablaufdatum.
