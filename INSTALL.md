# Installation

Schritt für Schritt vom Board in der Verpackung bis zu Werten in Home Assistant oder der Handy-App.
Rechne mit einer Stunde, die meiste Zeit geht aufs erste Kompilieren.

> **Vorher lesen:** die [Sicherheitsregeln im README](README.md#sicherheitsregeln--bitte-lesen).
> Die Firmware schreibt nie auf den Kessel und fragt den WEM nie von selbst ab – daran bitte
> auch beim Anpassen nichts ändern.

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

1. **JSON-Schnittstelle am WEM einschalten** – nur nötig, wenn du über das Board schalten willst
   (Pakete `wem-schalten`, `warmwasser-schalten`). Parameter **10.8.1** in der Fachmann-Ebene auf
   *Ein*. Die IP-Adresse des WEM im Router nachsehen und möglichst fest reservieren.
2. **Wenn möglich das WEM-Portal nicht gleichzeitig mit anderen lokalen JSON-Clients betreiben.**
   Mehrere Clients gleichzeitig können die Schnittstelle sperren (siehe
   [kraiz/hassio-weishaupt #9](https://github.com/kraiz/hassio-weishaupt/issues/9)). Das Board
   selbst fragt nur, wenn geschaltet wird.

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
| `web_password` | Weboberfläche, Benutzer `admin` |
| `mqtt_broker`, `mqtt_user`, `mqtt_password` | nur mit Paket `mqtt` |
| `api_key` | nur mit Paket `homeassistant-api`, erzeugen mit `openssl rand -base64 32` |
| `wem_ip` | IP des WEM, nur mit `wem-schalten`; später in der Weboberfläche änderbar |
| `wem_user`, `wem_pass` | Werkszugang des WEM, **so lassen** |

`secrets.yaml` gehört nie in ein Repository (steht in `.gitignore`).

## 5. Pakete auswählen

In `weishaupt-wem-can.yaml` unter `packages:` nicht benötigte Zeilen mit `#` auskommentieren:

| Du willst … | Pakete |
|---|---|
| nur Werte ansehen (Weboberfläche) | `kessel`, ggf. `warmwasser` |
| Werte in Home Assistant / Grafana | zusätzlich `mqtt` **oder** `api` |
| auch schalten | zusätzlich `schalten`, ggf. `warmwasser_schalten` |
| die Handy-App | `mqtt`, `schalten` (und `warmwasser`, `warmwasser_schalten`) |

`warmwasser` nur, wenn ein Warmwasserspeicher am Gerät hängt.
`warmwasser_schalten` braucht `schalten` und `warmwasser`.

Optional anpassen unter `substitutions:`:

- `geraet` – Gerätename und MQTT-Präfix (Vorgabe `weact-can485-weishaupt`)
- `anzeigename` – Name in Home Assistant
- `can_anfragen_start` – **beim ersten Mal auf `RESTORE_DEFAULT_OFF` lassen**: das Board hört
  dann nur mit und sendet selbst nichts auf den Bus.

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
- Debians eigenes `esptool`-Paket bringt teils die Stub-Dateien nicht mit
  (`FileNotFoundError … stub_flasher_32.json`). Dann `--no-stub` anhängen oder esptool per pip
  installieren.

## 7. Board im Netz finden

Nach dem Start verbindet sich das Board mit dem WLAN.

1. IP im Router nachsehen (Gerätename wie `geraet`) und dort **fest reservieren**.
   Geht das nicht, Paket `feste-ip` einschalten und die `feste_ip_*`-Werte eintragen.
2. Weboberfläche öffnen: `http://<IP>`, Benutzer `admin`, Passwort `web_password`.
3. Ab jetzt braucht das Board kein USB mehr – ans Netzteil, Updates gehen per OTA.

Findet es kein WLAN, macht das Board nach etwa einer Minute den Hotspot **Weishaupt-WEM-CAN** auf
(Passwort `ap_password`); darüber lässt sich ein anderes WLAN eintragen.

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

Heizung wieder einschalten. In der Weboberfläche:

- **„Letzter CAN-Frame vor“** muss bei wenigen Sekunden stehen.
- **Außentemperatur** und **Warmwasser** erscheinen innerhalb einer Minute (der Kessel sendet sie
  von selbst). Mit dem Kesseldisplay vergleichen.

Kommt nichts: H und L tauschen, Adern prüfen, notfalls **−** zusätzlich auflegen (gemeinsame Masse).

## 9. Eigene Leseanfragen einschalten (optional)

Erst wenn das Mithören ein paar Tage stabil läuft: in der Weboberfläche unter *Einstellungen* den
Schalter **„Eigene CAN-Anfragen“** einschalten (den aktuellen Zustand samt Anlaufpause zeigt die
gleichnamige Zeile unter *Diagnose*). Das Board fragt dann Kesselwerte wie Druck, Leistung oder
Volumenstrom im Weishaupt-Takt (40 s / 60 s / 5 min) selbst ab – nur lesend, siehe
[PROTOKOLL.md](PROTOKOLL.md#takt-der-eigenen-anfragen).

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

Home Assistant findet das Board per **MQTT-Discovery** automatisch
(Einstellungen → Geräte → MQTT). Mit dem Paket `homeassistant-api` stattdessen über die
ESPHome-Integration hinzufügen (Schlüssel = `api_key`).

Schalten per MQTT:

```
mosquitto_pub -t weact-can485-weishaupt/cmd/heizkreis  -m "Zeitprogramm 1"
mosquitto_pub -t weact-can485-weishaupt/cmd/warmwasser -m "Ein"
mosquitto_pub -t weact-can485-weishaupt/cmd/status     -m 1      # Betriebsarten einmal vom Bus lesen
```

Zwischen zwei Befehlen liegt mindestens eine Minute; spätere landen in der Warteschlange.

## 11. Handy-App (optional)

Auf einem Rechner mit Docker, der den Broker erreicht:

```
cd app
cp .env.example .env
```

`.env` ausfüllen:

| Schlüssel | Bedeutung |
|---|---|
| `MQTT_URL` | z. B. `mqtt://<broker>:1883` |
| `MQTT_USER`, `MQTT_PASS` | eigenes Konto, s. [README](README.md#einrichten-1) – nur lesen plus die zwei Schaltbefehle und `cmd/status` |
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
[CHANGELOG](CHANGELOG.md); die laufende Version zeigt die Weboberfläche unter *Firmware*.

## Fehlersuche

| Beobachtung | Ursache / Abhilfe |
|---|---|
| „Letzter CAN-Frame vor“ steigt immer weiter | Heizung aus, Kabel ab, H/L vertauscht; der Bus-Wächter startet das Board nach der eingestellten Zeit neu |
| Werte da, aber Rücklauf, Druck usw. bleiben leer | „Eigene CAN-Anfragen“ ist aus (Absicht beim Start) – s. Schritt 9 |
| Schaltbefehl endet mit „abgelehnt (CM=05)“ oder „keine Rückmeldung“ | JSON-Schnittstelle am WEM aus (10.8.1), falsche `wem_ip`, oder der WEM ist gesperrt – dann hilft nur, die Heizung kurz stromlos zu machen. Danach seltener schalten |
| OTA findet das Board nicht | `esphome upload weishaupt-wem-can.yaml --device <IP>` |
| Weboberfläche fragt nach Passwort | Benutzer `admin`, Passwort `web_password` aus `secrets.yaml` |
