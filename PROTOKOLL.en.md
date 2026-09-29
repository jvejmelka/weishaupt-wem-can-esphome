> 🇩🇪 German original: [PROTOKOLL.md](PROTOKOLL.md). This is a translation; if in doubt, the German version is authoritative.

# Protocol: CAN bus and WEM JSON

What happens on the CAN bus of a Weishaupt condensing boiler with a WEM system unit, what this
firmware picks up by passive listening, what it requests itself, and how it switches via the WEM's
JSON interface. Measured on a **WTC-GW 15-B** with WEM system unit; where a statement comes only
from third-party sources or is merely plausible, this is noted.

> The firmware **never** writes to the bus. On the bus it only issues read requests
> (SDO, command byte `0x40`). Switching is done exclusively via the WEM's JSON interface.

- [1. Overview](#1-overview)
- [2. Passive listening and on-demand reading](#2-passive-listening-and-on-demand-reading)
- [3. What is picked up by passive listening](#3-what-is-picked-up-by-passive-listening)
- [4. What is read on demand](#4-what-is-read-on-demand)
- [5. Writing via the WEM (JSON)](#5-writing-via-the-wem-json)
- [6. Web interface: reading and writing](#6-web-interface-reading-and-writing)
- [6a. Rules: reading operating modes on suspicion](#6a-rules-reading-operating-modes-on-suspicion)
- [6b. Additional displays for the phone app](#6b-additional-displays-for-the-phone-app)
- [7. Finding new objects: scan and diff](#7-finding-new-objects-scan-and-diff)
- [8. When the WEM locks up](#8-when-the-wem-locks-up)
- [9. Example: heating circuit to “Time program 1”](#9-example-heating-circuit-to-time-program-1)
- [10. Example: DHW off](#10-example-dhw-off)
- [11. Sources](#11-sources)

## 1. Overview

What this looks like on a specific installation with heat pump and PV (setpoints, measurements):
[ANLAGE.md](ANLAGE.md) (German).

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
| `40` | Read request (upload). **The only one this firmware sends.** |
| `4F` / `4B` / `47` / `43` | Response with 1 / 2 / 3 / 4 data bytes |
| `2F` / `2B` / `23` | Write request with 1 / 2 / 4 data bytes (sent only by the WEM) |
| `60` | Acknowledgement of a write |
| `80` | Abort; bytes 4–7 = abort code, e.g. `06020000` object does not exist, `06090011` subindex does not exist |

Example: read request to node 1, object `0x2933` subindex 2:

```
CAN-ID 0x601   40 33 29 02 00 00 00 00
Antwort 0x581  4F 33 29 02 02 00 00 00     -> 1 Byte, Wert 2
```

(`Antwort` = response; `1 Byte, Wert 2` = 1 byte, value 2.)

## 2. Passive listening and on-demand reading

The board obtains values in two ways:

| | Passive listening | On-demand reading (active) |
|---|---|---|
| What | PDOs, plus the SDO responses to requests that **the WEM itself** sends to the boiler | own SDO read requests `40 …` |
| Load | none – the board sends nothing | one request plus one response per value |
| Can be disabled | no, always running | yes, switch **“Eigene CAN-Anfragen”** (“own CAN requests”; default: off) |

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
- when the status bits change (PDO `0x1C1`, e.g. on a change at the display or in the portal),
- after the firmware's own DHW switching command,
- on request: button “Status lesen” (“read status”; web interface, phone app) or MQTT `<gerät>/cmd/status`.
  Requests are accepted at most every 10 s and are only sent while the bus is alive.

The heating circuit operating mode is read at most every 2 min – except on explicit request. Known gap: a change
between two time programs at the display does not change the status bits and goes unnoticed until
the next event.

### Why JSON is never polled

The WEM's JSON interface is a translator between HTTP and the CAN bus – and it is fragile
(see [section 8](#8-when-the-wem-locks-up)). Regular polling via JSON disabled it five times on the
measured installation, each time until the system was power-cycled. The bus itself has no problem
with it: the WEM polls the boiler there three times per second. **Therefore always read on the bus,
and address the WEM only for switching.**

### What remains if the WEM fails

| Failure | Consequence |
|---|---|
| only the JSON interface hangs (the case observed so far) | everything on the bus continues; the board reads as before, only switching is unavailable |
| the WEM is completely gone | all values are missing that are only picked up as responses to **WEM requests**, plus everything the WEM sends |

By CANopen convention, PDOs on `0x180 + n` come from node *n*; `0x182` therefore comes from the
boiler, `0x1C1` from the WEM. Which values actually keep coming during a complete WEM failure has
**not been tested**. The expectation:

| probably remain (PDOs, see [section 3](#3-what-is-picked-up-by-passive-listening)) | disappear |
|---|---|
| outside temperature, flow setpoint/“heat demand”, DHW, time of day, boiler status | everything that is only picked up as a response to **requests from the WEM** (pressure, output, heat quantities …) and the WEM write telegrams |

The boiler temperature is additionally contained in PDO `0x241`, bytes 2–3 – so it can also be
picked up without WEM requests. Which device sends which of the PDOs is not settled in every case. With own requests enabled, the board reads the boiler values from
[section 4](#4-what-is-read-on-demand) itself and is thus independent of the WEM.

## 3. What is picked up by passive listening

The tables in sections 3–5 are generated from [`register.yaml`](register.yaml)
([INSTALL.md, section 14](INSTALL.md#14-register-ändern) (German)); please add extensions there.

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
| `0x6C2` | SDO write telegram, object `0x2699`/1 | Return temperature VPT | 0.1 °C | Ruecklauf | plausible |
| `0x6C2` | SDO write telegram, object `0x2697`/1 | Flow temperature VPT | 0.1 °C | Vorlauf VPT | plausible |
| `0x6C2` | SDO write telegram, object `0x2698`/1 | Target output | 0.01 % | Sollleistung | plausible |

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
| `0x2533`/2 | not evaluated (return temperature VPT, candidate) | yes |
| `0x2536`/0 | Vorlauf | **no** – only arrives with own requests |
| `0x2540`/0 | Drehzahl | **no** – only arrives with own requests |

<!-- REGISTER:END wem-antwort -->

(Firmware names: Abgastemperatur = flue gas temperature, Leistung = output, Brenner = burner,
Vorlauf Soll = flow setpoint, Volumenstrom = volume flow, Anlagendruck = system pressure,
Wärmemenge Vortag Heizung/WW/Gesamt = heat quantity previous day heating/DHW/total,
Vorlauf = flow, Drehzahl = fan speed.)

The WEM requests further objects (among others `0x2530`, `0x2531`, `0x2739`, `0x2753`/2) that the
firmware does not evaluate. According to the Weishaupt register table and geronet1, `0x2533`/2 is the
return temperature VPT – **not yet checked** on the installation.

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

All requests use command byte `0x40`. The size follows from the response (`4F` 1 byte, `4B`
2 bytes, `43` 4 bytes); for node 2 the firmware reads up to 4 bytes, for node 1 mostly 2 bytes
signed (operating modes: 1 byte). The value `0x8000` means “no value” and is discarded.

<!-- REGISTER:BEGIN eigene-anfragen -->
<!-- erzeugt aus register.yaml von werkzeuge/register_erzeugen.py - nicht von Hand aendern -->

| Node | Object | Factor | Rate | Class | Name in the firmware | Status |
|---|---|---|---|---|---|---|
| 2 | `0x2532`/0 | 0.1 °C | 40 s | s | Kesseltemperatur | **verified** (display) |
| 2 | `0x2537`/0 | 0.1 °C | 40 s | – | Abgastemperatur | **unconfirmed** – called “return” in some templates |
| 2 | `0x2536`/0 | 0.1 °C | 40 s | – | Vorlauf | **unconfirmed** – responds; the interpretation can only be checked with the burner running (flow must then be above boiler) |
| 2 | `0x2534`/0 | 0.01 % | 40 s | – | Leistung | **unconfirmed** – taken over from the template, check with the burner running |
| 2 | `0x2540`/0 | 1 rpm | 40 s | – | Drehzahl | **unconfirmed** – taken over from the template, check with the burner running |
| 2 | `0x2541`/0 | Phase: 0 off, 1 pre-purge, 2 control operation, 3 modulating operation, 4 post-purge | 40 s | s | Brenner Status, Brenner, Brennerphase, Brennerstarts | **verified** (display “Heizkreise inaktiv”) |
| 2 | `0x2545`/0 | 0.1 °C | 60 s | m | Vorlauf Soll | **unconfirmed** |
| 2 | `0x2713`/2 | 1 l/h | 60 s | m | Volumenstrom | **unconfirmed** – taken over from the template, check with the burner running |
| 2 | `0x2714`/2 | 0.01 bar | 60 s | m | Anlagendruck | **verified** (display) |
| 1 | `0x2907`/2 | 0.1 °C | 60 s | s | Vorlauf Heizkreis | plausible (JSON MI 02 `0x2507` actual flow temperature, +0x400) |
| 1 | `0x2640`/3 | 0.1 °C | 60 s | – | Vorlaufsoll Anforderung | plausible (50 °C observed with DHW on, 23 °C with off) |
| 1 | `0x2958`/2 | 0.1 °C | 5 min | m | Raumsoll aktuell | plausible (JSON MI 02 `0x2558`, +0x400) |
| 1 | `0x2933`/2 | Code 1–8, see [section 5](#5-writing-via-the-wem-json) | event, at most every 2 min | m | Heizkreis Betriebsart | **verified** (for standby, time program 1–3, summer. **Comfort, normal, setback never observed**) |
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

## 5. Writing via the WEM (JSON)

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

The firmware only sends `CM 03` and never `CM 01`. It evaluates the first two characters of the
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

Further objects can be written via the write field of the web interface, but only in the
modules MI 01, 02 and 03 (see [section 6](#6-web-interface-reading-and-writing)).

### Verified read objects: heat quantities

The firmware reads nothing via JSON. If you do anyway: these objects respond on the
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

**The firmware never writes directly via SDO** – neither to the boiler nor to the WEM. Writing
is done exclusively via JSON; the bus serves only for verification.

### Sequence in the firmware

1. A request arrives via the web interface, Home Assistant or MQTT (`<gerät>/cmd/heizkreis`,
   `<gerät>/cmd/warmwasser`) and lands in the **queue**: at most one entry per target,
   the newest request replaces the old one; heating circuit before DHW.
2. Once per second the firmware checks the queue. At most **one command
   per minute** is sent (lock-out minute from the last send).
3. The WEM's response is displayed. With `CM 05` the operation ends (“rejected”).
4. Otherwise the firmware reads the target object on the bus **3 s** after the response (node 1, `0x2933`/2
   or `0x2A20`/2). This single verification read is sent even with own requests switched off.
5. **The bus decides, not the WEM response:** if the value read matches the request,
   the result is “OK”, otherwise “NICHT übernommen” (not applied). If no response arrives within 10 s:
   “keine Rückmeldung vom Bus – Ergebnis unbekannt” (no feedback from the bus – result unknown). The bus is also
   checked after an empty or unclear WEM response – it has been observed that the switch took place despite an empty response.
6. The result appears in **“Ergebnis letzter Schaltbefehl”** (result of last switching command) and in the
   **“Schaltprotokoll”** (switching log) (plain text, deprecated), and from v27 onwards in structured form on
   **`<gerät>/befehl/status`** (every phase, with ID) and in **`<gerät>/status/json`** → `schalten` – structure in
   [TOPICS.md](TOPICS.md) (German).

## 6. Web interface: reading and writing

The board's web interface (`http://<Board>`, user `admin`) is divided into groups. The
names here are those from the firmware (German, with English translations in parentheses).

### Lesebefehl (read command, read only)

| Element | Function |
|---|---|
| **Lesebefehl (KN IDX SUB hex)** (read command) | input `NODE INDEX SUB` in hex, e.g. `01 2933 02` |
| **Lesen** (read) | sends a read request (`0x40`) to node `0x600 + KN` – only if the bus is alive |
| **Lesebefehl Antwort** (read command response) | `01 2933/02 = 2 (0x02, 1 Byte)` or `… Abbruch 06020000 (Objekt fehlt?)` (abort, object missing?); with a dead bus “Bus tot - nicht gesendet” (bus dead – not sent) |

The same works via MQTT: `<gerät>/cmd/lesen` with `01 2933 02`.

### Operating modes: read status

| Element | Function |
|---|---|
| **Status lesen** (read status; button) | reads the heating circuit operating mode (`0x2933`/2) and – with package warmwasser – `0x2A20`, `0x2A2C`, `0x2A39` (each subindex 2) once from node 1. Read requests only, no WEM JSON |
| **Status gelesen** (status read) | `27.09. 09:32:51 gelesen` (read), during the request `09:32:46 angefordert ...` (requested), with a dead bus `… Bus schweigt - nicht gelesen` (bus silent – not read) |

Via MQTT: `<gerät>/cmd/status` with any payload. At most one request every 10 s.

### Switching (via WEM, verification on the bus)

| Element | Function |
|---|---|
| **Heizkreis Betriebsart setzen** (set heating circuit operating mode) | selection Standby … Absenk; shows the value on the bus after each verification read |
| **Warmwasser setzen** (set DHW) | selection Ein / Aus (on / off) |
| **Warteschlange** (queue) | pending commands with source, e.g. `1. Heizkreis -> Zeitprogramm 1 (MQTT) - naechster Befehl ab 14:03:12` (next command from 14:03:12), otherwise `leer` (empty) |
| **Ergebnis letzter Schaltbefehl** (result of last switching command) | progress and result as plain text, see [section 5](#sequence-in-the-firmware) (deprecated – structured: `befehl/status`, [TOPICS.md](TOPICS.md) (German)) |
| **Warteschlange leeren** (clear queue) | discards all pending commands (is logged) |
| **Schaltprotokoll** (switching log) | the last ten commands with time, source and result; via MQTT retained under `<gerät>/schaltprotokoll`. Empty after a reboot |

### Schreibbefehl über WEM (write command via WEM; experts)

| Element | Function |
|---|---|
| **Schreibbefehl (MI MX OX OS WERT hex)** (write command; WERT = value) | e.g. `03 00 2520 02 02` = DHW off. WERT 1–4 hex digits; the length (VS) is derived from it |
| **Schreiben** (write) | builds `CM 03` and sends it via JSON. **Only MI 01, 02, 03** – the boiler is locked. The same lock-out minute as for switching applies |
| **Schreibbefehl Antwort** (write command response) | `OK, WEM bestaetigt (VG …)` (WEM confirmed), `abgelehnt (CM=05): Objekt unbekannt oder nicht schreibbar - VG …` (rejected: object unknown or not writable), `unklare Antwort, HTTP …` (unclear response) or `WEM nicht erreichbar` (WEM unreachable) |

The write field does **not** verify on the bus and has no queue. Before writing, look at the
object on the bus using the read command and apply the mapping from
[section 5](#mapping-json-object--can-object). Every `CM 05` response costs one of the
ten slots from [section 8](#8-when-the-wem-locks-up).

### Einstellungen (settings)

| Element | Function |
|---|---|
| **Eigene CAN-Anfragen** (own CAN requests) | on = read requests at the rates from [section 2](#rate-of-own-requests); off = passive listening only. Persists across reboots; initial state via `can_anfragen_start` in the main file |
| **CAN-Rohmitschnitt nach MQTT** (raw CAN capture to MQTT) | every received frame as `ID:DATEN` (ID:DATA) to `<gerät>/canraw` (package `mqtt`); always on during a scan |
| **Heizung: IP-Adresse** (heating: IP address) | address of the WEM for the JSON commands |
| **Waechter: Neustart nach Minuten ohne Bus (0 = aus)** (watchdog: reboot after minutes without bus, 0 = off) | reboots the board if no frame has arrived for that long (bus-off trap) |

## 6a. Rules: reading operating modes on suspicion

Package `verdacht.yaml` reads the operating modes **only** when passive listening suggests a
change – exclusively read requests (`0x40`) to node 1, never JSON, no fixed schedule, with a
minimum interval per rule and a cap per hour. **In detail: [REGELN.md](REGELN.md) (German).**

| Rule | Trigger on the bus | reads | Default |
|---|---|---|---|
| R1 | WEM requests `2101/0A`, `2102/0D`, `2102/01` from the boiler (SDO block upload `0xA4`) | DHW | on |
| R2 | Boiler status 15 or `0x252B` = `0F` while DHW is known to be “off” | DHW | on |
| R3 | Standby bit `0x1000` in `0x1C1` changes | heating circuit | on |
| R4 | Heat demand does not match the known heating circuit operating mode | heating circuit | off |
| R5 | other status bits in `0x1C1` change | heating circuit | off |

Disabled rules keep running in **shadow mode** (they report “would have triggered” but do not read).
Custom rules of the form “CAN ID + mask + pattern” are experimental and have their own
master switch. Configure via the web interface, file `<gerät>/cmd/regeln` or command
`<gerät>/cmd/regel`; state under `<gerät>/regeln/stand`, log under
`<gerät>/verdacht/protokoll`, events under `<gerät>/verdacht/ereignis`.

## 6b. Additional displays for the phone app

Package `zusatz.yaml` (from v24) has nothing to do with the boiler bus: it only manages a **list
of third-party MQTT topics** (up to 6) that the phone app displays in addition – e.g. room temperatures or whether
a heat pump is running. **The board does not read these topics**; it stores the list persistently and
publishes it retained under `<gerät>/app/zusatz`. The app subscribes to the topics itself.

| Field | Meaning |
|---|---|
| `name` | display name, 1–24 bytes |
| `topic` | MQTT topic without wildcards `+`/`#` |
| `feld` | JSON field in the payload, nested with a dot (`aenergy.total`); if absent, the payload itself is the number |
| `einheit` | up to 8 characters, e.g. `°C` |
| `art` | `wert` (default) shows the number; `laeuft` shows “läuft” (running) from `schwelle` (threshold, default 1) upwards, otherwise “aus” (off) |

Configuration: JSON `{"version":N,"eintraege":[…]}` to `<gerät>/cmd/zusatz` (retained possible,
checksum as with `cmd/regeln`) or in the web interface, field **Zusatz-Befehl** (additional-display command):

```
Wohnzimmer topic=sensoren/wz/temp feld=tC einheit=°C
WP topic=sensoren/wp/status feld=apower einheit=W art=laeuft schwelle=50
WP loeschen
alle loeschen
```

(`loeschen` = delete, `alle loeschen` = delete all; `Wohnzimmer` = living room, `WP` = heat pump.)

The name is everything before the first word containing `=` (may contain spaces); values without
spaces. An existing entry with the same name is replaced. Result in **Zusatz Meldung** (additional-display message),
the list in **Zusatzanzeigen** (additional displays). In the app, values older than 10 min are shown as “--” – for sensors
that only transmit every few minutes (battery devices), the age is correspondingly high.

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

> **Scan only on the bus, never via JSON.** On the bus, read scans with many abort responses showed
> no impairment on the measured installation. Via JSON, by contrast, every request to a
> non-existent object consumes one of ten slots until the next power cycle
> (see [section 8](#8-when-the-wem-locks-up)). A JSON scan will reliably disable the interface.

A scan generates a lot of bus traffic (40 requests per second). Keep ranges small and only scan
when necessary.

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

### Resulting rules

1. **Only address objects whose existence is verified.** Read unknown objects on the bus first.
   Cautionary example: requesting the heat quantities “to be safe under both modules” (MI 09/01 **and**
   MI 07/00, see [section 5](#verified-read-objects-heat-quantities)) costs four
   `CM 05` per call – after a few calls the ten slots are used up.
2. **At least one minute between two JSON commands**, preferably more.
3. **No polling via JSON.** Read values on the bus.
4. **One client** on the JSON interface. If you have several users (app, automation), route all of them
   through **one** point with a lock (mutex). This firmware only addresses the WEM when
   someone switches something.
5. **At most six registers per request** – more sporadically produces `CM 05`
   ([kraiz #15](https://github.com/kraiz/hassio-weishaupt/issues/15)).
6. After an empty response or a pure `CM 05`, **do not retry immediately**, but back off in
   stages: 2 → 4 → 8 → 16 → 30 min; the first healthy response resets the backoff.
7. **Keep-alive does not help:** the WEM closes every TCP connection itself after the response.
   The only lever is the number of requests.

## 9. Example: heating circuit to “Time program 1”

**1. Command.** The “ZP 1” button (TP 1) in the phone app sends via the app

```
MQTT  <gerät>/cmd/heizkreis   {"id":"app-mg3k2-1","wert":2,"quelle":"App"}
```

Equivalent: `2` or `Zeitprogramm 1` as text via MQTT (the board then assigns the ID), or
“Heizkreis Betriebsart setzen” in the web interface or in Home Assistant. On
`<gerät>/befehl/status`, `angenommen` (accepted) and `vorgemerkt` (queued, with `warten_s`) appear, later
`gesendet` (sent), `pruefe_bus` (checking bus) and `bestaetigt` (confirmed) – see [TOPICS.md](TOPICS.md#gerätbefehlstatus) (German).
The board additionally reports (deprecated) in “Ergebnis letzter Schaltbefehl”:

```
vorgemerkt (App): Heizkreis Zeitprogramm 1
```

**2. Queue.** If the lock-out minute is still running, “Warteschlange” shows something like
`1. Heizkreis -> Zeitprogramm 1 (MQTT) - naechster Befehl ab 14:03:12`. If another
heating circuit request arrives before that, it replaces this one.

**3. JSON to the WEM.** After the lock-out minute has expired:

```
POST http://<WEM>/ajax/CanApiJson.json
{"ID":"…","SRC":"DDC","CAPI":{"NN":1,"N01":{"VG":"030200253302000102"}}}

VG = 03 02 00 2533 02 0001 02
     CM MI MX OX   OS VS   VA      schreiben, HZK0, Betriebsart, 1 Byte, Wert 2
```

(`schreiben, HZK0, Betriebsart, 1 Byte, Wert 2` = write, HZK0, operating mode, 1 byte, value 2.)

Status: `sende an WEM ...` (sending to WEM), then for a response with `CM 04`:
`WEM hat bestaetigt, pruefe am Bus ...` (WEM confirmed, checking on the bus).

**4. Verification on the bus.** 3 s later the board reads:

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
| WEM responds `CM 05` | `14:03 abgelehnt (CM=05) - VG 05…` (rejected) – no verification read |
| no response on the bus within 10 s | `keine Rueckmeldung vom Bus - Ergebnis unbekannt` (no feedback from the bus – result unknown) |
| WEM unreachable | `WEM nicht erreichbar, pruefe trotzdem am Bus ...` (WEM unreachable, checking on the bus anyway), then as above |

The switching log gets a line like
`27.09. 14:03 Heizkreis -> Zeitprogramm 1 (App): ok`. From v27 onwards, the phone app only evaluates the
phase from `befehl/status` or `status/json` and shows it for three minutes – green for
`bestaetigt` (confirmed), red for `gescheitert` (failed: not applied, rejected CM=05, no feedback) or
`abgelehnt` (rejected: invalid value).

On the bus, the status bits in PDO `0x1C1` then change (bit `0x1000` heating circuit standby is cleared);
“Heizkreis Status” jumps from “Standby” to “Zeitprogramm” (time program).

## 10. Example: DHW off

**1. Command.** “AUS” (OFF) button in the phone app:

```
MQTT  <gerät>/cmd/warmwasser   Aus
```

`aus`, `2`, `OFF`, `off` are also accepted; in the web interface “Warmwasser setzen” → Aus.
Status: `vorgemerkt (MQTT): Warmwasser Aus` (queued: DHW off). Queue and lock-out minute as for the heating circuit;
if a heating circuit request is pending at the same time, it goes first.

**2. JSON to the WEM.**

```
{"ID":"…","SRC":"DDC","CAPI":{"NN":1,"N01":{"VG":"030300252002000102"}}}

VG = 03 03 00 2520 02 0001 02
     CM MI MX OX   OS VS   VA      schreiben, WW0, Betriebsart, 1 Byte, Wert 2 = Aus
```

(`schreiben, WW0, Betriebsart, 1 Byte, Wert 2 = Aus` = write, WW0, operating mode, 1 byte, value 2 = off.)

`CM 04` is expected.

**3. Verification on the bus**, 3 s later:

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
