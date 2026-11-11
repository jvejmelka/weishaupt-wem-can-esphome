> 🇩🇪 German original: [VERSUCHE.md](VERSUCHE.md). This is a translation; if in doubt, the German version is authoritative.

# Experiments: WEM-RG2 room unit and CAN tunnel

Captures and experiments on the measured installation (WTC-GW 15-B with WEM system unit) that go beyond the
everyday operation of the firmware: how a WEM-RG2 room unit registers, reads and writes on the bus, and the CAN
tunnel over WLAN that was used to capture it. The tunnel lives in the branch `can-tunnel` and is not part of `main`.
The basics of the bus are described in [PROTOKOLL.en.md](PROTOKOLL.en.md).

Until November 2026 these sections were part of PROTOKOLL.en.md; they keep their numbers from there (10a, 10b)
so that older references stay valid.

- [10a. WEM-RG2 room unit on the bus (capture, November 2026)](#10a-wem-rg2-room-unit-on-the-bus-capture-november-2026)
- [10b. CAN tunnel over WLAN (experiment, branch `can-tunnel`)](#10b-can-tunnel-over-wlan-experiment-branch-can-tunnel)

## 10a. WEM-RG2 room unit on the bus (capture, November 2026)

Captured on a WTC-GW 15-B with a used WEM-RG2 2.1 (software 453.210). At first listening only; on 8 Nov 2026 the RG2
ran over the CAN tunnel (section 10b), and values were deliberately changed on the RG2 and set back to see the write side.
The board itself wrote nothing; it only passed on the RG2's frames.

### Commissioning

The RG2 appeared as node 4 (`0x704`, SDO `0x604`/`0x584`, channel to the WEM `0x6C4`/`0x684`). It needs a DC supply
(manual 15–30 V, base label 12–26 V, at most 0.7 W) and shows “Waiting for commissioning ready” without a bus. Commissioning
worked **only after a cold start of boiler and RG2 together**: the WEM queries `0x1018/1`, opens the assistant after “System has detected new
devices”, and node 4 then appears in `systable.csv` with its modules (`TERMINAL1`, `GROUND2`).

If the RG2 simply keeps running during a WEM restart, ours did not answer this query and lost its node address (LSS query afterwards:
`FF`). This was a single attempt over the CAN tunnel (10b); the cause is not proven.

After commissioning the WEM transfers a **menu bundle of about 1.6 MB** by SDO block to `0x20F4/0`
(`604: C6 F4 20 00 00 10 00 00`, blocks of 4096 bytes) with multi-language text and parameter tables. It took about 25 minutes at up to
16,000 frames per minute. The content belongs to Weishaupt and is not reproduced here.
How the transfer works is described in the next section.

### File transfer between WEM and RG2

The menu bundle does not arrive in one piece. The WEM drives a small file service on the RG2, made of seven objects
(all sub 0), and pushes **eleven files** through it one after the other, each in pieces of 4096 bytes. Captured on
5 Nov 2026 from 23:14:33 to 23:39:49, RG2 directly on the bus of the installation; we sent none of this ourselves.

| Object on the RG2 | Content |
|---|---|
| `0x2559` | command, 4 bytes: `00 00 <step> <service>` |
| `0x2558` | state of it: `FF FF <state> <service>`, idle `00 00 00 00` |
| `0x255A` | file name, 13 bytes (8.3 name) |
| `0x255B` | file size in bytes |
| `0x255E` | when reading and writing the position in the file (0, `0x1000`, `0x2000` …), in the directory the number of the entry |
| `0x255D` | 1 byte, always 0 in this capture – meaning open |
| `0x20F4` | the data, as an SDO block transfer, always 4096 bytes |

The WEM reads every value back right after writing it and polls `0x2558` after each command until the expected
state arrives.

**1. The WEM fetches three files from the RG2** (47, 82 and 9810 bytes, five pieces in total):

```
2559 = 00 00 00 FE                       2558 → 00 00 00 00
2559 = 00 00 00 01                       2558 → FF FF 00 01
255A = <file name>
2559 = 00 00 01 04                       2558 → FF FF 00 04      read 255B → size
per piece:
  255D = 00, 255E = <position>
  2559 = 00 00 02 04                     2558 → FF FF 04 04
  read 20F4 (SDO block upload, 584: C6 F4 20 00 00 10 00 00)
```

**2. The WEM reads the directory of the RG2** – again and again between the files, 26 times:

```
2559 = 00 00 01 02                       2558 → FF FF 01 02
per entry:
  255E = <number>
  2559 = 00 00 02 02                     2558 → FF FF 04 02      read 255A → file name
                                         2558 → FF FF F2 02      no further entry
```

After that the same loop runs with `00 00 03 02` – apparently a second list; we do not know how it differs.

**3. The WEM writes eleven files to the RG2** (82 bytes to 414 kB, 1.57 MB in total, 386 pieces):

```
2559 = 00 00 00 FE, then 00 00 00 01     2558 → FF FF 00 01
255A = <file name>, 255B = <size>
255D = 00, 255E = 0
2559 = 00 00 01 03                       2558 → FF FF 01 03
write 20F4 (SDO block download, 604: C6 F4 20 00 00 10 00 00; the RG2 accepts 127 segments per block)
                                         2558 → FF FF 04 03      … the RG2 is storing
                                         2558 → FF FF 05 03      piece done
per further piece:
  2559 = 00 00 06 03
  255D = 00, 255E = <position>           2558 → FF FF 01 03
  write 20F4                             2558 → FF FF 04 03 … FF FF 05 03
after the last piece                     2558 → 00 00 00 00
```

One piece takes about 3 seconds (2 to 4): a good 1.5 s for the block transfer, the rest is waiting for `FF FF 05 03`. While the
RG2 is storing, the WEM polls `0x2558` about twelve times per second – hence the many frames.

| Command to `0x2559` | Count | Reading (inferred from the order, not proven) |
|---|---|---|
| `00 00 00 FE` | 40 | reset the service; comes before every operation |
| `00 00 00 01` | 41 | open the service |
| `00 00 01 04` / `00 00 02 04` | 3 / 5 | open a file for reading / provide a piece |
| `00 00 01 02` | 26 | open the directory |
| `00 00 02 02` / `00 00 03 02` | 459 / 190 | fetch an entry (first / second list) |
| `00 00 01 03` / `00 00 06 03` | 11 / 375 | open a file for writing / next piece |

So the last byte selects the service (`01` open, `02` directory, `03` write, `04` read, `FE` reset).
`01 AA 55 0C`, with which the WEM restarts the RG2, belongs to the same series
([Restarting the RG2 via CAN](#restarting-the-rg2-via-can-0x2559)).

**Open:** the meaning of `0x255D` and of the rarely seen state `FF FF 02 03` between two pieces; the difference between
the two directory lists; whether the WEM checks the content afterwards. Two of the eleven files had the same size – we
have not checked whether the same file went twice. **Do not replay this:** the service writes files into the device; we
do not know what a wrong command or a wrong file would do.

### Reading through 0x5E00

The RG2 orders values with a segmented SDO write to `0x5E00/4`; the WEM delivers them with a segmented SDO write to `0x5E00/1`:

```
6C4 → WEM   order      01 MI MX OXlo OXhi OS 04 <counter> 00
604 → RG2   delivery   02 MI MX OXlo OXhi OS 00 xx xx <value>
```

The value is 1, 2 or 4 bytes (little endian) or a string. **The same object number means something else per MI**: `0x253E` is time
program 1 on MI 2 and the statistics on MI 9.

| MI | Object/sub | Meaning | Scale | Checked against |
|---|---|---|---|---|
| 1 | `0x2562`/2, 3 | time hour, minute | 1 | RG2 |
| 1 | `0x2563`/2–5 | year, month, day, weekday | 1 | RG2 |
| 1 | `0x26B1`/2 | outside temperature | 0.1 °C | portal |
| 1 | `0x27B9`/2, `0x27BA`/2 | outside temperature minimum, maximum (one comparison) | 0.1 °C | portal |
| 2 | `0x2507`/2 | heating circuit flow | 0.1 °C | portal |
| 2 | `0x2533`/2 | operating mode (2 = time program 1) | 1 | RG2 |
| 2 | `0x2539`, `0x253A`, `0x253B` | room setpoint setback, normal, comfort; sub 2 value, 3 min, 4 max | 0.1 °C | RG2, portal |
| 2 | `0x254B`/2–4 | heating curve slope, min, max | 0.1 | portal |
| 2 | `0x25AD`/2–4 | heating curve parallel shift, min, max | 0.1 K | portal |
| 2 | `0x257B`/2–4 | summer/winter limit, min, max | 0.1 °C | portal |
| 2 | `0x2582`/2 | summer/winter function | 0/1 | portal |
| 2 | `0x2104`/4, 3: `0x2105`/4 | module name (`\HZK0`, `\WW0`) | text | RG2 |
| 3 | `0x2529`/2 | hot water | 0.1 °C | portal |

**Time programs (MI 2):** `0x253E`, `0x253F`, `0x2551` for programs 1, 2, 3. Sub 2 = `0x0F`; one block of 16 subs per day, Monday from sub 4
(sub 3 marker `01`), each further day 16 higher, up to 10 entries. Each entry has 4 bytes: the lower word is `level << 12 | start minute`
with level 1 setback, 2 normal, 3 comfort. The upper word was always `0x3188` here; its meaning is open. Example: `0x2168` = normal from
06:00 (360 min), `0x1537` = setback from 22:15. In operation the RG2 fetches today’s block.

**Statistics (MI 9, `0x253E`):** 4 bytes, unit 0.01 kWh, “energy total”: subs 26–37 days, 38–49 months, 50–54 years, newest first. Shortly
after midnight the days had not moved on yet. Sums match to 0.01 kWh: month = sum of days, year = sum of months plus the running previous day. **Over several
years these heat values were 20–50 % above our gas meter (gross calorific value)**, which is physically too high; use them for comparison only. After a
boiler restart the running day is largely lost (hypothesis from one case).

### PDO 0x184

The RG2 sends this on its own, without a request: on a change, after 1 s at the earliest, otherwise about once a minute.
Bytes 0–1 room temperature (0.1 °C, signed), bytes 2–3 room humidity (0.1 %), bytes 4–5 mostly `0000`, at times changing
(after a start e.g. falling from `0x0352` in steps of 50, otherwise slowly rising and falling); the meaning is open.

The **room sensor correction** in the RG2 menu applies to the transmitted value (−0.5 K → bytes 0–1 lower by 5), and the
humidity is recalculated for the corrected temperature. It does not use the bus, nor do brightness, night mode, limits and
the light ring. Without correction the used RG2 read 2–4 K above a reference sensor in the same room; inside a closed
plastic box together with the board and the voltage converter the offset rose slowly (self-heating suspected). On 10 Nov 2026 it was only 0.8–1.2 K, measured over
an hour (the unit had been switched on that same evening) – a fixed correction value is not established yet. If no room
sensor is assigned in the WEM, it keeps controlling by outdoor temperature only – the room value then does not change the
flow setpoint.

### Writing via 0x5E00

A changed value takes the same path as an order, only with type byte `03`; the WEM acknowledges with `04`:

```
6C4 → WEM   order        03 MI MX OXlo OXhi OS 04 <counter> 00 <value>
604 → RG2   acknowledge  04 MI MX OXlo OXhi OS 00 00 00
```

Type bytes: `01` read, `02` delivery, `03` write, `04` write acknowledged. The value is little endian, 1 or 2 bytes
depending on the object (order 10 or 11 bytes). Example room setpoint normal to 20.5 °C:
`03 02 00 3A 25 02 04 00 00 CD 00` → acknowledge `04 02 00 3A 25 02 00 00 00`.

One confirmation on the RG2 gives **one** order with the final value; intermediate steps while turning are not written.
Party and holiday write the whole set one after another on confirmation; the counter counts down to `00`, and the last
order is the function itself.

| MI | Object/sub | Meaning | Values |
|---|---|---|---|
| 2 | `0x2539`/2, `0x253A`/2, `0x253B`/2 | Room setpoint reduced, normal, comfort | 0.1 °C; nested limits (sub 3/4 = neighbouring value), reduced from −100.0, comfort up to 300.0 |
| 2 | `0x2533`/2 | Heating circuit operating mode | 1 byte; 2 = time program 1, 3 = time program 2 |
| 2 | `0x254B`/2 | Heating curve slope | 0.1; 100–250 (depends on the heating circuit type, see below) |
| 2 | `0x25AD`/2 | Heating curve parallel shift | 0.1 K; −500…500. The RG2 steps by 0.5 K, the display rounds to whole K (5 → "1 K") |
| 2 | `0x2587`/2 | Party/heating pause function | 0 off, 1 on |
| 2 | `0x2589`/2 | Party room setpoint level | 1 reduced, 3 comfort |
| 2 | `0x2593`/2, /3 | Party start hour, minute | 1 |
| 2 | `0x2588`/2, /3 | Party end hour, minute | 1 |
| 2 | `0x258A`/2 | Holiday function | 0 off, 1 on |
| 2 | `0x2586`/2 | Holiday room setpoint | 0 frost, 1 reduced |
| 2 | `0x2584`/2–4, `0x2585`/2–4 | Holiday start, end | year − 2000, month, day |
| 3 | `0x2539`/2 | DHW setpoint normal | 0.1 °C |
| 3 | `0x2520`/2 | DHW operating mode | 1 on, 2 off |
| 3 | `0x2549`/2 | DHW push | 1 = trigger; whether the WEM resets the value is open |

Each value was changed on the RG2, checked by the WEM's acknowledgement on the bus (operating modes also on the
firmware's display) and set back afterwards. `0x2539` is the reduced room setpoint in MI 2 and the DHW setpoint in MI 3.
The party menu suggests the rounded-down current time as start and three hours later as end; the WEM may have
something else stored.

**The slope limits depend on the heating circuit type** – pointed out by
[ChristophKomenda](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/issues/8#issuecomment-6763314919): according to the Weishaupt
documentation, “Universal” allows 1.5–40.0, “Radiator 60” 8.0–20.0 and “Radiator 70” 10.0–25.0. The limits read here,
100–250 (`0x254B`/3 and /4), match “Radiator 70”; which type is set on the measured WEM has not been looked up yet. On his
WTC-GW 15-B (heating circuit type “Universal”, slope 8.5) he read both objects via the JSON interface and
[compared them with the controller display](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/issues/8#issuecomment-6708250969):
`02 00 254B 02` (slope) and `02 00 25AD 02` (parallel shift, changed back and forth three times). The objects are thus
confirmed on two installations; the limits apply per setting. On 11 Nov 2026 he also read the limits themselves via JSON
([comment](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/issues/8#issuecomment-6781859717)):
`02 00 254B 03` = 25 (2.5) and `02 00 254B 04` = 400 (40.0) with "Universal". Sub 3 and 4 are thus confirmed as min and
max on two installations. The minimum of 2.5 differs from the 1.5 in the Weishaupt document; the reason is open.

**DHW program (MI 3, `0x2580`):** same layout as the heating time programs, the upper word was `0x3288` here. The flags
before each day block (sub 3, 19, 35, …) were `01` for the Monday–Friday group and `02` for Saturday/Sunday; the RG2 read
only the Monday (sub 4–13) and the Saturday block (sub 84–93).

### Device list, failure, restart

- Opening the **device list** on the boiler display: each time the WEM writes `604: 23 F0 20 01 01 00 00 04` (`0x20F0`/1 =
  `0x04000001`), the RG2 acknowledges and its light ring flashes green (interpretation: identify the device). The WEM also
  sends `011: 01 F0 20 00 00 00 00 04`.
- **RG2 start:** it sends `011: 01 F0 20 00 00 00 00 1C` and goes `704:00` → `704:7F` → `704:05` in about 20 s. With the
  WEM running it kept node 4 without intervention – directly on the bus and over the CAN tunnel.
- **RG2 failure:** 30 s after the last heartbeat the WEM reports `081: FF FF 01 02 05 DD 01 00` (`0x05DD` = **warning 1501**, connection to RG2 no. 1 lost, see [PROTOKOLL.en.md, section 10c](PROTOKOLL.en.md#10c-fault-messages-on-the-bus-and-error-codes)) and
  clears it as soon as the RG2 is back. The device list then shows it under "inactive devices".

### Assignment as room sensor and measured room temperature in the WEM

On 10 Nov 2026 the RG2 was assigned to heating circuit 1 as room sensor for eleven minutes (on the system device:
expert level → commissioning → assignments) and then removed again. On the bus:

- The WEM writes the assignment into the RG2 (node 4, `604`): first `0x2604`/1…5, `0x2605`/1…5 and `0x2606`/0 to `FF`,
  then the new values. Seen: `0x2604`/1 = `00` and `0x2605`/1 = `00` (going by the display: operating access to heating
  circuit 1 and DHW 1) and `0x2606`/0 = `01` with the room sensor on heating circuit 1, `FF` without.
- After that the WEM restarts the RG2 (next section) – when assigning as well as when removing.
- **Measured room temperature in the WEM:** node 1 `0x298B`/2, via JSON `02 00 258B 02` (+0x400 again), 2 bytes signed,
  0.1 °C. Without an assignment it holds `0x8000` ("no value"), with the assignment `0x00DC` = 22.0 °C – the same value
  as in PDO `0x184` at that moment. `0x298C`/2 and `0x298D`/2 exist too; they stayed `0x8000` (probably further room
  units). Read via CAN here. **Confirmed via JSON by PiLLoX** on a WTC-GB 25-B with built-in WEM-SG and RG2:
  response `020200258B02000200B0` = 17.6 °C, exactly what the RG2 displayed
  ([comment of 10 Nov 2026](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/issues/4#issuecomment-6781466298)).
- **Room humidity in the WEM: `02 00 25B6 02`** (0.1 %). Confirmed by PiLLoX on a WTC-GB 25-B with built-in WEM-SG
  and an assigned RG2: response `02020025B602000201F5` = 50.1 %, RG2 display 50 %
  ([comment of 11 Nov 2026](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/issues/4#issuecomment-6793283297)) – one
  observation. Not read on our system (no room unit assigned); on CAN this would be node 1 `0x29B6`/2. On the bus
  the humidity is also in the RG2's PDO `0x184` (bytes 2–3).
- The assignment had no effect on the heating: flow setpoint and heat demand carried on unchanged. The control variant
  stayed weather-compensated; the display did not ask about it.

### Restarting the RG2 via CAN (`0x2559`)

After a changed assignment the WEM restarts the RG2 – not via NMT, but with two write commands to `0x2559`/0 of the
RG2. Captured twice (10 Nov 2026), identical both times:

```
604: 23 59 25 00 00 00 00 01      write 0x2559/0                  584: 60 59 25 00 …   confirmed
604: A4 58 25 00 40 40 00 00      read 0x2558/0                   584: 43 58 25 00 00 00 00 00
604: A4 58 25 00 40 40 00 00      read 0x2558/0 again             584: 43 58 25 00 FF FF 00 01
604: 23 59 25 00 01 AA 55 0C      write 0x2559/0 (0.7 s after     584: 60 59 25 00 …   confirmed
                                  the first)
          10 s of silence
704: 00   and   084: 00 00 00 00 00 00 00 00                      boot-up of the RG2
```

**Reproduced by us** (bench setup: board B at the RG2, tunnel on, [free CAN writing](PROTOKOLL.en.md#writing-via-can-from-v35-default-since-v42)
with node `04` released): only the two write commands, 1.1 s apart, without reading `0x2558` in between. The RG2
confirmed both and restarted in the same way. Input in the field “CAN-Schreibbefehl”:

```
04 2559 00 = 00 00 00 01
04 2559 00 = 01 AA 55 0C
```

What follows, compared with a cold start (power off and on again) the same evening. Times from the boot-up `704:00`;
bus from the capture, display from a camera at two frames per second:

| Step | Restart via CAN | Cold start |
|---|---|---|
| second write command confirmed | −10.0 s | – |
| light ring yellowish at the bottom | from about −7.5 s | −6.8 s to +0.7 s |
| `704:00` and `084:00…` | 0 | 0 |
| logo on the display | +0.9 s | +1.2 s |
| RG2 reads from the WEM (`6C4`): `0x5F01`/0…3, then `0x5F03`/0…D | from +3.1 s | from +3.1 s |
| `704:7F` | +9.9 s | +9.9 s |
| first PDO `0x184` | +16.0 s | +11.0 s |
| `704:05` | +19.8 s | +19.9 s |
| display dark once more, light ring yellowish | +62 s | +62 s |
| main screen | +77 s | about +83 s |

From the boot-up on, the restart matches the cold start; the unit is back on the bus after 20 s, the display takes a
good minute longer. On the two restarts triggered by the WEM the PDO was back as early as 9 s after the boot-up.

**Open points and cautions:**
- The meaning of the bytes is not known. On the RG2, `0x2559` is a command object the WEM also uses to exchange [files](#file-transfer-between-wem-and-rg2)
  with the unit (with other values then); `0x2558` returns the state. Guess: `0C` = restart, `AA 55` = guard pattern
  against triggering it by accident.
- Not checked: whether the first write command is needed, and other room units or software versions.
- **Not tried on the WEM or the boiler.** The WEM has both objects: node 1 answers a read of `0x2559`/0 and of
  `0x2558`/0 with `00 00 00 00` each (10 Nov 2026). What a write would trigger there we do not know; on the boiler we
  have not even read them. Do not send it to other devices on a guess. Do not confuse: `0x2959`/2 on the WEM (JSON `02 00 2559 02`) is the flow setpoint
  of the heating circuit and has nothing to do with this.
- It is meant for bench experiments – capturing the start of a room unit without pulling the plug.

## 10b. CAN tunnel over WLAN (experiment, branch `can-tunnel`)

> **Not to be confused with the project itself:** the project is the "ESP bridge" – one board connecting the CAN bus
> to the home network. The **CAN tunnel** is different: two boards extend the CAN bus itself over WLAN.

Two boards of this project join two CAN buses over WLAN: board A sits at the boiler on the real bus, board B sits on the
desk with the room unit and forms a small bus of its own with it. This lets you operate and capture an RG2 without running
a cable to the boiler room. The RG2 needs its own supply there (DC 15–30 V, we used a step-up converter set to 24 V).

> **Warning – read before switching it on.** The tunnel **does not filter**: everything the device at the other end
> sends reaches the heating bus, including write orders. The UDP packets are unencrypted and protected only by the
> sender address. So: for experiments only, only inside your own home network, only while you are watching – and cut the
> power of board B when nobody is there. The tunnel is not part of `main` or of the ready-made firmware files.

**Why CAN and not JSON.** To the WEM, a room unit or room sensor is a participant of its own on the CAN bus: the WEM
admits it, it sends a heartbeat and delivers the room value on its own initiative (section 10a). The WEM's JSON
interface only answers single requests for its own values – no device can register through it or send anything
continuously, and the WEM does not tolerate regular requests. We know of no JSON way to give the WEM a room value.
Anyone who wants a room unit or room sensor at the WEM – a real one, one behind the tunnel, or later an emulated one –
therefore needs a board **on the CAN bus** (WeAct CAN485; for Ethernet the
[Olimex variant by cg089](OLIMEX-ESP32-EVB.en.md)). A board without a CAN cable is not enough for that.

**What you need.**
- Two boards with the firmware from the branch `can-tunnel`, both in the same WLAN. The tunnel has only been tried with
  two WeAct CAN485 boards; the package requires WLAN (package `netz-wlan`).
- A **fixed address** for both boards (reservation in the router or package `feste-ip`): each board only accepts
  packets from the configured address of the other.
- At the room unit: its own supply (see above) and a bus termination on the small bus – switch "120 Ω CAN" on board B
  **on** (end of a short bus of its own), on board A **off** as always ([INSTALL.en.md](INSTALL.en.md), German).
- Optionally a computer in the home network that takes the capture.

**Setting it up** – for both boards, each with the address of the other:

1. Put the addresses into `secrets.yaml` (template: `secrets.yaml.example`). They live only there, not in the main file:
   ```yaml
   tunnel_ziel: "192.0.2.54"   # IP of the OTHER board (example address)
   tunnel_log: "192.0.2.30"    # only for the capture: IP of the computer
   ```
2. In `weishaupt-wem-can.yaml` uncomment the prepared lines and adjust them:
   ```yaml
   substitutions:
     tunnel_ziel: !secret tunnel_ziel
     tunnel_log: !secret tunnel_log       # omit the line = no capture
     tunnel_name: "A"                     # "B" on the other board
     tunnel_nur_bei_bus: "true"           # "false" on the board at the room unit

   packages:
     can_tunnel: !include pakete/can-tunnel.yaml
   ```
3. Build and flash as described in [INSTALL.en.md](INSTALL.en.md).
4. On **both** boards turn on the switch "CAN-Tunnel (Experten)" in the web interface. Turning it off on either board
   cuts the connection.

| Setting | Default | Meaning |
|---|---|---|
| `tunnel_ziel` | – (required) | IPv4 address of the other board. If it is missing or not an address, the build stops with a message. |
| `tunnel_log` | `""` | address of a computer for the capture; empty = no capture |
| `tunnel_name` | `"A"` | tag of this board in the capture and in the log |
| `tunnel_nur_bei_bus` | `"true"` | `"true"`: only send to the bus if something came from the bus in the last 3 s (board at the boiler). `"false"`: always send (board at the room unit). |
| `tunnel_selbst_ein` | `"false"` | `"false"`: the tunnel is off after every restart. `"true"`: it turns itself on once after every restart as soon as WLAN is up. |

The settings are fixed in the firmware; at runtime only the switch can be operated. The defaults and their description
are in the header of [`pakete/can-tunnel.yaml`](https://github.com/jvejmelka/weishaupt-wem-can-esphome/blob/can-tunnel/pakete/can-tunnel.yaml), the UDP ports in
[`can_tunnel.h`](https://github.com/jvejmelka/weishaupt-wem-can-esphome/blob/can-tunnel/components/weishaupt_can/can_tunnel.h) (if you change them, rebuild both boards).

**The two boards side by side** – build both from the same firmware state; only these settings differ:

| | Board A (at the boiler) | Board B (at the room unit) |
|---|---|---|
| `tunnel_ziel` | address of board B | address of board A |
| `tunnel_name` | `"A"` | `"B"` |
| `tunnel_nur_bei_bus` | `"true"` | `"false"` |
| `tunnel_selbst_ein` | `"false"` – switch on by hand | `"true"` is acceptable here: board B is only powered for the experiment |
| switch “120 Ω CAN” on the board | off | on |
| device name (`geraet`) | as in everyday use | **a different name**, or leave out the `mqtt` package – otherwise both boards write to the same MQTT topics |
| free CAN writing | off, permitted nodes `01` | off; switch on only for an experiment at the room unit and release its node (e.g. `04`), then back |

Board A remains the normal bridge to the heating with all packages. Board B does not need rules, extra displays or
MQTT; they do no harm either, as long as the device name is a different one.

**How it works.** Each board sends every frame it receives on its own bus via UDP (port 47011) to the configured peer
and puts what arrives from there on its own bus. Only packets from the peer's address are accepted. A packet is
`'W' 'B' seq id_lo id_hi(+0x80 = RTR) len d0…d7`; the board counts lost packets from the sequence number. The round
trip order → delivery was 25–35 ms.

**Protection.** Board A only sends if something came from the bus in the last 3 s – with the boiler switched
off the error counter otherwise rose to bus-off. Board B does not use this lock, so that the WEM's requests reach the
silent RG2. Both try a recovery every 10 s if the CAN controller is in bus-off. Without a valid peer address the channel
stays closed.

**Switch** "CAN-Tunnel (Experten)": it is off after a restart. With `tunnel_selbst_ein: "true"` it turns itself on once
after every restart as soon as WLAN is up; switched off by hand it then stays off until the next restart. That is
convenient for long captures, but it also means the board writes to the bus again on its own after every power cut.

**Capture.** If `tunnel_log` is set, each board sends its frames as text lines via UDP (port 47012) to that computer,
plus a status line every 10 s (from/to bus, lost, errors, locked, bus-off). Receive it with
[`werkzeuge/tunnel_mitschnitt.py`](https://github.com/jvejmelka/weishaupt-wem-can-esphome/blob/can-tunnel/werkzeuge/tunnel_mitschnitt.py) (Python 3 only):

```
python3 werkzeuge/tunnel_mitschnitt.py --datei tunnel.txt

14:03:08.009 A 1234 R 184:FA00C201      A received the frame on its bus and forwarded it
14:03:08.021 B 1250 T 184:FA00C201      B got it and sent it on its bus (X = sending refused)
14:05:33.593 B 9876 E 584:6059250000000000   reply to a write command of B's own – stays at B
```

The number after the tag is milliseconds since the board started. Captures can contain serial numbers and device
identifiers – do not publish them raw.

**Writing yourself while the tunnel runs.** [Free CAN writing](PROTOKOLL.en.md#writing-via-can-from-v35-default-since-v42)
from a board also works while the tunnel is running. Two things keep it from disturbing the traffic between WEM and
room unit: the board only sends once the SDO channel of the target node has been quiet for 200 ms – including anything
it has just put on the bus from the tunnel itself – and it does not forward the reply to its own command to the other
end (`E` instead of `R` in the capture): nobody asked there.

**What else the package changes:** a larger receive buffer for UDP (64 instead of the ESP-IDF default 6), WLAN power
saving off, longer CAN queues (receive 64, send 32) and stopping the CAN controller during a firmware update. Without
the package the firmware behaves as in `main`.

**What worked:** commissioning the RG2, reading and writing all values from section 10a, restarting the RG2 and
restarting board B, each without intervention; restarting the RG2 via CAN from board B with the tunnel running
([section 10a](#restarting-the-rg2-via-can-0x2559)).

**Known problems:**
- Cold start of the boiler with the tunnel running (5 Nov 2026, older firmware): the RG2 kept running, did not answer
  `0x1018/1` and lost its node address (LSS: `FF`). Directly on the bus, with a joint cold start, it worked. Not yet
  repeated with the current state.
- Firmware update: on the board at the boiler `esphome upload` (port 3232) failed several times with "receiving chunk
  result: Unknown error"; the log showed `canbus took a long time … (3–5 s)`. Uploading in the browser (`/update`)
  worked every time. Stopping the CAN controller during the update (in the branch) did not remove the warning; the cause
  is open.
