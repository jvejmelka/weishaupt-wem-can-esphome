> 🇩🇪 German original: [ANLAGE.md](ANLAGE.md). This is a translation; if in doubt, the German version is authoritative.

# Example installation – how it runs at our place

The firmware was developed and measured on exactly one installation. So that you can compare yours,
this page describes how it is set up – and what you therefore see on the bus.

## Layout

| Part | Device |
|---|---|
| Heat generator | **Weishaupt Thermo Condens WTC-GW 15-B** (gas condensing boiler) with **WEM system unit** |
| DHW | **Weishaupt WWP T 300 WA** – domestic hot water heat pump with a 300 l storage tank and built-in solar heat exchanger (1.5 m²) |
| Gas connection to the storage tank | The **solar heat exchanger is used for the WTC**: the pipes of the gas boiler are connected to the solar connections of the tank. This way heat pump and gas charge the same tank |
| DHW module in the WEM | WW0 in the system unit (MI 03), **no** separate EM-WW |
| Electricity | PV system **19.4 kWp** with home power station **E3/DC S10 E** |
| Battery | in the E3/DC, **13 kWh** (2 × LG battery module). During the day it is charged from PV, in the evening and at night it supplies the house – including the heat pump |
| Wallbox | **E3/DC Wallbox easy connect**, 11 kW, controlled by the E3/DC – currently without an electric car, i.e. without consumption |
| Heat pump control | **SG Ready**, directly from the E3/DC. Without surplus the E3/DC signals operating state 2 (normal operation): the heat pump controls to its own DHW setpoint – hence the runs at night in the measurement below. At night its electricity mostly comes from the **house battery**, i.e. from stored PV electricity. With PV surplus operating state 3: the heat pump raises the setpoint – depending on the decision of the E3/DC step by step **up to 65 °C**. Described in the [manual of the WWP T 300 WA (Weishaupt, PDF)](https://www.weishaupt.de/uploads/tx_weishaupt_documents/documents/83297701.pdf), section Smart Grid function, and in the [SG Ready documentation by E3/DC (PDF)](https://community.viessmann.de/viessmann/attachments/viessmann/customers-heatpump-hybrid/63997/1/SG-Ready-Dokumentation_V1.70_2022-02-08.pdf) |
| Heat pump power metering | **Shelly Plug M Gen3** upstream of the heat pump, power via MQTT |
| Listener on the bus | **WeAct CAN485 DevBoard V1** with this firmware |

**No lockout via input H2 of the WTC.** Gas boiler and heat pump know nothing about each other.
It is not needed either – the division of work follows from setpoints and sensor positions, see below
[Why no H2 lockout is needed](#why-no-h2-lockout-is-needed).

## Setpoints

| Who | Setting | Value |
|---|---|---|
| Heat pump, normal operation (SG Ready state 2) | DHW setpoint, sensor at the top of the tank | **50 °C** |
| Heat pump, raised operation (state 3) | setpoint + SG increase (factory default 5 K, adjustable 0–20 K) | 55 °C |
| Heat pump, forced operation (state 4, by the E3/DC depending on surplus) | | **up to 65 °C** |
| Gas (WEM), DHW setpoint normal and setback | node 1 `0x2A39/2`, `0x2A38/2`, sensor at the **bottom** of the tank | **35 °C** |
| Gas, excess temperature while charging | boiler flow setpoint = DHW setpoint + 15 K | 50 °C |
| Gas, DHW operating mode **Off** | current DHW setpoint `0x2A2C/2` (frost protection) | 8 °C |

So the gas boiler is deliberately only a **preheater** (Weishaupt recommends 30–35 °C for this): it keeps
the lower part of the tank at 35 °C, the heat pump does the rest.

## What you therefore see on the bus

The DHW sensor of the WTC sits in the same tank, namely at the **bottom**. The tank temperature
from PDO `0x241` (see [PROTOKOLL.en.md](PROTOKOLL.en.md)) therefore shows the charge state of the lower
part of the tank, no matter who charged it. It is warmer at the top – the heat pump controls to its own
sensor at the top.

This becomes obvious when the DHW operating mode at the WEM is **Off** (node 1 `0x2A20/2` = 2)
and the tank is warm nevertheless:

| Display | Value |
|---|---|
| DHW operating mode (requested and actual) | Off |
| Tank temperature | 42.5 °C |
| Burner | Off |

Here the heat pump did the heating; the gas burner was not involved.

## Measured: one afternoon and one night

Tank temperature from the bus, heat pump power from the Shelly, burner phase from the bus
(Friday 16:00 to Saturday 08:00, late summer, heating circuit in Standby):

| Time | Who heats | Power | Tank |
|---|---|---|---|
| 16:49–18:04 | Heat pump | ~530 W | 28.8 → 37.2 °C |
| 18:15, 19:30 | – (draw-offs) | – | 37.2 → 32.2 °C, 30.3 → 23.6 °C |
| 20:16–20:42 | **Gas** (DHW operating mode On) | – | 23.8 → 37.0 °C in 26 min |
| 21:47–22:15 | Heat pump | ~530 W | 37.0 → 38.9 °C |
| 04:31–05:08 | Heat pump | ~530 W | 38.6 → 40.8 °C |
| 07:12–07:46 | Heat pump | ~530 W | 40.5 → 42.5 °C |

What follows from this:

- **The tank holds the heat very well:** at night without draw-off only about **0.05–0.1 K/h** loss.
  The big drops come from drawing off hot water, not from standing.
- **The heat pump needs time:** about 7 K per hour at roughly 0.5 kW. Gas manages 13 K in
  26 minutes.
- The temperature sensor occasionally reports an **error value (−3276.8 °C)**. Anyone who builds an
  automation on it must discard such values.

## Why no H2 lockout is needed

The usual solution to keep gas from working against the heat pump is a lockout of
DHW operation via input H2 of the WTC (e.g. by a contactor controlled by the energy manager).
Here it works without one, because **setpoints and sensor positions already separate the tasks:**

- The heat pump controls to its sensor at the **top** and keeps 50 °C there, more with PV surplus.
- Gas controls to its sensor at the **bottom** and only starts when the temperature there drops below 35 °C.
- As long as the heat pump recharges, it never gets that cold at the bottom – gas stays off.
- It only gets cold at the bottom when **a lot of water is drawn off** or the **heat pump does not recharge**.
  That is exactly when gas is supposed to step in.

The measurement above shows this: over the whole night and the morning gas ran **once** for
DHW – after two large draw-offs, when the bottom of the tank had dropped to 23.6 °C.

### What the board can do in addition (not implemented)

If you want to give the heat pump even more priority, you can lock the DHW operating mode at the WEM during
the day with `cmd/warmwasser Aus` and release it again only in the evening or below a threshold – the board
then becomes an H2 replacement without wiring. With these settings it is not needed.

The same rules apply as everywhere: at most one switch command per minute, preferably far
fewer – twice a day is harmless for the WEM, twenty times is not
(see [When the WEM locks up](PROTOKOLL.en.md#8-when-the-wem-locks-up)). And discard error values of the
temperature sensor before a threshold reacts to them.
