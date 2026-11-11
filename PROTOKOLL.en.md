> 🇩🇪 German original: [PROTOKOLL.md](PROTOKOLL.md). This is a translation; if in doubt, the German version is authoritative.

# Protocol: CAN bus and WEM JSON

What happens on the CAN bus of a Weishaupt condensing boiler with a WEM system unit, what this
firmware picks up by passive listening, what it requests itself, and how it switches at the WEM – via CAN
and via its JSON interface. Measured on a **WTC-GW 15-B** with WEM system unit; where a statement comes only
from third-party sources or is merely plausible, this is noted.

What the firmware offers for operation (expert interface, all settings, rules, additional displays) is described in
[FIRMWARE.en.md](FIRMWARE.en.md); the captures of the WEM-RG2 room unit and the CAN tunnel in [VERSUCHE.en.md](VERSUCHE.en.md).

## Ways to the heating system

The ways the firmware talks to the heating system and which of them are on by default (as of v42 – every version
added one). **This table is the one place for it**; README, INSTALL and the other sections point here. The
“on”/“off” values in the “Default” column are generated from the code.

>
> | Way | What | Default |
> |---|---|---|
> | **CAN listening** | PDOs and the answers to requests the WEM sends to the boiler. The board sends nothing. | always on |
> | **CAN reading** (SDO, command byte `0x40`) | own read requests to the WEM (node 1) and the boiler (node 2): on a schedule, on occasion (operating modes, WEM only), for verification after switching, by hand | schedule <!--ab-werk:can_anfragen-->off<!--/ab-werk--> (switch “Eigene CAN-Anfragen”), everything else on |
> | **CAN writing to the WEM** (SDO, node 1) | switching heating circuit (`0x2933`/2) and hot water (`0x2A20`/2) – directly on the bus, without JSON | **<!--ab-werk:can_schalten-->on<!--/ab-werk-->** since v42 (v35–v41: could be enabled) |
> | **Free CAN writing** (experts) | single objects by hand, by default on the WEM (node 1) only – further nodes only after explicit release, never the boiler; `0x2000`–`0x5FFF` only | <!--ab-werk:can_frei-->off<!--/ab-werk--> |
> | **DHW push** (“Warmwasser-Push”; Olimex variant only, package `kraiz-per-can` by cg089) | one button writes `1` to `0x2A49`/2 of the WEM (node 1): fixed target, fixed value, with minimum interval and verification on the bus. The switch “Freies CAN-Schreiben” does not apply to this button ([OLIMEX-ESP32-EVB.en.md](OLIMEX-ESP32-EVB.en.md#dhw-push-warmwasser-push), German). | not included in the main file (WeAct); in the Olimex variant on button press only |
> | **JSON writing** (HTTP to the WEM) | fallback for switching when “Schalten per CAN statt JSON” is off. Up to v34 this was the only way. Also the field “Schreibbefehl über WEM (Experten)”, which always goes via JSON. | unused as long as nobody uses the fallback or the write field |
> | **JSON reading** (HTTP to the WEM) | by hand only: “Werte per JSON abrufen” (one request, from v42), files on the SD card. Never on a schedule – the WEM does not tolerate polling. | on button press only |
> | **CAN tunnel** (experiment, branch `can-tunnel`) | forwards unfiltered whatever the device at the other end sends – including the write requests of a room unit ([VERSUCHE.en.md, section 10b](VERSUCHE.en.md#10b-can-tunnel-over-wlan-experiment-branch-can-tunnel)) | not included |
>
> **The firmware never writes to the boiler (node 2)** – only read requests go there. Writing goes to the
> WEM. Details: [Listening and reading](#2-passive-listening-and-on-demand-reading),
> [Writing via CAN](#writing-via-can-from-v35-default-since-v42). The measured installation has been
> switching via CAN since 26 Oct 2026.

## Contents

- [Ways to the heating system](#ways-to-the-heating-system)
- [1. Overview](#1-overview)
- [2. Passive listening and on-demand reading](#2-passive-listening-and-on-demand-reading)
- [3. What is picked up by passive listening](#3-what-is-picked-up-by-passive-listening)
- [4. What is read on demand](#4-what-is-read-on-demand)
- [5. Writing at the WEM: via JSON and via CAN](#5-writing-at-the-wem-via-json-and-via-can)
- [6. Web interface: reading and writing](FIRMWARE.en.md#6-web-interface-reading-and-writing) → FIRMWARE.en.md
- [6a. Rules: reading operating modes on suspicion](FIRMWARE.en.md#6a-rules-reading-operating-modes-on-suspicion) → FIRMWARE.en.md
- [6b. Additional displays for the phone app](FIRMWARE.en.md#6b-additional-displays-for-the-phone-app) → FIRMWARE.en.md
- [7. Finding new objects: scan and diff](#7-finding-new-objects-scan-and-diff)
- [7a. Objects taken from other projects (unverified)](#7a-objects-taken-from-other-projects-unverified)
- [8. When the WEM locks up](#8-when-the-wem-locks-up)
- [9. Example: heating circuit to “Time program 1”](#9-example-heating-circuit-to-time-program-1)
- [10. Example: DHW off](#10-example-dhw-off)
- [10a. WEM-RG2 room unit on the bus](VERSUCHE.en.md#10a-wem-rg2-room-unit-on-the-bus-capture-november-2026) → VERSUCHE.en.md
- [10b. CAN tunnel over WLAN](VERSUCHE.en.md#10b-can-tunnel-over-wlan-experiment-branch-can-tunnel) → VERSUCHE.en.md
- [10c. Fault messages on the bus and error codes](#10c-fault-messages-on-the-bus-and-error-codes)
- [11. Sources](#11-sources)

## 1. Overview

What this looks like on a specific installation with heat pump and PV (setpoints, measurements):
[ANLAGE.en.md](ANLAGE.en.md).

### Bus parameters

| | |
|---|---|
| Bit rate | **50 kbit/s** |
| Identifier | 11 bit (standard frames) |
| Terminal on the boiler | H, L, −, + (only H and L are needed) |
| Protocol | CANopen-like: PDOs, SDOs, heartbeats |

### Nodes and modules

Which modules live on which node is listed in the system table on the WEM's SD card
(`/sd/systable.csv`, retrievable via the WEM's web interface). On the measured installation:

| Node | Device | Modules (JSON module index MI/MX) |
|---|---|---|
| **1** | WEM system unit | among others SYSTEM0 (MI 01), heating circuit **HZK0 (MI 02 / MX 00)**, domestic hot water **WW0 (MI 03)**, TERMINAL0 (MI 05), GATEWAY0 (MI 06), device module MI 09 / MX 00 |
| **2** | Boiler (WTC) | WE0 (MI 07), device module MI 09 / MX 01 |

There are no further nodes on this installation. Installations with a second heating circuit, solar
or extension modules have more entries – check your own `systable.csv` first.

**Installation with extension modules** (WTC-GW 25-B with WEM-EM-HK and WEM-EM-Sol, same WEM firmware
428CC226; `systable.csv` and table by [geronet1](https://github.com/geronet1/wem-python), thanks!):

| Node | Device | Firmware | Modules (MI/MX) | Heartbeat | Write channel into the WEM |
|---|---|---|---|---|---|
| 1 | WEM system unit | 428CC226 | as above | `0x701` | – |
| 2 | Boiler (WTC) | 334EE004 | WE0 (MI 07), MI 09/01, MI 0A/01 | `0x702` | `0x6C2` → `0x682` |
| **3** | **WEM-EM-HK** (heating circuit 2, mixed) | 451CC220 | heating circuit MI 02 / **MX 01**, MI 09/02, MI 0A/02 | `0x703` | `0x6C3` → `0x683` |
| **4** | **WEM-EM-Sol** | 452CC210 | SOLAR0 (MI 04), MI 09/03, MI 0A/03 | `0x704` | `0x6C4` → `0x684` |

Hence the rule: **node n announces itself with heartbeat `0x700 + n` and writes its readings into the
WEM via `0x6C0 + n`** (acknowledged on `0x680 + n`). The JSON → CAN offset also applies on the module
nodes: EM-HK flow temperature = JSON `MI 02 / MX 01 / 0x2507 / 02` = node 3 `0x2607`/2 (+0x100).
By this pattern the operating mode of heating circuit 2 would be node 3 `0x2633`/2 – **not verified**.

The WEM also serves other files from the SD card under `/sd/<file>` (same login as for JSON;
**fetch one at a time, never periodically**):

| File | Content |
|---|---|
| `systable.csv` | system table: nodes, modules (MI/MX), firmware files per module |
| `DLOG_ACT.CSV` | last recording of the display's data logger: header rows `DP-Nr`, `MI`, `MX`, `OX`, `OS`, `INT` (interval in s, hex), `UNIT`, `FF` (divisor), then `date;values` – on the measured installation an old recording, not the portal's |
| `YYMMDDxx.CSV` | daily files in the same format (according to BorgNumberOne) |
| `FAC_TEST.TXT` | factory test of the SD card (according to BorgNumberOne) |

The WEM provides no directory listing (`/sd/` → 404, web server “SEVENSTAX”); the names must be known.
From v36 the board fetches a file on a button press or via `cmd/sd_datei` (interval as for
switching via JSON, <!--ab-werk:abstand_json-->60<!--/ab-werk--> s by default) and evaluates `systable.csv`, e.g. “Knoten 01 (FW 428CC226): MI 01 SYSTEM0, MI 02 HZK0, …”.

### CANopen in three sentences

- **PDO** (Process Data Object): a device sends values on its own, cyclically or on change.
  No index, no subindex – the meaning depends solely on the CAN ID and the byte position.
- **SDO** (Service Data Object): request and response for an object (index 16 bit, subindex 8 bit).
  Request to node *n* on CAN ID **`0x600 + n`**, response on **`0x580 + n`**.
- **Heartbeat** on `0x700 + n`: “node *n* is alive”.

### Structure of an SDO frame (8 bytes)

| Byte | 0 | 1 | 2 | 3 | 4–7 |
|---|---|---|---|---|---|
| Content | Command | Index low | Index high | Subindex | Data, little endian |

| Command byte | Meaning |
|---|---|
| `40` | Read request (upload). For long objects the firmware also sends the read commands `60`/`70` and `A4`/`A3`/`A2`/`A1` (see below). |
| `41` | Announcement: object is longer than 4 bytes, length in bytes 4–7 |
| `4F` / `4B` / `47` / `43` | Response with 1 / 2 / 3 / 4 data bytes |
| `2F` / `2B` / `23` | Write request (download) with 1 / 2 / 4 data bytes. Sent by the WEM – and by the firmware to the WEM when it switches via CAN ([Writing via CAN](#writing-via-can-from-v35-default-since-v42)) |
| `60` | Acknowledgement of a write |
| `80` | Abort; bytes 4–7 = abort code, e.g. `06020000` object does not exist, `06090011` subindex does not exist |

Example: read request to node 1, object `0x2933` subindex 2:

```
CAN-ID 0x601   40 33 29 02 00 00 00 00
Antwort 0x581  4F 33 29 02 02 00 00 00     -> 1 Byte, Wert 2
```

(`Antwort` = response; `1 Byte, Wert 2` = 1 byte, value 2.)

### Long objects (more than 4 bytes): segments and block

A simple response carries at most 4 bytes. The other side announces longer objects (e.g. the WEM fault
block, 8 bytes, or texts) with `41` and the length; there are then two read procedures:

```
Segmented                                    Block
>601 40 3F 27 00 …     request               >601 A4 3F 27 00 08 …   request as block, 8 segments per block
<581 41 3F 27 00 08 …  "8 bytes"             <581 C6 3F 27 00 08 …   "ok, 8 bytes"
>601 60 …              segment 1 please      >601 A3 …               "go"
<581 00 xx xx xx xx xx xx xx                 <581 01 xx … / 82 xx …  segments 1, 2 (0x80 = last)
>601 70 …              segment 2 please      >601 A2 02 08 …         "received up to 2"
<581 1D xx …           last segment          <581 D9 …               "end" (+ CRC)
                                             >601 A1 …               "end acknowledged"
```

- **The WEM needs a short pause** between the announcement and the first segment request. If `60`
  arrives in the same instant, it answers with abort `05040001` (command unknown). With a 50 ms pause
  it delivers (from v33).
- The WEM also handles **block upload** as the server (from v34: read command with suffix `B`). It
  queries the boiler itself almost exclusively by block (`602: A4 …`).
- The firmware always uses block and segmented reads on the WEM (node 1); on the boiler (node 2) only while
  “Lange Lesebefehle auch am Kessel” is on (default <!--ab-werk:lang_am_kessel-->on<!--/ab-werk-->) – see next section.

### SDO channels: who asks whom

| Channel (request → response) | Server | who asks |
|---|---|---|
| `0x601` → `0x581` | WEM | only us (read commands, event reads, switching via CAN, verification after switching) |
| `0x602` → `0x582` | boiler | **the WEM, about 3× per second, mostly by block** – and possibly us |
| `0x6C2` → `0x682` | WEM | the boiler: writes its readings into the WEM (e.g. `0x2A29` DHW temperature, `0x2694`–`0x2699`) |
| `0x6C0 + n` → `0x680 + n` | WEM | further modules (n = node, e.g. `0x6C3` EM-HK, `0x6C4` EM-Sol, see [Nodes and modules](#nodes-and-modules)) |

CAN frames carry no sender: a request from us on `0x602` looks exactly like one from the WEM to the
boiler. In the middle of a WEM block dialogue it can disturb the WEM's query. Therefore: on the boiler
only **single** read commands, no series. On the measured installation,
scans over `0x602` made the WEM portal stall until the scan stopped. Since v35 the read command also reads
long objects (segments, block) on the boiler; “Lange Lesebefehle auch am Kessel” (default <!--ab-werk:lang_am_kessel-->on<!--/ab-werk-->)
switches that off. Nobody else asks on the WEM
(`0x601`) – long dialogues are harmless there.

Conversely, **only the WEM** writes to the boiler: seven values every ~5 s (`0x2709`, `0x270A`,
`0x252B`–`0x252F`: demand, flow and DHW setpoint). The board never writes to the boiler;
it writes to the WEM when it switches (from v35, default since v42, see [Writing via CAN](#writing-via-can-from-v35-default-since-v42)).

### Start-up, system table and identity

Capture of a cold start (unit switched off and on again), WTC-GW 15-B:

```
t = 0 s     702: 7F, 701: 7F         boiler and WEM report ("pre-operational")
t = 5–52 s  603…63C: 40 18 10 01 …   the WEM asks nodes 3 to 60, ~1 s each, for 0x1018/1 (identity)
            582: 43 18 10 01 9C 03 …  only the boiler answers (924)
            7E5 …                    LSS: search for unconfigured devices
                                     -> display: "Systemtabelle wird erstellt" (building system table)
t = 74 s    000: 01 00               NMT: start all nodes (repeated every 10 s)
            602: 2F/2B …             the WEM writes setpoints to the boiler every ~5 s
```

Whoever answers the roll call ends up in the system table. The board never answers – it is not a
CANopen node, needs no node ID and is not missed by the WEM. A room sensor or room unit (WEM-RF/RG),
on the other hand, is a node of its own and sends its own heartbeat `0x700 + n`.

Standard identity objects (readable with the read command; the texts on the boiler have not been read here – they
need segments on `0x602`, which the read command only sends there since v35):

| Object | WEM (node 1) | boiler (node 2) |
|---|---|---|
| `0x1008` device name | "Systemgeraet" | – |
| `0x1009` hardware | "69503511" | – |
| `0x100A` software | "428.226.000" (system table: `428CC226.FWU`) | – |
| `0x1018`/1 vendor | 924 (`0x39C`, Weishaupt) | 924 |
| `0x1018`/2 product | 5500 | 11 |

## 2. Passive listening and on-demand reading

The board obtains values in two ways:

| | Passive listening | On-demand reading (active) |
|---|---|---|
| What | PDOs, plus the SDO responses to requests that **the WEM itself** sends to the boiler | own SDO read requests `40 …` |
| Load | none – the board sends nothing | one request plus one response per value |
| Can be disabled | no, always running | yes, switch **“Eigene CAN-Anfragen”** (“own CAN requests”; default: <!--ab-werk:can_anfragen-->off<!--/ab-werk-->) |

The WEM polls the boiler constantly anyway – on the measured installation about three SDO requests
per second. That is why a large share of the boiler values reaches the board without it asking
itself. Which values these are is listed in [section 3](#3-what-is-picked-up-by-passive-listening).

### Rate of own requests

The figures come from Weishaupt itself:

| Source | Statement |
|---|---|
| WEM portal, data logger | shortest selectable interval **40 s** per value |
| Register table (data point list of the WEM Modbus gateway) | update classes **s / m / l = 30 s / 60 s / 10 min** per object |
| WEM Modbus gateway | polls the WEM itself at a **30 s rate** – but in doing so excludes the WEM portal |

So regular polling is not the problem in itself, but several pollers at the same time are (see
[section 8](#8-when-the-wem-locks-up)). The firmware requests no value more often than every 40 s:

| Values | Rate |
|---|---|
| Boiler: temperature, flue gas, flow, output, fan speed, burner | 40 s |
| Boiler: flow setpoint, volume flow, pressure; WEM: heating circuit flow, flow setpoint demand | 60 s |
| WEM: current room setpoint | 5 min |
| WEM: heating circuit operating mode, DHW operating mode and setpoints | only on an event (see below) |

Two requests within one rate group are 150 ms apart; the 60 s group starts offset by 7 s, the
5 min group offset by 13 s.

**Requests are sent only if:**

- the “Eigene CAN-Anfragen” switch is on (applies to the periodic groups, not to the event-triggered reads),
- the bus is alive (last received frame younger than 30 s),
- the **start-up pause** is over: 1 min after a reboot of the board; **10 min**
  (`anlaufpause_min`) if the bus disappeared during operation and comes back – the heating system
  was then without power, and the WEM is left alone while it boots.

**Event-triggered reads:** operating modes and setpoints reside in the WEM and never appear on the
bus by themselves. They are read even when “Eigene CAN-Anfragen” is off, but only

- once after the start-up pause,
- when the status bits change (PDO `0x1C1`, e.g. on a change at the display or in the portal) –
  with package `verdacht` (included in the main file) through the rules R3 and R5 instead,
- when another rule triggers ([FIRMWARE.en.md, section 6a](FIRMWARE.en.md#6a-rules-reading-operating-modes-on-suspicion)),
- after the firmware's own DHW switching command,
- on request: button “Status lesen” (“read status”; expert interface, phone app) or MQTT `<gerät>/cmd/status`.
  Requests are accepted at most every 10 s.

Event-triggered reads, too, are only sent while the bus is alive and after the start-up pause. An
event during the pause is kept (“angefordert – Anlaufpause, liest danach”, i.e. “requested – start-up
pause, reads afterwards”) and read once afterwards – since v42 for DHW as well; up to v41 the DHW
part already read during the pause.

The heating circuit operating mode is read at most every 2 min – except on explicit request and when a rule triggers (then the minimum interval of the rule applies). Known gap: a change
between two time programs at the display does not change the status bits and goes unnoticed until
the next event.

### Why JSON is never polled

The WEM's JSON interface is a translator between HTTP and the CAN bus – and it is fragile
(see [section 8](#8-when-the-wem-locks-up)). Regular polling via JSON disabled it five times on the
measured installation, each time until the system was power-cycled. The bus itself has no problem
with it: the WEM polls the boiler there three times per second. **Therefore always read on the bus –
and since v42 switch there as well: via CAN directly at the WEM. The JSON interface is only the fallback
and is never addressed unprompted.**

Exception from v42: **manual refresh** (package `json-lesen`: button “Werte per JSON abrufen” in the expert interface, “Aktualisieren” in the app on the board) for boards without a CAN cable.
Observed on 28 Oct 2026:
- The WEM answers **at most 7 objects per request**; the eighth always gets CM 05 – whichever object is there.
- Six requests within 7 minutes (each at least 60 s apart) – after that only empty answers; after a 10-minute pause
  all values again. The minimum interval is therefore <!--ab-werk:json_abstand-->300<!--/ab-werk--> seconds by default (at least 120); nothing is ever polled automatically.

### What remains if the WEM fails

| Failure | Consequence |
|---|---|
| only the JSON interface hangs (the case observed so far) | everything on the bus continues; the board reads as before. Switching via JSON is unavailable; whether the WEM still switches via CAN in this state (the default since v42) has not been measured yet |
| only the cloud connection and portal logger hang (October 2026) | regulation, heating curve and bus run normally; only the WEM's logger queries (flue gas, flow rate) are missing |
| the WEM is completely gone | all values are missing that are only picked up as responses to **WEM requests**, plus everything the WEM sends |

By CANopen convention, PDOs on `0x180 + n` come from node *n*; `0x182` therefore comes from the
boiler, `0x1C1` from the WEM. Which values actually keep coming during a complete WEM failure has
**not been tested**. The expectation:

| probably remain (PDOs, see [section 3](#3-what-is-picked-up-by-passive-listening)) | disappear |
|---|---|
| outside temperature, flow setpoint/“heat demand”, DHW, time of day, boiler status | everything that is only picked up as a response to **requests from the WEM** (pressure, output, heat quantities …) and the WEM write telegrams |

According to the Weishaupt manual WTC-GW 15…32-B (83307201), the boiler reports **F 91**
“communication fault system unit/burner control” when the connection to the system unit is lost (fault,
the unit locks out if operational safety is not ensured); the manual describes no emergency mode.
The heating curve lives in the WEM – without it the boiler gets no flow setpoint.

The boiler temperature is additionally contained in PDO `0x241`, bytes 2–3 – so it can also be
picked up without WEM requests. Which device sends which of the PDOs is not settled in every case. With own requests enabled, the board reads the boiler values from
[section 4](#4-what-is-read-on-demand) itself and is thus independent of the WEM.

## 3. What is picked up by passive listening

The tables in sections 3–5 are generated from [`register.yaml`](register.yaml)
([INSTALL.en.md, section 14](INSTALL.en.md#14-changing-registers)); please add extensions there.

**Status:** *verified* (**belegt**) = checked on the installation against the boiler display or an independent source;
*plausible* (plausibel) = values fit, but not cross-checked; *unconfirmed* (**unbestätigt**) = interpretation from third-party sources.

> **What counts as a cross-check – and what does not:** an app or the portal reading the same
> registers only confirms the shared assumption (circular reasoning). Independent sources are the
> **boiler display**, a **weather service** for the outside temperature, and physical criteria,
> e.g. **flow > boiler > return** while the burner is running.

### PDOs and write telegrams

The column “Name in the firmware” gives the (German) entity names used by the firmware; they are
left untranslated so they can be matched against the code.

<!-- REGISTER:BEGIN mithoeren -->
<!-- erzeugt aus register.yaml von werkzeuge/register_erzeugen.py - nicht von Hand aendern -->

| CAN ID | Bytes | Content | Factor | Name in the firmware | Status |
|---|---|---|---|---|---|
| `0x201` | 1–2 | Outside temperature, int16 | 0.1 °C | Aussentemperatur | **verified** (display, weather service) |
| `0x201` | 0 | System operating mode: 0 off, 1 standby, 2 summer, 3 automatic | – | Systembetriebsart | plausible |
| `0x241` | 0–1 | **Heating circuit flow setpoint**, int16 – the same value the WEM writes to `0x252C` (see `0x602`); 0 = no demand | 0.1 °C | Heizanforderung *(historical name, “heat demand”)* | plausible (0 with “Heizkreise inaktiv” / heating circuits inactive) |
| `0x241` | 2–3 | Boiler temperature (same as `0x2532`) | 0.1 °C | Kesseltemperatur | plausible |
| `0x241` | 6–7 | DHW temperature | 0.1 °C | Warmwasser | **verified** (display) |
| `0x181` | 0–5 | Hour, minute, year − 2000, month, day, weekday | – | Uhrzeit Heizung | **verified** |
| `0x182` | 0 | Boiler status (object `0x2530`): 0 standby, 1 off, 10 heating mode, 15 DHW mode, 101 chimney sweep, 104 maintenance | – | Kesselstatus, Warmwasser aktiv (status 15) | **verified** (for the idle state) |
| `0x1C1` | 2–3 | Status bits node 1 (object `0x274D`): `0x1000` heating circuit standby, `0x0040` heating mode, `0x0010` DHW charging ¹ | – | Heizkreis Status | plausible |
| `0x602` | SDO write telegram from the WEM to the boiler, object `0x252B`/0 | Demand to the boiler: 01 no demand, 0A heating, 0F DHW | – | Rules R2/R4 | plausible |
| `0x602` | SDO write telegram from the WEM to the boiler, object `0x252C`/0 | Heating circuit flow setpoint (also appears in PDO `0x241` B0–1) | – | not evaluated | plausible |
| `0x602` | SDO write telegram from the WEM to the boiler, object `0x252D`/0 | ramps up during DHW charging (observed up to 50 °C) | – | not evaluated | plausible |
| `0x602` | SDO write telegram from the WEM to the boiler, object `0x2709`/0 | 0x64 heat demand active, 0x32 overrun | – | not evaluated | plausible |
| `0x6C2` | SDO write telegram, object `0x2699`/1 | Return temperature VPT | 0.1 °C | Ruecklauf | **verified** (compared with the WEM portal, installer view, burner running, 26 Oct 2026: portal 35.7 °C, bus 35.6–36.7 °C) |
| `0x6C2` | SDO write telegram, object `0x2697`/1 | Flow temperature VPT | 0.1 °C | Vorlauf VPT | **verified** (compared with the WEM portal, installer view, burner running, 26 Oct 2026: portal 49.9 °C, bus 49.7–50.2 °C) |
| `0x6C2` | SDO write telegram, object `0x2698`/1 | Target output | 0.01 % | Sollleistung | **verified** (compared with the WEM portal, installer view, burner running, 26 Oct 2026: portal 66.98 %, bus 66–67 %) |

<!-- REGISTER:END mithoeren -->

Regarding `0x6C2`: this CAN ID carries write telegrams every few seconds (command `2B`/`2F`/`23`),
acknowledged on `0x682`. The object meanings come from
[geronet1/wem-python](https://github.com/geronet1/wem-python); the values fit on the installation
(return 17.9 °C at 18.5 °C flow, burner off). The same ID also carries `0x261E`/1–2, `0x2A29`/1–2,
`0x2694`/1 and `0x2695`/1 – not evaluated.

Also not evaluated are the PDOs `0x1C2` and `0x082` and the heartbeats `0x701`/`0x702`.

¹ `0x1C1` is sent **only on change**, not cyclically – the firmware stores the last state.
Watch the notation: bytes 2–3 are little-endian. Raw bytes `00 00 10 00` yield
m = `0x0010` (DHW charging), raw bytes `00 00 00 10` yield m = `0x1000` (standby).

### SDO responses to requests from the WEM

The board evaluates every SDO response on `0x581` and `0x582`, regardless of who asked. In the
capture, the WEM itself requested (with command byte `A4`) among others the following boiler
objects – so their values arrive even with own requests switched off:

<!-- REGISTER:BEGIN wem-antwort -->
<!-- erzeugt aus register.yaml von werkzeuge/register_erzeugen.py - nicht von Hand aendern -->

| Object (node 2) | Name in the firmware | requested by the WEM |
|---|---|---|
| `0x2532`/0 | Kesseltemperatur | yes |
| `0x2537`/0 | Abgastemperatur | yes |
| `0x2534`/0 | Leistung | yes |
| `0x2541`/0 | Brenner Status, Brenner, Brennerphase, Brennerstarts | yes, rarely |
| `0x2545`/0 | Vorlauf Soll | yes, rarely |
| `0x2713`/2 | Volumenstrom | yes, rarely |
| `0x2714`/2 | Anlagendruck | yes |
| `0x2726`/2 | Wärmemenge Vortag Heizung | yes – **only** this way, the firmware never requests it itself |
| `0x2727`/2 | Wärmemenge Vortag WW | yes – **only** this way, the firmware never requests it itself |
| `0x2728`/2 | Wärmemenge Vortag Gesamt | yes – **only** this way, the firmware never requests it itself |
| `0x2731`/2 | not evaluated (current heat output) | yes |
| `0x2533`/2 | not evaluated (return temperature VPT) | yes |
| `0x2536`/0 | Vorlauf | **no** – only arrives with own requests |
| `0x2540`/0 | Drehzahl | **no** – only arrives with own requests |

<!-- REGISTER:END wem-antwort -->

(Firmware names: Abgastemperatur = flue gas temperature, Leistung = output, Brenner = burner,
Vorlauf Soll = flow setpoint, Volumenstrom = volume flow, Anlagendruck = system pressure,
Wärmemenge Vortag Heizung/WW/Gesamt = heat quantity previous day heating/DHW/total,
Vorlauf = flow, Drehzahl = fan speed.)

The WEM requests further objects (among others `0x2530`, `0x2531`, `0x2739`, `0x2753`/2) that the
firmware does not evaluate. According to the Weishaupt register table and geronet1, `0x2533`/2 is the
return temperature VPT – **verified** on 26 Oct 2026 against the WEM portal (portal 35.7 °C, bus 35.6–36.7 °C; same value as `0x2699`).

How often the WEM requests an object varies greatly; “rarely” means: considerably less often than once
per minute. The list comes from a capture in summer mode and is not complete.

### Detecting passively: what works and what does not

Operating modes reside in the WEM and never appear on the bus as a value of their own. Captures
of mode changes at the display and via JSON (heating circuit and DHW, several times each) show:

| Change | detectable passively? | by what |
|---|---|---|
| Heating circuit standby ↔ time program | **yes** | `0x1C1`, bit `0x1000` |
| Time program 1 ↔ 2 ↔ 3, summer, comfort, normal, setback among each other | **no** – no signal of its own | only visible is whether a heat demand currently exists (`0x252B`, `0x252C`, `0x2709`). TP 2 ↔ TP 3 via JSON produced **zero** bus traffic; time programs differ only in their switching times |
| Room setpoint level (when heat is demanded) | limited | flow setpoint from `0x241` B0–1: comfort is about 2.2 K flow above normal. Back-calculation RT ≈ (FT − 1.4 + 1.1 · OT) / 2.1 (RT = room, FT = flow, OT = outside temperature), calibrated only for OT 12.9–15.2 °C. Setback only demands heat below OT ≈ RT − 4.5; summer looks like setback in mild weather |
| DHW on ↔ off | **that** a change happened: yes · **to what**: no | the WEM reads from the boiler (as SDO block upload, command byte `0xA4`) the sequence `2101/0A, 2102/0D, 2102/01, 2101/0A, 273F/01`, and 4–13 s later once more `2101/0A, 273F/01`. Only the full sequence with `2102/0D` + `2102/01` is specific (the short form also occurs at the start of charging; the full sequence without `273F` occurred once when the WEM rebooted). The direction only shows when charging starts: `0x252B` = 0F, ramp in `0x252D`, `0x1C1` bit `0x0010` |

**Open:** changes via the WEM portal or the WEM app have never been captured – whether they produce
the same sequences is unknown. The back-calculation needs winter data (OT 0–8 °C).

## 4. What is read on demand

All requests use command byte `0x40`. The size is given in the command byte of the response (`4F` 1 byte,
`4B` 2 bytes, `43` 4 bytes); the firmware reads exactly that many bytes, 2-byte values signed.

**“No value”:** in 2-byte values `0x8000` means “no value”. On the measured installation the boiler answers
`0x2527`/0 and `0x273B`/2 this way (`582: 4B 27 25 00 00 80 00 00`), and once the outdoor temperature in PDO
`0x201` and the hot water in PDO `0x241` showed `0x8000` for a moment. cg089 saw it for system pressure and
temperature after restarting the installation ([issue #2](https://github.com/jvejmelka/weishaupt-wem-can-esphome/issues/2)). The firmware discards `0x8000` and `0x7FFF`
in all 2-byte values from SDO frames and in the temperatures of the PDOs; the last valid value stays. Earlier
versions always read boiler responses as 4 bytes regardless of the length – `0x8000` then appeared as
327.68 bar or 3276.8 °C. There is no range filter per value (such as 0–3 bar): it would hide real
conditions like overpressure or frost at the outdoor sensor.

<!-- REGISTER:BEGIN eigene-anfragen -->
<!-- erzeugt aus register.yaml von werkzeuge/register_erzeugen.py - nicht von Hand aendern -->

| Node | Object | Factor | Rate | Class | Name in the firmware | Status |
|---|---|---|---|---|---|---|
| 2 | `0x2532`/0 | 0.1 °C | 40 s | s | Kesseltemperatur | **verified** (display) |
| 2 | `0x2537`/0 | 0.1 °C | 40 s | – | Abgastemperatur | **verified** (compared with the WEM portal, installer view, burner running, 26 Oct 2026: portal 49 °C, bus 49.3 °C; display 53 °C / bus 53.9 °C on 26 Sep) – some templates call it “return”, which is wrong |
| 2 | `0x2536`/0 | 0.1 °C | 40 s | – | Vorlauf | **verified** (compared with the WEM portal, installer view, burner running, 26 Oct 2026: portal “Vorlauftemperatur” 52.1 °C, bus 52.7 °C; boiler flow, not VPT) |
| 2 | `0x2534`/0 | 0.01 % | 40 s | – | Leistung | **verified** (compared with the WEM portal, installer view, burner running, 26 Oct 2026: portal “Istleistung” 66.88 %, bus 65.30 %) |
| 2 | `0x2540`/0 | 1 rpm | 40 s | – | Drehzahl | **verified** (compared with the WEM portal, installer view, burner running, 26 Oct 2026: portal “Gebläsedrehzahl” 3478 rpm, bus 3478 / 3380 rpm) |
| 2 | `0x2541`/0 | Phase: 0 off, 1 pre-purge, 2 control operation, 3 modulating operation, 4 post-purge | 40 s | s | Brenner Status, Brenner, Brennerphase, Brennerstarts | **verified** (display “Heizkreise inaktiv”) |
| 2 | `0x2545`/0 | 0.1 °C | 60 s | m | Vorlauf Soll | **verified** (compared with the WEM portal, installer view, burner running, 26 Oct 2026: portal 50 °C, bus 50.0 °C) |
| 2 | `0x2713`/2 | 1 l/h | 60 s | m | Volumenstrom | **verified** (compared with the WEM portal, installer view, burner running, 26 Oct 2026: portal “Volumenstrom VPT” 529 l/h, bus 531 l/h) |
| 2 | `0x2714`/2 | 0.01 bar | 60 s | m | Anlagendruck | **verified** (display) |
| 1 | `0x2907`/2 | 0.1 °C | 60 s | s | Vorlauf Heizkreis | plausible (JSON MI 02 `0x2507` actual flow temperature, +0x400) |
| 1 | `0x2640`/3 | 0.1 °C | 60 s | – | Vorlaufsoll Anforderung | plausible (50 °C observed with DHW on, 23 °C with off) |
| 1 | `0x2958`/2 | 0.1 °C | 5 min | m | Raumsoll aktuell | plausible (JSON MI 02 `0x2558`, +0x400) |
| 1 | `0x2933`/2 | Code 1–8, see [section 5](#5-writing-at-the-wem-via-json-and-via-can) | event, at most every 2 min | m | Heizkreis Betriebsart | **verified** (for standby, time program 1–3, summer. **Comfort, normal, setback never observed**) |
| 1 | `0x2A20`/2 | 1 on, 2 off | event | – | Warmwasser Betriebsart | **verified** (switched and seen on the display) |
| 1 | `0x2A2C`/2 | 0.1 °C | event | m | Warmwasser Soll aktuell | plausible (JSON MI 03 `0x252C`, +0x500; 8.0 °C with DHW off) |
| 1 | `0x2A39`/2 | 0.1 °C | event | m | Warmwasser Soll normal | plausible (JSON MI 03 `0x2539`, +0x500) |

<!-- REGISTER:END eigene-anfragen -->

(Firmware names: Vorlauf Heizkreis = heating circuit flow, Vorlaufsoll Anforderung = flow setpoint
demand, Raumsoll aktuell = current room setpoint, Heizkreis Betriebsart = heating circuit operating
mode, Warmwasser Betriebsart = DHW operating mode, Warmwasser Soll aktuell/normal = DHW setpoint
current/normal.)

“Brenner” (burner, 0/1) and the counter “Brennerstarts” (burner starts) only count with a flame (phase 2 or 3).
If “Eigene CAN-Anfragen” is switched off, the values that were only kept current by own requests
go to “unknown”; if the WEM requests one of them itself, it comes back via passive listening.

Feedback from other installations is welcome – especially measurements with the burner running.

<a id="5-writing-via-the-wem-json"></a>

## 5. Writing at the WEM: via JSON and via CAN

First the way through the WEM's JSON interface (the only one up to v34, today the fallback and the way of the write field), then the
[mapping JSON object → CAN object](#mapping-json-object--can-object) and
[writing via CAN](#writing-via-can-from-v35-default-since-v42), which the firmware uses for switching by default since v42.

### The request

```
POST http://<WEM>/ajax/CanApiJson.json
Authorization: Basic <Benutzer:Passwort>      Werkseinstellung, siehe secrets.yaml.example
Referer: http://<WEM>/
Content-Type: application/json

{"ID":"12345678","SRC":"DDC","CAPI":{"NN":1,"N01":{"VG":"030200253302000102"}}}
```

(`<Benutzer:Passwort>` = `<user:password>`, factory default, see `secrets.yaml.example`.)

The firmware sets `ID` from the uptime in milliseconds. The response has the same structure with
`"SRC":"SYS"`; the `VG` field is evaluated.

### The VG field

| Part | CM | MI | MX | OX | OS | VS | VA |
|---|---|---|---|---|---|---|---|
| Meaning | Command | Module | Module instance | Object | Sub-object | Length of VA in bytes | Value |
| Example | `03` | `02` | `00` | `2533` | `02` | `0001` | `02` |

| CM | Meaning |
|---|---|
| `01` | read (request) |
| `02` | read response |
| `03` | write (request) |
| `04` | write acknowledgement |
| `05` | error – object unknown or not writable |

For switching and writing the firmware sends `CM 03`; it sends `CM 01` only for the manual refresh
(package `json-lesen`, from v42), never unprompted. It evaluates the first two characters of the
response: `04` or `02` counts as confirmed, `05` as rejected.

### What the firmware writes

<!-- REGISTER:BEGIN schreiben -->
<!-- erzeugt aus register.yaml von werkzeuge/register_erzeugen.py - nicht von Hand aendern -->

| Target | MI | MX | OX | OS | VS | Values | Observed |
|---|---|---|---|---|---|---|---|
| Heating circuit operating mode | `02` | `00` | `0x2533` | `02` | `0001` | 1 standby, 2 time program 1, 3 time program 2, 4 time program 3, 5 summer, 6 comfort, 7 normal, 8 setback | 1–5; **6–8 never** |
| DHW operating mode | `03` | `00` | `0x2520` | `02` | `0001` | 1 on, 2 off | both |

<!-- REGISTER:END schreiben -->

(German mode names as shown on the display and in the firmware: Standby, Zeitprogramm 1/2/3,
Sommer, Komfort, Normal, Absenk; DHW: Ein, Aus.)

The heating circuit codes are listed this way in the Weishaupt register table. The DHW operating mode
is not listed there; it was found by scan and diff (see [section 7](#7-finding-new-objects-scan-and-diff)).

Further objects can be written via the write field of the expert interface, but only in the
modules MI 01, 02 and 03 (see [FIRMWARE.en.md, section 6](FIRMWARE.en.md#6-web-interface-reading-and-writing)).

### Verified read objects: heat quantities

The firmware does not read these objects; its manual refresh fetches seven other values
([Why JSON is never polled](#why-json-is-never-polled)). If you read them via JSON: these objects respond on the
WTC-GW 15-B with `CM 02` (all **MI 09 / MX 01**, OS `02`, 4 bytes):

<!-- REGISTER:BEGIN json-lesen -->
<!-- erzeugt aus register.yaml von werkzeuge/register_erzeugen.py - nicht von Hand aendern -->

| OX | Content | Factor | on the bus |
|---|---|---|---|
| `0x2626` | Heat quantity previous day, heating | 0.01 kWh | node 2 `0x2726`/2 |
| `0x2627` | Heat quantity previous day, DHW | 0.01 kWh | node 2 `0x2727`/2 |
| `0x2628` | Heat quantity previous day, total | 0.01 kWh | node 2 `0x2728`/2 |
| `0x2631` | Current heat output | 0.01 kW | node 2 `0x2731`/2 |

<!-- REGISTER:END json-lesen -->

Under **MI 07 / MX 00** the same OX return `CM 05` – see the cautionary example in
[section 8](#resulting-rules).

### Mapping JSON object → CAN object

The same object that is addressed via JSON is located on the bus on a fixed node with a
fixed offset. Measured on the WTC-GW 15-B:

| JSON module | CAN node | Offset | Example JSON → CAN |
|---|---|---|---|
| MI 07 (boiler WE0) | 2 | identical | `07 00 2532 00` → node 2 `0x2532`/0 boiler temperature |
| MI 09 / MX 01 (boiler device module) | 2 | +0x100 | `09 01 2614 02` → node 2 `0x2714`/2 system pressure |
| MI 01 (SYSTEM0) | 1 | +0x100 | `01 00 261E 00` → node 1 `0x271E` system operating mode (value 3 = automatic read) |
| MI 02 (heating circuit HZK0) | 1 | +0x400 | `02 00 2533 02` → node 1 `0x2933`/2 heating circuit operating mode |
| MI 03 (DHW WW0) | 1 | +0x500 | `03 00 2520 02` → node 1 `0x2A20`/2 DHW operating mode |

The offsets apply per module, not generally: for HZK0 and WW0, +0x100 explicitly does **not** apply.
Other installations (second heating circuit, solar) may have different mappings.

Confirmed on further objects on 2026-10-26 (JSON number from BorgNumberOne's table or the gateway
manual, converted, read singly on the bus):

| JSON | CAN | Meaning | read |
|---|---|---|---|
| `01 00 259C 01` | node 1 `0x269C`/1 | heat demand heating | 35.3 °C = PDO `0x241` |
| `01 00 259D 01` | node 1 `0x269D`/1 | heat demand DHW | 0 (no charging) |
| `01 00 26B1 02` | node 1 `0x27B1`/2 | outdoor temperature | 8.1 °C (PDO 8.0) |
| `01 00 263F 00` | node 1 `0x273F`/0 | **fault block**, 8 bytes (fault/warning bits, fault number, module) | `00 00 00 00 00 00 00 00` – nothing pending |
| `02 00 2559 02` | node 1 `0x2959`/2 | current flow setpoint heating circuit | 35.3 °C |
| `03 00 2569 02` | node 1 `0x2A69`/2 | "DHW status" | 1 = "time programme – normal" according to kraiz's value list (see below) |

Added on 10 Nov 2026: `02 00 258B 02` = node 1 `0x298B`/2, the **measured room temperature of the assigned room
unit** (0.1 °C, signed; `0x8000` without an assignment) – read via CAN here, confirmed via JSON by PiLLoX on a
WTC-GB 25-B ([comment](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/issues/4#issuecomment-6781466298)), see
[VERSUCHE.en.md](VERSUCHE.en.md#assignment-as-room-sensor-and-measured-room-temperature-in-the-wem).

On `03 00 2569 02` ("DHW status"): the gateway's Modbus manual lists the corresponding register 130 as
"warning/fault pending"; our value 1 with an empty fault block did not fit that. The integrations by
[kraiz](https://github.com/kraiz/hassio-weishaupt) and [JS-DE-Tech](https://github.com/JS-DE-Tech/hacs-weishaupt-lan)
read the object with the value list of the DHW status: 0 standby inactive, 1 time programme normal, 2 time programme
setback, 3 DHW charge active, 4 service, 5–7 alternative energy, 8 frost protection, 9 standby via the system
programme selector, 10/12/13 standby/normal/setback via input H2 (11 is missing). Our package `kraiz-per-can` uses
the same list. PiLLoX confirmed two values on a WTC-GB 25-B: 3 during a displayed DHW charge, 0 in standby
([comment of 11 Nov 2026](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/issues/4#issuecomment-6793283297)). Our value 1 fits
"time programme – normal". The remaining values are not confirmed on the built-in system device.

Guess at the origin of the offsets: the JSON object number is **module-relative** (`0x2533` is the
operating mode in the heating circuit, the VPT return temperature in the boiler); in a node's CAN
object dictionary each module is shifted by a fixed amount. Slots +0x200/+0x300 on the WEM probably
belong to modules that the measured installation does not have. **Help wanted:** anyone with a second
heating circuit, solar or room units can check with single read commands on the WEM (`0x601`) where
their objects are.

<a name="writing-via-can-from-v35-off-by-default"></a>

### Writing via CAN (from v35, default since v42)

Up to v34 the firmware never wrote via SDO on the bus: switching went through JSON, the bus served
for verification. Since v35 it can switch heating circuit and hot water directly via CAN on the WEM,
and **since v42 this is the default** – the WEM accepts the command at once, and its fragile JSON
interface stays untouched. JSON is the fallback (switch off). Three switches in the expert interface
(the first two in group “Schreiben per CAN”, the third under “Lesebefehl (nur lesen)”):

| Switch | default | effect |
|---|---|---|
| Schalten per CAN statt JSON (Heizkreis, Warmwasser) | **<!--ab-werk:can_schalten-->on<!--/ab-werk-->** (since v42; v35–v41 off) | app buttons, `cmd/heizkreis`, `cmd/warmwasser` write via SDO download to the WEM: `0x2933`/2 or `0x2A20`/2, 1 byte. Queue and bus verification as on the JSON path; interval to the previous command <!--ab-werk:abstand_can-->10<!--/ab-werk--> s by default (via JSON <!--ab-werk:abstand_json-->60<!--/ab-werk--> s); WEM abort = “rejected” |
| Freies CAN-Schreiben (Experten) | <!--ab-werk:can_frei-->off<!--/ab-werk--> | input field and `cmd/can_schreiben`: `NODE IDX SUB VALUE [LENGTH]` or `NODE IDX SUB = B1 B2 …` (hex, up to 64 bytes, from 5 bytes segmented with a 50 ms pause). **By default node 01 only, 2000–5FFF only** – never the boiler, never communication objects (0x1000–0x1FFF). Further nodes: see below |
| Lange Lesebefehle auch am Kessel | **<!--ab-werk:lang_am_kessel-->on<!--/ab-werk-->** | segmented/block reads also on node 2 (there the board shares the channel with the WEM) |

**Releasing further nodes (from v42).** The text field “Freies CAN-Schreiben: erlaubte Knoten (hex, ab Werk 01)” in
the same group lists the nodes free CAN writing may send to – for example `01 04` for the WEM and a room unit, or just
`04`. The list survives a restart.

- Nodes `02` (boiler) and `00` (all) cannot be entered. With an invalid list the board sends nothing.
- The object range 2000–5FFF applies to every node, and the switch “Freies CAN-Schreiben” remains the second barrier.
- Switching heating circuit and DHW always goes to the WEM, regardless of this list.
- The board only sends once the SDO channel of the target node has been quiet for 200 ms; if it is busy for 3 s, the
  board reports “nicht gesendet” (not sent). This keeps the command from landing between another participant's request
  and its reply.
- The field is in the expert interface. With the `mqtt` package it can, like every switch and input field, also be
  reached through the broker – so keep write permissions there tight. After an experiment enter `01` again.

Tried on the bench with an RG2 (node 4): [VERSUCHE.en.md, restarting the RG2 via CAN](VERSUCHE.en.md#restarting-the-rg2-via-can-0x2559).

**Confirmed on the WTC-GW 15-B (WEM 428.226) on 2026-10-26:**

| Command | WEM | effect |
|---|---|---|
| `601: 2F 33 29 02 02` (heating circuit = TP 1, same value) | `60` confirmed | none |
| `601: 2F 33 29 02 01` (standby) | `60` | within 1 s heat demand 0, boiler off; display “Heizkreise inaktiv”, mode “Standby”, “Heizung aus”, “Warmwasser aus”; app standby |
| `601: 2F 33 29 02 02` (back to TP 1) | `60` | boiler heats again, display “Automatik/Heizbetrieb” |
| `601: 2F 20 2A 02 02` / `01` (DHW off/on, via the switch) | `60` | current DHW setpoint 8 °C or 35 °C, bus verification confirmed |

So the WEM accepts a value written via CAN like one written via JSON (control, display, app). Open:
portal, behaviour after a restart, other installations. Every changed value probably ends up in the
WEM's EEPROM – never write periodically. Other reports that the devices reject SDO writes probably
concerned other objects or the boiler.

### Sequence in the firmware

1. A request arrives via the app on the board, the expert interface, Home Assistant or MQTT
   (`<gerät>/cmd/heizkreis`, `<gerät>/cmd/warmwasser`) and lands in the **queue**: at most one entry
   per target, the newest request replaces the old one; commands are sent in the order of arrival
   (up to v37: heating circuit before DHW).
2. Once per second the firmware checks the queue. Between two commands there is a **minimum
   interval** from the last send: via CAN <!--ab-werk:abstand_can-->10<!--/ab-werk--> s by default, via JSON <!--ab-werk:abstand_json-->60<!--/ab-werk--> s (both adjustable,
   limits in the table under [Einstellungen](FIRMWARE.en.md#einstellungen-settings)).
3. **Via CAN (default):** one SDO download to the WEM (`0x601`, `0x2933`/2 or `0x2A20`/2, 1 byte).
   After 1 s the firmware evaluates the response: acknowledgement `60` = confirmed; abort `80` =
   “rejected”, the operation ends; no response = the bus is checked anyway.
   **Via JSON (switch off):** `CM 03` to the WEM, the response is displayed; with `CM 05` the
   operation ends (“rejected”).
4. Otherwise the firmware reads the target object on the bus (node 1, `0x2933`/2 or `0x2A20`/2) – via
   CAN **3 s** after sending, via JSON **3 s** after the response. This single verification read is
   sent even with own requests switched off.
5. **The bus decides, not the WEM response:** if the value read matches the request,
   the result is “OK”, otherwise “NICHT übernommen” (not applied). If no response arrives within 10 s:
   “keine Rückmeldung vom Bus – Ergebnis unbekannt” (no feedback from the bus – result unknown). The bus is also
   checked after an empty or unclear WEM response – it has been observed that the switch took place despite an empty response.
6. The result appears in **“Ergebnis letzter Schaltbefehl”** (result of last switching command) and in the
   **“Schaltprotokoll”** (switching log) (plain text, deprecated), and from v27 onwards in structured form on
   **`<gerät>/befehl/status`** (every phase, with ID) and in **`<gerät>/status/json`** → `schalten` – structure in
   [TOPICS.en.md](TOPICS.en.md).

## 6. Web interface: reading and writing

Moved to [FIRMWARE.en.md](FIRMWARE.en.md#6-web-interface-reading-and-writing).

## 6a. Rules: reading operating modes on suspicion

Moved to [FIRMWARE.en.md](FIRMWARE.en.md#6a-rules-reading-operating-modes-on-suspicion).

## 6b. Additional displays for the phone app

Moved to [FIRMWARE.en.md](FIRMWARE.en.md#6b-additional-displays-for-the-phone-app).

## 7. Finding new objects: scan and diff

This is how the heating circuit operating mode `0x2933`/2 and the DHW operating mode `0x2A20`/2 were found –
and from them the offsets in [section 5](#mapping-json-object--can-object) were derived.

1. **Look at the system table.** The WEM's `/sd/systable.csv` shows which module lives on which
   node. This tells you where to search (heating circuit and DHW: node 1).
2. **Listen to what the WEM itself requests.** With the switch “CAN-Rohmitschnitt nach MQTT”, record
   `<gerät>/canraw` for a while. The WEM's requests on `0x602` show which objects
   it considers important.
3. **Scan state A.** Read a range via MQTT, e.g.
   `<gerät>/cmd/scan` with `01 2A00 2AFF 3` (node 1, index `0x2A00` to `0x2AFF`, subindex 0–3).
   The board sends a read request every 25 ms; the responses land in `<gerät>/canraw`.
   Abort with `<gerät>/cmd/stop`.
4. **Change exactly one thing** – at the display or with a single JSON command, e.g. heating circuit
   from standby to time program 1, or DHW from on to off.
5. **Scan state B**, same range.
6. **Compute the diff:** merge the responses of both scans by (node, index, subindex) and
   output only the changed values. Whatever changes is a candidate. Abort responses (`80 …`)
   mark objects that do not exist.
7. **Confirm individually:** read the candidate with the read command, switch again, read again.
8. **Cross-check at the display.**

Result on the measured installation: standby → time program 1 changed node 1 `0x2933`/2 from 1 to
2 – the JSON value `0x2533`/2 plus `0x400`. DHW on → off in the range `0x2A00`–`0x2AFF`
changed `0x2A20`/2 from 1 to 2 – an object that is not in any published list.

> **Scan only on the bus, never via JSON – and on the bus only on the WEM (node 1).** There, read scans with
> many abort responses showed no impairment on the measured installation. On the boiler (node 2, `0x602`),
> by contrast, the portal stalled for as long as a scan was running
> ([failure no. 10](#observed-failures-wtc-gw-15-b)) – only single read commands there. Via JSON, every request to a
> non-existent object consumes one of ten slots until the next power cycle
> (see [section 8](#8-when-the-wem-locks-up)). A JSON scan will reliably disable the interface.

A scan generates a lot of bus traffic (40 requests per second). Keep ranges small and only scan
when necessary.

## 7a. Objects taken from other projects (unverified)

Others have found objects on their installations that are missing from the tables above. They are listed
here so that nobody has to search for them a second time – with our own wording and the source for each
object. **Status of every row: taken over, unverified.** None of them has been read and compared on the
measured installation; scaling and meaning are those of the source. Once an object is verified, it moves
with its evidence into the tables of sections 3 and 4.

- Only objects of modules that exist on the measured installation are included (boiler, system, heating
  circuit 1, hot water). Objects for a second heating circuit, extension modules and solar are in the
  sources – most completely in [`wem_codes.py`](https://github.com/geronet1/wem-python/blob/main/wem_codes.py) by geronet1 (just under
  200 objects for the heating circuit extension module and solar; not checked on any installation here).
- Where the source gives a JSON address, the CAN index is converted with the offsets from
  [section 5](#mapping-json-object--can-object). The offsets were measured on a WTC-GW 15-B; whether they
  hold for every object of a module has only been checked for the examples listed there.
- The network objects of the WEM (module MI 06) are known as JSON addresses only and have their own table
  further down – without values, because those would describe your own home network.
- The system module also contains objects for buffer tank, cascade, plate heat exchanger and circulation.
  Whether they return a value on an installation without these functions is unverified.
- Many boiler objects are installer-level settings. This is about reading only: the firmware has no
  write commands of its own to the boiler (node 2); switching via CAN goes to the WEM as well.
- The list is maintained in [`register-uebernommen.yaml`](register-uebernommen.yaml).

<!-- REGISTER:BEGIN uebernommen -->
<!-- erzeugt aus register-uebernommen.yaml von werkzeuge/register_erzeugen.py - nicht von Hand aendern -->

**Boiler (node 2)**

| Object | Meaning | Scaling | JSON (MI MX OX OS) | Modbus | Source | Note |
|---|---|---|---|---|---|---|
| `0x2503`/2 | Controller switching differential, hot water charging | 0.1 K | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x250A`/0 | Burner cycle lock, heating | 1 min | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x250D`/2 | Controller switching differential, heating | 0.1 K | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2513`/0 | Duration of forced partial load, heating | 0.1 s | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2516`/2 | Internal pump mode, hot water charging | code | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2523`/2 | Maximum output, heating | 0.01 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2528`/2 | Maximum output, hot water charging | 0.01 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x253D`/0 | Gas valve offset | 0.01 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2542`/0 | Boiler output setpoint | 0.01 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x256B`/2 | Minimum pump output, heating | 1 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x256C`/2 | Maximum pump output, heating | 1 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x256E`/2 | Minimum pump output, hot water charging | 1 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2579`/0 | Ionisation signal, setpoint | 1 points | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x257A`/0 | Ionisation signal at start | 1 points | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x257D`/0 | Gas/air ratio, current value | 0.01 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x257E`/0 | Fan control signal, current value | 0.01 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2581`/2 | Gas quantity correction at start | 1 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x258B`/2 | Correction of minimum output | 1 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x259B`/2 | Maximum flue gas temperature (limit) | 1 °C | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2715`/2 | Flow temperature at the VPT sensor | 0.1 °C | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2716`/2 | System pressure, warning threshold | 0.01 bar | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2717`/2 | System pressure, burner lock threshold | 0.01 bar | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2719`/2 | Internal pump, output setpoint | 1 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x271B`/2 | Internal pump, electrical power | 1 W | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2729`/2 | Maximum flow rate | 1 l/h | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x272A`/2 | Flow rate factor, heating | 1 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x272B`/2 | Flow rate factor, hot water charging | 1 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2741`/0 | Time until next service | 1 h | `09 01 2641 00` | – | [kraiz](https://github.com/kraiz/hassio-weishaupt) | – |
| `0x2742`/0 | Service interval | 1 h | `09 01 2642 00` | – | [kraiz](https://github.com/kraiz/hassio-weishaupt) | – |
| `0x274E`/0 | Output test | code | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x274F`/2 | Output MFA1 | code | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2763`/2 | Gas pressure switch | code | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x276A`/2 | Internal pump mode, heating | code | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2A01`/0 | Burner starts, total counter | counter | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2A21`/0 | Burner operating hours | 1 h | `09 01 2921 00` | – | [geronet1](https://github.com/geronet1/wem-python), [kraiz](https://github.com/kraiz/hassio-weishaupt) | geronet1: counter since reset; kraiz: total |

**Heating circuit 1 (node 1, JSON module MI 02 + 0x400)**

| Object | Meaning | Scaling | JSON (MI MX OX OS) | Modbus | Source | Note |
|---|---|---|---|---|---|---|
| `0x291B`/2 | Flow setpoint, lower limit | 0.1 °C | `02 00 251B 02` | – | [ChristophKomenda](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/pull/10) | – |
| `0x291C`/2 | Flow setpoint, upper limit | 0.1 °C | `02 00 251C 02` | – | [ChristophKomenda](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/pull/10) | – |
| `0x292C`/2 | Flow setpoint, special level | 0.1 °C | `02 00 252C 02` | 113 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | read by the package `kraiz-per-can` |
| `0x293A`/2 | Room setpoint, normal | 0.1 °C | `02 00 253A 02` | 107 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | JSON address seen on the RG2 (VERSUCHE.en.md, section 10a); read by the package `kraiz-per-can` |
| `0x293B`/2 | Room setpoint, comfort | 0.1 °C | `02 00 253B 02` | 106 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | JSON address seen on the RG2 (VERSUCHE.en.md, section 10a); read by the package `kraiz-per-can` |
| `0x2954`/2 | Frost protection limit, outdoor temperature | 0.1 °C | `02 00 2554 02` | – | [ChristophKomenda](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/pull/10) | – |
| `0x2969`/2 | Flow setpoint, setback | 0.1 °C | `02 00 2569 02` | 112 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | read by the package `kraiz-per-can` |
| `0x296A`/2 | Flow setpoint, normal | 0.1 °C | `02 00 256A 02` | 111 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | read by the package `kraiz-per-can` |
| `0x296B`/2 | Flow setpoint, comfort | 0.1 °C | `02 00 256B 02` | 110 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | read by the package `kraiz-per-can` |
| `0x296E`/2 | Building construction type | code | `02 00 256E 02` | – | [ChristophKomenda](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/pull/10) | – |
| `0x297C`/2 | Heating limit by room setpoint | code | `02 00 257C 02` | – | [ChristophKomenda](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/pull/10) | – |
| `0x297E`/4 | Current mode of the heating circuit | code | `02 00 257E 04` | 102 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | read by the package `kraiz-per-can` |
| `0x297E`/5 | Status of the heating circuit | code | `02 00 257E 05` | 103 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | read by the package `kraiz-per-can` |
| `0x2981`/2 | Level boost by outdoor temperature | 0.1 °C | `02 00 2581 02` | – | [ChristophKomenda](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/pull/10) | – |
| `0x29A3`/2 | Heat-up optimisation | code | `02 00 25A3 02` | – | [ChristophKomenda](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/pull/10) | – |
| `0x29C7`/2 | Heating limit by flow temperature | 0.1 °C | `02 00 25C7 02` | – | [ChristophKomenda](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/pull/10) | – |

**System (node 1, JSON module MI 01 + 0x100)**

| Object | Meaning | Scaling | JSON (MI MX OX OS) | Modbus | Source | Note |
|---|---|---|---|---|---|---|
| `0x2660`/2 | Buffer tank, top temperature | 0.1 °C | `01 00 2560 02` | 118 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | read by the package `kraiz-per-can` |
| `0x2661`/2 | Buffer tank, bottom temperature | 0.1 °C | `01 00 2561 02` | 119 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | read by the package `kraiz-per-can` |
| `0x2712`/0 | Cascade, heating setpoint | 0.1 °C | `01 00 2612 00` | 145 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | read by the package `kraiz-per-can` |
| `0x271C`/0 | Cascade, target modulation heating | 0.01 % | `01 00 261C 00` | 143 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | read by the package `kraiz-per-can` |
| `0x271D`/0 | Cascade, target modulation hot water | 0.01 % | `01 00 261D 00` | 144 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | read by the package `kraiz-per-can` |
| `0x271F`/2 | Plate heat exchanger temperature (sensor B2) | 0.1 °C | `01 00 261F 02` | 117 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | read by the package `kraiz-per-can` |
| `0x27E4`/2 | Boiler sequence change | code | `01 00 26E4 02` | 140 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | read by the package `kraiz-per-can` |
| `0x27E6`/2 | Sequence change time | 1 h | `01 00 26E6 02` | 141 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | read by the package `kraiz-per-can` |
| `0x27FE`/2 | Cascade, balancing temperature | 0.1 °C | `01 00 26FE 02` | 142 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | read by the package `kraiz-per-can` |
| `0x28F9`/2 | Output demand for input N1 via Ethernet (0–100 %) | 0.01 % | `01 00 27F9 02` | 114 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | read by the package `kraiz-per-can` |

**Hot water (node 1, JSON module MI 03 + 0x500)**

| Object | Meaning | Scaling | JSON (MI MX OX OS) | Modbus | Source | Note |
|---|---|---|---|---|---|---|
| `0x2A38`/2 | Hot water setpoint, setback | 0.1 °C | `03 00 2538 02` | 133 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | read by the package `kraiz-per-can` |
| `0x2A51`/2 | Hot water pump | 0 off, 1 on | `03 00 2551 02` | 137 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | read by the package `kraiz-per-can` |
| `0x2A57`/2 | Circulation return temperature | 0.1 °C | `03 00 2557 02` | 136 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | read by the package `kraiz-per-can` |

**Network of the WEM (JSON module MI 06, JSON address only)**

This only says **where** the WEM keeps its network settings. None of it has been read on the measured installation, and no CAN object is known for it: no offset has been measured for MI 06 ([section 5](#mapping-json-object--can-object)). The source reads texts with command `11` (reply `12`), not with `01`. The values describe your own home network (addresses, MAC address, device name): keep them out of issues, bus logs and screenshots. This firmware neither reads nor publishes them.

| JSON (MI MX OX OS) | Meaning | Length and kind | Source |
|---|---|---|---|
| `06 00 2507 00` | Address assignment mode | 1 byte: 1 set by hand, 3 automatic (DHCP) | [JS-DE-Tech](https://github.com/JS-DE-Tech/hacs-weishaupt-lan) |
| `06 00 2508 00` | IP address | 4 bytes, IPv4 | [JS-DE-Tech](https://github.com/JS-DE-Tech/hacs-weishaupt-lan) |
| `06 00 2509 00` | Subnet mask | 4 bytes, IPv4 | [JS-DE-Tech](https://github.com/JS-DE-Tech/hacs-weishaupt-lan) |
| `06 00 250A 00` | Default gateway | 4 bytes, IPv4 | [JS-DE-Tech](https://github.com/JS-DE-Tech/hacs-weishaupt-lan) |
| `06 00 250B 00` | DNS server | 4 bytes, IPv4 | [JS-DE-Tech](https://github.com/JS-DE-Tech/hacs-weishaupt-lan) |
| `06 00 250C 01` … `06` | MAC address, one of its six parts per sub-object | 2 bytes each | [JS-DE-Tech](https://github.com/JS-DE-Tech/hacs-weishaupt-lan) |
| `06 00 250E 00` | Device name on the network | text, up to 16 characters | [JS-DE-Tech](https://github.com/JS-DE-Tech/hacs-weishaupt-lan) |
| `06 00 2511 00` | Name in the certificate (CN) | text, up to 50 characters | [JS-DE-Tech](https://github.com/JS-DE-Tech/hacs-weishaupt-lan) |

**Listed in the source without a meaning**

- node 2, replies of the boiler ([geronet1](https://github.com/geronet1/wem-python)): `0x2503`/1, `0x250D`/1, `0x2516`/1, `0x2523`/1, `0x2528`/1, `0x252E`/0, `0x2569`/1, `0x2569`/2, `0x256B`/1, `0x256C`/1, `0x256E`/1, `0x2574`/1, `0x2574`/2, `0x2581`/1, `0x258B`/1, `0x258C`/0, `0x2594`/1, `0x2594`/2, `0x259B`/1, `0x2716`/1, `0x2717`/1, `0x271A`/2, `0x2729`/1, `0x272A`/1, `0x272B`/1, `0x272C`/2, `0x2732`/2, `0x273B`/2, `0x273D`/1, `0x273D`/2, `0x2754`/1, `0x2754`/2, `0x2764`/2, `0x2765`/1, `0x2766`/2, `0x276A`/1, `0x2770`/2, `0x2772`/1, `0x2772`/2, `0x2778`/1, `0x2778`/2, `0x2A22`/0
- write telegrams on `0x602` ([geronet1](https://github.com/geronet1/wem-python)): `0x2528`/2, `0x252E`/0, `0x2705`/1
- write telegrams on `0x6C2` ([geronet1](https://github.com/geronet1/wem-python)): `0x251F`/1, `0x251F`/2

**Sources**

- [geronet1/wem-python](https://github.com/geronet1/wem-python) – object list of the bus logger (`wem_codes.py`, MIT licence)
- [BorgNumberOne/Weishaupt_CanApiJson](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) – address table of the JSON interface with Modbus registers
- [kraiz/hassio-weishaupt](https://github.com/kraiz/hassio-weishaupt) – sensor list of the Home Assistant integration (MIT licence)
- [BorgNumberOne/Weishaupt_CanApiJson, Pull Request #10 (ChristophKomenda)](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/pull/10) – heating circuit parameters of the JSON interface
- [JS-DE-Tech/hacs-weishaupt-lan](https://github.com/JS-DE-Tech/hacs-weishaupt-lan) – Home Assistant integration using the JSON interface (MIT licence); source of the network objects of the WEM

<!-- REGISTER:END uebernommen -->

## 8. When the WEM locks up

### Symptoms

- HTTP 200 with an empty body instead of JSON (“empty response”),
- or `CM 05` for **all** registers, including ones that responded before,
- the WEM portal shows frozen values although its timestamp is fresh. Portal values are
  a **cloud cache** anyway, lagging by minutes, and individual values stall even in normal operation.
  Authenticity test: compare the outside temperature against a weather service – if it does not follow the
  daily cycle, it is frozen,
- meanwhile the CAN bus keeps running normally: the WEM keeps polling the boiler, the boiler
  responds, the board reads as before.

### Two severity levels

| | short dropout | hard lock |
|---|---|---|
| Symptom | single empty response or briefly `CM 05` | `CM 05` for all registers, permanently |
| Recovery | by itself, observed after about 2 min of quiet | **only by power-cycling the heating system** |

### Known triggers

| Trigger | Source |
|---|---|
| **Requests to non-existent objects:** every `CM 05` response occupies one of ten slots that are not freed until a power cycle; after the tenth, the interface locks all objects | [kraiz/hassio-weishaupt #15](https://github.com/kraiz/hassio-weishaupt/issues/15) |
| **Regular polling via JSON** – even at hourly intervals | measured on the WTC-GW 15-B, five hard locks |
| **WEM portal and local interface at the same time** | [kraiz/hassio-weishaupt #13](https://github.com/kraiz/hassio-weishaupt/issues/13); Weishaupt: gateway and portal are mutually exclusive |
| **Two clients at the same time** | [kraiz/hassio-weishaupt #9](https://github.com/kraiz/hassio-weishaupt/issues/9) |
| **Several switching commands in quick succession** | measured on the WTC-GW 15-B |

**Recurring empty-response windows:** with the portal enabled, empty responses came roughly every
two hours. The minute shifts with the time at which the heating system was switched on –
so a fixed polling schedule cannot permanently avoid the window. Interpretation (not
verified): an internal task of the WEM, presumably related to the portal.

Not every hard lock could be traced back to the ten-slot limit: once it occurred without a
single preceding `CM 05` response, after an empty response and an immediate retry.

### Observed failures (WTC-GW 15-B)

| # | Type | Circumstances |
|---|---|---|
| 1 | hard, unnoticed for days | polling every minute |
| 2 | hard | polling every 10 min |
| 3 | hard | polling every 15 min |
| 4 | hard | empty response, then immediate retry after 2.5 s |
| 5 | hard, portal values frozen | polling hourly |
| 6 | two short dropouts, recovered by themselves after 2 min of quiet | normal operation in portal and app at the same time |
| 7 | empty response after a switching command – the switch had nevertheless taken place | commands too close together |
| 8 | hard | four switching commands within five minutes, plus one read request |
| 9 | portal frozen, heating keeps regulating normally | cloud connection dropped (cause unknown); three days later the WEM also stopped querying the logger values (flue gas, flow rate every ~32 s). Only switching off the power helped – after two weeks |
| 10 | portal stalls, recovers right after the end | SDO read series (scan) on `0x602`, even at 2.5 s and 6 s spacing |

### Resulting rules

1. **Only address objects whose existence is verified.** Read unknown objects on the bus first.
   Cautionary example: requesting the heat quantities “to be safe under both modules” (MI 09/01 **and**
   MI 07/00, see [section 5](#verified-read-objects-heat-quantities)) costs four
   `CM 05` per call – after a few calls the ten slots are used up.
2. **At least one minute between two JSON commands**, preferably more.
3. **No polling via JSON.** Read values on the bus.
4. **One client** on the JSON interface. If you have several users (app, automation), route all of them
   through **one** point with a lock (mutex). This firmware never calls the JSON
   interface unprompted, only on user action: switching via JSON (fallback), write field, manual refresh.
5. **Few registers per request.** On the measured installation the WEM answers at most seven, the eighth
   always gets `CM 05` ([Why JSON is never polled](#why-json-is-never-polled)); another report mentions
   sporadic `CM 05` already with more than six
   ([kraiz #15](https://github.com/kraiz/hassio-weishaupt/issues/15)).
6. After an empty response or a pure `CM 05`, **do not retry immediately**, but back off in
   stages: 2 → 4 → 8 → 16 → 30 min; the first healthy response resets the backoff.
7. **Keep-alive does not help:** the WEM closes every TCP connection itself after the response.
   The only lever is the number of requests.

## 9. Example: heating circuit to “Time program 1”

**1. Command.** The “ZP 1” button (TP 1) in the Docker app sends

```
MQTT  <gerät>/cmd/heizkreis   {"id":"app-mg3k2-1","wert":2,"quelle":"App"}
```

The app on the board (package `app`) hands the same request to the firmware directly, without MQTT, also with
source `App`. Equivalent: `2` or `Zeitprogramm 1` as text via MQTT (the board then assigns the ID), or
“Heizkreis Betriebsart setzen” in the expert interface or in Home Assistant. On
`<gerät>/befehl/status`, `angenommen` (accepted) and `vorgemerkt` (queued, with `warten_s`) appear, later
`gesendet` (sent), `pruefe_bus` (checking bus) and `bestaetigt` (confirmed) – see [TOPICS.en.md](TOPICS.en.md#gerätbefehlstatus).
The board additionally reports (deprecated) in “Ergebnis letzter Schaltbefehl”:

```
vorgemerkt (App): Heizkreis Zeitprogramm 1
```

**2. Queue.** If the minimum interval to the previous command is still running (via CAN <!--ab-werk:abstand_can-->10<!--/ab-werk--> s by default,
via JSON <!--ab-werk:abstand_json-->60<!--/ab-werk--> s), “Warteschlange” shows something like
`1. Heizkreis -> Zeitprogramm 1 (App) - naechster Befehl ab 14:03:12`. If another
heating circuit request arrives before that, it replaces this one.

**3. Command to the WEM.** After the interval has expired, via CAN (default since v42):

```
CAN-ID 0x601   2F 33 29 02 02 00 00 00      Schreib-Anfrage Knoten 1, 0x2933/2, 1 Byte, Wert 2
Antwort 0x581  60 33 29 02 …                Quittung
```

(`Schreib-Anfrage Knoten 1` = write request node 1; `Quittung` = acknowledgement.)

Status: `sende per CAN an WEM ...` (sending to WEM via CAN), then
`WEM hat per CAN bestaetigt, pruefe am Bus ...` (WEM confirmed via CAN, checking on the bus).

Via JSON (switch “Schalten per CAN statt JSON” off) instead:

```
POST http://<WEM>/ajax/CanApiJson.json
{"ID":"…","SRC":"DDC","CAPI":{"NN":1,"N01":{"VG":"030200253302000102"}}}

VG = 03 02 00 2533 02 0001 02
     CM MI MX OX   OS VS   VA      schreiben, HZK0, Betriebsart, 1 Byte, Wert 2
```

(`schreiben, HZK0, Betriebsart, 1 Byte, Wert 2` = write, HZK0, operating mode, 1 byte, value 2.)

Status: `sende an WEM ...` (sending to WEM), then for a response with `CM 04`:
`WEM hat bestaetigt, pruefe am Bus ...` (WEM confirmed, checking on the bus).

**4. Verification on the bus.** 3 s after sending (via JSON: 3 s after the response) the board reads:

```
CAN-ID 0x601   40 33 29 02 00 00 00 00      Lese-Anfrage Knoten 1, 0x2933/2
Antwort 0x581  4F 33 29 02 02 00 00 00      1 Byte, Wert 2 = Zeitprogramm 1
```

(`Lese-Anfrage Knoten 1` = read request node 1; `Antwort` = response; `Wert 2 = Zeitprogramm 1` = value 2 = time program 1.)

**5. Result.**

| Case | “Ergebnis letzter Schaltbefehl” |
|---|---|
| Bus reports 2 | `14:03:16 OK: Heizkreis steht auf Zeitprogramm 1` (heating circuit is set to time program 1) |
| Bus reports a different value | `14:03:16 NICHT uebernommen, Heizkreis steht auf Standby` (NOT applied, heating circuit is on standby) |
| WEM aborts via CAN (`80`) | `14:03 per CAN 01 2933/02: abgelehnt, Abbruch …` (rejected, abort) – no verification read |
| via JSON: WEM responds `CM 05` | `14:03 abgelehnt (CM=05) - VG 05…` (rejected) – no verification read |
| no response to the verification read within 10 s | `keine Rueckmeldung vom Bus - Ergebnis unbekannt` (no feedback from the bus – result unknown) |
| WEM does not acknowledge via CAN | `keine CAN-Antwort vom WEM, pruefe trotzdem am Bus ...` (no CAN response from the WEM, checking on the bus anyway), then as above |
| via JSON: WEM unreachable | `WEM nicht erreichbar, pruefe trotzdem am Bus ...` (WEM unreachable, checking on the bus anyway), then as above |

The switching log gets a line like
`27.09. 14:03 Heizkreis -> Zeitprogramm 1 (App): ok`. From v27 onwards, the phone app only evaluates the
phase from `befehl/status` or `status/json` and shows it for three minutes – green for
`bestaetigt` (confirmed), red for `gescheitert` (failed: not applied, rejected by the WEM, no feedback) or
`abgelehnt` (rejected: invalid value).

On the bus, the status bits in PDO `0x1C1` then change (bit `0x1000` heating circuit standby is cleared);
“Heizkreis Status” jumps from “Standby” to “Zeitprogramm” (time program).

## 10. Example: DHW off

**1. Command.** As plain text via MQTT:

```
MQTT  <gerät>/cmd/warmwasser   Aus
```

`aus`, `2`, `OFF`, `off`, `false` are also accepted; in the expert interface “Warmwasser setzen” → Aus.
The “AUS” (OFF) button in the app sends the same as JSON with an ID (as in section 9, source `App`).
Status: `vorgemerkt (MQTT): Warmwasser Aus` (queued: DHW off). Queue and minimum interval as for the heating circuit;
if a heating circuit request is pending at the same time, the one that arrived first goes first
(up to v37: always the heating circuit).

**2. Command to the WEM.** Via CAN (default since v42):

```
CAN-ID 0x601   2F 20 2A 02 02 00 00 00      Schreib-Anfrage Knoten 1, 0x2A20/2, 1 Byte, Wert 2 = Aus
Antwort 0x581  60 20 2A 02 …                Quittung
```

Via JSON (switch “Schalten per CAN statt JSON” off) instead:

```
{"ID":"…","SRC":"DDC","CAPI":{"NN":1,"N01":{"VG":"030300252002000102"}}}

VG = 03 03 00 2520 02 0001 02
     CM MI MX OX   OS VS   VA      schreiben, WW0, Betriebsart, 1 Byte, Wert 2 = Aus
```

(`schreiben, WW0, Betriebsart, 1 Byte, Wert 2 = Aus` = write, WW0, operating mode, 1 byte, value 2 = off.)

`CM 04` is expected.

**3. Verification on the bus**, 3 s after sending (via JSON: 3 s after the response):

```
CAN-ID 0x601   40 20 2A 02 00 00 00 00      Lese-Anfrage Knoten 1, 0x2A20/2
Antwort 0x581  4F 20 2A 02 02 00 00 00      Wert 2 = Aus
```

(`Lese-Anfrage Knoten 1` = read request node 1; `Antwort` = response; `Wert 2 = Aus` = value 2 = off.)

Result: `14:05:04 OK: Warmwasser Aus`, in the log
`27.09. 14:05 Warmwasser -> Aus (MQTT): ok`.

**4. Consequences on the bus.** After the DHW command the firmware triggers an event-triggered read and
reads the DHW values 8 s later:

```
0x601  40 20 2A 02 …   Warmwasser Betriebsart   -> 2 (Aus)
0x601  40 2C 2A 02 …   Warmwasser Soll aktuell  -> 80 = 8,0 °C
0x601  40 39 2A 02 …   Warmwasser Soll normal   -> unverändert
```

(DHW operating mode → 2 (off); DHW setpoint current → 80 = 8.0 °C; DHW setpoint normal → unchanged.)

“Warmwasser Soll aktuell” (current DHW setpoint) drops to **8.0 °C** when off – observed on the installation, the switch
itself confirmed at the display. “Vorlaufsoll Anforderung” (flow setpoint demand, `0x2640`/3) was at 50 °C in summer mode with
DHW on, and at 23 °C with off. “Warmwasser aktiv” (DHW active; boiler status 15 in PDO `0x182`) continues to show whether charging is in progress.

How the object `0x2A20`/2 was found is described in [section 7](#7-finding-new-objects-scan-and-diff).

## 10a. WEM-RG2 room unit on the bus (capture, November 2026)

Moved to [VERSUCHE.en.md](VERSUCHE.en.md#10a-wem-rg2-room-unit-on-the-bus-capture-november-2026).

## 10b. CAN tunnel over WLAN (experiment, branch `can-tunnel`)

Moved to [VERSUCHE.en.md](VERSUCHE.en.md#10b-can-tunnel-over-wlan-experiment-branch-can-tunnel).

## 10c. Fault messages on the bus and error codes

The WEM reports warnings and faults **on its own** as a CANopen emergency message (EMCY) on `0x081`, the boiler on
`0x082`. The firmware only listens and shows the message as "Meldung WEM" or "Meldung Kessel", for example
"W 1501: Connection to room unit RG2 lost (device 1)". Nothing is requested for this.

```
081: FF FF 01 02 05 DD 01 00   message: error code FFFF, register 01, type 02 = warning, number 0x05DD = 1501
081: 00 00 01 00 00 00 01 00   cleared (error code 0000)
```

Bytes 0–1 error code (`0000` = no message), byte 3 the type, bytes 4–5 the number (big endian). Only warning 1501 is
confirmed so far (RG2 disconnected, several times). That type `01` means a fault is an **assumption** based on the error
block `0x273F` (as kraiz/hassio-weishaupt reads it); we have not yet seen a real fault on the bus. The meaning of byte 6
is open. The boiler has so far only sent `082: 00 … 01` and `082: 00 … 88` without an error code.

**Communication warnings:** numbers 1101–1925 mean a lost connection to a bus device, the last two digits are its
number: 1101–1112 system unit, 1201–1212 burner control, 1302–1325 EM heating circuit, 1401 EM solar,
**1501–1532 room unit RG2**, 1601–1632 room sensor RF, 1701–1732 room unit RG1, 1800 EM cascade, 1902–1925 EM DHW. So a
WEM is designed for up to 32 RG2 and 32 room sensors.

**Texts and copyright.** Numbers and meaning come from chapter 10 of the installation and operating instruction
"Gas condensing unit WTC-GW 15 … 32-B", which Weishaupt publishes:
[German, 83307201](https://www.weishaupt.de/uploads/tx_weishaupt_documents/documents/83307201.pdf),
[English, 83307202](https://www.weishaupt.de/uploads/tx_weishaupt_documents/documents/83307202.pdf).
The texts of the manual are protected by copyright (rights with Max Weishaupt SE). This repo therefore contains **no
original texts**, but in [`fehlercodes.yaml`](fehlercodes.yaml) its own short texts in German and English that only
describe the cause. What to do is in the manual; most items may only be fixed by a qualified technician.

**Original texts for your own installation:** if you prefer the manual's wording, download it yourself and create local
files with it:

```
python3 werkzeuge/fehlertexte_aus_handbuch.py --header 83307202.pdf
```

The tool (needs `pdftotext` from poppler-utils) reads the "warning code" and "fault code" tables and writes
`fehlertexte-original.json`, with `--header` also `components/weishaupt_can/fehlertexte_original.h`. Two ways:

- **Without building** (also with the release image): settings page → "Störmeldungen" → upload `fehlertexte-original.json`
  with the expert password (home network only). The board checks the file, stores it in the file system and uses the
  original texts right away; "Zurück auf eigene Texte" deletes it again. The info page shows which texts are active.
- **When building:** create it with `--header` and build the firmware.

Anything missing from the file is still shown with the board's own texts. English: set `meldungen_englisch: "true"` in the main YAML and use the
English manual. The English edition (2018) lacks six newer codes (W 7, W 8, W 1101, W 1302, W 1800, W 1902); for these
our own English text remains. Both files are in `.gitignore` and do **not** belong in a public repo.

Thanks to [cg089](https://github.com/cg089/weishaupt-wem-can-esphome/tree/olimex-esp32-evb) for the first mapping of the
communication warnings and to [kraiz](https://github.com/kraiz/hassio-weishaupt) for the layout of the error block.

## 11. Sources

- [MenkeC/Weishaupt-C3supermini](https://github.com/MenkeC/Weishaupt-C3supermini) – 50 kbit/s,
  boiler = node 2, SDO `0x602`/`0x582`, PDOs `0x201`/`0x241`.
- [geronet1/wem-python](https://github.com/geronet1/wem-python) – object meanings, among others the
  write telegrams on `0x6C2`.
- [BorgNumberOne/Weishaupt_CanApiJson](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) –
  register table of the JSON interface with MI/MX/OX/OS, factors and update classes;
  reference to `/sd/systable.csv`.
- [kraiz/hassio-weishaupt](https://github.com/kraiz/hassio-weishaupt) – issues
  [#15](https://github.com/kraiz/hassio-weishaupt/issues/15) (ten slots),
  [#13](https://github.com/kraiz/hassio-weishaupt/issues/13) (portal),
  [#9](https://github.com/kraiz/hassio-weishaupt/issues/9) (two clients).
- [Home Assistant forum: Weishaupt WTC, CAPI VG, CanApiJson](https://community.home-assistant.io/t/weishaupt-wtc-weishaupt-capi-vg-canapijson/997400) –
  structure of the VG field.
- [Manual WEM-Modbus TCP (PDF)](https://www.loebbeshop.de/media/67944/file/static/pdf/weishaupt/manual-wem-modbustcp.pdf) –
  data point list with update classes; gateway and portal are mutually exclusive.
- [Installation and operating instructions WTC-GW 15–32-B (PDF, German)](https://www.intec-heizung.de/media/pdf/9f/c7/72/Weishaupt-Thermo-Condens-WTC-GW_15-32-B-Montage-u-Betriebsanleitung.pdf) –
  parameter 10.8.1 (JSON interface), terminal H/L/−/+.
- [WEM portal FAQ (PDF, German)](https://www.wemportal.com/Web/Documents/FAQ/FAQ.de.pdf?lang=de)
