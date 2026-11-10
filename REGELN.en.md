> 🇩🇪 German original: [REGELN.md](REGELN.md). This is a translation; if in doubt, the German version is authoritative.

# Rules: reading operating modes when something may have changed

Package `pakete/verdacht.yaml`, from firmware v22, in this form from **v25**; since **v30**, R1–R5 are
data rules in the same format as the custom rules (behaviour unchanged, verified against v29). This page explains what
the rules do, how they detect a change, how reliable that is and how to configure them.

- [1. What this is about](#1-what-this-is-about)
- [2. Overview R1–R5](#2-overview-r1r5)
- [3. The rules in detail](#3-the-rules-in-detail)
- [4. Shadow mode: observing disabled rules](#4-shadow-mode-observing-disabled-rules)
- [5. Custom rules (experimental)](#5-custom-rules-experimental)
- [6. Configuration](#6-configuration)
- [7. Switching via MQTT](#7-switching-via-mqtt)
- [8. Trigger log, log and events](#8-trigger-log-log-and-events)
- [9. Verified against captures](#9-verified-against-captures)
- [10. Limits and open questions](#10-limits-and-open-questions)

## 1. What this is about

The operating modes – heating circuit (Standby, time program 1–3, Summer, Comfort, Normal, Setback) and
DHW (on/off) – reside in the **WEM system unit**, not in the boiler. The WEM does **not** report them
on the bus **of its own accord**. Anyone who switches at the display, in the WEM portal or in the Weishaupt app
leaves only **indirect traces** on the bus: the WEM requests certain values from the boiler, its
status bits change, the boiler starts a DHW charge.

The board could poll the operating modes periodically – we do not want that. Instead the rules
evaluate these traces and **re-read only when a change is to be suspected**.

**What a rule may do – and what it may not:**

- A rule **never switches**. At most it triggers **one read request**.
- Reading happens exclusively on the bus, with SDO read requests (command byte `0x40`) to **node 1**
  (`0x601`): heating circuit operating mode `0x2933/2`, DHW `0x2A20/2`, `0x2A2C/2`, `0x2A39/2`.
- **Never** a request to the boiler (node 2), **never** a request to the WEM's JSON
  interface, **never** a fixed schedule.
- Reading happens only while the bus is alive and outside the start-up pause (1 min after a board start,
  10 min after a bus outage).
- Every rule has a **minimum interval**; all together have an **upper limit per hour**
  (default 6).
- **For 2 minutes after one of the board's own switching commands all rules rest** – the command re-reads
  by itself anyway, and its traces on the bus would otherwise be a false alarm.

If all rules are off, the board reads the operating modes only at start-up, after its own switching commands
and at the press of a button („Status lesen“ ("read status") or `<gerät>/cmd/status`).

## 2. Overview R1–R5

| Rule | Trigger on the bus | reads | Default | Interval | typical frequency |
|---|---|---|---|---|---|
| **R1** DHW switched | WEM requests `2101/0A`, `2102/0D`, `2102/01` from the boiler | DHW | **<!--ab-werk:vd_r1-->on<!--/ab-werk-->** | <!--ab-werk:vd_ab_r1-->1<!--/ab-werk--> min | once per DHW mode change, rarely when paging through the display menu |
| **R2** DHW mode despite off | boiler starts a DHW charge while “off” is the known state | DHW | **<!--ab-werk:vd_r2-->on<!--/ab-werk-->** | <!--ab-werk:vd_ab_r2-->10<!--/ab-werk--> min | only if DHW was switched on unnoticed |
| **R3** Heating on ↔ Standby | standby bit `0x1000` of the WEM status bits changes | heating circuit | **<!--ab-werk:vd_r3-->on<!--/ab-werk-->** | <!--ab-werk:vd_ab_r3-->1<!--/ab-werk--> min | once per change Standby ↔ (time program, Summer, Comfort …) |
| **R4** Heat demand does not match | heat demand while Standby/Summer is known, or the flow setpoint does not match the room setpoint level | heating circuit | <!--ab-werk:vd_r4-->off<!--/ab-werk--> | <!--ab-werk:vd_ab_r4-->30<!--/ab-werk--> min | rare; calibrated only for mild outdoor temperatures |
| **R5** other status bits | other WEM status bits change (without standby, DHW charging, heating mode) | heating circuit | <!--ab-werk:vd_r5-->off<!--/ab-werk--> | <!--ab-werk:vd_ab_r5-->5<!--/ab-werk--> min | on every change of the heat requirement, i.e. often |

Disabled rules keep running in [shadow mode](#4-shadow-mode-observing-disabled-rules):
they report that they **would have** triggered, but do not read.

## 3. The rules in detail

### R1 – DHW switched

**What happens on the bus.** On **every** change of the DHW operating mode the WEM requests from the
boiler (`0x602`), one after the other:

```
2101/0A   2102/0D   2102/01   2101/0A   273F/01
```

and 4–13 s later once more briefly `2101/0A`, `273F/01`. The requests come as **SDO
block upload** (command byte `0xA4`), not as an ordinary upload (`0x40`). Index and subindex
are in the same place in both (bytes 1–2 little endian, byte 3), e.g.

```
602:A401210A40400000   -> 2101/0A
602:A402210D40400000   -> 2102/0D
602:A402210140400000   -> 2102/01
```

**Trigger.** The three requests `2101/0A` → `2102/0D` → `2102/01` in this order, the last one
at most 10 s after the middle one and 15 s after the first. Only the **full sequence with `2102/0D`
and `2102/01`** is specific; the short form `2101/0A` + `273F/01` also occurs at every start of charging
and does not count.

**Why this is a sign.** In all captures the full sequence occurred on every
DHW mode change, at the display as well as via JSON, and on **no** mode change of the heating circuit.

**Safeguards.** Rests for 2 min after the board's own switching command and **for 5 min after a restart of the WEM**
(boot-up message `0x701` = `00`) – during which the WEM requests the full sequence once without anyone
having switched. Minimum interval <!--ab-werk:vd_ab_r1-->1<!--/ab-werk--> min (the sequence occurs exactly once per mode change; two
mode changes within a few minutes do happen).

**What R1 does not detect.** The **direction**: the boiler's responses are the same for on and off.
That is why R1 re-reads. Whether mode changes in the **WEM portal or the Weishaupt app** produce the same
sequence has not been captured.

**How reliable.** High. The sequence also occurred when **paging through the menu at the display** – harmless; the
board then re-reads once and reports „geändert: nein“ ("changed: no").

> **Fixed in v25:** Up to v24, R1 evaluated only requests with command byte `0x40`. But the WEM
> requests with `0xA4`. **R1 therefore never triggered in v22–v24.**

### R2 – DHW mode despite known “off”

**Trigger.** The boiler status (PDO `0x182`, byte 0) jumps to **15** (DHW mode), or
the WEM writes `0x252B` = `0F` to the boiler (`0x602`, download with command byte `0x2F`, value in
byte 4) – and the DHW operating mode last read is **off**. Only the edge counts, not
every frame. The WEM writes `0x252B` only via `0x602`, never via `0x6C2`.

**Why this is a sign.** The boiler is charging DHW although it should be off – presumably
it was switched on. In the captures a charge began shortly after each switch-on.

**Safeguards.** 2 min after the board's own switching command, minimum interval <!--ab-werk:vd_ab_r2-->10<!--/ab-werk--> min.

**What R2 does not detect.** Switching on without a subsequent charge (the storage tank is warm enough) and
any switching off. That is what R1 is for.

### R3 – Heating on ↔ Standby

**Trigger.** Bit **`0x1000`** of the WEM status bits changes. The WEM sends the status bits (object
`0x274D`) as PDO **`0x1C1`, only on change**; the value is
`m = x[3]<<8 | x[2]` (raw bytes `00 00 00 10` = `m` `0x1000` = Standby). The last state is
stored persistently so that a change during a board restart is noticed as well.

**Why this is a sign.** The bit is set as long as the heating circuit is on **Standby**,
and cleared in every other operating mode. Every change is a real mode change.

**Safeguards.** 2 min after the board's own switching command, minimum interval <!--ab-werk:vd_ab_r3-->1<!--/ab-werk--> min.

**What R3 does not detect.** Changes between **time program 1/2/3, Summer, Comfort, Normal and
Setback** – the bit stays cleared throughout. These changes are **not visible at all** on the bus, not
even when they come via the WEM app; a change TP 2 → TP 3 via JSON did not produce a single
frame. For that there is the button **„Status lesen“** ("read status").

**How reliable.** High: in all captures every change of the bit belonged to a mode change
Standby ↔ another operating mode.

> **Changed in v25:** Up to v24, R3 reacted to **every** change of the status bits (without DHW charging and
> heating mode). That is now R5; R3 now reacts only to the standby bit.

### R4 – Heat demand does not match the operating mode

**Trigger**, two cases:

1. The WEM demands heating – it writes `0x252B` = `0A` to the boiler or the boiler status
   jumps to **10** (heating mode) – but **Standby** or **Summer** is the known mode.
2. With Comfort, Normal or Setback: from the flow setpoint (FT; PDO `0x241`, bytes 0–1, ×0.1) and the
   outdoor temperature (OT; PDO `0x201`, bytes 1–2, ×0.1) the room setpoint level is calculated back,
   `RT ≈ (FT − 1.4 + 1.1 · OT) / 2.1`, classified into 18 / 20 / 21 °C, and compared with the known
   operating mode (Comfort 21, Normal 20, Setback 18). Only with flow setpoint > 0; the same
   contradiction does not trigger twice in a row.

**Why this is a sign.** Heating operating modes have no signal of their own, but the heat demand
partly gives them away. In the captures the flow setpoint showed a change Comfort →
Normal a few seconds before it was confirmed by the read.

**Safeguards.** 2 min after the board's own switching command, minimum interval <!--ab-werk:vd_ab_r4-->30<!--/ab-werk--> min.

**What R4 does not detect.** Time programs are not checked (they differ only in
their switching times). Setback only demands heat at a lower outdoor temperature; Summer looks like
Setback in mild weather.

**How reliable.** Limited. The back-calculation is **calibrated only for outdoor temperatures of about
13–15 °C**; your own heating curve may give different values. Hence default **off** – first
observe in [shadow mode](#4-shadow-mode-observing-disabled-rules).

### R5 – other status bits changed

**Trigger.** The WEM status bits (`0x1C1`) change **without** the standby bit `0x1000`, the
DHW charging bit `0x0010` and the heating mode bit `0x0040` – i.e. `m & ~0x1050`. If the standby bit
also changes in the same frame and R3 reads anyway, R5 does not read in addition.

**Why this can be a sign.** Some states show up only here, e.g.
chimney sweep mode. Most changes, however, belong to the **heat requirement** (bits `0x0004`,
`0x0400`), which can come and go several times per hour.

**Safeguards.** 2 min after the board's own switching command, minimum interval <!--ab-werk:vd_ab_r5-->5<!--/ab-werk--> min.

**How reliable.** Low as an indication of a mode change: R5 triggers often and mostly only confirms
the known state. Hence default **off**. This corresponds to the behaviour of R3 up to v24.

## 4. Shadow mode: observing disabled rules

A **disabled** rule keeps checking its condition anyway. If it is met, **nothing is
read**, but it is noted:

- in the log (INFO): `R4 waere ausgeloest (Regel aus) - nicht gelesen` (R4 would have triggered (rule off) – not read),
- as an event to `<gerät>/verdacht/ereignis` with `"phase":"waere"`.

This applies to R1–R5 and to custom rules (individually off **or** master switch of the custom rules
off). To prevent flooding, the other locks apply analogously: 2 min after the board's own
switching command, for R1 5 min after a WEM restart, and per rule the minimum interval (separate timestamp,
independent of the real triggers). Shadow messages do **not** count towards the upper limit
per hour and do **not** appear in the trigger log of the last ten triggers – that remains reserved for real
reads.

**What for:** first **observe** a rule, then switch it on. Anyone who writes the events to a database
(see [section 8](#8-trigger-log-log-and-events)) can see in Grafana how often a
disabled rule would have triggered – and can compare that with the known mode changes.

## 5. Custom rules (experimental)

Custom triggers without programming. A separate block „Eigene Regeln (experimentell)“ ("custom rules (experimental)") with **its own
master switch, default off**. At most 8 rules, stored persistently in flash (they survive a
restart even without a broker).

**Since v30 the fixed rules R1–R5 are themselves data rules in exactly this format** (defined in
[`regelwerk.h`](components/weishaupt_can/regelwerk.h), section „die festen Regeln als Daten“ ("the fixed rules as data")) and
run through the same evaluator as the custom ones. What R1–R5 can do – sequences with time windows,
edges, conditions on the known state, resting after a frame, “not in addition if
another rule is already reading” – custom rules can therefore do as well.

### Short form (as up to v29)

*If a frame arrives with this CAN ID whose data bytes AND mask equal pattern AND mask, then
read after the minimum interval.* Old rules remain valid unchanged and are stored and reported
byte-for-byte identically.

| Field | Content |
|---|---|
| `name` | 1–24 characters `A-Z a-z 0-9 - _ .`, unique, not `R1`–`R5`, `eigene`, `verdacht` |
| `an` | `true`/`false` (default `true`) |
| `can_id` | `"0x602"` or a number. **`0x601` and `0x581` are blocked** – these are the board's own requests and their responses; a rule on them would trigger itself |
| `muster` | 1–8 bytes hex, e.g. `"A4 3E 22"` – from v30 also a **list** of 1–4 patterns (one must match) |
| `maske` | 1–8 bytes hex; if it is missing, exactly the bytes given in the pattern count (with a list the patterns must then be of equal length) |
| `laenge_min` | from v30, optional: the frame must have at least this many data bytes (0–8) |
| `wenn` | from v30, optional: only if the state last read matches, e.g. `{"ww":[2]}` or `{"hk":[1,5]}` (values as in [section 3](#3-the-rules-in-detail)) |
| `lesen` | `"hk"` (heating circuit), `"ww"` (DHW) or `"beide"` (both; default) |
| `abstand_min` | 1–1440 (default 10) |
| `beschreibung` | why the rule exists, up to 120 characters |

**Example:** the WEM requests `0x223E` from the boiler (occurred once when a portal page was opened –
connection unproven). Byte 0 is masked out so that `0x40` and `0xA4` both match:

```json
{"name": "Portal-Seitenaufruf", "an": true, "can_id": "0x602",
 "maske": "00 FF FF 00 00 00 00 00", "muster": "00 3E 22 00 00 00 00 00",
 "lesen": "beide", "abstand_min": 30}
```

### Long form (from v30): several triggers, sequences, edges

Instead of `can_id`/`maske`/`muster` a list `ausloeser` (1–4). Each trigger has **exactly one**
kind; if one applies, a read takes place (minimum interval, upper limit, shadow mode and resting after the board's own
switching command apply as always):

| Kind | Notation | applies when … |
|---|---|---|
| Frame | `{"frame": {can_id, maske, muster, laenge_min}}` | a matching frame arrives |
| Sequence | `{"folge": [step, step, …]}` (2–4 steps, each `{can_id, maske, muster, laenge_min, fenster_ms}`) | the steps arrived in this order and, at the last one, every earlier one is at most `fenster_ms` old (no `fenster_ms`: does not matter). After that the sequence starts over |
| Edge | `{"flanke": {can_id, maske, muster, laenge_min}, "byte": 2, "bytes": 2, "bits": "0x1000", "erster": false, "gleich": [15]}` | a value in the frame changes: `bytes` (1–2) bytes starting at `byte`, little-endian, AND `bits`. If `bits` has exactly one bit set, the value counts as 0/1. `erster: true` = even the first value seen counts as a change. `gleich`: only for these new values |
| Building block | `{"baustein": "r4_vorlauf"}` | a calculation in the code applies (see below) |

Additionally per trigger: `wenn` (as above), `text` (label in the log, up to 40 characters),
`meldung_ruht` and `meldung_unterdrueckt` (custom log text, placeholders `{regel}`, `{grund}`,
`{text}`, `{alt}`, `{neu}`, `{althex}`, `{neuhex}`, `{durch}`).

At rule level:

| Field | Content |
|---|---|
| `ruhe_nach` | `{can_id, maske, muster, laenge_min, "ms": 300000, "grund": "WEM-Neustart"}` – after such a frame the whole rule rests for `ms` (1–86,400,000); the log shows `grund` |
| `unterdrueckt_durch` | name of another rule: if that one (enabled) already reads in the same frame, this one is not called in addition |

**Example:** the R1 sequence as a custom rule, additionally “boiler status jumps to 15 while
DHW off is known”, both with 5 min rest after a restart of the WEM:

```json
{"name": "ww-folge", "an": false, "lesen": "ww", "abstand_min": 1,
 "ausloeser": [
   {"folge": [
      {"can_id": "0x602", "maske": "FF FF FF FF", "muster": ["A4 01 21 0A", "40 01 21 0A"], "laenge_min": 5, "fenster_ms": 15000},
      {"can_id": "0x602", "maske": "FF FF FF FF", "muster": ["A4 02 21 0D", "40 02 21 0D"], "laenge_min": 5, "fenster_ms": 10000},
      {"can_id": "0x602", "maske": "FF FF FF FF", "muster": ["A4 02 21 01", "40 02 21 01"], "laenge_min": 5}],
    "text": "WW-Folge"},
   {"flanke": {"can_id": "0x182", "laenge_min": 1}, "byte": 0, "gleich": [15], "wenn": {"ww": [2]}}],
 "ruhe_nach": {"can_id": "0x701", "muster": "00", "laenge_min": 1, "ms": 300000, "grund": "WEM-Neustart"}}
```

The long form needs more memory: all custom rules together may be at most
2400 characters long in their stored form, otherwise the board rejects the file („eigene Regeln zu lang“ ("custom rules too long")).

### R1–R5 in the rule format

This is how the fixed rules are defined in the code (written as JSON; for them `an` and `abstand_min` come
from the switches and minimum intervals of the web interface):

| Rule | Trigger |
|---|---|
| R1 | sequence `0x602` `A4/40 01 21 0A` (≤ 15 s) → `A4/40 02 21 0D` (≤ 10 s) → `A4/40 02 21 01`, each at least 5 bytes; `ruhe_nach` `0x701` = `00` 300,000 ms „WEM-Neustart“ ("WEM restart"); reads ww |
| R2 | edge `0x602` mask `E0 FF FF` pattern `20 2B 25` (WEM writes 0x252B), byte 4, `erster: true`, `gleich [15]`, `wenn {"ww":[2]}`; edge `0x182` byte 0, `gleich [15]`, `wenn {"ww":[2]}`; reads ww |
| R3 | edge `0x1C1` bytes 2–3, `bits 0x1000` (standby bit → 0/1); reads hk |
| R4 | as R2 with `gleich [10]`/`[10]` and `wenn {"hk":[1,5]}`, plus building block `r4_vorlauf`; reads hk |
| R5 | edge `0x1C1` bytes 2–3, `bits 0xEFAF` (without standby, DHW charging, heating mode), `unterdrueckt_durch: "R3"`; reads hk |

The test `aequivalenz_feste_regeln_als_json` writes R1–R5 as JSON in exactly this way, reads them back in
and compares them with v29 (see [section 9](#9-verified-against-captures)).

**Why `r4_vorlauf` remains a building block:** R4 calculates a room setpoint level back from the flow setpoint (PDO `0x241`) and
the outdoor temperature (PDO `0x201`, a different frame!) –
`RT = (FT − 1.4 + 1.1 · OT) / 2.1`, then level 18/20/21 – and compares it with the known
operating mode (Comfort 21, Normal 20, Setback 18). This is floating-point arithmetic across two frames with
levels and a memory of its own (“the same contradiction not twice”); it cannot sensibly be written as a
byte pattern, and the formula is calibrated only for mild outdoor temperatures. The building block
has a name and can also be used in custom rules; its state belongs to the respective
trigger, R4 is not affected by it. Also only in the code (not in the file format): where the
previous values of R3/R5 are stored persistently – so that they keep the same keys across
an update.

### Adding, displaying, switching

**Adding or changing:** list `eigene` in the file (see [6](#6-configuration)). If `eigene` is present
in the file, it replaces the whole list (counters and „zuletzt“ ("last") of rules with the same name are kept);
if the key is missing, the custom rules remain unchanged.

**Displaying:** web interface, group **Eigene Regeln (experimentell)** (custom rules (experimental)), e.g.
`Portal-Seitenaufruf an: 602 [.. 3E 22] -> beide, 30 min, zuletzt …, 3 x`, in the long form
`ww-folge an: folge 602 x3 + flanke 182 -> ww, 1 min, …`; in full under `<gerät>/regeln/stand`
(short-form rules with the same fields as before, long form with `ausloeser`).

**Switching and deleting:** `NAME an`, `NAME aus`, `NAME loeschen` – via MQTT (see
[7](#7-switching-via-mqtt)) or in the field **Regel-Befehl** (rule command) of the web interface.

**Safeguards:** as for the fixed rules (own command 2 min, minimum interval, shared
upper limit per hour), plus the block on `0x601`/`0x581` in every step.

Anything beyond that (times of day, values from other devices) is better built in Home Assistant or
Node-RED, sending `<gerät>/cmd/status` – see
[IDEEN.en.md, section 7](IDEEN.en.md#7-custom-read-rules-in-home-assistant-or-node-red).

## 6. Configuration

Three ways that change the same values; **the last write wins**, everything is stored
persistently.

**At build time** – initial values in the main file under `substitutions:` (they apply only as long as
nothing has been stored yet):

```yaml
  regel_r1_start: RESTORE_DEFAULT_ON
  regel_r2_start: RESTORE_DEFAULT_ON
  regel_r3_start: RESTORE_DEFAULT_ON
  regel_r4_start: RESTORE_DEFAULT_OFF
  regel_r5_start: RESTORE_DEFAULT_OFF
  regel_abstand_r1: "1"      # ... up to regel_abstand_r5
  regeln_max_h: "6"
  eigene_regeln_start: RESTORE_DEFAULT_OFF
```

**In the web interface** – group **Regeln (R1-R5)** (rules (R1–R5)): per rule a switch, minimum interval, „was und
warum“ ("what and why"), „zuletzt ausgelöst“ ("last triggered"); plus the upper limit per hour, „Regeln aktiv“ ("rules active"), „Regeln Protokoll“ ("rules log") and
„Regeln-Datei“ ("rules file"; result of the last file).

**At runtime by file** – JSON to `<gerät>/cmd/regeln`, preferably retained. Template:
[`regeln.json.example`](regeln.json.example).

```json
{
  "version": 1,
  "R1": {"an": true,  "abstand_min": 1},
  "R2": {"an": true,  "abstand_min": 10},
  "R3": {"an": true,  "abstand_min": 1},
  "R4": {"an": false, "abstand_min": 30},
  "R5": {"an": false, "abstand_min": 5},
  "max_pro_stunde": 6,
  "eigene_regeln_an": false,
  "eigene": []
}
```

```
mosquitto_pub -h <broker> -u <account> -P <password> -r -f regeln.json -t <gerät>/cmd/regeln
```

| Field | Content |
|---|---|
| `version` | integer ≥ 0, freely chosen; the board reports it back in the state |
| `R1` … `R5` | each `{"an": true/false, "abstand_min": 1–1440}` |
| `max_pro_stunde` | 1–60, applies to all rules together |
| `eigene_regeln_an` | master switch of the custom rules |
| `eigene` | list of the custom rules |
| `kommentar`, `_…` | are ignored |

Every field is optional; whatever is missing stays as it is. **Invalid JSON, unknown fields and
values outside the limits are rejected by the board as a whole** („Regeln-Datei: ABGELEHNT: …“ ("rules file: REJECTED: …")) and nothing
is changed. The field `verdacht` (up to v22) is rejected with a hint.

**Retained and checksum:** The board remembers a checksum of the last file it accepted.
The same file is **not applied again** after a restart or reconnect – otherwise it would
overwrite every later change made in the web interface. If an unchanged file is deliberately meant to
apply again, increment `version`.

**Feedback channel:** the board reports the active state retained under `<gerät>/regeln/stand` (at start-up
and on every change), in the same format plus `firmware` and runtime values of the custom rules:

```json
{"firmware":"v25 vom 27.09.2026","version":1,
 "R1":{"an":true,"abstand_min":1},"R2":{"an":true,"abstand_min":10},
 "R3":{"an":true,"abstand_min":1},"R4":{"an":false,"abstand_min":30},
 "R5":{"an":false,"abstand_min":5},
 "max_pro_stunde":6,"eigene_regeln_an":false,"eigene":[]}
```

## 7. Switching via MQTT

Individual rules are switched without a file using `<gerät>/cmd/regel`:

```
mosquitto_pub -h <broker> -u <account> -P <password> -t <gerät>/cmd/regel -m "R4 an"
mosquitto_pub -h <broker> -u <account> -P <password> -t <gerät>/cmd/regel -m "R5 aus"
mosquitto_pub -h <broker> -u <account> -P <password> -t <gerät>/cmd/regel -m "eigene an"
mosquitto_pub -h <broker> -u <account> -P <password> -t <gerät>/cmd/regel -m "Portal-Seitenaufruf loeschen"
```

`R1`–`R5` and `eigene` (master switch of the custom rules) can only be switched on and off;
custom rules can also be deleted. Unknown names or commands are rejected with a message; the
response is shown in the web interface under **Regel-Befehl Antwort** (rule command response), the result under
`<gerät>/regeln/stand`.

**Broker permissions:** Anyone allowed to write `cmd/regel` or `cmd/regeln` can switch rules and thereby
trigger read requests – read-only, to node 1 and throttled, but still bus traffic. Grant these rights only
to an admin account; the account of the phone app does **not** have them.

## 8. Trigger log, log and events

**Trigger log** of the last ten **real** triggers since the restart – web interface („Regeln
Protokoll“ ("rules log")) and retained under `<gerät>/verdacht/protokoll`, one line per trigger:

```
27.09. 09:52 R1: Warmwasser Ein (vorher Aus), geaendert: JA
27.09. 10:03 R3: Heizkreis Standby, geaendert: nein
```

„geändert“ ("changed") compares the value read with the previously known one; if that was unknown, `?` is shown.
If no response arrives within 3 min (for instance because the bus fell into the start-up pause), it says
`keine Antwort` (no response). If several rules triggered the same read, they are listed together (`R3+R5`).

**Log** (tag `verdacht`): every trigger and every result as **INFO**, every **suppressed**
trigger (own command, WEM restart, minimum interval, upper limit, bus not ready) as
**DEBUG**, shadow messages as INFO.

**Events** – every trigger, every shadow message and every result as JSON to
**`<gerät>/verdacht/ereignis`** (**not** retained):

| `phase` | Meaning | further fields |
|---|---|---|
| `ausgeloest` | rule has triggered a read | `regel`, `ziel` (`hk`, `ww`, `beide`) |
| `waere` | condition was met, rule was off – **not** read | `regel`, `ziel` |
| `ergebnis` | response to the read | `regel`, `ziel`, `wert` (e.g. `Standby`, `Ein`), `geaendert` (`true`, `false`, `null` = previously unknown) |
| `keine_antwort` | no response within 3 min | `regel`, `ziel` |

```json
{"regel":"R3","phase":"ausgeloest","ziel":"hk","zeit":"2026-09-27T13:20:05"}
{"regel":"R3","phase":"ergebnis","ziel":"hk","wert":"Standby","geaendert":false,"zeit":"2026-09-27T13:20:06"}
{"regel":"R4","phase":"waere","ziel":"hk","zeit":"2026-09-27T13:31:40"}
```

`zeit` is missing as long as the board's clock has not been set yet.

**Writing to a database** (Telegraf → InfluxDB), one measurement with the tags `regel`,
`phase`, `ziel` and the fields `wert`, `geaendert` (0/1), `anzahl` (=1, for counting):

```toml
[[inputs.mqtt_consumer]]
  servers   = ["tcp://<broker>:1883"]
  username  = "telegraf"
  password  = "${MQTT_PASS}"
  client_id = "telegraf-heizung-regel"      # separate ID per input
  topics    = ["<gerät>/verdacht/ereignis"]
  data_format = "json_v2"
  [[inputs.mqtt_consumer.json_v2]]
    measurement_name = "heizung_regel"
    [[inputs.mqtt_consumer.json_v2.tag]]
      path = "regel"
    [[inputs.mqtt_consumer.json_v2.tag]]
      path = "phase"
    [[inputs.mqtt_consumer.json_v2.tag]]
      path = "ziel"
      optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "phase"
      rename = "ereignis"
      type = "string"
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "wert"
      type = "string"
      optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "geaendert"
      optional = true

[[processors.starlark]]
  namepass = ["heizung_regel"]
  source = '''
def apply(metric):
    metric.fields["anzahl"] = 1
    g = metric.fields.get("geaendert")
    if type(g) == "bool":
        metric.fields["geaendert"] = 1 if g else 0
    elif g != None and type(g) != "int":
        metric.fields.pop("geaendert")     # null: omit
    return metric
'''
```

> For testing a copy with `telegraf --test`: if inputs run with **the same
> `client_id`** as in the running service, the two kick each other off the broker. Test a
> copy containing only the new input.

In **Grafana**, a table of the latest events (time, rule, phase, target, value,
changed), a bar chart “triggers per rule” split by `ausgeloest` and `waere`, and
annotations for `phase = ausgeloest` in a heating chart go well with this. Example query for the bar chart:

```
from(bucket: "<bucket>")
  |> range(start: -7d)
  |> filter(fn: (r) => r._measurement == "heizung_regel" and r._field == "anzahl")
  |> filter(fn: (r) => r.phase == "ausgeloest" or r.phase == "waere")
  |> group(columns: ["regel", "phase"])
  |> sum()
  |> map(fn: (r) => ({name: r.regel + " " + r.phase, _value: r._value}))
  |> group()
```

## 9. Verified against captures

The rule logic was reproduced byte for byte in Python (same bit masks, byte order,
time windows and locks) and run over all raw captures of one evening with known mode changes
(about 295,000 frames, heating circuit and DHW each several times at the display and via JSON):

| Rule | Hits | False alarms | missed | Note |
|---|---|---|---|---|
| R1 (`0x40` only, up to v24) | 0 | 0 | **all** | the bug that v25 fixes |
| R1 (v25) | all DHW mode changes | 0 | 0 | plus two hits when paging through the display menu; never on heating circuit mode changes. With the earlier interval of 10 min, two quick mode changes in a row would have been missed – hence now 1 min |
| R2 | – | 0 | 0 | the condition occurred exactly twice, each time at the start of charging after switching on; at that point the DHW operating mode was not yet known in the capture |
| R3 | all standby mode changes | 0 | 0 | with the earlier interval of 5 min, a quick switch back would have been missed – hence now 1 min |
| R4 | 2 | 0 | 0 | once Summer → time program (heat demand), once Comfort → Normal (flow setpoint), both before confirmation by a read; the second fell within the minimum interval |
| R5 | 5 reads | – | – | triggered 26 times, almost always heat requirement on/off; once chimney sweep mode |

Byte positions were cross-checked in the process: boiler status `0x182` byte 0; flow setpoint `0x241`
bytes 0–1; outdoor temperature `0x201` bytes 1–2; the WEM writes `0x252B` as a 1-byte download
(`0x2F`, value in byte 4) via `0x602` with the values `01` (no demand), `0A` (heating) and
`0F` (DHW).

**Conversion to data rules (v30), verified against v29:** the logic of v29 is kept frozen in
[`tests/referenz_v29/`](tests/referenz_v29/). The test `aequivalenz_zufall_v29_gleich_neu` plays
400 random runs of 1500 frames each (half of them across the overflow of `millis()`, with interspersed
R1 sequences, mode-change scenes, responses, WEM restarts, own switching command, changing switches,
intervals and upper limits) through both versions and demands after **every** frame the same
log lines, the same calls with the same result (trigger, shadow, interval, upper limit,
bus), the same events and the same stored state. It also checks that every case
occurred (every rule triggered, every „ruht“ ("resting") message, „R3 liest schon“ ("R3 is already reading"), building block R4, shadow,
upper limit …). In addition there are fixed scenarios exactly at the time windows of R1 and a cross-check: nine
deliberately introduced bugs (window, command byte, WEM restart, condition, bits, target, log text
…) each turn the comparison red.

## 10. Limits and open questions

- **Mode changes via the WEM portal and the Weishaupt app** have never been captured. Whether they leave the same
  traces as at the display is open – feedback welcome.
- **Time program 1 ↔ 2 ↔ 3, Summer, Comfort, Normal, Setback** among each other are invisible on the bus
  (R4 sees only part of this, when there is currently a heat requirement). For that: **„Status lesen“** ("read status").
- **The back-calculation of R4** needs winter data (outdoor temperature 0–8 °C) and depends on the installation
  (heating curve).
- **R5** is weak as an indication of mode changes; useful mainly in shadow mode, to
  see rare states (chimney sweep, faults).
- Measured on a **WTC-GW 15-B** with WEM system unit. Other boilers or WEM versions may send
  different sequences – in that case observe in shadow mode first.

More on the bus protocol: [PROTOKOLL.en.md](PROTOKOLL.en.md), in particular
[“Detecting passively: what works and what does not”](PROTOKOLL.en.md#detecting-passively-what-works-and-what-does-not).
