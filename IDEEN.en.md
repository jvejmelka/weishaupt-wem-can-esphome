> 🇩🇪 German original: [IDEEN.md](IDEEN.md). This is a translation; if in doubt, the German version is authoritative.

# Ideas – what else could be built with the board

None of this has been implemented. This page describes what is conceivable with this firmware and some
additional hardware, how much effort it would take and where the catches are. Feedback and experiments by others are
welcome – please as a [GitHub issue](https://github.com/jvejmelka/weishaupt-wem-can-esphome/issues).

The ideas start from the [example installation](ANLAGE.en.md): gas condensing boiler with WEM, DHW heat pump
on the same storage tank, PV with a home power station.

| Idea | Effort | Risk | Assessment |
|---|---|---|---|
| [DHW lockout via JSON](#1-dhw-lockout-via-json-h2-replacement-without-a-cable) | low – automation only | low | already works today |
| [DHW lockout via H2 with a relay](#2-dhw-lockout-via-the-input-h2-with-a-relay) | medium – electrical work in the boiler | low | the “proper” way, independent of the WEM |
| [Room temperature from your own sensors](#3-room-temperature-from-your-own-sensors) | medium | medium | evaluating right away, controlling only to a limited extent |
| [Wireless Weishaupt room unit (CAN over WLAN)](#4-wireless-weishaupt-room-unit-can-over-wlan) | high | high | technically conceivable, experimental |
| [Individual room control with radiator valves](#5-individual-room-control-with-wireless-radiator-valves) | medium to high | low | stand-alone, complements the heating system |
| [Checking the installation: is everything set correctly?](#6-checking-the-installation-is-everything-set-correctly) | low | none | worth doing first |
| [Custom read rules in Home Assistant or Node-RED](#7-custom-read-rules-in-home-assistant-or-node-red) | low | low | already works today |
| [Storage tank temperature at the top](#8-storage-tank-temperature-at-the-top) | low to medium | low | approximations only, without intervening in the storage tank |
| [Wall panel for about €75 – goal: RG-compatible](#9-wall-panel-for-about-75--goal-rg-compatible) | medium (display) to high (RG emulation) | low (display) to high (RG emulation) | display and operation feasible, RG emulation only after a capture |
| [Portable room unit made of just the phone app and wireless sensors](#10-portable-room-unit-made-of-just-the-phone-app-and-wireless-sensors) | high | high – the board transmits on the bus continuously | the goal behind ideas 3, 4 and 9; protocol captured, not built yet |

Ideas 3, 4, 9 and 10 are different ways to the same goal, a room value in the WEM. They are set side by side
at the end: [All ways to the room value at a glance](#all-ways-to-the-room-value-at-a-glance).

But first, what all of this stands or falls with: the measurement data.

## 0. Logging everything: database and Grafana

The board delivers its values via MQTT. In our setup **Telegraf** writes them into an **InfluxDB**,
and **Grafana** shows them as a history – together with everything else that is measured in the house:

| Source | What |
|---|---|
| this board | boiler, burner, DHW, operating modes, heat quantities |
| Shelly plugs | power of the heat pump and other consumers |
| Shelly and Bluetooth sensors | temperature and humidity in several rooms |
| home power station / meter | PV generation, battery, grid consumption, feed-in |
| weather service | outdoor temperature and irradiance for cross-checking |

Only the history allows statements that a momentary value never yields: how often the burner
starts, how fast the storage tank cools down (example in [ANLAGE.en.md](ANLAGE.en.md#measured-one-afternoon-and-one-night)),
when the heat pump runs and whether the rooms get warm with the heating curve that is set.

The board's **rule events** (`<gerät>/verdacht/ereignis`) can be logged the same way –
Grafana then shows when a rule triggered, what the read returned and how often a
**disabled** rule would have triggered. Telegraf input and query for this:
[REGELN.en.md, section 8](REGELN.en.md#8-trigger-log-log-and-events).

A Telegraf input for the board values looks roughly like this:

```toml
[[inputs.mqtt_consumer]]
  servers  = ["tcp://<broker>:1883"]
  topics   = ["weact-can485-weishaupt/sensor/+/state"]
  username = "telegraf"
  password = "${MQTT_PASS}"
  data_format = "value"
  data_type   = "string"      # text sensors live in the same topic tree
  name_override = "heizung_can"
  [[inputs.mqtt_consumer.topic_parsing]]
    topic = "weact-can485-weishaupt/sensor/+/state"
    tags  = "_/_/messwert/_"
```

**Why `data_type = "string"`:** ESPHome publishes text sensors (operating mode, switching status …)
under `sensor/+/state` **as well**. With `float` each of them would be a parse error in the log – about
40 per minute in our case. So read them in as text and let only numbers through:

```toml
[[processors.starlark]]
  namepass = ["heizung_can"]
  source = '''
def apply(metric):
    v = metric.fields.get("value")
    if type(v) != "string":
        return metric
    t = v.strip()
    u = t[1:] if t.startswith("-") else t
    if u == "" or u.count(".") > 1 or not u.replace(".", "").isdigit():
        return None          # text: discard
    metric.fields["value"] = float(t)
    return metric
'''
```

> **Test changes on a copy first.** A faulty file in `telegraf.d/` stops the
> whole service, not just this input:
> `telegraf --config /etc/telegraf/telegraf.conf --config-directory <kopie> --test`

### Alerts

A data-flow rule (“are values still arriving?”) does **not** notice a dead bus: the board keeps sending
even then (uptime, WLAN, „Letzter CAN-Frame vor“ ("last CAN frame ago")). For that, use a separate rule on the age of the
last frame, in Grafana with a threshold of **> 300 s**:

```
from(bucket: "<bucket>")
  |> range(start: -15m)
  |> filter(fn: (r) => r._measurement == "heizung_can" and r.messwert == "letzter_can-frame_vor" and r._field == "value")
  |> last()
  |> group()
  |> keep(columns: ["_time", "_value"])
```

`group()` and `keep()` are mandatory: without them the tag columns remain, Grafana gets a
“long series” and the rule is permanently in the error state. Set `execErrState` to **Error** (not
Alerting) – otherwise a query error reports a silent bus. If the board itself is gone (no
data), the data-flow rule reports that.

## 1. DHW lockout via JSON (H2 replacement without a cable)

**Already works today.** An automation (Home Assistant, Node-RED, a script) sends
`cmd/warmwasser Aus` when the heat pump is to do the charging, and `cmd/warmwasser Ein` when gas
is allowed again.

**A fixed time of day is no good for this** (“gas off during the day, on at night”): with a home battery the
heat pump also likes to charge at night from stored PV electricity – twice between 21:00 and
5:00 in our measurement. More sensible triggers are the **bottom of the storage tank below a threshold** together with the
**battery's state of charge** or the PV forecast: if the battery is empty and no sun is coming,
gas is allowed; otherwise not.

- **Advantage:** no wiring, everything through the board, verification on the bus.
- **Catch:** it depends on the WEM's fragile JSON interface. Two commands a day are
  uncritical, a control loop at one-minute intervals is not (see
  [When the WEM locks up](PROTOKOLL.en.md#8-when-the-wem-locks-up)).
- **Necessary?** Not in our installation – the sensor positions already separate the tasks
  ([Why no H2 lockout is needed](ANLAGE.en.md#why-no-h2-lockout-is-needed)).

## 2. DHW lockout via the input H2 with a relay

The WTC has an **input H2** whose function can be set at installer level
(see the WTC's installation manual). With a potential-free contact on it, DHW heating can be
locked out, for example – this is the way manufacturers intend for heat pump plus boiler.

- **Who switches the contact?** A small relay on the board (free GPIO, relay module with
  galvanic isolation) or a stand-alone wireless relay (e.g. a Shelly with a potential-free
  contact). It could be driven by the energy manager, Home Assistant or the board itself.
- **Advantage over idea 1:** **not a single JSON command.** The boiler only sees a contact,
  the WEM is never asked. This is robust even when the JSON interface happens to be locked.
- **Verification:** on the bus you can see whether the boiler is making DHW (boiler status, status bits).
- **Catch:** work on the boiler's electrics – qualified installer, heating system de-energised, contact truly
  potential-free. Which H2 function belongs to which parameter: look it up in the manual of your own
  unit before connecting.

## 3. Room temperature from your own sensors

Without a room unit the heating circuit runs purely **weather-compensated** according to the heating curve. Room compensation
exists in the WEM but gets no room value. Temperature and humidity sensors in several rooms
(Shelly H&T via WLAN, Bluetooth sensors through a Shelly gateway) deliver this value – the question is
what to do with it:

- **Evaluating – possible right away:** room temperatures next to outdoor temperature, flow and burner in
  Grafana. This shows whether the heating curve fits (rooms too warm = curve too steep) and which room
  lags behind.
- **Coarse control – possible with the board:** switch the operating mode (e.g. Setback when all rooms are
  warm, Normal when one gets too cold). That is a few commands a day, so within limits.
- **Fine control – not without further ado:** getting the room value into the WEM as if it came from a
  Weishaupt room unit would mean faking a foreign participant on the bus. This
  firmware deliberately does not do that – see idea 4.

**This does not work via JSON.** To the WEM, a room unit or room sensor is a participant of its own on the CAN bus: the WEM
admits it, it sends a heartbeat and delivers the room value on its own initiative
([VERSUCHE.en.md, 10a](VERSUCHE.en.md#10a-wem-rg2-room-unit-on-the-bus-capture-november-2026)). The WEM's JSON interface
only answers single requests for its own values – no device can register through it or send anything
continuously, and the WEM does not tolerate regular requests. We know of no JSON way to give the WEM a room
value. A board without a CAN cable (JSON only) is therefore enough for fetching values and for switching by hand; anyone who wants a
room unit or room sensor at the WEM – a real one, over the tunnel (idea 4) or emulated (idea 9, stage 2, and idea 10) –
needs a board **on the CAN bus**: the WeAct CAN485 or, for Ethernet, the
[Olimex variant by cg089](OLIMEX-ESP32-EVB.en.md).

## 4. Wireless Weishaupt room unit (CAN over WLAN)

Weishaupt's own room units are wired to the same CAN bus (terminal H L − +; − and + supply
the unit). The idea: instead of a cable, two boards – one at the boiler, one at the room unit – that pass the
CAN telegrams on over WLAN. That would make the room unit portable.

**Technically conceivable, but not “replacing a cable with WLAN”:**

- Each bus segment needs its own **acknowledgements (ACK)** at CAN timing. The boards therefore have to
  work as a **gateway**: both sides acknowledge locally and pass the telegrams on. This is
  feasible with an ESP32 and its CAN controller.
- WLAN brings **delay and dropouts**. PDOs and heartbeats tolerate a few dozen
  milliseconds, but if the connection fails, the WEM reports the room unit as lost and falls
  back to its fallback operation. This has to be tested beforehand on your own installation.
- At the new location the room unit needs **its own supply** instead of − and + from the bus.
- Anyone who has no room unit would have to buy one – whether that is worth it depends on what it offers compared with
  ideas 3 and 5.

**Risk:** Here telegrams are **written** to the bus, not just read – even if only
ones that a real Weishaupt device produced. Errors in the forwarding can confuse
nodes. Hence only as an experiment, with a way back (unplug the cable or the device) and never on node 2.

### Wanted: someone with a Weishaupt room unit on the bus

> **As of 8 Nov 2026:** A used WEM-RG2 2.1 is running here, also over a CAN tunnel (WLAN) from the desk. Commissioning, reading and writing have been captured and decoded: [VERSUCHE.en.md, 10a](VERSUCHE.en.md#10a-wem-rg2-room-unit-on-the-bus-capture-november-2026) and [10b](VERSUCHE.en.md#10b-can-tunnel-over-wlan-experiment-branch-can-tunnel). The tunnel can be configured as package `can-tunnel` in the branch `can-tunnel` and is described there (two boards, addresses in `secrets.yaml`). Further captures (RG1, WEM-RF, other versions, several room units, room sensor assigned) remain welcome.

Until now we had **no** room unit and therefore cannot see what it sends on the bus. But exactly that
would be the basis for this idea – and, incidentally, for room compensation from your own sensors
(idea 3). Anyone who has a Weishaupt room unit on the CAN bus of their WEM (control unit with room sensor) and
runs this board could help with a few captures:

1. Switch on the raw capture (switch „CAN-Rohmitschnitt nach MQTT“ ("raw CAN capture to MQTT"), the board only listens)
   and write the frames from `<gerät>/canraw` to a file.
2. Capture for a few minutes **without** intervening – this shows what the room unit sends regularly
   (heartbeat, room temperature, setpoint).
3. On the room unit, change **one** thing at a time, with a one-minute pause in between, and note the
   time: room setpoint up/down, change the operating mode.
4. If possible: briefly unplug the room unit and plug it back in (boot-up, registration at the WEM).
5. Note the hardware/software versions of room unit and WEM.

**Please do not upload raw files publicly** – they can contain serial numbers and device identifiers.
Better: a summary as a [GitHub issue](https://github.com/jvejmelka/weishaupt-wem-can-esphome/issues)
(which CAN IDs the room unit sends, how often, which bytes change when something is adjusted) and, for
details, contact through the issue. The evaluation follows the “scan and diff” method from
[PROTOKOLL.en.md, section 7](PROTOKOLL.en.md#7-finding-new-objects-scan-and-diff).

## 5. Individual room control with wireless radiator valves

Wireless thermostats on the radiators (e.g. Shelly BLU TRV, FRITZ!DECT, Homematic) control each room
themselves; the heating system keeps delivering according to the heating curve. The board is not needed for that – but it provides
the context:

- If all valves are nearly closed although it is cold outside, the **heating curve is too high**.
- If one valve is fully open all the time and the room is still cold, output is missing there –
  **hydraulic balancing** or a radiator that is too small.
- If the burner cycles frequently while many valves are closed, there is not enough heat being taken off.

Later a simple feedback loop could be built from this: if a room reports demand permanently,
raise the operating mode or setpoint – again with a few commands a day.

## 6. Checking the installation: is everything set correctly?

The least conspicuous but most useful item: using the logged data to check the settings of the
installation.

| Question | How you can tell |
|---|---|
| Is the burner cycling? | burner starts per hour (counter in the board), short run times |
| Does the heating curve fit? | room temperatures against outdoor temperature, flow setpoint against actual |
| Is the unit condensing? | return temperature below about 55 °C |
| Is gas working against the heat pump? | burner in DHW mode while the heat pump is running |
| How expensive is DHW? | heat quantities from the boiler against the heat pump's electricity |

Many of these questions can already be answered today without switching anything.

## 7. Custom read rules in Home Assistant or Node-RED

The board knows fixed rules (R1–R5, individually switchable, disabled ones in shadow mode) and simple
custom rules of the form “CAN ID + mask + pattern” ([REGELN.en.md](REGELN.en.md)).
Whatever is too involved for that – several conditions, times of day, presence, values of other devices –
can be built outside: an automation in Home Assistant or a flow in Node-RED watches
the board's MQTT values (or `<gerät>/canraw` if the raw capture is on) and sends
**`<gerät>/cmd/status`** on suspicion. The board then reads the heating circuit and DHW operating mode
from the bus once (accepted at most every 10 s, read requests only).

Examples: read once after coming home; when the heating circuit's flow temperature rises
although Standby is reported; once in the morning. The only important thing is that the automation throttles itself –
`cmd/status` is built for individual requests, not for a polling schedule.

## 8. Storage tank temperature at the top

On the bus there is only the gas unit's DHW sensor, and in the
[example installation](ANLAGE.en.md) it sits at the **bottom** of the storage tank. At the top only the DHW heat pump measures – but it
has only a display, no data interface. How to get at “top” anyway without opening the storage
tank:

| Way | How | Catch |
|---|---|---|
| **Sensor on the DHW outlet pipe** | contact sensor (e.g. DS18B20 on the board or on a Shelly with a sensor input) on the pipe right at the tank outlet, with thermal paste, **under** the pipe insulation | measures pipe temperature, not water temperature. Right at the outlet the pipe follows the tank quite well; further away it cools down when no water is drawn |
| **Sensor under the tank insulation** | where the casing can be opened without tools, a flat contact sensor on the tank wall at the top | only if casing and insulation allow it; a wireless sensor on the outside of the insulation only measures the room air |
| **Second sensor in the existing immersion sleeve** | push it into the same sleeve next to the heat pump's sensor | only if there is room, requires opening the casing – qualified installer |
| **Photographing the display** | small camera in front of the display, read the number by text recognition | laborious, sensitive to light, more of a gimmick |
| **Calculating instead of measuring** | estimate from bottom temperature, heat pump power (plug) and run time | only a rough approximation |

For most questions the temperature at the bottom together with “heat pump running” is enough: cold at the bottom means
that a lot of water was drawn – and that is exactly when gas kicks in.

## 9. Wall panel for about €75 – goal: RG-compatible

A Weishaupt room unit costs about €300 new. The idea: the **CAN board at the boiler** (this project)
takes over the bus side, in the room there is only a **WLAN touch panel** – no second CAN board, no
bus cable, no 24 V in the room. Anyone who wants nothing at all on the wall uses the phone instead of the panel:
[idea 10](#10-portable-room-unit-made-of-just-the-phone-app-and-wireless-sensors).

```
Room                                    Boiler room
┌───────────────────────┐   WLAN/MQTT   ┌───────────────────────┐   CAN   ┌─────┐
│ Touch panel (ESP32-S3)│ ◄───────────► │ CAN board (this        │ ◄─────► │ WEM │
│ + wireless room sensor│               │ project) on boiler bus │         └─────┘
└───────────────────────┘               └───────────────────────┘
```

| Part | Where | approx. price |
|---|---|---|
| CAN board (e.g. WeAct CAN485) | once at the boiler | €10–15 |
| 4" touch panel ESP32-S3, 480×480, “86 box” format (e.g. Guition ESP32-4848S040) | per room | ~€25 |
| Bluetooth room sensor (e.g. Shelly BLU H&T) – the panel receives it itself via BLE | per room | €15–20 |
| USB power supplies | boiler + per panel | €5–8 each |
| **first room** | | **~€60–75** |

Prices are rough guide values (as of Nov 2026), not verified.

**Stage 1 – display and operation (feasible with today's state):** room temperature and humidity,
boiler, flow and DHW status, dew point/mould warning; switching operating mode and DHW
through the firmware's existing way (switching via CAN with minimum interval, see README).
Display off at night, on when touched.

**Stage 2 – RG-compatible (goal, not yet proven):** towards the WEM the CAN board plays a
room unit and delivers the sensor's room value – real room compensation without a Weishaupt device.
This only works on the CAN bus, not via JSON (see idea 3). The bus part is the same as in
[idea 10](#10-portable-room-unit-made-of-just-the-phone-app-and-wireless-sensors); there you find what the board would have to emulate,
what is open and what belongs to safety. For the panel, add:

- the sensor does **not** belong in the panel housing (self-heating from display and chip, typ. +1–3 K);
- if the panel receives the sensor, the same applies when the panel fails as for a silent sensor: no old
  room value onto the bus (idea 10, safety). Operation remains possible through the phone app.

With the panel, make sure it is an **ESP32-S3** (WLAN + BLE built in); variants with **ESP32-P4** have no radio
module of their own. “Without bottom shell” means the panel only (power via USB-C); the variant with
a base for 230 V belongs in the hands of a qualified electrician.

## 10. Portable room unit made of just the phone app and wireless sensors

This is the goal that ideas 3, 4 and 9 lead up to: a room unit that does not exist as a device at all.
A Weishaupt room unit consists of two things – **sensors** (room temperature, room humidity) and a
**user interface**. Both can be replaced:

- the sensors by **real room sensors** placed where the measurement is to be taken – e.g. Shelly H&T (WLAN) or
  Shelly BLU H&T (Bluetooth through a Shelly gateway), as well as any other sensor that delivers its value via MQTT;
- the interface by this project's **phone app**, which runs on the board anyway.

The **CAN board at the boiler** registers with the WEM as a room unit and delivers the room value to it. Nothing hangs on the wall,
only the sensor stands in the room, and the whole thing is portable because the phone is the user interface.

```
Room                                   Boiler room
┌──────────────────────┐   WLAN/MQTT   ┌───────────────────────┐   CAN   ┌─────┐
│ Wireless room sensor │ ────────────► │ CAN board (this        │ ◄─────► │ WEM │
└──────────────────────┘               │ project) on boiler bus,│         └─────┘
┌──────────────────────┐     WLAN      │ registers as a         │
│ Phone with the app   │ ◄───────────► │ room unit              │
└──────────────────────┘               └───────────────────────┘
```

**What you need in the end:**

| Part | Where | approx. price |
|---|---|---|
| CAN board (WeAct CAN485; for Ethernet the [Olimex variant by cg089](OLIMEX-ESP32-EVB.en.md)) | once at the boiler | €10–15 |
| Wireless room sensor with MQTT (e.g. Shelly H&T or Shelly BLU H&T with gateway) | per room | from ~€15 |
| USB power supply | boiler | €5–8 |
| MQTT broker and WLAN | in the house | usually present |
| Phone | – | present |

Prices are rough guide values (as of Nov 2026), not verified. No Weishaupt room unit, no second board, no panel,
no bus cable into the room. It does not work via JSON alone (see idea 3) – the one board has to be on the CAN bus.

**What already exists of this (as of Nov 2026):**

- The protocol of a real WEM-RG2 has been captured: commissioning, heartbeat, room value, reading, writing, failure
  ([VERSUCHE.en.md, 10a](VERSUCHE.en.md#10a-wem-rg2-room-unit-on-the-bus-capture-november-2026)). The real room unit and the
  CAN tunnel (idea 4) were only the means of watching how it is done.
- The phone app runs on the board, switches operating mode and DHW and shows third-party MQTT values (e.g.
  room temperatures) as extra displays (packages `app` and `zusatz`).
- **Not built** is the virtual room unit itself. To this day this firmware does not register as a room unit and
  sends no room value.

**What the board would have to emulate** (all from capture 10a):

- a node of its own with heartbeat `0x704` and the RG2's start-up sequence;
- the answer to `0x1018/1` when the WEM starts – commissioning runs through this, in our case only after a cold start of
  boiler and room unit together;
- the acknowledgement of `0x20F0` (device list);
- the **room value via PDO `0x184`**: temperature and humidity, on a change after 1 s at the earliest, otherwise about once
  per minute;
- for operation, the values an RG2 writes via `0x5E00` – room setpoint setback/normal/comfort, party, holiday,
  heating curve – and those it reads, such as the time programs. The firmware already switches operating mode and DHW today.

**Open:**

- the **menu bundle**: after commissioning the WEM sends about 1.6 MB to the room unit (about 25 minutes in our case). A
  device without a display of its own does not need the content. Whether the board has to accept and discard the transfer or
  whether the WEM does not send it at all to a device without a control panel has not been tried;
- how the WEM assigns a room unit to a heating circuit, whether several work on one heating circuit and what it does with the room value
  once a room sensor is assigned. In our capture none was assigned, the room value did not change the
  flow setpoint;
- **which room counts**: a reference room or an average of several sensors. The board decides that; the WEM
  sees only one value per room unit;
- the meaning of bytes 4–5 in PDO `0x184`.

**Safety – part of it from the start:**

- An emulated room unit **transmits continuously**. That contradicts the principle of this firmware (no write series,
  nothing triggered from rules). Hence only as a separate mode that is **off** by default, never on node 2 (boiler),
  with a way back: switch off = the board is only a listener on the bus again.
- **No stale room value:** if the sensor is silent (battery empty, WLAN or MQTT gone), the board must not keep passing on the last
  value. After a maximum age it behaves like “room unit missing” and stops the
  heartbeat. The WEM reports this after 30 s as warning 1501 (measured); how it then controls with a room sensor
  assigned is open and must be checked on your own installation before continuous operation.
- **Limits for the value:** pass on only plausible temperatures and humidities, limit jumps – a sensor in the
  sun or on the radiator must not throw the installation off balance.
- **Operation from outside** only as described for the app (login, from outside at most behind a reverse proxy
  with TLS). Setpoints stay within the limits the WEM itself specifies.
- Unlike the CAN tunnel, where the second board only runs under supervision, this board would be **continuously** active on the bus.
  All the more important are the switch, the maximum age and a trial run under supervision before leaving it alone.

### All ways to the room value at a glance

| Way | In the room | At the boiler | Room value reaches the WEM | As of Nov 2026 |
|---|---|---|---|---|
| Weishaupt room unit on a cable | room unit, bus cable | – | yes | Weishaupt's own way; captured on the WEM-RG2 ([10a](VERSUCHE.en.md#10a-wem-rg2-room-unit-on-the-bus-capture-november-2026)) |
| Wireless Weishaupt room unit ([idea 4](#4-wireless-weishaupt-room-unit-can-over-wlan)) | room unit, second CAN board, own supply | CAN board | yes | running as an experiment ([10b](VERSUCHE.en.md#10b-can-tunnel-over-wlan-experiment-branch-can-tunnel)) |
| Evaluating your own sensors and coarse control ([idea 3](#3-room-temperature-from-your-own-sensors)) | wireless room sensor | CAN board (for control) | no | works today |
| Wall panel, stage 1 ([idea 9](#9-wall-panel-for-about-75--goal-rg-compatible)) | touch panel, wireless room sensor | CAN board | no | feasible, not built |
| Wall panel, stage 2 ([idea 9](#9-wall-panel-for-about-75--goal-rg-compatible)) | touch panel, wireless room sensor | CAN board as room unit | yes | not built |
| Phone app and wireless sensors only (idea 10) | wireless room sensor | CAN board as room unit | yes | the goal; not built |
| Wireless radiator valves ([idea 5](#5-individual-room-control-with-wireless-radiator-valves)) | valves with their own sensor | – | no | stand-alone, complements the heating system |
| JSON only, without a board on the CAN bus | – | – | no | no known way for a room unit or room sensor (idea 3) |
