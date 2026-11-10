# Ideen – was man mit dem Board noch bauen könnte

> 🇬🇧 English version: [IDEEN.en.md](IDEEN.en.md)

Nichts davon ist umgesetzt. Hier steht, was mit dieser Firmware und etwas Zusatzhardware denkbar
ist, wie aufwendig es wäre und wo die Haken liegen. Rückmeldungen und Versuche anderer sind
willkommen – bitte als [GitHub-Issue](https://github.com/jvejmelka/weishaupt-wem-can-esphome/issues).

Die Ideen gehen von der [Beispielanlage](ANLAGE.md) aus: Gas-Brennwert mit WEM, Trinkwasser-Wärmepumpe
am selben Speicher, PV mit Hauskraftwerk.

| Idee | Aufwand | Risiko | Einschätzung |
|---|---|---|---|
| [Warmwasser-Sperre per JSON](#1-warmwasser-sperre-per-json-h2-ersatz-ohne-kabel) | gering – nur Automation | gering | geht heute schon |
| [Warmwasser-Sperre über H2 mit Relais](#2-warmwasser-sperre-über-den-eingang-h2-mit-relais) | mittel – Elektrik im Kessel | gering | der „richtige“ Weg, unabhängig vom WEM |
| [Raumtemperatur aus eigenen Sensoren](#3-raumtemperatur-aus-eigenen-sensoren) | mittel | mittel | Auswerten sofort, Regeln nur eingeschränkt |
| [Weishaupt-Raumgerät per Funk (CAN über WLAN)](#4-weishaupt-raumgerät-per-funk-can-über-wlan) | hoch | hoch | technisch denkbar, experimentell |
| [Einzelraumregelung mit Heizkörperventilen](#5-einzelraumregelung-mit-funk-heizkörperventilen) | mittel bis hoch | gering | eigenständig, ergänzt die Heizung |
| [Anlage prüfen: ist alles richtig eingestellt?](#6-anlage-prüfen-ist-alles-richtig-eingestellt) | gering | keins | lohnt sich zuerst |
| [Eigene Lese-Regeln in Home Assistant oder Node-RED](#7-eigene-lese-regeln-in-home-assistant-oder-node-red) | gering | gering | geht heute schon |
| [Speichertemperatur oben](#8-speichertemperatur-oben) | gering bis mittel | gering | nur Näherungen, ohne Eingriff in den Speicher |
| [Wandgerät für rund 75 € – Ziel: RG-kompatibel](#9-wandgerät-für-rund-75---ziel-rg-kompatibel) | mittel (Anzeige) bis hoch (RG-Nachbau) | gering (Anzeige) bis hoch (RG-Nachbau) | Anzeige und Bedienung machbar, RG-Nachbau erst nach Mitschnitt |
| [Tragbares Raumgerät nur aus Handy-App und Funkfühlern](#10-tragbares-raumgerät-nur-aus-handy-app-und-funkfühlern) | hoch | hoch – das Board sendet dauernd auf den Bus | das Ziel hinter Idee 3, 4 und 9; Protokoll mitgeschnitten, noch nicht gebaut |

Die Ideen 3, 4, 9 und 10 sind verschiedene Wege zum selben Ziel, einem Raumwert im WEM. Sie stehen am Ende
nebeneinander: [Alle Wege zum Raumwert im Überblick](#alle-wege-zum-raumwert-im-überblick).

Zuerst aber, womit das alles steht und fällt: den Messdaten.

## 0. Alles mitschreiben: Datenbank und Grafana

Das Board liefert seine Werte per MQTT. Bei uns schreibt **Telegraf** sie in eine **InfluxDB**,
und **Grafana** zeigt sie als Verlauf – zusammen mit allem anderen, was im Haus gemessen wird:

| Quelle | Was |
|---|---|
| dieses Board | Kessel, Brenner, Warmwasser, Betriebsarten, Wärmemengen |
| Shelly-Steckdosen | Leistung der Wärmepumpe und anderer Verbraucher |
| Shelly- und Bluetooth-Sensoren | Temperatur und Feuchte in mehreren Räumen |
| Hauskraftwerk / Zähler | PV-Erzeugung, Batterie, Netzbezug, Einspeisung |
| Wetterdienst | Außentemperatur und Einstrahlung zum Gegenprüfen |

Erst der Verlauf macht Aussagen möglich, die ein Momentwert nie hergibt: wie oft der Brenner
startet, wie schnell der Speicher abkühlt (Beispiel in [ANLAGE.md](ANLAGE.md#gemessen-ein-nachmittag-und-eine-nacht)),
wann die Wärmepumpe läuft und ob die Räume bei der eingestellten Heizkurve warm werden.

Auch die **Regel-Ereignisse** des Boards (`<gerät>/verdacht/ereignis`) lassen sich so mitschreiben –
dann zeigt Grafana, wann eine Regel ausgelöst hat, was die Lesung ergab und wie oft eine
**ausgeschaltete** Regel angeschlagen hätte. Telegraf-Eingang und Abfrage dafür:
[REGELN.md, Abschnitt 8](REGELN.md#8-protokoll-log-und-ereignisse).

Ein Telegraf-Eingang für die Board-Werte sieht etwa so aus:

```toml
[[inputs.mqtt_consumer]]
  servers  = ["tcp://<broker>:1883"]
  topics   = ["weact-can485-weishaupt/sensor/+/state"]
  username = "telegraf"
  password = "${MQTT_PASS}"
  data_format = "value"
  data_type   = "string"      # Textsensoren liegen im selben Topic-Baum
  name_override = "heizung_can"
  [[inputs.mqtt_consumer.topic_parsing]]
    topic = "weact-can485-weishaupt/sensor/+/state"
    tags  = "_/_/messwert/_"
```

**Warum `data_type = "string"`:** ESPHome veröffentlicht Textsensoren (Betriebsart, Schaltstatus …)
**auch** unter `sensor/+/state`. Mit `float` wäre jeder davon ein Parsefehler im Log – bei uns rund
40 pro Minute. Deshalb als Text einlesen und nur Zahlen durchlassen:

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
        return None          # Text: verwerfen
    metric.fields["value"] = float(t)
    return metric
'''
```

> **Änderungen erst an einer Kopie prüfen.** Eine fehlerhafte Datei in `telegraf.d/` hält den
> ganzen Dienst an, nicht nur diesen Eingang:
> `telegraf --config /etc/telegraf/telegraf.conf --config-directory <kopie> --test`

### Alarme

Eine Datenfluss-Regel („kommen noch Werte?“) merkt einen toten Bus **nicht**: das Board sendet auch
dann weiter (Laufzeit, WLAN, „Letzter CAN-Frame vor“). Dafür eine eigene Regel auf das Alter des
letzten Frames, in Grafana mit Schwelle **> 300 s**:

```
from(bucket: "<bucket>")
  |> range(start: -15m)
  |> filter(fn: (r) => r._measurement == "heizung_can" and r.messwert == "letzter_can-frame_vor" and r._field == "value")
  |> last()
  |> group()
  |> keep(columns: ["_time", "_value"])
```

`group()` und `keep()` sind Pflicht: ohne sie bleiben die Tag-Spalten stehen, Grafana bekommt eine
„long series“ und die Regel steht dauerhaft im Fehler. `execErrState` auf **Error** setzen (nicht
Alerting) – sonst meldet ein Abfragefehler einen stillen Bus. Ist das Board selbst weg (keine
Daten), meldet das die Datenfluss-Regel.

## 1. Warmwasser-Sperre per JSON (H2-Ersatz ohne Kabel)

**Geht heute schon.** Eine Automation (Home Assistant, Node-RED, ein Skript) schickt
`cmd/warmwasser Aus`, wenn die Wärmepumpe laden soll, und `cmd/warmwasser Ein`, wenn das Gas
wieder dürfen soll.

**Eine feste Uhrzeit taugt dafür nicht** („tagsüber Gas aus, nachts an“): Mit Hausbatterie lädt die
Wärmepumpe gern auch nachts aus gespeichertem PV-Strom – in unserer Messung zweimal zwischen 21 und
5 Uhr. Sinnvolle Auslöser sind eher der **Speicher unten unter einer Schwelle** zusammen mit dem
**Ladezustand der Batterie** oder der PV-Prognose: Ist die Batterie leer und kommt keine Sonne, darf
das Gas; sonst nicht.

- **Vorteil:** keine Verdrahtung, alles über das Board, Kontrolle am Bus.
- **Haken:** es hängt an der empfindlichen JSON-Schnittstelle des WEM. Zwei Befehle am Tag sind
  unkritisch, eine Regelung im Minutentakt nicht (siehe
  [Wann sich der WEM aufhängt](PROTOKOLL.md#8-wann-sich-der-wem-aufhängt)).
- **Nötig?** In unserer Anlage nicht – die Fühlerlage trennt die Aufgaben schon
  ([Warum keine H2-Sperre nötig ist](ANLAGE.md#warum-keine-h2-sperre-nötig-ist)).

## 2. Warmwasser-Sperre über den Eingang H2 mit Relais

Der WTC hat einen **Eingang H2**, dessen Funktion sich in der Fachmann-Ebene einstellen lässt
(siehe Montageanleitung des WTC). Mit einem potenzialfreien Kontakt daran lässt sich zum Beispiel die
Warmwasserbereitung sperren – das ist der Weg, den Hersteller für Wärmepumpe plus Kessel vorsehen.

- **Wer schaltet den Kontakt?** Ein kleines Relais am Board (freier GPIO, Relaismodul mit
  galvanischer Trennung) oder ein eigenständiges Funk-Relais (z. B. Shelly mit potenzialfreiem
  Kontakt). Ansteuern könnte ihn der Energiemanager, Home Assistant oder das Board selbst.
- **Vorteil gegenüber Idee 1:** **kein einziger JSON-Befehl.** Der Kessel sieht nur einen Kontakt,
  der WEM wird nie gefragt. Das ist robust, auch wenn die JSON-Schnittstelle gerade gesperrt ist.
- **Kontrolle:** am Bus sieht man, ob der Kessel Warmwasser macht (Kesselstatus, Statusbits).
- **Haken:** Arbeit an der Kesselelektrik – Fachbetrieb, Heizung stromlos, Kontakt wirklich
  potenzialfrei. Welche H2-Funktion zu welchem Parameter gehört, vor dem Anschließen in der Anleitung
  des eigenen Geräts nachlesen.

## 3. Raumtemperatur aus eigenen Sensoren

Der Heizkreis läuft ohne Raumgerät rein **witterungsgeführt** nach Heizkurve. Die Raumaufschaltung im
WEM ist vorhanden, bekommt aber keinen Raumwert. Temperatur- und Feuchtesensoren in mehreren Räumen
(Shelly H&T per WLAN, Bluetooth-Sensoren über ein Shelly-Gateway) liefern diesen Wert – die Frage ist,
was man damit macht:

- **Auswerten – sofort möglich:** Raumtemperaturen neben Außentemperatur, Vorlauf und Brenner in
  Grafana. Daran sieht man, ob die Heizkurve passt (Räume zu warm = Kurve zu steil) und welcher Raum
  hinterherhinkt.
- **Grob regeln – mit dem Board möglich:** Betriebsart umschalten (z. B. Absenk, wenn alle Räume warm
  sind, Normal, wenn einer zu kalt wird). Das sind wenige Befehle am Tag, also im Rahmen.
- **Fein regeln – nicht ohne Weiteres:** den Raumwert so in den WEM zu bringen, als käme er von einem
  Weishaupt-Raumgerät, hieße, auf dem Bus einen fremden Teilnehmer vorzutäuschen. Das tut diese
  Firmware bewusst nicht – s. Idee 4.

**Über JSON geht das nicht.** Ein Raumgerät oder Raumfühler ist für den WEM ein eigener Teilnehmer am CAN-Bus: Es wird
vom WEM aufgenommen, sendet ein Lebenszeichen und liefert den Raumwert von sich aus
([VERSUCHE.md, 10a](VERSUCHE.md#10a-raumgerät-wem-rg2-am-bus-mitschnitt-november-2026)). Die JSON-Schnittstelle des WEM
beantwortet nur einzelne Anfragen nach seinen eigenen Werten – darüber kann sich kein Gerät anmelden und nichts laufend
senden, und regelmäßige Anfragen verträgt der WEM nicht. Einen JSON-Weg, dem WEM einen Raumwert zu liefern, kennen wir
nicht. Ein Board ohne CAN-Kabel (nur JSON) reicht also zum Abrufen von Werten und zum Schalten von Hand; wer ein
Raumgerät oder einen Raumfühler am WEM haben will – echt, über den Tunnel (Idee 4) oder nachgebildet (Idee 9, Stufe 2, und Idee 10) –,
braucht ein Board **am CAN-Bus**: das WeAct CAN485 oder, für Ethernet, die
[Olimex-Variante von cg089](OLIMEX-ESP32-EVB.md).

## 4. Weishaupt-Raumgerät per Funk (CAN über WLAN)

Weishaupts eigene Raumgeräte hängen per Kabel am selben CAN-Bus (Klemme H L − +, − und + versorgen
das Gerät). Die Idee: statt eines Kabels zwei Boards – eines am Kessel, eines am Raumgerät – die die
CAN-Telegramme über WLAN weiterreichen. Das Raumgerät wäre damit tragbar.

**Technisch denkbar, aber kein „Kabel durch WLAN ersetzen“:**

- Jeder Busabschnitt braucht eigene **Bestätigungen (ACK)** im CAN-Takt. Die Boards müssen deshalb
  als **Gateway** arbeiten: beide Seiten bestätigen lokal und reichen die Telegramme weiter. Das ist
  mit ESP32 und seinem CAN-Controller machbar.
- WLAN bringt **Verzögerung und Aussetzer**. PDOs und Heartbeats vertragen ein paar Dutzend
  Millisekunden, aber fällt die Verbindung aus, meldet der WEM das Raumgerät als verloren und fällt
  auf seinen Ersatzbetrieb zurück. Das muss man vorher an der eigenen Anlage testen.
- Das Raumgerät braucht am neuen Ort **eigene Versorgung** statt − und + vom Bus.
- Wer kein Raumgerät hat, müsste eines kaufen – ob sich das lohnt, hängt davon ab, was es gegenüber
  Idee 3 und 5 bringt.

**Risiko:** Hier werden Telegramme auf den Bus **geschrieben**, nicht nur gelesen – wenn auch nur
solche, die ein echtes Weishaupt-Gerät erzeugt hat. Fehler in der Weiterleitung können Knoten
verwirren. Deshalb nur als Experiment, mit Rückweg (Kabel oder Gerät abziehen) und nie auf Knoten 2.

### Gesucht: jemand mit Weishaupt-Raumgerät am Bus

> **Stand 08.11.2026:** Ein gebrauchtes WEM-RG2 2.1 läuft bei uns, auch über einen CAN-Tunnel (WLAN) vom Schreibtisch aus. Aufnahme, Lesen und Schreiben sind mitgeschnitten und entschlüsselt: [VERSUCHE.md, 10a](VERSUCHE.md#10a-raumgerät-wem-rg2-am-bus-mitschnitt-november-2026) und [10b](VERSUCHE.md#10b-can-tunnel-über-wlan-versuch-zweig-can-tunnel). Der Tunnel ist als Paket `can-tunnel` im Zweig `can-tunnel` einstellbar und beschrieben (zwei Boards, Adressen in `secrets.yaml`). Weitere Mitschnitte (RG1, WEM-RF, andere Stände, mehrere Raumgeräte, Raumfühler zugeordnet) bleiben willkommen.

Wir hatten bisher **kein** Raumgerät und können deshalb nicht sehen, was es auf dem Bus sendet. Genau das
wäre aber die Grundlage für diese Idee – und nebenbei für eine Raumaufschaltung aus eigenen Sensoren
(Idee 3). Wer ein Weishaupt-Raumgerät am CAN-Bus seines WEM hat (Bedieneinheit mit Raumfühler) und
dieses Board betreibt, könnte mit wenigen Mitschnitten helfen:

1. Rohmitschnitt einschalten (Schalter „CAN-Rohmitschnitt nach MQTT“, das Board hört nur zu)
   und die Frames aus `<gerät>/canraw` in eine Datei schreiben.
2. Einige Minuten **ohne** Eingriff mitschneiden – zeigt, was das Raumgerät regelmäßig sendet
   (Heartbeat, Raumtemperatur, Sollwert).
3. Am Raumgerät nacheinander **eine** Sache ändern, mit einer Minute Pause dazwischen, und die
   Uhrzeit notieren: Raumsollwert hoch/runter, Betriebsart umstellen.
4. Wenn möglich: Raumgerät kurz abziehen und wieder anstecken (Bootup, Anmeldung am WEM).
5. Die Hardware-/Softwarestände von Raumgerät und WEM notieren.

**Bitte keine Rohdateien öffentlich hochladen** – sie können Seriennummern und Gerätekennungen
enthalten. Besser: eine Zusammenfassung als [GitHub-Issue](https://github.com/jvejmelka/weishaupt-wem-can-esphome/issues)
(welche CAN-IDs das Raumgerät sendet, wie oft, welche Bytes sich beim Verstellen ändern) und für
Details Kontakt über das Issue. Die Auswertung folgt dem Verfahren „Scan und Differenz“ aus
[PROTOKOLL.md, Abschnitt 7](PROTOKOLL.md#7-neue-objekte-finden-scan-und-differenz).

## 5. Einzelraumregelung mit Funk-Heizkörperventilen

Funk-Thermostate an den Heizkörpern (z. B. Shelly BLU TRV, FRITZ!DECT, Homematic) regeln jeden Raum
selbst; die Heizung liefert weiter nach Heizkurve. Das Board braucht es dafür nicht – es liefert aber
den Kontext:

- Sind alle Ventile weit zu, obwohl es draußen kalt ist, ist die **Heizkurve zu hoch**.
- Ist ein Ventil dauernd ganz offen und der Raum trotzdem kalt, fehlt dort Leistung –
  **hydraulischer Abgleich** oder zu kleiner Heizkörper.
- Takt der Brenner häufig, während viele Ventile zu sind, fehlt Abnahme.

Später ließe sich daraus eine einfache Rückkopplung bauen: meldet ein Raum dauerhaft Bedarf,
Betriebsart oder Sollwert anheben – wieder mit wenigen Befehlen am Tag.

## 6. Anlage prüfen: ist alles richtig eingestellt?

Der unscheinbarste, aber nützlichste Punkt: mit den mitgeschriebenen Daten die Einstellungen der
Anlage überprüfen.

| Frage | Woran man es sieht |
|---|---|
| Taktet der Brenner? | Brennerstarts pro Stunde (Zähler im Board), kurze Laufzeiten |
| Passt die Heizkurve? | Raumtemperaturen gegen Außentemperatur, Vorlauf-Soll gegen Ist |
| Kondensiert das Gerät? | Rücklauftemperatur unter etwa 55 °C |
| Arbeitet das Gas gegen die Wärmepumpe? | Brenner im Warmwasserbetrieb, während die Wärmepumpe läuft |
| Wie teuer ist Warmwasser? | Wärmemengen vom Kessel gegen Strom der Wärmepumpe |

Viele dieser Fragen lassen sich schon heute beantworten, ohne irgendetwas zu schalten.

## 7. Eigene Lese-Regeln in Home Assistant oder Node-RED

Das Board kennt feste Regeln (R1–R5, einzeln schaltbar, ausgeschaltete im Schattenmodus) und einfache
eigene Regeln der Form „CAN-ID + Maske + Muster“ ([REGELN.md](REGELN.md)).
Was dafür zu verwickelt ist – mehrere Bedingungen, Uhrzeiten, Anwesenheit, Werte anderer Geräte –
lässt sich außerhalb bauen: eine Automation in Home Assistant oder ein Flow in Node-RED beobachtet
die MQTT-Werte des Boards (oder `<gerät>/canraw`, wenn der Rohmitschnitt an ist) und schickt bei
Verdacht **`<gerät>/cmd/status`**. Das Board liest dann Heizkreis- und Warmwasser-Betriebsart
einmal vom Bus (höchstens alle 10 s angenommen, nur Leseanfragen).

Beispiele: nach dem Heimkommen einmal lesen; wenn die Vorlauftemperatur des Heizkreises steigt,
obwohl Standby gemeldet ist; einmal morgens. Wichtig ist nur, dass die Automation selbst drosselt –
`cmd/status` ist für Einzelwünsche gebaut, nicht für ein Abfrageraster.

## 8. Speichertemperatur oben

Am Bus gibt es nur den Warmwasserfühler des Gasgeräts, und der sitzt in der
[Beispielanlage](ANLAGE.md) **unten** im Speicher. Oben misst nur die Trinkwasser-Wärmepumpe – sie hat
aber nur ein Display, keine Datenschnittstelle. Wie man trotzdem an „oben“ kommt, ohne den Speicher
zu öffnen:

| Weg | Wie | Haken |
|---|---|---|
| **Fühler auf der Warmwasser-Austrittsleitung** | Anlegefühler (z. B. DS18B20 am Board oder an einem Shelly mit Fühlereingang) direkt am Speicheraustritt auf das Rohr, mit Wärmeleitpaste, **unter** die Rohrdämmung | misst Rohr-, nicht Wassertemperatur. Direkt am Austritt folgt das Rohr dem Speicher recht gut; weiter weg kühlt es ohne Zapfung ab |
| **Fühler unter die Speicherdämmung** | wo sich die Verkleidung ohne Werkzeug öffnen lässt, einen flachen Anlegefühler auf die Speicherwand oben | nur wenn Verkleidung und Dämmung das zulassen; ein Funk-Sensor außen auf der Dämmung misst nur die Raumluft |
| **Zweiter Fühler in der vorhandenen Tauchhülse** | neben den Fühler der Wärmepumpe in dieselbe Hülse schieben | nur wenn Platz ist, braucht Öffnen der Verkleidung – Fachbetrieb |
| **Display abfotografieren** | kleine Kamera vor dem Display, Zahl per Texterkennung auslesen | aufwendig, lichtempfindlich, eher Spielerei |
| **Rechnen statt messen** | aus unten-Temperatur, Wärmepumpen-Leistung (Steckdose) und Laufzeit schätzen | nur eine grobe Näherung |

Für die meisten Fragen reicht die Temperatur unten zusammen mit „Wärmepumpe läuft“: unten kalt heißt,
dass viel gezapft wurde – und genau dann springt das Gas an.

## 9. Wandgerät für rund 75 € – Ziel: RG-kompatibel

Ein Weishaupt-Raumgerät kostet neu rund 300 €. Die Idee: Das **CAN-Board am Kessel** (dieses Projekt)
übernimmt die Busseite, im Raum hängt nur ein **WLAN-Touchpanel** – kein zweites CAN-Board, kein
Buskabel, keine 24 V im Zimmer. Wer gar nichts an der Wand haben will, nimmt statt des Panels das Handy:
[Idee 10](#10-tragbares-raumgerät-nur-aus-handy-app-und-funkfühlern).

```
Raum                                   Heizraum
┌──────────────────────┐   WLAN/MQTT   ┌───────────────────────┐   CAN   ┌─────┐
│ Touchpanel (ESP32-S3)│ ◄───────────► │ CAN-Board (dieses      │ ◄─────► │ WEM │
│ + Funk-Raumfühler    │               │ Projekt) am Kesselbus  │         └─────┘
└──────────────────────┘               └───────────────────────┘
```

| Teil | Wo | ca. Preis |
|---|---|---|
| CAN-Board (z. B. WeAct CAN485) | einmal am Kessel | 10–15 € |
| 4"-Touchpanel ESP32-S3, 480×480, „86-Box“-Format (z. B. Guition ESP32-4848S040) | je Raum | ~25 € |
| Bluetooth-Raumfühler (z. B. Shelly BLU H&T) – das Panel empfängt ihn selbst per BLE | je Raum | 15–20 € |
| USB-Netzteile | Kessel + je Panel | je 5–8 € |
| **erster Raum** | | **~60–75 €** |

Preise sind grobe Richtwerte (Stand 11/2026), nicht geprüft.

**Stufe 1 – Anzeige und Bedienung (machbar mit dem heutigen Stand):** Raumtemperatur und -feuchte,
Kessel-, Vorlauf- und Warmwasserstatus, Taupunkt/Schimmelwarnung; Betriebsart und Warmwasser
umschalten über den vorhandenen Weg der Firmware (CAN-Schalten mit Mindestabstand, s. README).
Display nachts aus, an bei Berührung.

**Stufe 2 – RG-kompatibel (Ziel, noch nicht belegt):** Das CAN-Board spielt gegenüber dem WEM ein
Raumgerät und liefert den Raumwert des Fühlers – echte Raumaufschaltung ohne Weishaupt-Gerät.
Das geht nur am CAN-Bus, nicht über JSON (s. Idee 3). Der Busteil ist derselbe wie in
[Idee 10](#10-tragbares-raumgerät-nur-aus-handy-app-und-funkfühlern); dort steht, was das Board nachbilden müsste,
was offen ist und was zur Sicherheit dazugehört. Für das Panel kommt dazu:

- der Fühler gehört **nicht** in das Panelgehäuse (Eigenwärme von Display und Chip, typ. +1–3 K);
- empfängt das Panel den Fühler, gilt bei seinem Ausfall dasselbe wie bei einem stummen Fühler: kein alter
  Raumwert auf den Bus (Idee 10, Sicherheit). Die Bedienung bleibt über die Handy-App möglich.

Beim Panel auf **ESP32-S3** achten (WLAN + BLE eingebaut); Varianten mit **ESP32-P4** haben kein eigenes
Funkmodul. Mit „ohne Bodenschale“ ist nur das Panel gemeint (Strom über USB-C); die Variante mit
Unterteil für 230 V gehört in die Hände einer Elektrofachkraft.

## 10. Tragbares Raumgerät nur aus Handy-App und Funkfühlern

Das ist das Ziel, auf das die Ideen 3, 4 und 9 hinauslaufen: ein Raumgerät, das es als Gerät gar nicht gibt.
Ein Weishaupt-Raumgerät besteht aus zwei Dingen – **Fühlern** (Raumtemperatur, Raumfeuchte) und einer
**Bedienoberfläche**. Beides lässt sich ersetzen:

- die Fühler durch **echte Raumfühler**, die dort stehen, wo gemessen werden soll – z. B. Shelly H&T (WLAN) oder
  Shelly BLU H&T (Bluetooth über ein Shelly-Gateway), ebenso jeder andere Sensor, der seinen Wert per MQTT liefert;
- die Oberfläche durch die **Handy-App** dieses Projekts, die ohnehin auf dem Board läuft.

Das **CAN-Board am Kessel** meldet sich beim WEM als Raumgerät und liefert ihm den Raumwert. An der Wand hängt nichts,
im Raum steht nur der Fühler, und tragbar ist das Ganze, weil das Handy die Bedienung ist.

```
Raum                                   Heizraum
┌──────────────────────┐   WLAN/MQTT   ┌───────────────────────┐   CAN   ┌─────┐
│ Funk-Raumfühler      │ ────────────► │ CAN-Board (dieses      │ ◄─────► │ WEM │
└──────────────────────┘               │ Projekt) am Kesselbus, │         └─────┘
┌──────────────────────┐     WLAN      │ meldet sich als        │
│ Handy mit der App    │ ◄───────────► │ Raumgerät              │
└──────────────────────┘               └───────────────────────┘
```

**Was man am Schluss braucht:**

| Teil | Wo | ca. Preis |
|---|---|---|
| CAN-Board (WeAct CAN485; für Ethernet die [Olimex-Variante von cg089](OLIMEX-ESP32-EVB.md)) | einmal am Kessel | 10–15 € |
| Funk-Raumfühler mit MQTT (z. B. Shelly H&T oder Shelly BLU H&T mit Gateway) | je Raum | ab ~15 € |
| USB-Netzteil | Kessel | 5–8 € |
| MQTT-Broker und WLAN | im Haus | meist vorhanden |
| Handy | – | vorhanden |

Preise sind grobe Richtwerte (Stand 11/2026), nicht geprüft. Kein Weishaupt-Raumgerät, kein zweites Board, kein Panel,
kein Buskabel ins Zimmer. Über JSON allein geht es nicht (s. Idee 3) – das eine Board muss am CAN-Bus hängen.

**Was davon schon steht (Stand 11/2026):**

- Das Protokoll eines echten WEM-RG2 ist mitgeschnitten: Aufnahme, Lebenszeichen, Raumwert, Lesen, Schreiben, Ausfall
  ([VERSUCHE.md, 10a](VERSUCHE.md#10a-raumgerät-wem-rg2-am-bus-mitschnitt-november-2026)). Das echte Raumgerät und der
  CAN-Tunnel (Idee 4) waren dafür nur das Mittel zum Abschauen.
- Die Handy-App läuft auf dem Board, schaltet Betriebsart und Warmwasser und zeigt fremde MQTT-Werte (z. B.
  Raumtemperaturen) als Zusatzanzeige (Pakete `app` und `zusatz`).
- **Nicht gebaut** ist das virtuelle Raumgerät selbst. Diese Firmware meldet sich bis heute nicht als Raumgerät und
  sendet keinen Raumwert.

**Was das Board nachbilden müsste** (alles aus dem Mitschnitt 10a):

- einen eigenen Knoten mit Lebenszeichen `0x704` und der Startfolge des RG2;
- die Antwort auf `0x1018/1` beim Start des WEM – darüber läuft die Aufnahme, bei uns erst nach einem Kaltstart von
  Therme und Raumgerät gemeinsam;
- die Quittung auf `0x20F0` (Geräteliste);
- den **Raumwert per PDO `0x184`**: Temperatur und Feuchte, bei einer Änderung frühestens nach 1 s, sonst etwa einmal
  pro Minute;
- für die Bedienung die Werte, die ein RG2 über `0x5E00` schreibt – Raumsoll Absenk/Normal/Komfort, Party, Urlaub,
  Heizkurve – und die es liest, etwa die Zeitprogramme. Betriebsart und Warmwasser schaltet die Firmware schon heute.

**Offen:**

- das **Menü-Paket**: Nach der Aufnahme schickt der WEM rund 1,6 MB an das Raumgerät (bei uns rund 25 Minuten). Ein
  Gerät ohne eigene Anzeige braucht den Inhalt nicht. Ob das Board die Übertragung annehmen und verwerfen muss oder
  ob der WEM sie einem Gerät ohne Bedienteil gar nicht schickt, ist nicht versucht;
- wie der WEM ein Raumgerät einem Heizkreis zuordnet, ob mehrere an einem Heizkreis gehen und was er mit dem Raumwert
  macht, sobald ein Raumfühler zugeordnet ist. In unserem Mitschnitt war keiner zugeordnet, der Raumwert änderte das
  Vorlaufsoll nicht;
- **welcher Raum zählt**: ein Führungsraum oder ein Mittel aus mehreren Fühlern. Das entscheidet das Board, der WEM
  sieht nur einen Wert je Raumgerät;
- die Bedeutung von Byte 4–5 im PDO `0x184`.

**Sicherheit – gehört von Anfang an dazu:**

- Ein nachgebautes Raumgerät **sendet dauernd**. Das widerspricht dem Grundsatz dieser Firmware (keine Schreibserien,
  nichts aus Regeln heraus). Deshalb nur als eigener, ab Werk **ausgeschalteter** Modus, nie an Knoten 2 (Kessel),
  mit Rückweg: Schalter aus = das Board ist am Bus wieder nur Zuhörer.
- **Kein veralteter Raumwert:** Ist der Fühler stumm (Batterie leer, WLAN oder MQTT weg), darf das Board den letzten
  Wert nicht weiterschicken. Nach einem Höchstalter verhält es sich wie „Raumgerät fehlt“ und stellt das
  Lebenszeichen ein. Der WEM meldet das nach 30 s als Warnung 1501 (gemessen); wie er dann mit zugeordnetem
  Raumfühler regelt, ist offen und muss vor dem Dauerbetrieb an der eigenen Anlage geprüft werden.
- **Grenzen für den Wert:** nur plausible Temperaturen und Feuchten weitergeben, Sprünge begrenzen – ein Fühler in der
  Sonne oder auf der Heizung darf die Anlage nicht aus dem Tritt bringen.
- **Bedienung von außen** nur so wie bei der App beschrieben (Login, von außen höchstens hinter einem Reverse Proxy
  mit TLS). Sollwerte bleiben in den Grenzen, die der WEM selbst vorgibt.
- Anders als beim CAN-Tunnel, wo das zweite Board nur unter Aufsicht läuft, wäre dieses Board **dauernd** am Bus aktiv.
  Umso wichtiger sind der Schalter, das Höchstalter und ein Versuchsbetrieb unter Aufsicht, bevor man es allein lässt.

### Alle Wege zum Raumwert im Überblick

| Weg | Im Raum | Am Kessel | Raumwert kommt beim WEM an | Stand 11/2026 |
|---|---|---|---|---|
| Weishaupt-Raumgerät am Kabel | Raumgerät, Buskabel | – | ja | Weishaupts eigener Weg; am WEM-RG2 mitgeschnitten ([10a](VERSUCHE.md#10a-raumgerät-wem-rg2-am-bus-mitschnitt-november-2026)) |
| Weishaupt-Raumgerät per Funk ([Idee 4](#4-weishaupt-raumgerät-per-funk-can-über-wlan)) | Raumgerät, zweites CAN-Board, eigene Versorgung | CAN-Board | ja | läuft als Versuch ([10b](VERSUCHE.md#10b-can-tunnel-über-wlan-versuch-zweig-can-tunnel)) |
| Eigene Fühler auswerten und grob regeln ([Idee 3](#3-raumtemperatur-aus-eigenen-sensoren)) | Funk-Raumfühler | CAN-Board (zum Regeln) | nein | geht heute |
| Wandgerät, Stufe 1 ([Idee 9](#9-wandgerät-für-rund-75---ziel-rg-kompatibel)) | Touchpanel, Funk-Raumfühler | CAN-Board | nein | machbar, nicht gebaut |
| Wandgerät, Stufe 2 ([Idee 9](#9-wandgerät-für-rund-75---ziel-rg-kompatibel)) | Touchpanel, Funk-Raumfühler | CAN-Board als Raumgerät | ja | nicht gebaut |
| Nur Handy-App und Funkfühler (Idee 10) | Funk-Raumfühler | CAN-Board als Raumgerät | ja | das Ziel; nicht gebaut |
| Funk-Heizkörperventile ([Idee 5](#5-einzelraumregelung-mit-funk-heizkörperventilen)) | Ventile mit eigenem Fühler | – | nein | eigenständig, ergänzt die Heizung |
| Nur JSON, ohne Board am CAN-Bus | – | – | nein | für Raumgerät oder Raumfühler kein Weg bekannt (Idee 3) |
