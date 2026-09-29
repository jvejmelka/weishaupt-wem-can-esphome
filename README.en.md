> 🇩🇪 German original: [README.md](README.md). This is a translation; if in doubt, the German version is authoritative.

# Weishaupt WEM on the CAN bus – ESPHome firmware

[![CI](https://github.com/jvejmelka/weishaupt-wem-can-esphome/actions/workflows/ci.yml/badge.svg)](https://github.com/jvejmelka/weishaupt-wem-can-esphome/actions/workflows/ci.yml)

Firmware for a **WeAct CAN485 DevBoard V1** (ESP32, galvanically isolated CAN transceiver)
that listens passively on the CAN bus of a Weishaupt condensing boiler with a **WEM system unit** (e.g. WTC-GW 15-B).

- **Reading on the bus** without putting load on the WEM: boiler, flow, flue gas, domestic hot water (DHW) and
  outdoor temperature, pressure, output, burner status with start counter, operating modes, heat quantities.
- **Switching via the WEM's JSON interface**, verified on the bus:
  heating circuit operating mode (Standby, time program 1–3, Summer, Comfort, Normal, Setback)
  and DHW on/off – via the web interface, Home Assistant or MQTT.
  At most one command per minute; anything arriving during the lockout minute is queued
  (at most one per target, the most recent request wins) and sent afterwards – visible in the field
  „Warteschlange“ ("queue"), with a button to clear it.
- **Switching log** of the last ten commands with source and result on the bus.
- **The WEM is never polled on its own initiative** – only when someone switches. Every additional
  JSON request is a risk for the fragile interface.
- **Web interface** with password, read field for arbitrary objects, write field for experts,
  Wi-Fi change without reflashing.
- **MQTT** for measurements, read, scan and switch commands; optionally the native Home Assistant API.

> **Note:** Private project with no affiliation to Weishaupt. Use at your own risk –
> the board talks to the controller of a gas-fired heating system. Work on the installation itself
> (terminals inside the boiler, installer-level parameters) belongs in qualified hands.

## Safety rules – please read

1. **The boiler (node 2) is never written to.** On the bus the firmware only sends
   read requests (SDO 0x40).
2. **Writing is done exclusively via the WEM** (JSON). The write field only allows the modules
   SYSTEM0 (MI 01), heating circuit HZK0 (MI 02) and DHW WW0 (MI 03).
3. **The WEM is fragile.** Frequent JSON requests, requests for non-existent objects or
   several clients at the same time can knock out its JSON interface until the next power cycle.
   Hence: at least one minute between two commands, no polling via JSON.
   Details: [PROTOKOLL.en.md – When the WEM hangs](PROTOKOLL.en.md#8-when-the-wem-locks-up).
4. The 120 Ω termination on the board stays **off**; the board sits in the middle of the bus.

## Wiring

| Board | Heating system (terminal H L − +) |
|---|---|
| CAN H | H |
| CAN L | L |

Take H and L from **the same twisted pair**. − and + stay unconnected.
Bus speed 50 kbit/s. The **JSON interface** must be enabled on the WEM
(parameter 10.8.1, installer level).

## Structure

```
weishaupt-wem-can.yaml        wählt die Pakete aus (substitutions, WLAN, Weboberfläche)
pakete/*.yaml                 Konfiguration und Verdrahtung: Entitäten, CAN-Takte, MQTT-Topics
components/weishaupt_can/     ESPHome-Bauteil (external component): Zustand und Logik in C++-Klassen
tests/                        Tests der Klassen auf dem PC (ohne ESPHome, ohne Board)
```

In English:

| Path | Purpose |
|---|---|
| `weishaupt-wem-can.yaml` | selects the packages (substitutions, Wi-Fi, web interface) |
| `pakete/*.yaml` | configuration and wiring: entities, CAN intervals, MQTT topics |
| `components/weishaupt_can/` | ESPHome component (external component): state and logic in C++ classes |
| `tests/` | tests of the classes on the PC (without ESPHome, without the board) |

Since v29, state and logic live in the **component `weishaupt_can`**; the packages are thin: they
create entities, send the CAN requests at their intervals and otherwise only call methods of the
component (`id(wcan)->…`). The classes in [`kern.h`](components/weishaupt_can/kern.h):

| Class | Responsibility |
|---|---|
| `wc::Bus` | bus heartbeat, start-up pause, send lock |
| `wc::Anlass` | when the operating modes are re-read (startup, status bits, switch command, „Status lesen“ ("read status"), rules) |
| `wc::Lesebefehl`, `wc::Scan` | manual read command and object scan (read requests only) |
| `wc::Brenner` | edge detection, burner starts (persisted) |
| `wc::Schalten`, `wc::Protokoll` | lockout minute, pending WEM call, write command, switching log |
| `wc::Verdacht` | runtime state of rules R1–R5 and of the custom rules (partly persisted) |
| `wc::Zusatz` | extra displays: storage format and file checksum (persisted) |

Plus the pure logic files: [`regelwerk.h`](components/weishaupt_can/regelwerk.h)
(evaluator for all rules; since v30, R1–R5 are defined there as **data rules** in the same format as the custom
rules, see [REGELN.md](REGELN.md#5-eigene-regeln-experimentell) (German)),
[`regellogik.h`](components/weishaupt_can/regellogik.h) (shared building blocks: quiet period,
masks, back-calculation for R4, pre-read check), [`befehle.h`](components/weishaupt_can/befehle.h) (queue,
command phases, status JSON), [`verdacht.h`](components/weishaupt_can/verdacht.h) and
[`zusatz.h`](components/weishaupt_can/zusatz.h) (parsers, JSON), `register_gen.h` (generated from
`register.yaml`). The ESPHome scaffold [`weishaupt_can.h`](components/weishaupt_can/weishaupt_can.h)
holds one instance of each and persists whatever has to survive a reboot – under the same
keys as up to v28, so settings are preserved across updates.

**Deliberately kept in YAML:** all entities (names, units, groups in the web interface), the
CAN requests with their intervals and pauses (`interval` + `canbus.send`), the assignment of
frames to packages (`on_frame`), the MQTT subscriptions and the WEM call (`http_request`). That is
configuration you adapt when building your own, and in ESPHome it is most readable as YAML; the
lambdas there are short and only call the component.

## Packages

`weishaupt-wem-can.yaml` only selects; the functionality lives in `pakete/`.
Comment out unneeded lines under `packages:`.

| Package | Contents | Required |
|---|---|---|
| `kessel.yaml` | boiler values, heating circuit state, diagnostics, read field, bus watchdog, Wi-Fi change | yes |
| `warmwasser.yaml` | DHW values (only with a storage tank connected to the WTC) | no |
| `mqtt.yaml` | MQTT broker, `cmd/lesen`, `cmd/scan`, `cmd/stop`, `cmd/status`, raw capture | no |
| `homeassistant-api.yaml` | native ESPHome API (encrypted) | no |
| `wem-schalten.yaml` | heating circuit switching, queue, log | no |
| `warmwasser-schalten.yaml` | DHW on/off (requires `wem-schalten` and `warmwasser`) | no |
| `verdacht.yaml` | **Rules:** read the operating modes only when passive listening suggests a change; fixed rules R1–R5 individually switchable (default: R1–R3 on, R4/R5 off; disabled ones keep running in shadow mode), plus custom rules (experimental, own master switch, default off); configurable via web, file and MQTT (requires `warmwasser`). Details: [REGELN.md](REGELN.md) (German) | no |
| `zusatz.yaml` | **Extra displays for the phone app:** list of up to 6 third-party MQTT topics (e.g. room temperatures, "heat pump running"), configurable via web and file. The board does **not** read these values itself, it only manages the list (requires `mqtt`). Details: [PROTOKOLL.en.md, section 6b](PROTOKOLL.en.md#6b-additional-displays-for-the-phone-app) | no |
| `feste-ip.yaml` | static IP instead of DHCP | no |

Optionally, there is also the phone app in the `app/` folder (see below).

## Setup

**Detailed step-by-step guide: [INSTALL.md](INSTALL.md) (German)** – short version:

```
cp secrets.yaml.example secrets.yaml     # Werte eintragen
esphome run weishaupt-wem-can.yaml       # erstes Mal per USB, danach per OTA
```

(`# Werte eintragen` = fill in your values; `# erstes Mal per USB, danach per OTA` = first time via USB, afterwards via OTA.)

**Network:** preferably DHCP with a fixed assignment (reservation) for the board in your router.
Only if that is not possible, enable the package `feste-ip.yaml`. If OTA cannot find the board
via mDNS: `esphome upload weishaupt-wem-can.yaml --device <IP>`.

**Changing Wi-Fi** without flashing: in the web interface under *Einstellungen* ("settings") enter the SSID and password,
then *WLAN übernehmen* ("apply Wi-Fi"). If the connection succeeds within 30 s, it is saved; otherwise
the previous Wi-Fi is kept. The last fallback is the emergency hotspot with captive portal.

## MQTT commands

| Topic | Payload | Effect |
|---|---|---|
| `<gerät>/cmd/heizkreis` | `1`–`8`, name (`Zeitprogramm 1`) or `ZP1`; from v27 also JSON `{"id":17,"wert":"ZP2"}` | heating circuit operating mode (queued); every phase on `befehl/status` |
| `<gerät>/cmd/warmwasser` | `Ein` / `Aus`; from v27 also JSON `{"id":18,"wert":"Aus"}` | DHW (queued); every phase on `befehl/status` |
| `<gerät>/cmd/lesen` | `01 2933 02` (node index sub, hex) | read one object |
| `<gerät>/cmd/scan` | `01 2A00 2AFF 3` | read a range, responses in `<gerät>/canraw` |
| `<gerät>/cmd/stop` | anything | abort scan |
| `<gerät>/cmd/status` | anything | read heating circuit and DHW operating mode from the bus once (at most every 10 s, read requests only); timestamp in „Status gelesen“ ("status read") |
| `<gerät>/cmd/regeln` | JSON file, preferably retained (`mosquitto_pub -r -f regeln.json`) | configure rules: R1–R5 each on/off and interval, upper limit, `eigene_regeln_an`, custom rules (template `regeln.json.example`, explanation in [REGELN.md](REGELN.md#6-einstellen) (German); the old field `verdacht` is rejected) |
| `<gerät>/cmd/regel` | `NAME an` / `NAME aus` / `NAME loeschen` | switch one rule on/off or delete it (NAME = `R1`–`R5`, `eigene` = master switch for the custom rules, or a custom rule), see [REGELN.md](REGELN.md#7-per-mqtt-schalten) (German) |
| `<gerät>/cmd/zusatz` | JSON file, preferably retained (`mosquitto_pub -r -f zusatz.json`) | configure the phone app's extra displays: `{"version":N,"eintraege":[{name, topic, feld, einheit, art, schwelle}]}` (template `zusatz.json.example`); invalid input is rejected with a message |
| `<gerät>/app/zusatz` | (retained, from the board) | current list of extra displays (the app subscribes to the topics listed there) |
| `<gerät>/status/json` | (retained, from the board, from v27) | **structured state** with fixed fields: operating modes (requested, actual, source, time), switch commands (pending, queue, last), bus, firmware – only on change, at most every 2 s. Structure: [TOPICS.md](TOPICS.md) (German) |
| `<gerät>/befehl/status` | (retained, from the board, from v27) | every phase of a switch command as JSON: angenommen → vorgemerkt → gesendet → pruefe_bus → bestaetigt / gescheitert (reason); ersetzt, abgelehnt (accepted → queued → sent → checking bus → confirmed / failed; replaced, rejected). [TOPICS.md](TOPICS.md#gerätbefehlstatus) (German) |
| `<gerät>/schaltprotokoll` | (retained, from the board) | **deprecated**, replaced by `befehl/status` – last ten commands, one per line |
| `<gerät>/regeln/stand` | (retained, from the board) | active rule configuration as JSON (same format as `cmd/regeln`, plus `firmware`), including custom rules |
| `<gerät>/verdacht/protokoll` | (retained, from the board) | last ten triggers: rule, read, changed yes/no |
| `<gerät>/verdacht/ereignis` | (not retained, from the board) | every rule event as JSON: `{"regel","phase":"ausgeloest"\|"waere"\|"ergebnis"\|"keine_antwort","ziel","wert","geaendert"}` – for Telegraf/Grafana, see [REGELN.md](REGELN.md#8-protokoll-log-und-ereignisse) (German) |

`<gerät>` = the device name (MQTT topic prefix).

**Anyone allowed to write `cmd/regeln` or `cmd/regel` can trigger read requests** – read-only, to
node 1, throttled by minimum interval and an hourly upper limit, but still bus traffic. The
app account deliberately does **not** get these rights; on the broker, grant them only to an admin account.

Heating circuit codes: 1 Standby, 2–4 time program 1–3, 5 Summer, 6 Comfort, 7 Normal, 8 Setback.

**Please build new integrations on `status/json` and `befehl/status`.** The text sensors
„Ergebnis letzter Schaltbefehl“ ("result of last switch command"), „Warteschlange“ ("queue"), „Status gelesen“ ("status read"), „Heizkreis Status“ ("heating circuit status") and the
topic `schaltprotokoll` remain available during the transition period, but are considered deprecated –
old/new comparison in [TOPICS.md](TOPICS.md#veraltete-text-topics) (German).

## Request timing

Passive listening is free. The board sends its own read requests at most every 40 s
per value (Weishaupt's specification for the data logger), operating modes only when there is a reason; after a
power cycle of the heating system it only listens for ten minutes. With the switch **„Eigene CAN-Anfragen“ ("own CAN requests")**
the requests can be disabled entirely – intervals and conditions:
[PROTOKOLL.en.md – Passive listening and reading on demand](PROTOKOLL.en.md#2-passive-listening-and-on-demand-reading).

## Monitoring

The sensor **„Letzter CAN-Frame vor“ ("last CAN frame ago")** (seconds) is suitable for an alert: if it stays
above 300 s for longer, the bus is silent (heating off, cable disconnected, controller in bus-off).
The bus watchdog reboots the board on its own after a configurable time without a frame.

## Protocol

What is passively captured on the bus and what is requested, what the JSON command to the WEM looks like, how
JSON objects map to CAN objects (e.g. heating circuit operating mode JSON `02 00 2533 02` =
CAN node 1 `0x2933/2`) and how new objects are discovered: **[PROTOKOLL.en.md](PROTOKOLL.en.md)**.

Example installation with heat pump and PV (setpoints, measurements, what you see on the bus):
**[ANLAGE.md](ANLAGE.md) (German)**.

The documents at a glance:

- [INSTALL.md](INSTALL.md) (German) – step by step from the board to the phone app
- [PROTOKOLL.en.md](PROTOKOLL.en.md) – CAN bus, WEM JSON, mapping and failure patterns
- [REGELN.md](REGELN.md) (German) – rules R1–R5 and custom rules: when the board re-reads operating modes
- [ANLAGE.md](ANLAGE.md) (German) – example installation with heat pump and PV
- [IDEEN.md](IDEEN.md) (German) – what could still be built: database and Grafana, H2 lockout,
  room sensors, wireless room unit, radiator valves, installation check
- [CHANGELOG.md](CHANGELOG.md) (German) – changes per firmware version
- [LICENSE](LICENSE) – license

## Phone app (optional, folder `app/`)

A lightweight web app (PWA) for phones: a narrow column (at most 520 CSS px) that uses the full height **without scrolling** – even on square displays. To achieve this, the base font size is measured and adjusted after loading (the largest font at which everything fits; recalculated on resize or when a progress line appears). With `?diag` appended to the URL, the footer shows viewport, pixel ratio, selected font size and scroll height.
Installable on the home screen („+ App“).

### What it shows

| Area | Contents | Source |
|---|---|---|
| Header | green dot = last fetch successful, red = problem; orange bar = countdown to the next fetch (30 s) | app |
| ⚙ (header) | opens the board's web interface in a new tab – only visible if `BOARD_URL` is set in the `.env`. The app passes nothing through; the board asks for its password itself and is only reachable on the LAN | `.env`: `BOARD_URL` |
| Heating circuit | eight buttons (Standby, ZP 1–3, Summer, Comfort, Normal, Setback). **Solid highlight = actual**: the operating mode the board read on the bus (0x2933/2). To the right of the title **„Zustand: …“** ("state: …") = running state from the status bits (e.g. „Standby“, „Zeitprogramm, heizt“ ("time program, heating"), „· WW lädt“ ("· DHW charging")) | board: `status/json` → `heizkreis`; button → `cmd/heizkreis` (JSON with ID) |
| ↻ (next to heating circuit) | makes the board read the heating circuit and DHW operating mode from the bus once (otherwise it only reads them when there is a reason); the status line shows „angefordert …“ ("requested …") and then „Status HH:MM:SS gelesen“ ("status read at HH:MM:SS") | button → `cmd/status`; board: `status/json` → `heizkreis.zeit`, `bus` |
| DHW | temperature (sensor at the **bottom** of the storage tank), ON/OFF buttons – **solid highlight = actual** on the bus (0x2A20/2); on the right **„Ladung: Gas“** ("charging: gas") (orange, pulsing, boiler in DHW mode = boiler status 15) or **„Ladung: aus“** ("charging: off") | board: `warmwasser`, `warmwasser_betriebsart`, `kesselstatus_code` (fallback `warmwasser_aktiv`); button → `cmd/warmwasser` |
| Requested (dashed) | **Requested = own switch command** via the board, as long as it has not been confirmed on the bus: **vorgemerkt** ("queued"; in the queue or just pressed), **gesendet** ("sent"; command sent to the WEM, bus verification pending). After „OK“ the dashed outline disappears and the button becomes solidly highlighted. **Red dashed** with short text: not applied / rejected (CM=05) / no response. **„von außen geändert“** ("changed externally") on the actual button: the bus shows something other than the last successful own command, without any command pending (display, portal) | board: `status/json` → `schalten`, `befehl/status` |
| Progress (below the buttons) | after a switch command, in the affected block: **vorgemerkt (ab HH:MM) → an WEM gesendet (JSON) → Bus liest nach → bestätigt ✓** ("queued (from HH:MM) → sent to WEM (JSON) → bus re-reads → confirmed ✓") (green); on failure the chain ends in red with **✗ nicht übernommen (steht auf …) / abgelehnt (CM=05) / keine Rückmeldung / ungültig** ("✗ not applied (set to …) / rejected (CM=05) / no response / invalid"). Derived solely from the command's **phase** (no text parsing). The current step pulses orange; after completion the line stays for 3 min; with no command pending there is no line | board: `status/json` → `schalten`, `befehl/status` |
| Boiler | boiler temperature large, next to it flow / setpoint and return; on the right **Brenner: aus / vorlüften / an / nachlüften** ("burner: off / pre-purge / on / post-purge") (on in orange, only with flame), followed by the **purpose** from the boiler status: „· Heizung“ ("· heating") or „· Warmwasser“ ("· DHW"); „(Heizung wartet)“ ("heating waiting") when the boiler is producing DHW while the heating circuit is demanding heat at the same time. **Kaminfeger** ("chimney sweep" mode) and **Wartung** ("maintenance") are shown in red, even with the burner off | board: `kesseltemperatur`, `vorlauf_vpt`, `vorlauf_soll`, `ruecklauf`, `brennerphase`, `kesselstatus_code`, `heizkreis_status_code` (bit 0x0040), `heizanforderung` |
| Extra (below boiler) | one compact line from the board's list, e.g. „Wohnzimmer 21,3 °C · Arbeitszimmer 22,1 °C · WP ◉ läuft“ ("living room … · study … · HP ◉ running") – type `laeuft` shows „läuft“ ("running"; orange, pulsing like the burner) or „aus“ ("off") from the threshold on. Values older than 10 min appear as „--“; empty list = no line. Tapping/hovering shows the age of the value | board: `<gerät>/app/zusatz` (package `zusatz`), values directly from the topics listed there |
| Outdoor | outdoor temperature large | board: `aussentemperatur` |
| Status line | „Aktualisiert HH:MM:SS · Status gelesen HH:MM“ ("updated … · status read …"), followed by „nächster Befehl ab …“ ("next command from …") while the lockout minute is running (at most two lines); after a button press, for 3 min the progress: vorgemerkt → gesendet → **OK** (green) or **NICHT übernommen / abgelehnt (CM=05) / keine Rückmeldung** ("NOT applied / rejected (CM=05) / no response") (red); errors such as „CAN-Board nicht erreichbar“ ("CAN board unreachable") | board: `befehl/status`, `status/json` → `schalten.frei_ab`, `heizkreis.zeit` |
| Footer | build version as a date | app |

Measurements that have not been updated for more than 10 min are shown by the app as „--“ instead of a
stale value. Operating modes and setpoints are exempt from this – the board only reads them when there is a
reason (after startup, on changed status bits, after switch commands).

### How it works

- **Reads only MQTT** from the board (`<gerät>/sensor/+/state`, retained) and **never talks to the WEM**.
- **Extra displays:** the app subscribes to `<gerät>/app/zusatz` and then to the third-party
  topics listed there (re-subscribing or unsubscribing when the list changes). The board itself does not read them.
- **Switching** is sent as an MQTT command with an ID to the board (`{"id":"app-…","wert":3,"quelle":"App"}`);
  the board executes it with lockout minute, queue and bus verification and reports every phase
  on `befehl/status`. The app reads only these fixed fields and `status/json` – **no free text**.
- Login with username and password from the `.env` – **without both, the app does not start**.
  Sessions are stored in `data/` and survive a rebuild.
- Requires board firmware **v27 or later** (`status/json`, commands with ID). With older firmware,
  progress and the requested-state marking are missing; actual values then fall back to the numeric codes.

### Setup

```
cd app
cp .env.example .env          # Broker, MQTT-Konto, App-Login eintragen
docker compose up -d --build  # danach http://<Host>:4000
```

(`# Broker, MQTT-Konto, App-Login eintragen` = fill in broker, MQTT account, app login; `# danach http://<Host>:4000` = then open http://<Host>:4000.)

A dedicated broker account that may only read and send the two switch commands and the status read command:

```
user heizungsapp
topic read  weact-can485-weishaupt/#
topic write weact-can485-weishaupt/cmd/heizkreis
topic write weact-can485-weishaupt/cmd/warmwasser
topic write weact-can485-weishaupt/cmd/status
# Zusatzanzeigen: je eingetragenem Topic genau eine Leserecht-Zeile, z. B.
topic read  sensoren/wohnzimmer/temperatur
```

(The comment reads: extra displays – exactly one read-permission line per configured topic, e.g.)

**Every topic in the extra-display list needs its own `topic read` line** – if it is missing, the
display silently stays at „--“ (the broker refuses the subscription without notifying the app). Grant read
permission only: with some devices (e.g. Shelly) a neighbouring topic can be used to switch them.

### Notes

- **From outside only behind a reverse proxy with TLS.** The app itself speaks HTTP.
- **No forward-auth portal in front of it** (Authelia & co.): the app fetches its values via `fetch`; when
  the portal session expires, it receives a redirect instead of JSON and only shows errors.
  Better use Basic Auth on the proxy or access via VPN.
- **Do not put backup copies in `app/public/`** – everything there ends up in the image and is
  served, including `*.bak`.
- **Cloudflare & co. cache stylesheets and scripts** (4 h in our case). That is why the build appends
  a version number to `style.css` and `app.js` – after an update, a single reload is enough.
- **Why the app does not query the WEM itself:** an earlier version read its values directly via the WEM's
  JSON interface every time it was opened. The interface cannot cope with that – it was locked
  several times as a result and only came back after power-cycling the heating system. Since then
  the app reads only MQTT from the board, and switch commands go through the board with the lockout minute.

## What is confirmed and what is not

The object mapping partly comes from third-party templates. Which value has been cross-checked on a real installation
(WTC-GW 15-B) and which has not is documented per object in
[PROTOKOLL.en.md, sections 3 and 4](PROTOKOLL.en.md#3-what-is-picked-up-by-passive-listening). Feedback from other
installations is welcome.

Overview of all objects the firmware evaluates (details and evidence in
[PROTOKOLL.en.md](PROTOKOLL.en.md); the source is [`register.yaml`](register.yaml)):

<!-- REGISTER:BEGIN uebersicht -->
<!-- erzeugt aus register.yaml von werkzeuge/register_erzeugen.py - nicht von Hand aendern -->

| Location | Name in the firmware | Path | Status |
|---|---|---|---|
| PDO `0x201` B1–2 | Aussentemperatur (outdoor temperature) | PDO | **confirmed** |
| PDO `0x201` B0 | Systembetriebsart (system operating mode) | PDO | plausible |
| PDO `0x241` B0–1 | Heizanforderung (heat demand) *(historical name)* | PDO | plausible |
| PDO `0x241` B2–3 | Kesseltemperatur (boiler temperature) | PDO | plausible |
| PDO `0x241` B6–7 | Warmwasser (DHW) | PDO | **confirmed** |
| PDO `0x181` B0–5 | Uhrzeit Heizung (heating system clock) | PDO | **confirmed** |
| PDO `0x182` B0 | Kesselstatus, Warmwasser aktiv (Status 15) (boiler status, DHW active) | PDO | **confirmed** |
| PDO `0x1C1` B2–3 | Heizkreis Status (heating circuit status) | PDO | plausible |
| Node 2 `0x252B`/0 | Regeln R2/R4 (rules R2/R4) | WEM writes | plausible |
| `0x6C2` `0x2699`/1 | Ruecklauf (return) | telegram 0x6C2 | plausible |
| `0x6C2` `0x2697`/1 | Vorlauf VPT (flow VPT) | telegram 0x6C2 | plausible |
| `0x6C2` `0x2698`/1 | Sollleistung (target output) | telegram 0x6C2 | plausible |
| Node 2 `0x2532`/0 | Kesseltemperatur (boiler temperature) | own request, response to the WEM | **confirmed** |
| Node 2 `0x2537`/0 | Abgastemperatur (flue gas temperature) | own request, response to the WEM | **unconfirmed** |
| Node 2 `0x2536`/0 | Vorlauf (flow) | own request | **unconfirmed** |
| Node 2 `0x2534`/0 | Leistung (output) | own request, response to the WEM | **unconfirmed** |
| Node 2 `0x2540`/0 | Drehzahl (fan speed) | own request | **unconfirmed** |
| Node 2 `0x2541`/0 | Brenner Status, Brenner, Brennerphase, Brennerstarts (burner status, burner, burner phase, burner starts) | own request, response to the WEM | **confirmed** |
| Node 2 `0x2545`/0 | Vorlauf Soll (flow setpoint) | own request, response to the WEM | **unconfirmed** |
| Node 2 `0x2713`/2 | Volumenstrom (volume flow) | own request, response to the WEM | **unconfirmed** |
| Node 2 `0x2714`/2 | Anlagendruck (system pressure) | own request, response to the WEM | **confirmed** |
| Node 2 `0x2726`/2 | Wärmemenge Vortag Heizung (heat quantity previous day, heating) | response to the WEM | **confirmed** |
| Node 2 `0x2727`/2 | Wärmemenge Vortag WW (heat quantity previous day, DHW) | response to the WEM | **confirmed** |
| Node 2 `0x2728`/2 | Wärmemenge Vortag Gesamt (heat quantity previous day, total) | response to the WEM | **confirmed** |
| Node 2 `0x2101`/0A | Regel R1 (rule R1) | query from the WEM | **confirmed** |
| Node 2 `0x2102`/0D | Regel R1 (rule R1) | query from the WEM | **confirmed** |
| Node 2 `0x2102`/1 | Regel R1 (rule R1) | query from the WEM | **confirmed** |
| Node 1 `0x2907`/2 | Vorlauf Heizkreis (heating circuit flow) | own request | plausible |
| Node 1 `0x2640`/3 | Vorlaufsoll Anforderung (flow setpoint demand) | own request | plausible |
| Node 1 `0x2958`/2 | Raumsoll aktuell (current room setpoint) | own request | plausible |
| Node 1 `0x2933`/2 | Heizkreis Betriebsart (heating circuit operating mode) | own request | **confirmed** |
| Node 1 `0x2A20`/2 | Warmwasser Betriebsart (DHW operating mode) | own request | **confirmed** |
| Node 1 `0x2A2C`/2 | Warmwasser Soll aktuell (current DHW setpoint) | own request | plausible |
| Node 1 `0x2A39`/2 | Warmwasser Soll normal (normal DHW setpoint) | own request | plausible |

<!-- REGISTER:END uebersicht -->

## Security

- The web interface uses Basic Auth over **unencrypted HTTP** – run it on the LAN only;
  from outside at most behind a reverse proxy with TLS and its own authentication.
- **Anyone allowed to write to `<gerät>/cmd/...` can switch the heating.** Set access rules (ACLs)
  on the broker: write access to `cmd/#` only for accounts that are supposed to switch.
- The WEM credentials are Weishaupt's factory defaults and, according to the manual, cannot be changed.
  The WEM interface therefore does not belong on the internet.
- The switching log is kept in RAM and is empty after a reboot
  (the last state remains retained in the MQTT topic `schaltprotokoll`).

## Hardware

**WeAct CAN485 DevBoard V1** – ESP32-D0WD-V3, 8 MB flash, CAN transceiver with 2.5 kV galvanic
isolation, RS485, USB-C; switchable 120 Ω termination (**off** here).

- Manufacturer repository with schematic and pinout:
  [WeActStudio/WeActStudio.CAN485DevBoardV1_ESP32](https://github.com/WeActStudio/WeActStudio.CAN485DevBoardV1_ESP32)
- Pinout also at Zephyr: [WeAct CAN485 DevBoard V1](https://docs.zephyrproject.org/latest/boards/weact/can485dbv1/doc/index.html)
- Introduction with prices: [CNX Software, 2026-01-14](https://www.cnx-software.com/2026/01/14/weact-can485-a-low-cost-esp32-board-with-can-bus-and-rs485-interfaces/)
- **Buy:** AliExpress (WeAct shop, link from the CNX article): https://a.aliexpress.com/_c4TXgZeJ –
  around $9–17 plus shipping, depending on the country. At the time of writing the board could not be found on Amazon.
- Power supply in operation: any USB power adapter rated 1 A or more.

Alternative built from individual parts (MenkeC's build): ESP32-C3 SuperMini plus SN65HVD230 transceiver –
cheaper, but **without** galvanic isolation.

## Sources – what helped during development

- [MenkeC/Weishaupt-C3supermini](https://github.com/MenkeC/Weishaupt-C3supermini) – ESPHome on the
  Weishaupt CAN bus (ESP32-C3 + SN65HVD230), starting point of this firmware: 50 kbit/s,
  boiler = CANopen node 2, SDO `0x602`/`0x582`, PDOs `0x201`/`0x241`.
- [geronet1/wem-python](https://github.com/geronet1/wem-python) – Python access to the WEM
  via CAN; object meanings (including system operating mode `0x28BE`). The hint towards the ESP approach
  came up in [Issue #2](https://github.com/geronet1/wem-python/issues/2).
- [BorgNumberOne/Weishaupt_CanApiJson](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) –
  register table of the WEM JSON interface (`/ajax/CanApiJson.json`) with MI/MX/OX/OS,
  factors and update classes (data point list of the Weishaupt WEM-Modbus gateway).
- [kraiz/hassio-weishaupt](https://github.com/kraiz/hassio-weishaupt) – Home Assistant integration
  via the JSON interface. The issues explain why the WEM has to be handled so carefully:
  - [#15](https://github.com/kraiz/hassio-weishaupt/issues/15) – ten requests for non-existent
    objects (CM=05) lock the interface until a power cycle
  - [#13](https://github.com/kraiz/hassio-weishaupt/issues/13) – the local JSON interface only becomes
    usable once the WEM portal is switched off
  - [#9](https://github.com/kraiz/hassio-weishaupt/issues/9) – two clients at the same time lead to a lockout
- **Weishaupt documentation:**
  - [WEM-Modbus TCP manual (Löbbeshop, PDF)](https://www.loebbeshop.de/media/67944/file/static/pdf/weishaupt/manual-wem-modbustcp.pdf) –
    data point list with update classes (30 s / 60 s / 10 min); shows that Weishaupt itself polls the
    WEM regularly, and that the gateway and the WEM portal are mutually exclusive.
  - [Installation and operating instructions WTC-GW 15–32-B (PDF)](https://www.intec-heizung.de/media/pdf/9f/c7/72/Weishaupt-Thermo-Condens-WTC-GW_15-32-B-Montage-u-Betriebsanleitung.pdf) –
    parameter 10.8.1 (JSON interface), factory credentials of the WEM, terminal H/L/−/+.
  - [WEM portal FAQ (PDF)](https://www.wemportal.com/Web/Documents/FAQ/FAQ.de.pdf?lang=de)
- Further projects and discussions:
  - [Varitras/weishaupt_modbus](https://github.com/Varitras/weishaupt_modbus) – Home Assistant via the WEM-Modbus gateway
  - [Home Assistant forum: Weishaupt WTC, CAPI VG, CanApiJson](https://community.home-assistant.io/t/weishaupt-wtc-weishaupt-capi-vg-canapijson/997400) – structure of the VG field (CM MI MX OX OS VS VA)
  - [geronet1/wem-python Issue #1](https://github.com/geronet1/wem-python/issues/1)
- ESPHome documentation: [CAN bus / esp32_can](https://esphome.io/components/canbus/esp32/),
  [Packages](https://esphome.io/components/packages/),
  [Wi-Fi incl. `wifi.configure`](https://esphome.io/components/wifi/),
  [HTTP Request](https://esphome.io/components/http_request/),
  [MQTT](https://esphome.io/components/mqtt/), [Web Server](https://esphome.io/components/web_server/).

## Tests & CI

The rules (format and evaluator) are in [`verdacht.h`](components/weishaupt_can/verdacht.h) and
[`regelwerk.h`](components/weishaupt_can/regelwerk.h), shared building blocks in
[`regellogik.h`](components/weishaupt_can/regellogik.h), the
switch commands (phases, queue, parser for `cmd/heizkreis`/`cmd/warmwasser`) and the
status JSON in [`befehle.h`](components/weishaupt_can/befehle.h), the board's state in the
classes of [`kern.h`](components/weishaupt_can/kern.h) – plain C++ without ESPHome. The class tests
drive the v28 code (kept as a reference version inside the test) and the classes with the same,
partly random inputs and require identical results; likewise, an equivalence test compares the
rules of v29 (frozen in [`tests/referenz_v29/`](tests/referenz_v29/)) with the data rules
frame by frame – log lines, triggers, events, persisted state. Tested with
made-up example frames based on [PROTOKOLL.en.md](PROTOKOLL.en.md), together with the parsers for
`cmd/regeln`, `cmd/regel` and `cmd/zusatz`. Locally (requires `g++` and
`curl`; ArduinoJson is downloaded in the board's version 7.4.3 and verified via SHA-256):

```
tests/run.sh                                     # Regellogik, Klassen, Datei-Parser, Registertabelle
python3 werkzeuge/register_erzeugen.py --pruefen # erzeugte Dateien passen zu register.yaml
python3 tests/links.py                           # relative Links und Anker in allen .md-Dateien
```

(Comments: rule logic, classes, file parsers, register table · generated files match `register.yaml` · relative links and anchors in all .md files.)

**Register table:** all CAN objects are defined only in [`register.yaml`](register.yaml); from it,
[`werkzeuge/register_erzeugen.py`](werkzeuge/register_erzeugen.py) generates the C++ constants
(`components/weishaupt_can/register_gen.h`) and the tables in PROTOKOLL.md and in the README. Procedure for changes:
[INSTALL.md, section 14](INSTALL.md#14-register-ändern) (German).

On every push, all three run in [GitHub Actions](https://github.com/jvejmelka/weishaupt-wem-can-esphome/actions/workflows/ci.yml), and in addition the
firmware is compiled with ESPHome 2026.9.0 and a dummy `secrets.yaml` (from `secrets.yaml.example`)
– including the component `weishaupt_can`. Please accompany new rules or changes to existing ones with a test – the bug from
v22–v24 (R1 only knew `0x40`, the WEM queries with `0xA4`) could not have happened that way.

### What the automated checks do (GitHub Actions, briefly explained)

GitHub automatically checks every state of this repository on its own machines – nobody has to
install anything for it. The "CI" badge at the very top shows the result of the latest run:
**green** = everything passed, **red** = something is broken. Clicking it opens the list of all runs.

On every upload (push) and every proposed change (pull request), four checks run
([`.github/workflows/ci.yml`](.github/workflows/ci.yml)):

| Check | What it ensures |
|---|---|
| Tests of the rule logic and the component | rules and switching logic react to the example telegrams as expected |
| Register table and generated files | firmware constants and documentation tables match `register.yaml`; no read request goes to the wrong node |
| Links in the .md files | all references in the documentation lead somewhere |
| Compile firmware | the firmware builds with ESPHome (with placeholder credentials; no real secrets needed) |

**For people building their own:** a green badge means the current state builds and the rules pass their
tests. If you **fork** the repository and change something yourself (for example for a different boiler type), you get
the same checks in your own fork – you may need to enable them once under *Actions*. The runs are
free for public repositories. The tests check the logic with **made-up** telegrams,
not your own installation: whether values and registers are correct on a different boiler can only be shown by testing on the
real bus.

## Contributing

Open questions that can only be resolved on other installations or in a different season:

| Question | What would help |
|---|---|
| Heating circuit codes 6–8 (Comfort, Normal, Setback) have never been seen on the bus | a capture of a switch to these operating modes |
| Switching via the WEM portal or WEM app has never been captured | a raw capture during a portal switch (see [PROTOKOLL.en.md](PROTOKOLL.en.md#detecting-passively-what-works-and-what-does-not)) |
| Return temperature candidate `0x2533`/2 is unverified | values with the burner running, next to the display readings |
| Back-calculation of the room setpoint level only calibrated for outdoor temperatures of 12.9–15.2 °C | winter data (outdoor temperature 0–8 °C, 10–15 min steady state per operating mode) |
| What does a Weishaupt room unit send? | captures, see [IDEEN.md, idea 4](IDEEN.md#gesucht-jemand-mit-weishaupt-raumgerät-am-bus) (German) |

Please open a [GitHub issue](https://github.com/jvejmelka/weishaupt-wem-can-esphome/issues), with a
summary rather than raw files (those may contain serial numbers and device identifiers).

## Contact

**Author:** Juergen Vejmelka

Please submit questions, bug reports and feedback from other installations as a
[GitHub issue](https://github.com/jvejmelka/weishaupt-wem-can-esphome/issues).

## Origin

The starting point was [MenkeC/Weishaupt-C3supermini](https://github.com/MenkeC/Weishaupt-C3supermini)
(published without a license). Protocol details were adopted from it – bus speed, node numbers,
PDOs, individual registers with factors; the code of this repository was written from scratch.
Further register knowledge comes from the projects listed under "Sources".

## License

[MIT](LICENSE) – © 2026 Juergen Vejmelka. Use at your own risk; no affiliation with Weishaupt.
