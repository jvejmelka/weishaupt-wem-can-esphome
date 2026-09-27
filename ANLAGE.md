# Beispielanlage – so läuft es bei uns

Die Firmware ist an genau einer Anlage entwickelt und gemessen worden. Damit du deine vergleichen
kannst, steht hier, wie sie aufgebaut ist – und was man deshalb am Bus sieht.

## Aufbau

| Teil | Gerät |
|---|---|
| Wärmeerzeuger | **Weishaupt Thermo Condens WTC-GW 15-B** (Gas-Brennwert) mit **WEM-Systemgerät** |
| Warmwasser | **Weishaupt WWP T 300 WA** – Trinkwasser-Wärmepumpe mit 300-l-Speicher und eingebautem Solar-Wärmetauscher (1,5 m²) |
| Anschluss Gas an den Speicher | Der **Solar-Wärmetauscher wird für den WTC genutzt**: Die Leitungen der Gasheizung hängen an den Solaranschlüssen des Speichers. So laden Wärmepumpe und Gas denselben Speicher |
| Warmwasser-Modul im WEM | WW0 im Systemgerät (MI 03), **kein** separates EM-WW |
| Strom | PV-Anlage **19,4 kWp** mit Hauskraftwerk **E3/DC S10 E** |
| Batterie | im E3/DC, **13 kWh** (2 × LG-Batteriemodul). Tagsüber wird sie aus PV geladen, abends und nachts versorgt sie das Haus – auch die Wärmepumpe |
| Wallbox | **E3/DC Wallbox easy connect**, 11 kW, vom E3/DC gesteuert – derzeit ohne E-Auto, also ohne Verbrauch |
| Ansteuerung Wärmepumpe | **SG-Ready**, direkt vom E3/DC. Ohne Überschuss meldet das E3/DC Betriebszustand 2 (Normalbetrieb): Die Wärmepumpe regelt nach ihrem eigenen Warmwasser-Sollwert – deshalb die Läufe nachts in der Messung unten. Nachts kommt ihr Strom meist aus der **Hausbatterie**, also aus gespeichertem PV-Strom. Bei PV-Überschuss Betriebszustand 3: Die Wärmepumpe hebt den Sollwert an – je nach Entscheidung des E3/DC schrittweise **bis 65 °C**. Beschrieben in der [Anleitung der WWP T 300 WA (Weishaupt, PDF)](https://www.weishaupt.de/uploads/tx_weishaupt_documents/documents/83297701.pdf), Abschnitt Smart-Grid-Funktion, und in der [SG-Ready-Dokumentation von E3/DC (PDF)](https://community.viessmann.de/viessmann/attachments/viessmann/customers-heatpump-hybrid/63997/1/SG-Ready-Dokumentation_V1.70_2022-02-08.pdf) |
| Strommessung Wärmepumpe | **Shelly Plug M Gen3** vor der Wärmepumpe, Leistung per MQTT |
| Mithörer am Bus | **WeAct CAN485 DevBoard V1** mit dieser Firmware |

**Keine Sperre über den Eingang H2 des WTC.** Gasgerät und Wärmepumpe wissen nichts voneinander.
Nötig ist das auch nicht – die Aufteilung ergibt sich aus Sollwerten und Fühlerlage, s. unten
[Warum keine H2-Sperre nötig ist](#warum-keine-h2-sperre-nötig-ist).

## Sollwerte

| Wer | Einstellung | Wert |
|---|---|---|
| Wärmepumpe, Normalbetrieb (SG-Ready Zustand 2) | Warmwasser-Sollwert, Fühler oben im Speicher | **50 °C** |
| Wärmepumpe, erhöhter Betrieb (Zustand 3) | Sollwert + SG-Erhöhung (Werk 5 K, einstellbar 0–20 K) | 55 °C |
| Wärmepumpe, Zwangsbetrieb (Zustand 4, vom E3/DC nach Überschuss) | | **bis 65 °C** |
| Gas (WEM), Warmwasser-Soll Normal und Absenk | Knoten 1 `0x2A39/2`, `0x2A38/2`, Fühler **unten** im Speicher | **35 °C** |
| Gas, Überhöhung beim Laden | Kessel-Vorlaufsoll = WW-Soll + 15 K | 50 °C |
| Gas, Warmwasser-Betriebsart **Aus** | WW-Soll aktuell `0x2A2C/2` (Frostschutz) | 8 °C |

Die Gasheizung ist also bewusst nur **Vorwärmung** (Weishaupt empfiehlt dafür 30–35 °C): Sie hält
den unteren Teil des Speichers auf 35 °C, den Rest macht die Wärmepumpe.

## Was man deshalb am Bus sieht

Der Warmwasserfühler des WTC steckt im selben Speicher, und zwar **unten**. Die Speichertemperatur
aus dem PDO `0x241` (siehe [PROTOKOLL.md](PROTOKOLL.md)) zeigt also den Ladezustand des unteren
Speicherteils, egal wer geladen hat. Oben ist es wärmer – die Wärmepumpe regelt auf ihren eigenen
Fühler oben.

Deutlich wird das, wenn die Warmwasser-Betriebsart am WEM auf **Aus** steht (Knoten 1 `0x2A20/2` = 2)
und der Speicher trotzdem warm ist:

| Anzeige | Wert |
|---|---|
| Warmwasser-Betriebsart (Vorgabe und Ist) | Aus |
| Speichertemperatur | 42,5 °C |
| Brenner | Aus |

Geheizt hat hier die Wärmepumpe; der Gasbrenner war nicht beteiligt.

## Gemessen: ein Nachmittag und eine Nacht

Speichertemperatur vom Bus, Leistung der Wärmepumpe vom Shelly, Brennerphase vom Bus
(Freitag 16:00 bis Samstag 08:00, Spätsommer, Heizkreis in Standby):

| Zeit | Wer heizt | Leistung | Speicher |
|---|---|---|---|
| 16:49–18:04 | Wärmepumpe | ~530 W | 28,8 → 37,2 °C |
| 18:15, 19:30 | – (Zapfungen) | – | 37,2 → 32,2 °C, 30,3 → 23,6 °C |
| 20:16–20:42 | **Gas** (WW-Betriebsart Ein) | – | 23,8 → 37,0 °C in 26 min |
| 21:47–22:15 | Wärmepumpe | ~530 W | 37,0 → 38,9 °C |
| 04:31–05:08 | Wärmepumpe | ~530 W | 38,6 → 40,8 °C |
| 07:12–07:46 | Wärmepumpe | ~530 W | 40,5 → 42,5 °C |

Was daraus folgt:

- **Der Speicher hält die Wärme sehr gut:** nachts ohne Zapfung nur etwa **0,05–0,1 K/h** Verlust.
  Die großen Einbrüche kommen vom Zapfen, nicht vom Stehen.
- **Die Wärmepumpe braucht Zeit:** rund 7 K pro Stunde bei etwa 0,5 kW. Das Gas schafft 13 K in
  26 Minuten.
- Der Temperatursensor meldet gelegentlich einen **Fehlerwert (−3276,8 °C)**. Wer eine Automation
  darauf baut, muss solche Werte verwerfen.

## Warum keine H2-Sperre nötig ist

Die übliche Lösung, damit das Gas nicht gegen die Wärmepumpe arbeitet, ist eine Sperre des
Warmwasserbetriebs über den Eingang H2 des WTC (z. B. per Schütz, gesteuert vom Energiemanager).
Hier geht es ohne, weil **Sollwerte und Fühlerlage die Aufgaben schon trennen:**

- Die Wärmepumpe regelt auf ihren Fühler **oben** und hält dort 50 °C, bei PV-Überschuss mehr.
- Das Gas regelt auf seinen Fühler **unten** und springt erst an, wenn es dort unter 35 °C fällt.
- Solange die Wärmepumpe nachlädt, wird es unten nie so kalt – das Gas bleibt aus.
- Kalt wird es unten nur, wenn **viel Wasser gezapft** wird oder die **Wärmepumpe nicht nachlädt**.
  Genau dann soll das Gas einspringen.

Die Messung oben zeigt das: Über die ganze Nacht und den Vormittag lief das Gas **einmal** für
Warmwasser – nach zwei großen Zapfungen, als der Speicher unten auf 23,6 °C gefallen war.

### Was das Board zusätzlich kann (nicht umgesetzt)

Wer die Wärmepumpe noch stärker bevorzugen will, kann die Warmwasser-Betriebsart am WEM tagsüber
per `cmd/warmwasser Aus` sperren und nur abends oder unter einer Schwelle wieder freigeben – das Board
wird damit zum H2-Ersatz ohne Verdrahtung. Nötig ist das bei dieser Einstellung nicht.

Dabei gelten dieselben Regeln wie überall: höchstens ein Schaltbefehl pro Minute, besser deutlich
seltener – zweimal am Tag ist für den WEM unbedenklich, zwanzigmal nicht
(siehe [Wann sich der WEM aufhängt](PROTOKOLL.md#8-wann-sich-der-wem-aufhängt)). Und Fehlerwerte des
Temperaturfühlers verwerfen, bevor eine Schwelle darauf reagiert.
