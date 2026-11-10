> 🇩🇪 German original: [INSTALL.md](INSTALL.md). This is a translation; if in doubt, the German version is authoritative.

# Installation

Step by step from the board in its packaging to values in Home Assistant or the phone app.
Allow about an hour; most of the time goes into the first compile.

> **Read first:** the [safety rules in the README](README.en.md#safety-rules--please-read).
> The firmware never writes to the boiler and never calls the WEM's JSON interface on its own initiative –
> please do not change that when adapting it either. The ways it talks to the heating system and which are on by
> default: [PROTOKOLL.en.md – Ways to the heating system](PROTOKOLL.en.md#ways-to-the-heating-system).

## Short route without ESPHome: ready-made release image (from v42)

For every version tag the CI builds a **release image without credentials** (`werkzeuge/release_bauen.py`):
no Wi-Fi, no passwords, no MQTT, update only in the browser. It is attached to the GitHub release
(`weishaupt-wem-can.factory.bin` for the first installation, `weishaupt-wem-can.ota.bin` for updates, `SHA256SUMS`)
and – as soon as GitHub Pages is switched on – available on an **install page** with a button (ESP Web Tools, Chrome/Edge):

1. Connect the board via USB-C, click „Installieren“ ("install") on the install page; then enter the Wi-Fi
   (or connect to the open hotspot `Weishaupt-WEM-CAN`).
2. Open `http://<board>/`: set user, app password and admin password; the setup assistant then guides you
   through Wi-Fi, CAN bus, WEM address and switching path.
3. Set an expert password on the settings page (required for updates and `/experten`).

If you need MQTT, Home Assistant or your own packages, take the long route below. The CI also builds the release image
on every push (artifact `firmware-release`) and checks that it contains no placeholder credentials.

### Without the install page: with web.esphome.io (tried in November 2026 with a new WeAct board)

This is how it worked within a few minutes with a board fresh out of the box:

1. Download `weishaupt-wem-can.factory.bin` (release or CI artifact `firmware-release`) and verify it with
   `SHA256SUMS`.
2. Connect the board to the PC via USB-C – the cable must carry **data**, not just charge. On Windows
   the USB converter shows up as **“USB-Enhanced-SERIAL CH343 (COMx)”**; if nothing appears, the CH343 driver from WCH
   is missing or the cable is charge-only. Check in PowerShell (before/after):
   ```powershell
   $vorher = Get-PnpDevice -PresentOnly | Select-Object -ExpandProperty InstanceId
   # plug in the board, then:
   Get-PnpDevice -PresentOnly | Where-Object { $vorher -notcontains $_.InstanceId } | Format-Table Status, Class, FriendlyName
   ```
3. Open **https://web.esphome.io** in Chrome or Edge, “Connect”. Chrome calls the port
   **“USB Single Serial (COMx)”** – choose that one, not the Bluetooth/COM1 entries.
4. Do **not** click „Ersteinrichtung durchführen“ ("prepare for first use") – that installs a generic basic ESPHome version.
   Instead use the icon **⬆ (Installieren aus Datei, "install from file")**, select the `.bin`, tick „Gerät vorher löschen“ ("erase device first").
   If the connection hangs: hold **GPIO0**, press **NRST** briefly, release GPIO0, try again.
5. After flashing, the page immediately asks for the **Wi-Fi** via Improv (choose the network, password, “Connect”),
   then “Visit Device”. The hotspot `Weishaupt-WEM-CAN` is then not needed.
6. In the app, set user, app password and admin password, click through the assistant, set an expert password.

**Second board in the same installation** (e.g. for testing or for a room unit): with a live bus the assistant reads the WEM's
address via CAN (without a bus it searches the network) – **do not accept** the address it finds. Only **one** board per installation should talk to the WEM via
JSON; the WEM does not tolerate additional requests and can otherwise lock up until it is disconnected from
power. Leave „Schalten über“ ("switch via") set to **CAN**.

Switches on the WeAct board (on the right-hand side; left = OFF, right = ON): top **120 Ω CAN** (ON as shipped – on the heating bus,
in the middle of the line, set it to **OFF**; ON only at the end of a short bus of your own), middle PUPD and bottom 120 Ω RS485
(not needed). Power either via USB-C or via terminal **VIN/GND 5–36 V** – but **not from the
"− +" terminals of the heating bus**: that bypasses the galvanic isolation; the reasons are given under
[Hardware](README.en.md#hardware).

## 1. What you need

| | |
|---|---|
| Heating system | Weishaupt condensing boiler with **WEM system unit**, tested with WTC-GW 15-B |
| Board | **WeAct CAN485 DevBoard V1** (ESP32, galvanically isolated CAN transceiver), see [Hardware](README.en.md#hardware) |
| Cable | USB-C **with data lines** for the first flash; two-core twisted cable for the bus (e.g. J-Y(St)Y) |
| Power | USB power adapter rated 1 A or more |
| Computer | Linux, macOS or Windows with Python 3 **or** Docker |
| Network | 2.4 GHz Wi-Fi at the installation location |
| optional | MQTT broker (e.g. Mosquitto), Home Assistant, Docker host for the phone app |

## 2. Prepare the heating system

1. **JSON interface on the WEM** – not needed for switching via CAN (the default since v42). It is only
   needed for the ways over the network: the fallback „Schalten über JSON“ ("switching via JSON"), the write field for experts and
   „Werte per JSON abrufen“ ("fetch values via JSON"). For these, set parameter **10.8.1** at the installer level to *Ein* (on); look up the
   WEM's IP address in the router and reserve it permanently if possible.
2. **If possible, do not run the WEM portal at the same time as other local JSON clients.**
   Several clients at the same time can lock the interface (see
   [kraiz/hassio-weishaupt #9](https://github.com/kraiz/hassio-weishaupt/issues/9)). The board
   itself never calls the JSON interface on its own initiative – only when someone operates it (fallback, write field, manual fetch).

## 3. Set up ESPHome

One of the two options:

```
# a) Python
python3 -m venv esphome-venv
. esphome-venv/bin/activate
pip install esphome

# b) Docker (then use this prefix for the commands below)
docker run --rm -it -v "$PWD":/config --device /dev/ttyACM0 ghcr.io/esphome/esphome <command>
```

Tested with ESPHome 2026.9.

## 4. Get the repository and enter the secrets

```
git clone https://github.com/jvejmelka/weishaupt-wem-can-esphome.git
cd weishaupt-wem-can-esphome
cp secrets.yaml.example secrets.yaml
```

Enter in `secrets.yaml`:

| Key | Meaning |
|---|---|
| `wifi_ssid`, `wifi_password` | your Wi-Fi |
| `ap_password` | emergency hotspot in case the Wi-Fi is gone (at least 8 characters) |
| `ota_password` | for later updates over the network |
| `web_password` | default for the **admin password** of the settings page (package `app`); without the package `app`, the password of the ESPHome web interface, user `admin` |
| `app_benutzer`, `app_passwort` | defaults for the login to the phone app on the board (package `app`); both empty (`""`) = the board requires a first-run setup on first access |
| `app_proxy` | IP of the reverse proxy if the app is to be reachable from outside through it – otherwise empty |
| `mqtt_broker`, `mqtt_user`, `mqtt_password` | only with the package `mqtt` |
| `api_key` | only with the package `homeassistant-api`, generate with `openssl rand -base64 32` |
| `wem_ip` | IP of the WEM, only with `wem-schalten`; can be changed later on the board (assistant, settings page) |
| `wem_user`, `wem_pass` | the WEM's factory credentials, **leave as they are** |

`secrets.yaml` never belongs in a repository (it is listed in `.gitignore`).

### The component `weishaupt_can`

The firmware's state and logic live in an ESPHome component (external component) in the folder
[`components/weishaupt_can/`](components/weishaupt_can/). With the cloned repository there is nothing
to do: `weishaupt-wem-can.yaml` includes it locally, and the package `kessel` already brings the block
`weishaupt_can:` with it.

```yaml
external_components:
  - source:
      type: local
      path: components
    components: [weishaupt_can]
```

**Without a copy of the repository** (your own YAML, packages and component straight from GitHub):

```yaml
external_components:
  - source: github://jvejmelka/weishaupt-wem-can-esphome@main   # better: a fixed revision instead of main
    components: [weishaupt_can]

packages:
  weishaupt:
    url: https://github.com/jvejmelka/weishaupt-wem-can-esphome
    ref: main
    files:
      - pakete/kessel.yaml
      - pakete/netz-wlan.yaml        # Wi-Fi displays and Wi-Fi change (part of kessel up to v42)
      - pakete/warmwasser.yaml
      - pakete/mqtt.yaml
      # further packages as needed, see section 5
```

The `substitutions:` (`geraet`, `firmware`, `can_anfragen_start`, rule defaults …), `wifi:`,
`web_server:` and `time:` with `id: uhr` then come from your own file – the template is
`weishaupt-wem-can.yaml`.

**Your own YAML without the packages:** the component only needs a CAN bus and (for timestamps)
a clock:

```yaml
weishaupt_can:
  id: wcan                # the packages refer to it as id(wcan)
  canbus_id: my_can_bus
  time_id: uhr            # optional
  anlaufpause: 10min      # quiet time after a bus outage before sending again
```

The component itself sends nothing: which requests go onto the bus, and when, is determined by the packages.

## 5. Select packages

In `weishaupt-wem-can.yaml`, comment out the lines you do not need under `packages:` with `#`:

| You want … | Packages |
|---|---|
| only to look at values (ESPHome web interface) | `kessel`, `warmwasser` if applicable |
| values in Home Assistant / Grafana | additionally `mqtt` **or** `api` |
| to switch as well | additionally `schalten`, `warmwasser_schalten` if applicable |
| the phone app on the board (including settings page and assistant) | additionally `app` (requires `schalten`, `warmwasser_schalten`) |
| the phone app as a Docker container | `mqtt`, `schalten` (and `warmwasser`, `warmwasser_schalten`) |
| to re-read the operating modes when they were changed on the display or in the portal | `verdacht` (requires `warmwasser`) |
| extra displays in the app (room temperatures, "heat pump running") | `zusatz` (requires `mqtt`) |
| to fetch values manually via JSON when the bus is silent | `json_lesen` (requires `schalten`, `warmwasser`) |

In the template everything is enabled except `api`, `feste_ip`, `kraiz` and `can_tunnel` (experiment, only in branch
`can-tunnel`: [VERSUCHE.en.md, 10b](VERSUCHE.en.md#10b-can-tunnel-over-wlan-experiment-branch-can-tunnel)).

`warmwasser` only if a DHW storage tank is connected to the unit.
`warmwasser_schalten` requires `schalten` and `warmwasser`.

Optionally adjust under `substitutions:`:

- `geraet` – device name and MQTT prefix (default `weact-can485-weishaupt`)
- `anzeigename` – name in Home Assistant
- `can_anfragen_start` – **leave it at `RESTORE_DEFAULT_OFF` the first time**: the board then
  requests nothing periodically. That does not make it completely silent: it reads the operating modes after start-up and
  when there is a reason, with individual read requests, and switching is done via CAN at the WEM by default
  ([all ways](PROTOKOLL.en.md#ways-to-the-heating-system)).

Check without building anything:

```
esphome config weishaupt-wem-can.yaml > /dev/null && echo OK
```

> The output of `esphome config` contains all secrets in plain text – hence the redirect to
> `/dev/null` above. Do not post it in forums.

## 6. First flash via USB

Connect the board to the computer via USB-C, then:

```
esphome run weishaupt-wem-can.yaml
```

ESPHome compiles (5–15 min the first time) and asks for the port – choose the serial port
(on Linux usually `/dev/ttyACM0`, on Windows `COMx`).

**If the upload fails:**

- Swap the cable – many USB-C cables can only charge.
- Hold the **BOOT** button, press **RESET** briefly, release BOOT, try again.
- Alternative without flashing locally: `esphome compile …`, then write the file
  `.esphome/build/<geraet>/build/firmware.factory.bin` to address 0 in the browser with
  https://web.esphome.io.
- `esphome run` sometimes ends with exit code 1 after compiling, without uploading. Then do it in
  two steps: `esphome compile …` and `esphome upload … --device <IP or port>`.
- Debian's own `esptool` package sometimes does not include the stub files
  (`FileNotFoundError … stub_flasher_32.json`). Then append `--no-stub` or install esptool via
  pip.

## 7. Find the board on the network

After start-up the board connects to the Wi-Fi.

1. Look up the IP in the router (device name as in `geraet`) and **reserve it permanently** there.
   If that is not possible, enable the package `feste-ip` and enter the `feste_ip_*` values.
2. Open `http://<IP>/` – with the package `app` this is the phone app. With defaults in `secrets.yaml` you log
   in with `app_benutzer`/`app_passwort`; without defaults you first set user, app password and admin password,
   after which the setup assistant opens. The **expert interface** (ESPHome: all values, switches, log)
   is at `http://<IP>/experten`, user `admin` – locked until an expert password has been set on the settings page (⚙, block
   *Expertenstufe* ("expert level")). Without the package `app`, the ESPHome web interface is directly
   at `http://<IP>/`, user `admin`, password `web_password`.
3. From now on the board no longer needs USB – connect it to the power adapter (**1 A or more**, otherwise it restarts during
   Wi-Fi transmit peaks); updates are done via OTA.

If it finds no Wi-Fi, the board opens the hotspot **Weishaupt-WEM-CAN** after about a minute
(password `ap_password`); a different Wi-Fi can be entered through it.

**Known pitfalls:**

- **Check for signs of life at the broker or in the browser, not on the console.** With
  the UART log switched off (`logger: baud_rate: 0`, in some templates) the serial
  console stays silent although the board is running.
- **The LED is the data-traffic LED of the USB converter**, not a power indicator – on the power adapter
  it stays dark.
- The expert interface brings its scripts along (`web_server: local: true`), the app is embedded in the
  firmware – both work without internet access.
- **Web API:** entities are addressed by their **name** (URL-encoded), with digest authentication
  (user `admin`, expert password);
  POST needs an (empty) body, otherwise HTTP 411:
  `curl --digest -u admin:<expert-password> -X POST -d '' "http://<IP>/switch/CAN-Rohmitschnitt%20nach%20MQTT/turn_on"`

## 8. Connect to the bus

**Disconnect the heating system from power first.** Only work inside the boiler if you are confident you can do it –
otherwise leave it to a qualified heating contractor.

| Board | Heating system (orange terminal **H L − +**) |
|---|---|
| CAN H | H |
| CAN L | L |

- Take H and L from **the same twisted pair**.
- **−** and **+** stay unconnected; the shield stays unconnected and insulated.
- Leave the **120 Ω termination on the board switched off** – the board sits in the middle of the bus.
- Swapping H/L does no harm; there is just no data then.

Switch the heating system back on. In the expert interface (`/experten`; the settings page shows the same
in short as „CAN-Bus: lebt (letzte Nachricht vor … s)“ ("CAN bus: alive (last message … s ago)")):

- **„Letzter CAN-Frame vor“ ("last CAN frame ago")** must be at a few seconds.
- **Außentemperatur** (outdoor temperature) and **Warmwasser** (DHW) appear within a minute (the boiler sends them
  on its own). Compare with the boiler display.

If nothing arrives: swap H and L, check the wires, if necessary connect **−** as well (common ground).

## 9. Switch on own read requests (optional)

Only once passive listening has been running stably for a few days: on the settings page (⚙) or in the expert interface under *Einstellungen* ("settings"), turn on the
switch **„Eigene CAN-Anfragen“ ("own CAN requests")** (the current state including the start-up pause is shown by the
line of the same name under *Diagnose* ("diagnostics")). The board then requests boiler values such as pressure, output or
flow rate itself at Weishaupt's rate (40 s / 60 s / 5 min) – read-only, see
[PROTOKOLL.en.md](PROTOKOLL.en.md#rate-of-own-requests).

Independently of this, the board re-reads the **operating modes** when passive listening suggests
a change (package `verdacht`, rules R1–R5, default R1–R3 on). What the rules do and how to
configure them: [REGELN.en.md](REGELN.en.md).

## 10. MQTT and Home Assistant

**Broker accounts with access rules** (Mosquitto `acl_file`). Anyone allowed to write to `…/cmd/…` can
switch the heating system:

```
user esp-heizung
topic readwrite weact-can485-weishaupt/#
topic readwrite esphome/#
topic write homeassistant/+/weact-can485-weishaupt/#
topic read homeassistant/status

user homeassistant
topic read weact-can485-weishaupt/#
topic readwrite homeassistant/#
# only if Home Assistant is to switch:
topic write weact-can485-weishaupt/cmd/#
```

**Test the rules – but properly:** MQTT 3.1.1 discards a forbidden publish **silently**;
`mosquitto_pub` still ends with exit code 0. Only MQTT 5 reports the error:

```
mosquitto_pub -V 5 -q 1 -u homeassistant -P <pw> -t weact-can485-weishaupt/cmd/status -m 1
# without write permission: "Publish 1 failed: Not authorized."
```

A new user **without** a `topic` line may do nothing – not even read.

Home Assistant finds the board automatically via **MQTT discovery**
(Settings → Devices → MQTT). With the package `homeassistant-api`, add it via the
ESPHome integration instead (key = `api_key`).

Switching via MQTT:

```
mosquitto_pub -t weact-can485-weishaupt/cmd/heizkreis  -m "Zeitprogramm 1"
mosquitto_pub -t weact-can485-weishaupt/cmd/warmwasser -m "Ein"
mosquitto_pub -t weact-can485-weishaupt/cmd/status     -m 1      # read the operating modes from the bus once
```

There is a minimum interval between two commands (by default <!--ab-werk:abstand_can-->10<!--/ab-werk--> s via CAN, <!--ab-werk:abstand_json-->60<!--/ab-werk--> s via JSON);
later ones end up in the queue.

Rules via MQTT: `cmd/regel` (e.g. `R4 an`) and `cmd/regeln` (JSON file), events under
`verdacht/ereignis` – see [REGELN.en.md](REGELN.en.md#7-switching-via-mqtt). Grant these rights only to an
admin account, not to the app account.

## 11. Phone app (optional)

The app exists in two forms. **On the board** (package `app`, enabled in the template) there is nothing more to do:
open `http://<IP>/`, see [README](README.en.md#on-the-board-from-v38-package-appyaml). Or **as a Docker container**
on a machine that can reach the broker:

```
cd app
cp .env.example .env
```

Fill in `.env`:

| Key | Meaning |
|---|---|
| `MQTT_URL` | e.g. `mqtt://<broker>:1883` |
| `MQTT_USER`, `MQTT_PASS` | a dedicated account, see [README](README.en.md#as-a-docker-container-folder-app) – read-only plus the two switch commands and `cmd/status` |
| `MQTT_GERAET` | same as `geraet` in the firmware |
| `APP_USERNAME`, `APP_PASSWORD` | login to the app – **it does not start without both** |

**Extra displays (optional, package `zusatz`, from firmware v24):** room temperatures or "heat pump
running" from other MQTT devices. Send the list to the board as JSON (template
`zusatz.json.example`, adapt the topics):

```
mosquitto_pub -h <broker> -u <admin-account> -P <pw> -r -f zusatz.json -t <gerät>/cmd/zusatz
```

**ACL:** the app account needs its own line `topic read <topic>` for **every** topic entered
– otherwise the entry stays at „--“. Read only, no write.

```
docker compose up -d --build
```

Then open `http://<Host>:4000` and choose "Add to Home screen" in the phone browser.
From outside only behind a reverse proxy with TLS.

## 12. Updates

```
git pull
esphome run weishaupt-wem-can.yaml --device <IP>   # via OTA
```

`secrets.yaml` and `.env` are not touched by this. What has changed is listed in the
[CHANGELOG](CHANGELOG.en.md); the running version is shown on the settings page and in the expert interface under *Firmware*.

## 13. Tests (optional)

If you change anything in the packages, check before flashing:

```
tests/run.sh                                   # rule logic, classes of the component, switch commands/status JSON, file parsers, register table (g++, curl)
python3 werkzeuge/register_erzeugen.py --pruefen  # generated files match register.yaml (PyYAML)
python3 werkzeuge/einstellungen_erzeugen.py --pruefen  # defaults stated in the docs match the packages
python3 tests/links.py                         # links in the .md files
python3 tests/sprachen.py                      # every doc in German and English, same structure
esphome compile weishaupt-wem-can.yaml         # firmware builds
```

The same runs on every push in GitHub Actions, see [README](README.en.md#tests--ci).

## 14. Changing registers

All CAN objects (node, index, subindex, factor, unit, status, evidence …) are defined **only** in
[`register.yaml`](register.yaml). From it, `components/weishaupt_can/register_gen.h` and the tables in
PROTOKOLL.md and README.md are generated – never change either by hand.

1. Edit `register.yaml` (the fields are explained at the top of the file). A new object that
   the firmware is to evaluate gets an `id`; in the YAML lambda it is then called `reg::<id>`.
2. Generate:
   ```
   pip install pyyaml                      # once
   python3 werkzeuge/register_erzeugen.py  # writes register_gen.h and the tables
   ```
   The generator aborts if a `reg::…` used in the packages is missing or a read request would go to the
   wrong node.
3. Check: `tests/run.sh` and `esphome compile weishaupt-wem-can.yaml`. If an
   existing value changes (factor, index), the tests `register_*_wie_v27` have to be adapted –
   this is intentional: that way every change in behaviour is noticed.
4. Commit everything together (`register.yaml`, `components/weishaupt_can/register_gen.h`, PROTOKOLL.md, README.md).
   The CI runs the generator and fails if anything differs afterwards
   (`git diff --exit-code`); locally the same is done with `python3 werkzeuge/register_erzeugen.py --pruefen`.

**Settings and their default values** are likewise defined only in the code: `restore_mode` (switches) and
`initial_value` (numbers) in `pakete/*.yaml`, the start values `…_start` in the main file. Anyone who changes
something there runs `python3 werkzeuge/einstellungen_erzeugen.py`. It rewrites the table in
[FIRMWARE.en.md](FIRMWARE.en.md#einstellungen-settings) and every value that stands in the documentation between
`<!--ab-werk:ID-->` and `<!--/ab-werk-->` (ID = `id` of the switch or the number). New passages
that state a default get this marker – then the documentation can no longer contradict the code.
The generator aborts if the settings page (`EINSTELLUNGEN` in `app_server.cpp`) lists a name
or a default value that does not exist in that form in the package.

Rule of thumb for the status: **belegt** (verified) only with an independent cross-check (boiler display,
weather service, physical criterion) – an app that reads the same registers does not count.

## Troubleshooting

| Observation | Cause / remedy |
|---|---|
| „Letzter CAN-Frame vor“ ("last CAN frame ago") keeps rising | heating system off, cable disconnected, H/L swapped; the bus watchdog restarts the board after the configured time |
| values are there, but flue gas, pressure, output, flow rate stay empty | „Eigene CAN-Anfragen“ ("own CAN requests") is off (intentional at the start) – see step 9 |
| switch command ends with „abgelehnt“ ("rejected") or „keine Rückmeldung“ ("no feedback") | **Via CAN** (default; the text starts with „CAN:“): the WEM aborted the write command or did not answer – check the bus („Letzter CAN-Frame vor“). **Via JSON** (fallback, „abgelehnt (CM=05)“): JSON interface on the WEM is off (10.8.1), wrong `wem_ip`, or the WEM is locked – then the only remedy is to disconnect the heating system from power briefly. After that, switch less often |
| switch command does not arrive, `befehl/status` reports `abgelehnt` | `text` gives the reason – usually an unknown value or an unknown JSON field; see [TOPICS.en.md](TOPICS.en.md#switch-commands-with-id) |
| phone app shows no progress | board firmware older than v27 (no `status/json`), or the app account is not allowed to read `weact-can485-weishaupt/#` |
| OTA cannot find the board | `esphome upload weishaupt-wem-can.yaml --device <IP>` |
| expert interface (`/experten`) asks for a password | user `admin`, password = the **expert password** from the settings page (⚙, block *Expertenstufe* ("expert level")); without an expert password set, the level is locked. Without the package `app`: `web_password` from `secrets.yaml` |
