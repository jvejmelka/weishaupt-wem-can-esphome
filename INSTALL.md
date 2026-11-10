# Installation

Schritt für Schritt vom Board in der Verpackung bis zu Werten in Home Assistant oder der Handy-App.
Rechne mit einer Stunde, die meiste Zeit geht aufs erste Kompilieren.

> **Vorher lesen:** die [Sicherheitsregeln im README](README.md#sicherheitsregeln--bitte-lesen).
> Die Firmware schreibt nie auf den Kessel und ruft die JSON-Schnittstelle des WEM nie von selbst auf –
> daran bitte auch beim Anpassen nichts ändern. Auf welchen Wegen sie mit der Heizung spricht und was ab
> Werk an ist: [PROTOKOLL.md – Wege zur Heizung](PROTOKOLL.md#wege-zur-heizung).

## Kurzweg ohne ESPHome: fertiges Release-Image (ab v42)

Für jedes Versions-Tag baut die CI ein **Release-Image ohne Zugangsdaten** (`werkzeuge/release_bauen.py`):
kein WLAN, keine Passwörter, kein MQTT, Update nur im Browser. Es liegt beim GitHub-Release
(`weishaupt-wem-can.factory.bin` zur Erstinstallation, `weishaupt-wem-can.ota.bin` für Updates, `SHA256SUMS`)
und – sobald GitHub Pages eingeschaltet ist – auf einer **Installationsseite** mit Knopf (ESP Web Tools, Chrome/Edge):

1. Board per USB-C anschließen, auf der Installationsseite „Installieren“; danach das WLAN eintragen
   (oder mit dem offenen Hotspot `Weishaupt-WEM-CAN` verbinden).
2. `http://<board>/` öffnen: Benutzer, App- und Admin-Passwort festlegen, dann führt der Einrichtungs-Assistent
   durch WLAN, CAN-Bus, WEM-Adresse und Schaltweg.
3. Auf der Einstellungsseite ein Experten-Passwort setzen (nötig für Updates und `/experten`).

Wer MQTT, Home Assistant oder eigene Pakete braucht, nimmt den langen Weg unten. Die CI baut das Release-Image
auch bei jedem Push (Artefakt `firmware-release`) und prüft, dass keine Platzhalter-Zugangsdaten darin stehen.

### Ohne Installationsseite: mit web.esphome.io (erprobt 11/2026 mit einem neuen WeAct-Board)

So hat es mit einem Board frisch aus der Packung in wenigen Minuten geklappt:

1. `weishaupt-wem-can.factory.bin` herunterladen (Release bzw. CI-Artefakt `firmware-release`) und mit
   `SHA256SUMS` prüfen.
2. Board per USB-C an den PC – das Kabel muss **Daten** können, nicht nur laden. Unter Windows meldet sich
   der USB-Wandler als **„USB-Enhanced-SERIAL CH343 (COMx)“**; kommt nichts, fehlt der CH343-Treiber von WCH
   oder das Kabel lädt nur. Prüfen in PowerShell (vorher/nachher):
   ```powershell
   $vorher = Get-PnpDevice -PresentOnly | Select-Object -ExpandProperty InstanceId
   # Board einstecken, dann:
   Get-PnpDevice -PresentOnly | Where-Object { $vorher -notcontains $_.InstanceId } | Format-Table Status, Class, FriendlyName
   ```
3. In Chrome oder Edge **https://web.esphome.io** öffnen, „Connect“. Chrome nennt den Port
   **„USB Single Serial (COMx)“** – den nehmen, nicht Bluetooth-/COM1-Einträge.
4. **Nicht** auf „Ersteinrichtung durchführen“ klicken – das installiert eine allgemeine ESPHome-Grundversion.
   Stattdessen das Symbol **⬆ (Installieren aus Datei)**, die `.bin` wählen, „Gerät vorher löschen“ anhaken.
   Hängt die Verbindung: **GPIO0** halten, **NRST** kurz drücken, GPIO0 loslassen, nochmal versuchen.
5. Nach dem Flashen fragt die Seite per Improv gleich nach dem **WLAN** (Netz wählen, Passwort, „Connect“),
   danach „Visit Device“. Den Hotspot `Weishaupt-WEM-CAN` braucht man dann nicht.
6. In der App Benutzer, App- und Admin-Passwort festlegen, Assistent durchklicken, Experten-Passwort setzen.

**Zweites Board in derselben Anlage** (z. B. zum Testen oder für ein Raumgerät): Der Assistent liest die Adresse des WEM
bei lebendem Bus per CAN aus (ohne Bus sucht er im Netz) – die gefundene Adresse **nicht übernehmen**. Es soll nur **ein** Board je Anlage den WEM über
JSON ansprechen; der WEM verträgt keine zusätzlichen Abfragen und kann sich sonst bis zum Stromlos-Machen
aufhängen. „Schalten über“ auf **CAN** lassen.

Schalter auf dem WeAct-Board (rechts, links = OFF, rechts = ON): oben **120 Ω CAN** (ab Werk ON – am Heizungsbus
mitten in der Leitung auf **OFF**, nur am Ende eines eigenen kurzen Busses ON), Mitte PUPD und unten 120 Ω RS485
(werden nicht gebraucht). Versorgung wahlweise USB-C oder Klemme **VIN/GND 5–36 V**.

## 1. Was du brauchst

| | |
|---|---|
| Heizung | Weishaupt-Brennwertgerät mit **WEM-Systemgerät**, getestet mit WTC-GW 15-B |
| Board | **WeAct CAN485 DevBoard V1** (ESP32, galvanisch getrennter CAN-Transceiver), s. [Hardware](README.md#hardware) |
| Kabel | USB-C **mit Datenleitungen** für das erste Flashen; zweiadriges, verdrilltes Kabel für den Bus (z. B. J-Y(St)Y) |
| Strom | USB-Netzteil ab 1 A |
| Rechner | Linux, macOS oder Windows mit Python 3 **oder** Docker |
| Netz | 2,4-GHz-WLAN am Einbauort |
| optional | MQTT-Broker (z. B. Mosquitto), Home Assistant, Docker-Host für die Handy-App |

## 2. Heizung vorbereiten

1. **JSON-Schnittstelle am WEM** – fürs Schalten per CAN (seit v42 ab Werk) nicht nötig. Gebraucht wird sie
   nur für die Wege übers Netz: den Rückweg „Schalten über JSON“, das Schreibfeld für Experten und
   „Werte per JSON abrufen“. Dafür Parameter **10.8.1** in der Fachmann-Ebene auf *Ein*; die IP-Adresse des
   WEM im Router nachsehen und möglichst fest reservieren.
2. **Wenn möglich das WEM-Portal nicht gleichzeitig mit anderen lokalen JSON-Clients betreiben.**
   Mehrere Clients gleichzeitig können die Schnittstelle sperren (siehe
   [kraiz/hassio-weishaupt #9](https://github.com/kraiz/hassio-weishaupt/issues/9)). Das Board
   selbst ruft die JSON-Schnittstelle nie von sich aus auf – nur auf Bedienung (Rückweg, Schreibfeld, Abruf von Hand).

## 3. ESPHome einrichten

Eine der beiden Varianten:

```
# a) Python
python3 -m venv esphome-venv
. esphome-venv/bin/activate
pip install esphome

# b) Docker (Befehle unten dann mit diesem Präfix)
docker run --rm -it -v "$PWD":/config --device /dev/ttyACM0 ghcr.io/esphome/esphome <befehl>
```

Getestet mit ESPHome 2026.9.

## 4. Repository holen und Geheimnisse eintragen

```
git clone https://github.com/jvejmelka/weishaupt-wem-can-esphome.git
cd weishaupt-wem-can-esphome
cp secrets.yaml.example secrets.yaml
```

In `secrets.yaml` eintragen:

| Schlüssel | Bedeutung |
|---|---|
| `wifi_ssid`, `wifi_password` | dein WLAN |
| `ap_password` | Notfall-Hotspot, falls das WLAN weg ist (mind. 8 Zeichen) |
| `ota_password` | für spätere Updates übers Netz |
| `web_password` | Vorgabe für das **Admin-Passwort** der Einstellungsseite (Paket `app`); ohne Paket `app` das Passwort der ESPHome-Weboberfläche, Benutzer `admin` |
| `app_benutzer`, `app_passwort` | Vorgaben für die Anmeldung in der Handy-App auf dem Board (Paket `app`); beide leer (`""`) = das Board verlangt beim ersten Aufruf eine Ersteinrichtung |
| `app_proxy` | IP des Reverse Proxys, falls die App von außen über ihn erreichbar sein soll – sonst leer |
| `mqtt_broker`, `mqtt_user`, `mqtt_password` | nur mit Paket `mqtt` |
| `api_key` | nur mit Paket `homeassistant-api`, erzeugen mit `openssl rand -base64 32` |
| `wem_ip` | IP des WEM, nur mit `wem-schalten`; später auf dem Board änderbar (Assistent, Einstellungsseite) |
| `wem_user`, `wem_pass` | Werkszugang des WEM, **so lassen** |

`secrets.yaml` gehört nie in ein Repository (steht in `.gitignore`).

### Das Bauteil `weishaupt_can`

Zustand und Logik der Firmware stecken in einem ESPHome-Bauteil (external component) im Ordner
[`components/weishaupt_can/`](components/weishaupt_can/). Mit dem geklonten Repository ist nichts
zu tun: `weishaupt-wem-can.yaml` bindet es lokal ein, und das Paket `kessel` bringt den Block
`weishaupt_can:` schon mit.

```yaml
external_components:
  - source:
      type: local
      path: components
    components: [weishaupt_can]
```

**Ohne Repository-Kopie** (eigene YAML, Pakete und Bauteil direkt von GitHub):

```yaml
external_components:
  - source: github://jvejmelka/weishaupt-wem-can-esphome@main   # besser: fester Stand statt main
    components: [weishaupt_can]

packages:
  weishaupt:
    url: https://github.com/jvejmelka/weishaupt-wem-can-esphome
    ref: main
    files:
      - pakete/kessel.yaml
      - pakete/netz-wlan.yaml        # WLAN-Anzeigen und WLAN-Wechsel (bis v42 Teil von kessel)
      - pakete/warmwasser.yaml
      - pakete/mqtt.yaml
      # weitere Pakete nach Bedarf, siehe Abschnitt 5
```

Die `substitutions:` (`geraet`, `firmware`, `can_anfragen_start`, Regel-Vorgaben …), `wifi:`,
`web_server:` und `time:` mit `id: uhr` kommen dann aus der eigenen Datei – Vorlage ist
`weishaupt-wem-can.yaml`.

**Eigene YAML ohne die Pakete:** das Bauteil braucht nur einen CAN-Bus und (für Zeitangaben)
eine Uhr:

```yaml
weishaupt_can:
  id: wcan                # die Pakete sprechen es als id(wcan) an
  canbus_id: my_can_bus
  time_id: uhr            # optional
  anlaufpause: 10min      # Ruhe nach einem Busausfall, bevor wieder gesendet wird
```

Das Bauteil selbst sendet nichts: welche Anfragen wann auf den Bus gehen, bestimmen die Pakete.

## 5. Pakete auswählen

In `weishaupt-wem-can.yaml` unter `packages:` nicht benötigte Zeilen mit `#` auskommentieren:

| Du willst … | Pakete |
|---|---|
| nur Werte ansehen (ESPHome-Weboberfläche) | `kessel`, ggf. `warmwasser` |
| Werte in Home Assistant / Grafana | zusätzlich `mqtt` **oder** `api` |
| auch schalten | zusätzlich `schalten`, ggf. `warmwasser_schalten` |
| die Handy-App auf dem Board (samt Einstellungsseite und Assistent) | zusätzlich `app` (braucht `schalten`, `warmwasser_schalten`) |
| die Handy-App als Docker-Container | `mqtt`, `schalten` (und `warmwasser`, `warmwasser_schalten`) |
| Betriebsarten nachlesen, wenn am Display oder im Portal umgeschaltet wurde | `verdacht` (braucht `warmwasser`) |
| Zusatzanzeigen in der App (Raumtemperaturen, „Wärmepumpe läuft“) | `zusatz` (braucht `mqtt`) |
| Werte bei stillem Bus von Hand per JSON holen | `json_lesen` (braucht `schalten`, `warmwasser`) |

In der Vorlage ist alles eingeschaltet außer `api`, `feste_ip` und `kraiz`.

`warmwasser` nur, wenn ein Warmwasserspeicher am Gerät hängt.
`warmwasser_schalten` braucht `schalten` und `warmwasser`.

Optional anpassen unter `substitutions:`:

- `geraet` – Gerätename und MQTT-Präfix (Vorgabe `weact-can485-weishaupt`)
- `anzeigename` – Name in Home Assistant
- `can_anfragen_start` – **beim ersten Mal auf `RESTORE_DEFAULT_OFF` lassen**: das Board fragt
  dann nichts im Takt ab. Ganz still ist es damit nicht: die Betriebsarten liest es nach dem Start und bei
  Anlass mit einzelnen Leseanfragen, und geschaltet wird ab Werk per CAN am WEM
  ([alle Wege](PROTOKOLL.md#wege-zur-heizung)).

Prüfen, ohne etwas zu bauen:

```
esphome config weishaupt-wem-can.yaml > /dev/null && echo OK
```

> Die Ausgabe von `esphome config` enthält alle Geheimnisse im Klartext – deshalb oben nach
> `/dev/null`. Nicht in Foren posten.

## 6. Erstes Flashen per USB

Board per USB-C an den Rechner, dann:

```
esphome run weishaupt-wem-can.yaml
```

ESPHome kompiliert (beim ersten Mal 5–15 min) und fragt nach dem Anschluss – den seriellen Port
wählen (Linux meist `/dev/ttyACM0`, Windows `COMx`).

**Klappt das Hochladen nicht:**

- Kabel tauschen – viele USB-C-Kabel können nur laden.
- **BOOT**-Taste gedrückt halten, **RESET** kurz drücken, BOOT loslassen, erneut versuchen.
- Alternative ohne lokales Flashen: `esphome compile …`, dann die Datei
  `.esphome/build/<geraet>/build/firmware.factory.bin` im Browser mit
  https://web.esphome.io an Adresse 0 schreiben.
- `esphome run` endet nach dem Kompilieren manchmal mit Exitcode 1, ohne hochzuladen. Dann in
  zwei Schritten: `esphome compile …` und `esphome upload … --device <IP oder Port>`.
- Debians eigenes `esptool`-Paket bringt teils die Stub-Dateien nicht mit
  (`FileNotFoundError … stub_flasher_32.json`). Dann `--no-stub` anhängen oder esptool per pip
  installieren.

## 7. Board im Netz finden

Nach dem Start verbindet sich das Board mit dem WLAN.

1. IP im Router nachsehen (Gerätename wie `geraet`) und dort **fest reservieren**.
   Geht das nicht, Paket `feste-ip` einschalten und die `feste_ip_*`-Werte eintragen.
2. `http://<IP>/` öffnen – mit dem Paket `app` ist das die Handy-App. Mit Vorgaben in `secrets.yaml` meldest du
   dich mit `app_benutzer`/`app_passwort` an; ohne Vorgaben legst du zuerst Benutzer, App- und Admin-Passwort fest,
   danach öffnet sich der Einrichtungs-Assistent. Die **Experten-Oberfläche** (ESPHome: alle Werte, Schalter, Log)
   liegt unter `http://<IP>/experten`, Benutzer `admin` – gesperrt, bis auf der Einstellungsseite (⚙, Block
   *Expertenstufe*) ein Experten-Passwort gesetzt ist. Ohne Paket `app` liegt die ESPHome-Weboberfläche direkt
   unter `http://<IP>/`, Benutzer `admin`, Passwort `web_password`.
3. Ab jetzt braucht das Board kein USB mehr – ans Netzteil (**ab 1 A**, sonst Neustarts bei
   WLAN-Sendespitzen), Updates gehen per OTA.

Findet es kein WLAN, macht das Board nach etwa einer Minute den Hotspot **Weishaupt-WEM-CAN** auf
(Passwort `ap_password`); darüber lässt sich ein anderes WLAN eintragen.

**Bekannte Fallen:**

- **Lebenszeichen am Broker oder im Browser prüfen, nicht an der Konsole.** Mit
  abgeschaltetem UART-Log (`logger: baud_rate: 0`, in manchen Vorlagen) bleibt die serielle
  Konsole still, obwohl das Board läuft.
- **Die LED ist die Datenverkehrs-LED des USB-Wandlers**, kein Betriebsanzeiger – am Netzteil
  bleibt sie dunkel.
- Die Experten-Oberfläche bringt ihre Skripte mit (`web_server: local: true`), die App ist in die Firmware
  eingebettet – beides funktioniert ohne Internet.
- **Web-API:** Entitäten werden über ihren **Namen** angesprochen (URL-kodiert), mit Digest-Anmeldung
  (Benutzer `admin`, Experten-Passwort);
  POST braucht einen (leeren) Körper, sonst HTTP 411:
  `curl --digest -u admin:<Experten-Passwort> -X POST -d '' "http://<IP>/switch/CAN-Rohmitschnitt%20nach%20MQTT/turn_on"`

## 8. An den Bus anschließen

**Vorher die Heizung stromlos machen.** Arbeiten im Kessel nur, wenn du dir das zutraust –
sonst Fachbetrieb.

| Board | Heizung (orange Klemme **H L − +**) |
|---|---|
| CAN H | H |
| CAN L | L |

- H und L aus **demselben verdrillten Adernpaar**.
- **−** und **+** bleiben frei, der Schirm bleibt unangeklemmt und isoliert.
- Den **120-Ω-Abschluss auf dem Board ausgeschaltet lassen** – das Board hängt mitten am Bus.
- H/L vertauscht schadet nichts, es kommen dann nur keine Daten.

Heizung wieder einschalten. In der Experten-Oberfläche (`/experten`; die Einstellungsseite zeigt dasselbe
knapp als „CAN-Bus: lebt (letzte Nachricht vor … s)“):

- **„Letzter CAN-Frame vor“** muss bei wenigen Sekunden stehen.
- **Außentemperatur** und **Warmwasser** erscheinen innerhalb einer Minute (der Kessel sendet sie
  von selbst). Mit dem Kesseldisplay vergleichen.

Kommt nichts: H und L tauschen, Adern prüfen, notfalls **−** zusätzlich auflegen (gemeinsame Masse).

## 9. Eigene Leseanfragen einschalten (optional)

Erst wenn das Mithören ein paar Tage stabil läuft: auf der Einstellungsseite (⚙) oder in der Experten-Oberfläche unter *Einstellungen* den
Schalter **„Eigene CAN-Anfragen“** einschalten (den aktuellen Zustand samt Anlaufpause zeigt die
gleichnamige Zeile unter *Diagnose*). Das Board fragt dann Kesselwerte wie Druck, Leistung oder
Volumenstrom im Weishaupt-Takt (40 s / 60 s / 5 min) selbst ab – nur lesend, siehe
[PROTOKOLL.md](PROTOKOLL.md#takt-der-eigenen-anfragen).

Unabhängig davon liest das Board die **Betriebsarten** nach, wenn das Mithören eine Umschaltung
vermuten lässt (Paket `verdacht`, Regeln R1–R5, Vorgabe R1–R3 an). Was die Regeln tun und wie man
sie einstellt: [REGELN.md](REGELN.md).

## 10. MQTT und Home Assistant

**Broker-Konten mit Zugriffsregeln** (Mosquitto `acl_file`). Wer auf `…/cmd/…` schreiben darf, kann
die Heizung schalten:

```
user esp-heizung
topic readwrite weact-can485-weishaupt/#
topic readwrite esphome/#
topic write homeassistant/+/weact-can485-weishaupt/#
topic read homeassistant/status

user homeassistant
topic read weact-can485-weishaupt/#
topic readwrite homeassistant/#
# nur wenn Home Assistant schalten soll:
topic write weact-can485-weishaupt/cmd/#
```

**Regeln testen – aber richtig:** MQTT 3.1.1 verwirft eine verbotene Veröffentlichung **still**,
`mosquitto_pub` endet trotzdem mit Exitcode 0. Erst MQTT 5 meldet den Fehler:

```
mosquitto_pub -V 5 -q 1 -u homeassistant -P <pw> -t weact-can485-weishaupt/cmd/status -m 1
# ohne Schreibrecht: "Publish 1 failed: Not authorized."
```

Ein neuer Benutzer **ohne** `topic`-Zeile darf nichts – auch nicht lesen.

Home Assistant findet das Board per **MQTT-Discovery** automatisch
(Einstellungen → Geräte → MQTT). Mit dem Paket `homeassistant-api` stattdessen über die
ESPHome-Integration hinzufügen (Schlüssel = `api_key`).

Schalten per MQTT:

```
mosquitto_pub -t weact-can485-weishaupt/cmd/heizkreis  -m "Zeitprogramm 1"
mosquitto_pub -t weact-can485-weishaupt/cmd/warmwasser -m "Ein"
mosquitto_pub -t weact-can485-weishaupt/cmd/status     -m 1      # Betriebsarten einmal vom Bus lesen
```

Zwischen zwei Befehlen liegt ein Mindestabstand (per CAN ab Werk <!--ab-werk:abstand_can-->10<!--/ab-werk--> s, über JSON <!--ab-werk:abstand_json-->60<!--/ab-werk--> s);
spätere landen in der Warteschlange.

Regeln per MQTT: `cmd/regel` (z. B. `R4 an`) und `cmd/regeln` (JSON-Datei), Ereignisse unter
`verdacht/ereignis` – siehe [REGELN.md](REGELN.md#7-per-mqtt-schalten). Diese Rechte nur einem
Verwaltungskonto geben, nicht dem App-Konto.

## 11. Handy-App (optional)

Die App gibt es zweimal. **Auf dem Board** (Paket `app`, in der Vorlage eingeschaltet) ist nichts weiter zu tun:
`http://<IP>/` öffnen, siehe [README](README.md#auf-dem-board-ab-v38-paket-appyaml). Oder **als Docker-Container**
auf einem Rechner, der den Broker erreicht:

```
cd app
cp .env.example .env
```

`.env` ausfüllen:

| Schlüssel | Bedeutung |
|---|---|
| `MQTT_URL` | z. B. `mqtt://<broker>:1883` |
| `MQTT_USER`, `MQTT_PASS` | eigenes Konto, s. [README](README.md#als-docker-container-ordner-app) – nur lesen plus die zwei Schaltbefehle und `cmd/status` |
| `MQTT_GERAET` | wie `geraet` in der Firmware |
| `APP_USERNAME`, `APP_PASSWORD` | Anmeldung in der App – **ohne beide startet sie nicht** |

**Zusatzanzeigen (optional, Paket `zusatz`, ab Firmware v24):** Raumtemperaturen oder „Wärmepumpe
läuft“ aus anderen MQTT-Geräten. Liste als JSON an das Board schicken (Vorlage
`zusatz.json.example`, Topics anpassen):

```
mosquitto_pub -h <broker> -u <verwaltungskonto> -P <pw> -r -f zusatz.json -t <gerät>/cmd/zusatz
```

**ACL:** das App-Konto braucht für **jedes** eingetragene Topic eine eigene Zeile
`topic read <topic>` – sonst bleibt der Eintrag auf „--“. Nur Lesen, kein Schreiben.

```
docker compose up -d --build
```

Dann `http://<Host>:4000` öffnen und im Handy-Browser „Zum Startbildschirm hinzufügen“.
Von außen nur hinter einem Reverse Proxy mit TLS.

## 12. Updates

```
git pull
esphome run weishaupt-wem-can.yaml --device <IP>   # per OTA
```

`secrets.yaml` und `.env` bleiben dabei unberührt. Was sich geändert hat, steht im
[CHANGELOG](CHANGELOG.md); die laufende Version zeigen die Einstellungsseite und die Experten-Oberfläche unter *Firmware*.

## 13. Tests (optional)

Wer an den Paketen etwas ändert, prüft vor dem Flashen:

```
tests/run.sh                                   # Regellogik, Klassen des Bauteils, Schaltbefehle/Status-JSON, Datei-Parser, Registertabelle (g++, curl)
python3 werkzeuge/register_erzeugen.py --pruefen  # erzeugte Dateien passen zu register.yaml (PyYAML)
python3 werkzeuge/einstellungen_erzeugen.py --pruefen  # Angaben „ab Werk“ in der Doku passen zu den Paketen
python3 tests/links.py                         # Links in den .md-Dateien
esphome compile weishaupt-wem-can.yaml         # Firmware baut
```

Dasselbe läuft bei jedem Push in GitHub Actions, siehe [README](README.md#tests--ci).

## 14. Register ändern

Alle CAN-Objekte (Knoten, Index, Subindex, Faktor, Einheit, Stand, Beleg …) stehen **nur** in
[`register.yaml`](register.yaml). Daraus werden `components/weishaupt_can/register_gen.h` und die Tabellen in
PROTOKOLL.md und README.md erzeugt – beides nie von Hand ändern.

1. `register.yaml` bearbeiten (die Felder sind im Kopf der Datei erklärt). Ein neues Objekt, das
   die Firmware auswerten soll, bekommt eine `id`; im YAML-Lambda heißt es dann `reg::<id>`.
2. Erzeugen:
   ```
   pip install pyyaml                      # einmalig
   python3 werkzeuge/register_erzeugen.py  # schreibt register_gen.h und die Tabellen
   ```
   Der Generator bricht ab, wenn ein `reg::…` in den Paketen fehlt oder eine Leseanfrage an den
   falschen Knoten ginge.
3. Prüfen: `tests/run.sh` und `esphome compile weishaupt-wem-can.yaml`. Ändert sich ein
   bestehender Wert (Faktor, Index), müssen die Tests `register_*_wie_v27` angepasst werden –
   das ist Absicht: so fällt jede Verhaltensänderung auf.
4. Alles zusammen einchecken (`register.yaml`, `components/weishaupt_can/register_gen.h`, PROTOKOLL.md, README.md).
   Die CI lässt den Generator laufen und bricht ab, wenn danach etwas anders ist
   (`git diff --exit-code`); lokal geht dasselbe mit `python3 werkzeuge/register_erzeugen.py --pruefen`.

**Einstellungen und ihre Werte ab Werk** stehen ebenfalls nur im Code: `restore_mode` (Schalter) und
`initial_value` (Zahlen) in `pakete/*.yaml`, die Startwerte `…_start` in der Hauptdatei. Wer dort etwas
ändert, lässt `python3 werkzeuge/einstellungen_erzeugen.py` laufen. Es schreibt die Tabelle in
[FIRMWARE.md](FIRMWARE.md#einstellungen) neu und jede Angabe, die in der Doku zwischen
`<!--ab-werk:ID-->` und `<!--/ab-werk-->` steht (ID = `id` des Schalters oder der Zahl). Neue Textstellen,
die eine Vorgabe nennen, bekommen diese Marke – dann kann die Doku dem Code nicht mehr widersprechen.
Der Generator bricht ab, wenn die Einstellungsseite (`EINSTELLUNGEN` in `app_server.cpp`) einen Namen
oder einen Vorgabewert führt, den es im Paket so nicht gibt.

Faustregel für den Stand: **belegt** nur mit unabhängiger Gegenprobe (Kesseldisplay,
Wetterdienst, physikalisches Kriterium) – eine App, die dieselben Register liest, zählt nicht.

## Fehlersuche

| Beobachtung | Ursache / Abhilfe |
|---|---|
| „Letzter CAN-Frame vor“ steigt immer weiter | Heizung aus, Kabel ab, H/L vertauscht; der Bus-Wächter startet das Board nach der eingestellten Zeit neu |
| Werte da, aber Abgas, Druck, Leistung, Volumenstrom bleiben leer | „Eigene CAN-Anfragen“ ist aus (Absicht beim Start) – s. Schritt 9 |
| Schaltbefehl endet mit „abgelehnt“ oder „keine Rückmeldung“ | **Per CAN** (ab Werk; der Text beginnt mit „CAN:“): der WEM hat den Schreibbefehl abgebrochen oder nicht geantwortet – Bus prüfen („Letzter CAN-Frame vor“). **Über JSON** (Rückweg, „abgelehnt (CM=05)“): JSON-Schnittstelle am WEM aus (10.8.1), falsche `wem_ip`, oder der WEM ist gesperrt – dann hilft nur, die Heizung kurz stromlos zu machen. Danach seltener schalten |
| Schaltbefehl kommt nicht an, `befehl/status` meldet `abgelehnt` | `text` nennt den Grund – meist ein unbekannter Wert oder ein unbekanntes JSON-Feld; siehe [TOPICS.md](TOPICS.md#schaltbefehle-mit-kennung) |
| Handy-App zeigt keinen Fortschritt | Board-Firmware älter als v27 (kein `status/json`), oder das App-Konto darf `weact-can485-weishaupt/#` nicht lesen |
| OTA findet das Board nicht | `esphome upload weishaupt-wem-can.yaml --device <IP>` |
| Experten-Oberfläche (`/experten`) fragt nach Passwort | Benutzer `admin`, Passwort = das **Experten-Passwort** von der Einstellungsseite (⚙, Block *Expertenstufe*); ohne gesetztes Experten-Passwort ist die Stufe gesperrt. Ohne Paket `app`: `web_password` aus `secrets.yaml` |
