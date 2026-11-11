> 🇩🇪 German original: [OLIMEX-ESP32-EVB.md](OLIMEX-ESP32-EVB.md). This is a translation; if in doubt, the German version is authoritative.

# Olimex ESP32-EVB (LAN) – variant for the Weishaupt WTC-GW

This variant comes from **cg089** ([fork cg089/weishaupt-wem-can-esphome](https://github.com/cg089/weishaupt-wem-can-esphome),
branch `olimex-esp32-evb`, 31 Oct 2026): a ready-made configuration for the **Olimex ESP32-EVB** (Ethernet instead of Wi-Fi,
CAN transceiver on board) and functions that a Home Assistant integration offers via the WEM JSON interface –
here **directly via the CAN bus**. In the fork everything is in one file; here it is built into the packages of the project
so that both boards are built from the same code base.

| File | Content |
|---|---|
| [`olimex-weishaupt.yaml`](olimex-weishaupt.yaml) | main YAML for the Olimex ESP32-EVB: board, Ethernet, selection of the packages, Dallas sensors |
| [`pakete/netz-lan.yaml`](pakete/netz-lan.yaml) | network displays for Ethernet (counterpart to `netz-wlan.yaml`) |
| [`pakete/kraiz-per-can.yaml`](pakete/kraiz-per-can.yaml) | more values via CAN, DHW push, error block of the WEM – **generated**, do not edit by hand |
| [`pakete/kraiz-per-can.h`](pakete/kraiz-per-can.h), [`pakete/kraiz-per-can-texte.h`](pakete/kraiz-per-can-texte.h) | C++ part of the package: request schedule and decoder; value texts (generated) |
| [`pakete/dallas.yaml`](pakete/dallas.yaml) | 1-Wire bus for DS18B20 sensors |
| [`secrets-olimex.yaml.example`](secrets-olimex.yaml.example) | template for `secrets.yaml` |
| [`werkzeuge/kraiz-per-can/`](werkzeuge/kraiz-per-can/) | scripts (Node.js): convert kraiz objects into CAN addresses, generate the package |

The packages `kraiz-per-can` and `dallas` also run on the WeAct CAN485 (uncomment them in `weishaupt-wem-can.yaml`).

## Differences from the WeAct board

- **Ethernet** (LAN8720: MDC GPIO23, MDIO GPIO18, external clock on GPIO0, PHY power GPIO12) instead of Wi-Fi. Package
  `netz-lan` instead of `netz-wlan`; Wi-Fi change, SSID and signal are omitted.
- **CAN pins** of the Olimex ESP32-EVB: TX **GPIO5**, RX **GPIO35** (according to the [schematic Rev I](https://github.com/OLIMEX/ESP32-EVB/files/11994875/ESP32-EVB_Rev_I_Schematic.pdf)),
  set via `can_tx_pin` / `can_rx_pin`.
- **No SD card reader and no LittleFS** (`sd_karte: false`, `dateisystem: false`; the board has only 4 MB of flash).
- Included: `kessel`, `netz-lan`, `warmwasser`, `wem-schalten` (switching via CAN is on by default),
  `warmwasser-schalten`, `kraiz-per-can`, `dallas`. Not included: `mqtt`, `app`, `verdacht`, `zusatz`, `json-lesen`.
- Connection to Home Assistant via the ESPHome API (no MQTT).

## Additions

### All values of the kraiz integration via CAN (`kraiz-per-can`)
On the CAN bus the JSON objects of the integration [kraiz/hassio-weishaupt](https://github.com/kraiz/hassio-weishaupt) are
shifted by a fixed amount per module (see [PROTOKOLL.en.md](PROTOKOLL.en.md)):

| JSON module | CAN node | Offset |
|---|---|---|
| MI 01 (SYSTEM0) | 1 | +0x100 |
| MI 02 (heating circuit HZK0) | 1 | +0x400 |
| MI 03 (DHW WW0) | 1 | +0x500 |
| MI 07 (boiler WE0) | 2 | identical |
| MI 09 / MX 01 (boiler device module) | 2 | +0x100 |

This way 29 values are read that the package `kessel` does not provide (room/flow setpoints comfort/normal/setback,
heating circuit and DHW status, WTC operating phase, cascade and others). Modules that the installation does not have (heating circuit 2/3, solar)
are not included. The requests only run while the switch „Eigene CAN-Anfragen“ ("own CAN requests") is on, at most one request
every 3 s, each object at most every 1 to 10 minutes; objects rejected by the node are not requested again.

The package is generated. To regenerate it (e.g. after a change to the selection in `kraiz_erzeugen.js`):

```
git clone https://github.com/kraiz/hassio-weishaupt
node werkzeuge/kraiz-per-can/kraiz_erzeugen.js hassio-weishaupt/custom_components/weishaupt_wtc/sensors.py .
```

### DHW push („Warmwasser-Push“)
A button writes `1` to `0x2A49/2` of the WEM (JSON `MI 03 / 0x2549/2` as in kraiz). Minimum interval and verification on the bus
from the package `wem-schalten` apply here as well; the switch „Freies CAN-Schreiben“ ("free CAN writing") does not apply to this button
(fixed target, fixed value). Tried on one installation only. From v42 every push counts as a write towards the
hourly limit ([PROTOKOLL.en.md, Write protection](PROTOKOLL.en.md#write-protection-write-as-little-as-possible-from-v42)).

### Error block and plain-text error message
The 8-byte error block of the WEM (`0x273F`) is read every 2 minutes: status, error number, module and a plain text.
The emergency telegrams (`0x081`/`0x082`) are already evaluated by the package `kessel` („Meldung WEM“ ("WEM message"), „Meldung Kessel“ ("boiler message")).

The plain text comes from [`fehlercodes.yaml`](fehlercodes.yaml) – the project's **own short texts**. The texts from
chapter 10 of the Weishaupt manual, which the fork contains typed out, are **not** adopted here: the rights to them
belong to Weishaupt. If you want to see the original texts, generate them from your own manual and then build yourself:

```
python3 werkzeuge/fehlertexte_aus_handbuch.py --header 83307201.pdf
esphome run olimex-weishaupt.yaml
```

This writes `components/weishaupt_can/fehlertexte_original.h` (listed in `.gitignore`, does not belong in the repo); the
firmware then shows the original texts instead of its own. The other way – uploading texts on the settings page –
needs the file system and does not work on this board.

**Unverified:** whether the number in the error block corresponds to the number in the manual and whether the type bit separates
"fault" from "warning"; a real fault has not occurred yet.

### Dallas temperature sensors (DS18B20)
Five sensors in parallel on **GPIO16** (UEXT pin 5, I²C SCL): ambient, DHW storage tank top, flow, return of
heating circuit 1 and 2. **The addresses in `olimex-weishaupt.yaml` are the author's – replace them!** A bench test with sensors
without an address (`index: 0 …`) writes the addresses to the log at start-up; assign them by warming each sensor. Recommended: external
4.7 kΩ pull-up to 3.3 V.

## Installation

1. Clone this repository (or copy `olimex-weishaupt.yaml` and the folders `pakete/` and `components/` into the
   ESPHome directory).
2. Copy `secrets-olimex.yaml.example` to `secrets.yaml` and fill it in (`ota_password`, `web_password`,
   `wem_ip`, `wem_user`, `wem_pass`). In the fork `web_password` was still called `web_server_password`.
3. Adjust the Dallas addresses in `olimex-weishaupt.yaml` or remove the section `sensor:` and the package `dallas`.
4. `esphome run olimex-weishaupt.yaml` (first time via USB, then OTA).
5. In the web interface or in Home Assistant switch on „Eigene CAN-Anfragen“ ("own CAN requests") once passive listening runs stably.

## Comparison with the fork

The version here is meant to produce the same result as `olimex-weishaupt.yaml` in the fork. What is compared is therefore not the YAML text
(one file there, packages here) but what ESPHome makes of it – the output of `esphome config`
(fork `211bd9b` against this repository, ESPHome 2026.9.1):

- **All 124 entities of the fork except one come out unchanged here** – 123 are identical in every field, including the
  code part (lambdas): name, ID, unit, group, request. So names and IDs in Home Assistant stay the same.
- Board, Ethernet, CAN bus pins, API, OTA, logger, 1-Wire, time, intervals and global variables are identical.

Deliberately different:

| In the fork | Here | Reason |
|---|---|---|
| text sensor „Letzte Emergency-Meldung“ ("last emergency message") | „Meldung WEM“ and „Meldung Kessel“ | the telegrams `0x081`/`0x082` are evaluated by the package `kessel` |
| plain-text error message from `weishaupt-helfer.h` (manual texts) | from `fehlercodes.yaml` (own short texts) | rights to the manual texts, see above |
| secret `web_server_password` | `web_password` | as in the WeAct variant |
| `weishaupt-helfer.h` | `pakete/kraiz-per-can.h`, `pakete/kraiz-per-can-texte.h` | part of the package |

[`tests/olimex-fork-soll.txt`](tests/olimex-fork-soll.txt) is the list of the fork's entities. On every commit the CI checks
that `olimex-weishaupt.yaml` still provides all of them. To check it yourself (with the Python in which ESPHome is
installed – the script needs PyYAML):

```
cp secrets-olimex.yaml.example secrets.yaml      # if there is no secrets.yaml yet
esphome config olimex-weishaupt.yaml > hier.cfg
python3 werkzeuge/olimex_vergleich.py --soll tests/olimex-fork-soll.txt hier.cfg

# full comparison against the fork, all fields and sections
git clone -b olimex-esp32-evb https://github.com/cg089/weishaupt-wem-can-esphome fork
cp fork/secrets-olimex.yaml.example fork/secrets.yaml
esphome config fork/olimex-weishaupt.yaml > fork.cfg
python3 werkzeuge/olimex_vergleich.py fork.cfg hier.cfg
```

## Test status

What has been tested on the Olimex ESP32-EVB is the **state of the fork** (cg089, installation with WTC-GW …-B, ESPHome 2026.10):

- The kraiz values read via CAN match the running kraiz integration (room/flow setpoints,
  operating mode and status of heating circuit 1, DHW status, WTC operating phase, pressure, volume flow).
- DHW push via CAN was triggered and charged the storage tank.
- Flue gas and return temperature: the project names `K2 0x2537` as flue gas and `K2 0x2533/2` as return. kraiz
  names the two the other way round. On the boiler display (return 68.4 °C, flue gas 67 °C) the project's naming is correct.

The **version in this repository** (assembled from packages, plain-text error message from `fehlercodes.yaml`) is compiled in the
CI with ESPHome 2026.9.1, but has **not yet run** on an Olimex ESP32-EVB.

## License and sources

- Project: MIT, © 2026 Juergen Vejmelka; the Olimex variant, `kraiz-per-can` and `dallas`: MIT, © 2026 cg089
  (see [`LICENSE`](LICENSE)).
- Object names and value texts are derived from kraiz/hassio-weishaupt: MIT, © 2026 Lars Kreisz
  (license text in [`LIZENZEN-DRITTER.en.md`](LIZENZEN-DRITTER.en.md)).
