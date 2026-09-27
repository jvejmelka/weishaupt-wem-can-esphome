# Ideen – was man mit dem Board noch bauen könnte

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

Zahlen und Texte kommen dabei gemischt an. Wer nur Zahlen speichern will, filtert die Texte mit
einem Prozessor heraus oder wandelt sie in Grafana um.

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

Wir haben **kein** Raumgerät und können deshalb nicht sehen, was es auf dem Bus sendet. Genau das
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

Das Board kennt feste Regeln (R1–R4, einzeln schaltbar) und einfache eigene Regeln der Form „CAN-ID +
Maske + Muster“ ([PROTOKOLL.md, Abschnitt 6a](PROTOKOLL.md#6a-regeln-betriebsarten-bei-verdacht-lesen)).
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
