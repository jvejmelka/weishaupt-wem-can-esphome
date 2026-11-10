> 🇩🇪 German original: [TOPICS.md](TOPICS.md). This is a translation; if in doubt, the German version is authoritative.

# MQTT topics – structured (from v27) and old

From firmware **v27** there are two topics with a **fixed JSON structure**. App, Grafana and Home Assistant
no longer have to interpret free text from the board. The previous text topics remain in parallel
(transition period), but are considered **deprecated** – see [below](#deprecated-text-topics).

Both topics are pure bookkeeping of what the board hears or reads anyway:
**no additional CAN request, no periodic polling of the operating modes, no call to the WEM.**
The logic is in [`befehle.h`](components/weishaupt_can/befehle.h) and is tested in [`tests/`](tests/).

| Topic | Direction | retained | Content |
|---|---|---|---|
| `<gerät>/status/json` | board → | yes | state: operating modes, switch commands, bus, firmware |
| `<gerät>/befehl/status` | board → | yes | every phase of every switch command (the last message stays) |
| `<gerät>/cmd/heizkreis` | → board | no | plain text as before **or** JSON with ID |
| `<gerät>/cmd/warmwasser` | → board | no | plain text as before **or** JSON with ID |

All other topics of the board (read request, scan, raw capture, rules, extra displays, SD files,
free CAN writing) are listed in the table in the [README](README.en.md#mqtt-commands).

## `<gerät>/status/json`

Published **only on change**, **at most every 2 s**, and once after every
MQTT reconnect. All fields are always present; unknown = `null`.
Times are Unix seconds of the board clock (SNTP), `null` as long as the clock is not set.

```json
{
  "v": 1,
  "firmware": "v42-dev vom 11.11.2026",
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
    "weg": "can",
    "letzte": { "heizkreis": null, "warmwasser": null }
  },
  "bus": { "lebt": true, "anlaufpause": false, "letzter_frame_s": 0 }
}
```

| Field | Meaning | Source on the bus |
|---|---|---|
| `heizkreis.vorgabe` | heating circuit operating mode 1–8 (1 Standby, 2–4 time program 1–3, 5 Summer, 6 Comfort, 7 Normal, 8 Setback) | node 1 `0x2933`/2, read only when there is a reason |
| `heizkreis.ist` | `standby` / `zeitprogramm` from the standby bit `0x1000` | PDO `0x1C1` (only sent on change); after a board start the board therefore reads the status bits once itself (node 1 `0x274D`), until then `null` |
| `heizkreis.heizt` | bit `0x0040` of the status bits | PDO `0x1C1` |
| `heizkreis.statusbits` | status bits `0x274D` as a number | PDO `0x1C1` |
| `*.quelle` | reason for the last read: `start`, `status` (cmd/status), `statusbits`, `regel R1` … `regel R5` / custom rule, `schaltbefehl`, `lesung` (otherwise, e.g. read request or manual fetch via JSON) | – |
| `*.zeit` | time of the last read | – |
| `warmwasser.vorgabe` | 1 On, 2 Off | node 1 `0x2A20`/2 |
| `warmwasser.ladung` | boiler in DHW mode (boiler status 15) | PDO `0x182` |
| `warmwasser.soll_*` | DHW setpoint current / normal in °C | node 1 `0x2A2C`/2, `0x2A39`/2 |
| `kessel.status` | boiler status (0 standby, 1 off, 10 heating mode, 15 DHW mode, 101 chimney sweep, 104 maintenance) | PDO `0x182` |
| `schalten.laufend` | command that is currently at the WEM or in the bus verification (structure as below) | – |
| `schalten.warteschlange` | queued commands (at most one per target, in order of arrival; up to v37 heating circuit first) | – |
| `schalten.frei_ab` | from when the next command may be sent (minimum interval to the previous one: via CAN by default <!--ab-werk:abstand_can-->10<!--/ab-werk--> s, via JSON <!--ab-werk:abstand_json-->60<!--/ab-werk--> s), otherwise `null` | – |
| `schalten.weg` | from v37: `"can"` if switch commands go to the WEM via CAN (switch „Schalten per CAN statt JSON“ ("switching via CAN instead of JSON"), since v42 by default <!--ab-werk:can_schalten-->on<!--/ab-werk-->), otherwise `"json"` | – |
| `schalten.letzte.heizkreis` / `.warmwasser` | most recently completed command per target (confirmed, failed or rejected; a replaced one does not count) | – |
| `bus.lebt` | last frame < 30 s | – |
| `bus.anlaufpause` | bus is alive, but own read requests are still waiting (1 min after start, 10 min after a bus outage) | – |
| `bus.letzter_frame_s` | seconds since the last frame **at the time of publication** (does not trigger a publication itself) | – |

Measurements (temperatures, pressures …) are **not** here but, as before, under
`<gerät>/sensor/<name>/state` – they change continuously and would flood the topic.

## Switch commands with ID

```
<gerät>/cmd/heizkreis   {"id":17,"wert":"ZP2","quelle":"App"}
<gerät>/cmd/warmwasser  {"id":"app-18","wert":"Aus"}
```

| Field | Required | Values |
|---|---|---|
| `wert` | yes | heating circuit: `1`–`8`, name (`"Zeitprogramm 2"`, `"Standby"` …) or short form `"ZP1"`–`"ZP3"`; DHW: `1`/`2`, `"Ein"`/`"Aus"`, `"an"`, `"on"`/`"off"`, `true`/`false` |
| `id` | no | number or text (1–32 characters). Reported back unchanged (a number stays a number). If it is missing, the board assigns `b1`, `b2`, … |
| `quelle` | no | text up to 16 characters (A–Z a–z 0–9 - _ / space), default `MQTT` |

Fields starting with `_` are ignored, all other unknown fields lead to rejection.
**The old plain-text form remains valid** (`2`, `Zeitprogramm 1`, `Ein`, `Aus` …) and is given
an ID `b…` internally. Commands from the expert interface and Home Assistant (source `Web/HA`) and from the app on the
board (source `App`) are given an ID as well.

## `<gerät>/befehl/status`

Every phase of a command as a separate message (retained – what stays is the most recently reported phase
of any command; for the state per target read `status/json` → `schalten`):

```json
{"id":17,"ziel":"heizkreis","wert":3,"wert_text":"Zeitprogramm 2","quelle":"App",
 "phase":"pruefe_bus","ende":false,"grund":null,"text":null,"wem":"bestaetigt",
 "ist":null,"ist_text":null,"warten_s":null,"ersetzt_durch":null,"zeit":1790510700}
```

```
angenommen -> vorgemerkt -> gesendet -> pruefe_bus -> bestaetigt
                   |            |             \-> gescheitert  (nicht_uebernommen | keine_rueckmeldung)
                   |            \-> gescheitert (cm05: WEM rejects)
                   \-> ersetzt   (newer request for the same target, ersetzt_durch = its id)
                   \-> gescheitert (geleert: button "Warteschlange leeren")
abgelehnt (ungueltig) - never in the queue, never at the WEM
```

| Phase | Meaning |
|---|---|
| `angenommen` | command valid, ID assigned |
| `vorgemerkt` | in the queue; `warten_s` = seconds until sending, as long as the minimum interval to the previous command is running (0 = at the next one-second tick) |
| `gesendet` | command on its way to the WEM – via CAN or via JSON, see `schalten.weg` in `status/json` |
| `pruefe_bus` | response of the WEM in `wem`: `bestaetigt` (via CAN acknowledgement `60`, via JSON `CM 04`), `unklar` (JSON only: response neither `CM 04` nor `CM 05`) or `nicht_erreichbar` (via CAN: no response). **The bus decides**, not the WEM's response |
| `bestaetigt` | verification read on the bus shows the requested value (`ist`) |
| `gescheitert` | `grund`: `cm05` (WEM rejects – via JSON with `CM 05`, via CAN with SDO abort `80`; no bus verification), `nicht_uebernommen` (bus shows a different value, see `ist`/`ist_text`), `keine_rueckmeldung` (no response on the bus within 10 s), `geleert` |
| `ersetzt` | a newer request for the same target has displaced this one (the newest wins) |
| `abgelehnt` | `grund` = `ungueltig`, `text` names the error – e.g. unknown value |

`ende` is `true` for `bestaetigt`, `gescheitert`, `ersetzt`, `abgelehnt`.
Queue, minimum interval (via CAN by default <!--ab-werk:abstand_can-->10<!--/ab-werk--> s, via JSON <!--ab-werk:abstand_json-->60<!--/ab-werk--> s) and verification on the bus:
see [PROTOKOLL.en.md](PROTOKOLL.en.md#sequence-in-the-firmware).

**Home Assistant** (MQTT sensor with JSON attributes), example:

```yaml
mqtt:
  sensor:
    - name: Heizkreis Vorgabe
      state_topic: weact-can485-weishaupt/status/json
      value_template: "{{ value_json.heizkreis.vorgabe_text }}"
      json_attributes_topic: weact-can485-weishaupt/status/json
      json_attributes_template: "{{ value_json.schalten | tojson }}"
```

## Broker permissions

Anyone who may read `weact-can485-weishaupt/#` also reads the two new topics – the app account from
the [README](README.en.md#as-a-docker-container-folder-app) needs no new line. Writing still only to
`cmd/heizkreis`, `cmd/warmwasser`, `cmd/status`.

## Deprecated text topics

They remain unchanged during the transition period, but are no longer extended. Please build new
integrations on `status/json` and `befehl/status`.

| old (free text) | Replacement |
|---|---|
| `…/text_sensor/ergebnis_letzter_schaltbefehl/state` | `befehl/status` or `status/json` → `schalten.laufend` / `schalten.letzte` |
| `…/text_sensor/warteschlange/state` | `status/json` → `schalten.warteschlange`, `schalten.frei_ab` |
| `<gerät>/schaltprotokoll` | `befehl/status` (every phase) |
| `…/text_sensor/status_gelesen/state` | `status/json` → `heizkreis.zeit`, `warmwasser.zeit`, `*.quelle`, `bus` |
| `…/text_sensor/heizkreis_status/state` | `status/json` → `heizkreis.ist`, `heizt`, `statusbits` |
| `…/text_sensor/heizkreis_betriebsart/state`, `…/warmwasser_betriebsart/state` | `status/json` → `heizkreis.vorgabe`, `warmwasser.vorgabe` |

The numeric codes under `…/sensor/*_code/state` (for Grafana/InfluxDB) remain without an expiry date.
