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
plastic box together with the board and the voltage converter the offset rose slowly (self-heating suspected). If no room
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
| 2 | `0x254B`/2 | Heating curve slope | 0.1; 100–250 |
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

## 10b. CAN tunnel over WLAN (experiment, branch `can-tunnel`)

> **Not to be confused with the project itself:** the project is the "ESP bridge" – one board connecting the CAN bus
> to the home network. The **CAN tunnel** is different: two boards extend the CAN bus itself over WLAN.

Two boards of this project join two CAN buses over WLAN: board A sits at the boiler on the real bus, board B sits on the
desk with the room unit and forms a small bus of its own with it. This lets you operate and capture an RG2 without running
a cable to the boiler room. The RG2 needs its own supply there (DC 15–30 V, we used a step-up converter set to 24 V).

**How it works.** Each board sends every frame it receives on its own bus via UDP (port 47011) to the configured peer
and puts what arrives from there on its own bus. Only packets from the peer's address are accepted. A packet is
`'W' 'B' seq id_lo id_hi(+0x80 = RTR) len d0…d7`; the board counts lost packets from the sequence number. Every 10 s
there is a status line (from/to bus, lost, errors, bus-off); optionally a text capture goes to a computer via UDP
(port 47012). The round trip order → delivery was 25–35 ms.

**Protection.** The tunnel does not filter: everything the RG2 sends reaches the WEM, including write orders. It is
meant for experiments only. Board A only sends if something came from the bus in the last 3 s – with the boiler switched
off the error counter otherwise rose to bus-off. Board B does not use this lock, so that the WEM's requests reach the
silent RG2. Both try a recovery every 10 s if the CAN controller is in bus-off.

**Switch** "CAN-Tunnel (Experten)": it turns itself on once after every restart as soon as WLAN is up. Switched off by
hand it stays off until the next restart.

**Including it** (addresses are placeholders):

```yaml
substitutions:
  tunnel_ziel: "<peer-ip>"
  tunnel_log: ""              # or <ip-of-a-computer> for the capture
  tunnel_name: "B1"           # tag in the capture
  tunnel_nur_bei_bus: "true"  # board at the boiler: true, board at the room unit: false

packages:
  can_tunnel: !include pakete/can-tunnel.yaml
```

**What worked:** commissioning the RG2, reading and writing all values from section 10a, restarting the RG2 and
restarting board B, each without intervention.

**Known problems:**
- Cold start of the boiler with the tunnel running (5 Nov 2026, older firmware): the RG2 kept running, did not answer
  `0x1018/1` and lost its node address (LSS: `FF`). Directly on the bus, with a joint cold start, it worked. Not yet
  repeated with the current state.
- Firmware update: on the board at the boiler `esphome upload` (port 3232) failed several times with "receiving chunk
  result: Unknown error"; the log showed `canbus took a long time … (3–5 s)`. Uploading in the browser (`/update`)
  worked every time. Stopping the CAN controller during the update (in the branch) did not remove the warning; the cause
  is open.
