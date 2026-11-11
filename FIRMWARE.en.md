> 🇩🇪 German original: [FIRMWARE.md](FIRMWARE.md). This is a translation; if in doubt, the German version is authoritative.

# Firmware: operation, settings, rules, additional displays

What the firmware offers for operation: reading and writing by hand in the expert interface, switching, all
settings with their factory defaults, the rules for re-reading the operating modes, and the additional displays of
the app. What happens on the bus and how the WEM answers is described in [PROTOKOLL.en.md](PROTOKOLL.en.md); the
ways the firmware talks to the heating system and which are on by default are in the table
[Ways to the heating system](PROTOKOLL.en.md#ways-to-the-heating-system).

Until November 2026 these sections were part of PROTOKOLL.en.md; they keep their numbers from there (6, 6a, 6b)
so that older references stay valid.

- [6. Web interface: reading and writing](#6-web-interface-reading-and-writing)
- [6a. Rules: reading operating modes on suspicion](#6a-rules-reading-operating-modes-on-suspicion)
- [6b. Additional displays for the phone app](#6b-additional-displays-for-the-phone-app)

## 6. Web interface: reading and writing

This is the board's **expert interface** (the ESPHome web interface): from v42 at
`http://<Board>/experten`, user `admin` with the expert password; with package `app`,
`http://<Board>/` is the app. It is divided into groups. The
names here are those from the firmware (German, with English translations in parentheses).

### Lesebefehl (read command, read only)

| Element | Function |
|---|---|
| **Lesebefehl (KN IDX SUB hex)** (read command) | input `NODE INDEX SUB` in hex, e.g. `01 2933 02` |
| **Lesen** (read) | sends a read request (`0x40`) to node `0x600 + KN` – only if the bus is alive |
| **Lesebefehl Antwort** (read command response) | `01 2933/02 = 2 (0x02, 1 Byte)` or `… Abbruch 06020000 (Objekt fehlt?)` (abort, object missing?); with a dead bus “Bus tot - nicht gesendet” (bus dead – not sent) |
| **Lesebefehl Rohdaten** (read command raw data, from v33) | every frame of the last read command, `>` sent, `<` received, e.g. `>403F270000000000 <413F270008000000 …` |

The read command reads long objects automatically in segments (from v33); with suffix `B` (e.g.
`01 273F 00 B`) by block – on the boiler (node 02) only while “Lange Lesebefehle auch am Kessel” is on.

The same works via MQTT: `<gerät>/cmd/lesen` with `01 2933 02`.

### Operating modes: read status

| Element | Function |
|---|---|
| **Status lesen** (read status; button) | reads the heating circuit operating mode (`0x2933`/2) and – with package warmwasser – `0x2A20`, `0x2A2C`, `0x2A39` (each subindex 2) once from node 1, and after a restart of the board once the status bits (`0x274D`). Read requests only, no WEM JSON. Group “Betriebsarten und Sollwerte” |
| **Status gelesen** (status read) | `27.09. 09:32:51 gelesen` (read), during the request `09:32:46 angefordert ...` (requested), with a dead bus `… Bus schweigt - nicht gelesen` (bus silent – not read) |

Via MQTT: `<gerät>/cmd/status` with any payload. At most one request every 10 s.

### Switching (via WEM, verification on the bus)

| Element | Function |
|---|---|
| **Heizkreis Betriebsart setzen** (set heating circuit operating mode) | selection Standby … Absenk; shows the value on the bus after each verification read |
| **Warmwasser setzen** (set DHW) | selection Ein / Aus (on / off) |
| **Warteschlange** (queue) | pending commands with source, e.g. `1. Heizkreis -> Zeitprogramm 1 (MQTT) - naechster Befehl ab 14:03:12` (next command from 14:03:12), otherwise `leer` (empty) |
| **Ergebnis letzter Schaltbefehl** (result of last switching command) | progress and result as plain text, see [PROTOKOLL.en.md, section 5](PROTOKOLL.en.md#sequence-in-the-firmware) (deprecated – structured: `befehl/status`, [TOPICS.en.md](TOPICS.en.md)) |
| **Warteschlange leeren** (clear queue) | discards all pending commands (is logged) |
| **Schaltprotokoll** (switching log) | the last ten commands with time, source and result; via MQTT retained under `<gerät>/schaltprotokoll`. Empty after a reboot |
| **Schreibvorgaenge letzte Stunde** (writes in the last hour) | how often the board has written to the WEM in the last 60 minutes (from v42): switching, free CAN writing, write field, DHW push |
| **Schreibvorgaenge gesamt** (writes in total) | the same counter without a time window; survives a reboot, counts from the update to v42 |
| **Schreibvorgaenge je Stunde hoechstens** (writes per hour at most) | hourly limit, default <!--ab-werk:schreib_max-->30<!--/ab-werk--> (1–120, experts). Once it is reached the board does not send – [PROTOKOLL.en.md, Write protection](PROTOKOLL.en.md#write-protection-write-as-little-as-possible-from-v42) |

### Schreibbefehl über WEM (write command via WEM; experts)

| Element | Function |
|---|---|
| **Schreibbefehl (MI MX OX OS WERT hex)** (write command; WERT = value) | e.g. `03 00 2520 02 02` = DHW off. WERT 1–4 hex digits; the length (VS) is derived from it |
| **Schreiben** (write) | builds `CM 03` and sends it via JSON. **Only MI 01, 02, 03** – the boiler is locked. The interval for commands via JSON applies (<!--ab-werk:abstand_json-->60<!--/ab-werk--> s by default) |
| **Schreibbefehl Antwort** (write command response) | `OK, WEM bestaetigt (VG …)` (WEM confirmed), `abgelehnt (CM=05): Objekt unbekannt oder nicht schreibbar - VG …` (rejected: object unknown or not writable), `unklare Antwort, HTTP …` (unclear response) or `WEM nicht erreichbar` (WEM unreachable) |

The write field does **not** verify on the bus and has no queue; it does not pre-read either,
and every write counts towards the hourly limit (from v42). Before writing, look at the
object on the bus using the read command and apply the mapping from
[PROTOKOLL.en.md, section 5](PROTOKOLL.en.md#mapping-json-object--can-object). Every `CM 05` response costs one of the
ten slots from [PROTOKOLL.en.md, section 8](PROTOKOLL.en.md#8-when-the-wem-locks-up).

### Further groups

- **Schreiben per CAN:** the switches “Schalten per CAN statt JSON” and “Freies CAN-Schreiben”, the
  field for the free write command, the list of nodes permitted for it (by default `01` only) and the interval via CAN – described under [Writing via CAN](PROTOKOLL.en.md#writing-via-can-from-v35-default-since-v42).
- **SD-Karte des WEM (Dateien lesen):** fetch a file, system table – described under
  [Nodes and modules](PROTOKOLL.en.md#nodes-and-modules).
- Package `json-lesen` (group “Einstellungen”): button “Werte per JSON abrufen”, message “JSON-Abruf”
  and “Mindestabstand JSON-Abruf (s)” – described under
  [Why JSON is never polled](PROTOKOLL.en.md#why-json-is-never-polled).

### Einstellungen (settings)

| Element | Function |
|---|---|
| **Eigene CAN-Anfragen** (own CAN requests) | on = read requests at the rates from [PROTOKOLL.en.md, section 2](PROTOKOLL.en.md#rate-of-own-requests); off = passive listening only. Persists across reboots; initial state via `can_anfragen_start` in the main file |
| **CAN-Rohmitschnitt nach MQTT** (raw CAN capture to MQTT) | every received frame as `ID:DATEN` (ID:DATA) to `<gerät>/canraw` (package `mqtt`); always on during a scan |
| **Heizung: IP-Adresse** (heating: IP address) | address of the WEM for everything that runs over HTTP: switching via JSON, write command, SD files, manual refresh |
| **Waechter: Neustart nach Minuten ohne Bus (0 = aus)** (watchdog: reboot after minutes without bus, 0 = off) | reboots the board if no frame has arrived for that long (bus-off trap) |

**All settings with their default value.** The table is generated from the code (`werkzeuge/einstellungen_erzeugen.py`: `restore_mode` and `initial_value` in `pakete/*.yaml`, the list of the settings page in `app_server.cpp`); CI fails if it does not match the code. The same holds for every “default” stated elsewhere in the documentation.

<!-- EINSTELLUNGEN:BEGIN -->
| Setting (name on the board) | Default | Range | Package | Group on the settings page | Changing it needs |
|---|---|---|---|---|---|
| Schalten per CAN statt JSON (Heizkreis, Warmwasser) | **on** | – | `wem-schalten` | Schalten | admin login |
| Abstand zwischen Befehlen per CAN (s) | 10 | 5–300 | `wem-schalten` | Schalten | admin login |
| Abstand zwischen Befehlen per JSON (s) | 60 | 60–600 | `wem-schalten` | Schalten | admin login |
| Mindestabstand JSON-Abruf (s) | 300 | 120–3600 | `json-lesen` | Schalten | admin login |
| Eigene CAN-Anfragen | **off** | – | `kessel` | Lesen | admin login and expert password |
| Lange Lesebefehle auch am Kessel | **on** | – | `kessel` | Lesen | admin login and expert password |
| SD-Dateien abrufen | **on** | – | `wem-schalten` | Lesen | admin login and expert password |
| R1 Warmwasser am Display oder Portal umgeschaltet | **on** | – | `verdacht` | Regeln | admin login |
| R2 Warmwasserbetrieb trotz Aus | **on** | – | `verdacht` | Regeln | admin login |
| R3 Heizung ein oder Standby | **on** | – | `verdacht` | Regeln | admin login |
| R4 Heizanforderung passt nicht | **off** | – | `verdacht` | Regeln | admin login |
| R5 uebrige Statusbits geaendert | **off** | – | `verdacht` | Regeln | admin login |
| Eigene Regeln (Hauptschalter) | **off** | – | `verdacht` | Regeln | admin login |
| Freies CAN-Schreiben (Experten) | **off** | – | `wem-schalten` | Experten | admin login and expert password |
| Schreibvorgaenge je Stunde hoechstens | 30 | 1–120 | `wem-schalten` | Experten | admin login and expert password |
| CAN-Rohmitschnitt nach MQTT | **off** | – | `mqtt` | Experten | admin login and expert password |
| Waechter: Neustart nach Minuten ohne Bus (0 = aus) | 5 | 0–120 | `kessel` | Experten | admin login and expert password |
| CAN-Tunnel (Experten) | **off** | – | `can-tunnel` (optional) | – | expert interface |
| R1 Mindestabstand (min) | 1 | 1–1440 | `verdacht` | – | expert interface |
| R2 Mindestabstand (min) | 10 | 1–1440 | `verdacht` | – | expert interface |
| R3 Mindestabstand (min) | 1 | 1–1440 | `verdacht` | – | expert interface |
| R4 Mindestabstand (min) | 30 | 1–1440 | `verdacht` | – | expert interface |
| R5 Mindestabstand (min) | 5 | 1–1440 | `verdacht` | – | expert interface |
| Regeln: hoechstens pro Stunde (alle zusammen) | 6 | 1–60 | `verdacht` | – | expert interface |
<!-- EINSTELLUNGEN:END -->

“Changing it needs” applies with the package `app`: the settings page requires the admin login, and in the groups “Lesen” (reading) and “Experten” (experts) the expert password as well. Settings without a group are not on the settings page but in the expert interface (`/experten`); the values of the rules can also be set by file and MQTT ([REGELN.en.md](REGELN.en.md), German). “(optional)” means: the package is not included in `weishaupt-wem-can.yaml`.

## 6a. Rules: reading operating modes on suspicion

Package `verdacht.yaml` reads the operating modes **only** when passive listening suggests a
change – exclusively read requests (`0x40`) to node 1, never JSON, no fixed schedule, with a
minimum interval per rule and a cap per hour. **In detail: [REGELN.en.md](REGELN.en.md).**

| Rule | Trigger on the bus | reads | Default |
|---|---|---|---|
| R1 | WEM requests `2101/0A`, `2102/0D`, `2102/01` from the boiler (SDO block upload `0xA4` or upload `0x40`) | DHW | <!--ab-werk:vd_r1-->on<!--/ab-werk--> |
| R2 | Boiler status 15 or `0x252B` = `0F` while DHW is known to be “off” | DHW | <!--ab-werk:vd_r2-->on<!--/ab-werk--> |
| R3 | Standby bit `0x1000` in `0x1C1` changes | heating circuit | <!--ab-werk:vd_r3-->on<!--/ab-werk--> |
| R4 | Heat demand does not match the known heating circuit operating mode | heating circuit | <!--ab-werk:vd_r4-->off<!--/ab-werk--> |
| R5 | other status bits in `0x1C1` change | heating circuit | <!--ab-werk:vd_r5-->off<!--/ab-werk--> |

Disabled rules keep running in **shadow mode** (they report “would have triggered” but do not read).
All rules rest for 2 min after the firmware's own switching command; R1 also rests for 5 min after a
restart of the WEM (heartbeat `0x701` = `00`), and R5 does not read when R3 is already reading at the
same moment. Custom rules (short form “CAN ID + mask + pattern”, from v30 also sequences, edges and
conditions) are experimental and have their own master switch. Configure via the expert interface, file `<gerät>/cmd/regeln` or command
`<gerät>/cmd/regel`; state under `<gerät>/regeln/stand`, log under
`<gerät>/verdacht/protokoll`, events under `<gerät>/verdacht/ereignis`.

## 6b. Additional displays for the phone app

Package `zusatz.yaml` (from v24) has nothing to do with the boiler bus: it only manages a **list
of third-party MQTT topics** (up to 6) that the phone app displays in addition – e.g. room temperatures or whether
a heat pump is running. The board stores the list persistently and
publishes it retained under `<gerät>/app/zusatz`. **Who reads the topics depends on the app:** the
Docker app subscribes to them itself, and the board does not read them. With package `app` (app on the
board, from v38) the board subscribes to the topics of the list itself – its MQTT account needs read access.

| Field | Meaning |
|---|---|
| `name` | display name, 1–24 bytes |
| `topic` | MQTT topic without wildcards `+`/`#` |
| `feld` | JSON field in the payload, nested with a dot (`aenergy.total`); if absent, the payload itself is the number |
| `einheit` | up to 8 bytes, e.g. `°C` (3 bytes) |
| `art` | `wert` (default) shows the number; `laeuft` shows “läuft” (running) from `schwelle` (threshold, default 1) upwards, otherwise “aus” (off) |

Configuration: JSON `{"version":N,"eintraege":[…]}` to `<gerät>/cmd/zusatz` (retained possible,
checksum as with `cmd/regeln`) on the settings page of the app (package `app`) or in the expert interface, field **Zusatz-Befehl** (additional-display command):

```
Wohnzimmer topic=sensoren/wz/temp feld=tC einheit=°C
WP topic=sensoren/wp/status feld=apower einheit=W art=laeuft schwelle=50
WP loeschen
alle loeschen
```

(`loeschen` = delete, `alle loeschen` = delete all; `Wohnzimmer` = living room, `WP` = heat pump.)

The name is everything before the first word containing `=` (may contain spaces); values without
spaces. An existing entry with the same name is replaced. Result in **Zusatz Meldung** (additional-display message),
the list in **Zusatzanzeigen** (additional displays). The app (the same on the board and in Docker) shows values older than
10 min dimmed together with their age (“20,6 °C (35 min)”) – battery devices often transmit only on change –
and as “--” after 2 hours (Docker app: environment variable `ZUSATZ_MAX_ALTER_MS`).
