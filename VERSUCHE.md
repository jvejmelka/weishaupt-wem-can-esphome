# Versuche: Raumgerät WEM-RG2 und CAN-Tunnel

> 🇬🇧 English version: [VERSUCHE.en.md](VERSUCHE.en.md)

Mitschnitte und Versuche an der gemessenen Anlage (WTC-GW 15-B mit WEM-Systemgerät), die über den Alltag der
Firmware hinausgehen: wie sich ein Raumgerät WEM-RG2 am Bus anmeldet, liest und schreibt, und der CAN-Tunnel über
WLAN, mit dem das mitgeschnitten wurde. Der Tunnel liegt im Zweig `can-tunnel` und ist in `main` nicht enthalten.
Die Grundlagen zum Bus stehen in [PROTOKOLL.md](PROTOKOLL.md).

Die Abschnitte standen bis November 2026 in PROTOKOLL.md und behalten ihre Nummern von dort (10a, 10b), damit
ältere Verweise weiter stimmen.

- [10a. Raumgerät WEM-RG2 am Bus (Mitschnitt, November 2026)](#10a-raumgerät-wem-rg2-am-bus-mitschnitt-november-2026)
- [10b. CAN-Tunnel über WLAN (Versuch, Zweig `can-tunnel`)](#10b-can-tunnel-über-wlan-versuch-zweig-can-tunnel)

## 10a. Raumgerät WEM-RG2 am Bus (Mitschnitt, November 2026)

Mitgeschnitten an einer Anlage mit WTC-GW 15-B und einem gebrauchten WEM-RG2 2.1 (Software
453.210). Zuerst nur lesend am Bus; am 08.11.2026 lief das RG2 über den CAN-Tunnel (Abschnitt 10b), und am RG2
wurden gezielt Werte verstellt und wieder zurückgestellt, um die Schreibseite zu sehen. Das Board selbst hat dabei
nichts geschrieben, es hat nur die Frames des RG2 weitergereicht.

### Aufnahme in die Anlage

Das RG2 meldete sich als Knoten 4 (`0x704`, SDO `0x604`/`0x584`, Kanal zum WEM `0x6C4`/`0x684`).
Es braucht DC-Versorgung (Anleitung 15–30 V, Aufdruck am Sockel 12–26 V, höchstens 0,7 W) und zeigt ohne
Bus „Waiting for commissioning ready“. Die Aufnahme lief **erst nach einem Kaltstart von Therme und RG2
gemeinsam**: Der WEM fragt `0x1018/1`, startet nach „System hat neue Geräte erkannt“ den Assistenten, und danach steht
Knoten 4 mit seinen Modulen (`TERMINAL1`, `GROUND2`) in der `systable.csv`.

Läuft das RG2 bei einem Neustart des WEM einfach weiter, antwortete es bei uns nicht auf diese Abfrage und
verlor seine Knotenadresse (LSS-Abfrage danach: `FF`). Das war ein einzelner Versuch über den CAN-Tunnel (10b);
die Ursache ist nicht bewiesen.

Nach der Aufnahme überträgt der WEM ein **Menü-Paket von rund 1,6 MB** per SDO-Block auf `0x20F4/0`
(`604: C6 F4 20 00 00 10 00 00`, Blöcke zu 4096 Byte) mit Texten in vielen Sprachen und Parametertabellen. Das
dauerte rund 25 Minuten bei bis zu 16.000 Telegrammen pro Minute. Der Inhalt gehört Weishaupt und steht deshalb nicht hier.

### Lesen über 0x5E00

Das RG2 bestellt Werte mit einem geteilten SDO-Schreiben auf `0x5E00/4`, der WEM liefert sie mit einem geteilten
SDO-Schreiben auf `0x5E00/1`:

```
6C4 → WEM   Bestellung   01 MI MX OXlo OXhi OS 04 <Zähler> 00
604 → RG2   Lieferung    02 MI MX OXlo OXhi OS 00 xx xx <Wert>
```

Der Wert ist 1, 2 oder 4 Byte (little endian) oder eine Zeichenkette. **Dieselbe Objektnummer bedeutet je MI etwas anderes**:
`0x253E` ist bei MI 2 das Zeitprogramm 1, bei MI 9 die Statistik.

| MI | Objekt/Sub | Bedeutung | Skalierung | Geprüft gegen |
|---|---|---|---|---|
| 1 | `0x2562`/2, 3 | Uhrzeit Stunde, Minute | 1 | RG2 |
| 1 | `0x2563`/2–5 | Jahr, Monat, Tag, Wochentag | 1 | RG2 |
| 1 | `0x26B1`/2 | Außentemperatur | 0,1 °C | Portal |
| 1 | `0x27B9`/2, `0x27BA`/2 | Außentemperatur Minimum, Maximum (ein Abgleich) | 0,1 °C | Portal |
| 2 | `0x2507`/2 | Vorlauf Heizkreis | 0,1 °C | Portal |
| 2 | `0x2533`/2 | Betriebsart (2 = Zeitprogramm 1) | 1 | RG2 |
| 2 | `0x2539`, `0x253A`, `0x253B` | Raumsoll Absenk, Normal, Komfort; Sub 2 Wert, 3 Min, 4 Max | 0,1 °C | RG2, Portal |
| 2 | `0x254B`/2–4 | Heizkurve Steilheit, Min, Max | 0,1 | Portal |
| 2 | `0x25AD`/2–4 | Heizkurve Parallelverschiebung, Min, Max | 0,1 K | Portal |
| 2 | `0x257B`/2–4 | So/Wi-Grenze, Min, Max | 0,1 °C | Portal |
| 2 | `0x2582`/2 | So/Wi-Funktion | 0/1 | Portal |
| 2 | `0x2104`/4, 3: `0x2105`/4 | Modulname (`\HZK0`, `\WW0`) | Text | RG2 |
| 3 | `0x2529`/2 | Warmwasser | 0,1 °C | Portal |

**Zeitprogramme (MI 2):** `0x253E`, `0x253F`, `0x2551` für Programm 1, 2, 3. Sub 2 = `0x0F`; je Tag ein Block von 16 Subs,
Montag ab Sub 4 (Sub 3 Kennwert `01`), jeder weitere Tag 16 höher, bis zu 10 Einträge. Jeder Eintrag ist 4 Byte: das untere
Wort ist `Niveau << 12 | Startminute` mit Niveau 1 Absenk, 2 Normal, 3 Komfort. Das obere Wort war bei uns immer `0x3188`,
seine Bedeutung ist offen. Beispiel: `0x2168` = Normal ab 06:00 (360 min), `0x1537` = Absenk ab 22:15. Das RG2 holt im
Betrieb den heutigen Tag.

**Statistik (MI 9, `0x253E`):** 4 Byte, Einheit 0,01 kWh, „Energie gesamt“: Sub 26–37 Tage, Sub 38–49 Monate, Sub 50–54 Jahre, jeweils
neueste zuerst. Die Tage waren kurz nach Mitternacht noch nicht weitergerückt. Summen passen auf 0,01 kWh: Monat = Summe der Tage,
Jahr = Summe der Monate plus laufender Vortag. **Diese Wärmemengen lagen bei uns über mehrere Jahre 20–50 % über dem Gaszähler
(Hs)**, also physikalisch zu hoch; sie taugen nur zum Vergleich untereinander. Nach einem Neustart der Therme ist der laufende Tag
weitgehend verloren (These aus einem Fall).

### PDO 0x184

Das RG2 sendet von sich aus, ohne Anfrage: bei einer Änderung, frühestens nach 1 s, sonst etwa einmal pro Minute.
Byte 0–1 Raumtemperatur (0,1 °C, mit Vorzeichen), Byte 2–3 Raumfeuchte (0,1 %), Byte 4–5 meist `0000`, zeitweise
veränderlich (nach einem Start zum Beispiel von `0x0352` in Schritten von 50 fallend, sonst langsam steigend und fallend);
die Bedeutung ist offen.

Die **Korrektur Raumfühler** im RG2-Menü wirkt auf den gesendeten Wert (−0,5 K → Byte 0–1 um 5 kleiner), die Feuchte wird
dabei auf die korrigierte Temperatur umgerechnet. Sie geht nicht über den Bus, ebenso wenig Helligkeit, Nachtmodus,
Grenzwerte und Leuchtring. Ohne Korrektur lag die Temperatur des gebrauchten RG2 2–4 K über einem Vergleichsfühler im
selben Raum; in einer geschlossenen Kunststoffbox zusammen mit Board und Spannungswandler stieg die Abweichung langsam
(Eigenerwärmung vermutet). Ist im WEM kein Raumfühler zugeordnet, regelt er weiter nur nach der Außentemperatur – der
Raumwert ändert dann das Vorlaufsoll nicht.

### Schreiben über 0x5E00

Ein geänderter Wert geht denselben Weg wie eine Bestellung, nur mit Kennbyte `03`; der WEM quittiert mit `04`:

```
6C4 → WEM   Auftrag     03 MI MX OXlo OXhi OS 04 <Zähler> 00 <Wert>
604 → RG2   Quittung    04 MI MX OXlo OXhi OS 00 00 00
```

Kennbytes: `01` Lesen, `02` Lieferung, `03` Schreiben, `04` Schreiben bestätigt. Der Wert ist little endian, 1 oder 2 Byte
je nach Objekt (Auftrag 10 oder 11 Byte). Beispiel Raumsoll Normal auf 20,5 °C:
`03 02 00 3A 25 02 04 00 00 CD 00` → Quittung `04 02 00 3A 25 02 00 00 00`.

Ein Bestätigen am RG2 ergibt **einen** Auftrag mit dem Endwert; Zwischenschritte beim Drehen werden nicht geschrieben.
Party und Urlaub schreiben beim Bestätigen den ganzen Satz hintereinander; der Zähler zählt dabei bis `00` herunter, der
letzte Auftrag ist die Funktion selbst.

| MI | Objekt/Sub | Bedeutung | Werte |
|---|---|---|---|
| 2 | `0x2539`/2, `0x253A`/2, `0x253B`/2 | Raumsoll Absenk, Normal, Komfort | 0,1 °C; Grenzen verschachtelt (Sub 3/4 = Nachbarwert), Absenk ab −100,0, Komfort bis 300,0 |
| 2 | `0x2533`/2 | Betriebsart Heizkreis | 1 Byte; 2 = Zeitprogramm 1, 3 = Zeitprogramm 2 |
| 2 | `0x254B`/2 | Heizkurve Steilheit | 0,1; 100–250 (je Heizkreistyp, siehe unten) |
| 2 | `0x25AD`/2 | Heizkurve Parallelverschiebung | 0,1 K; −500…500. Das RG2 stellt in 0,5-K-Schritten, die Anzeige rundet auf ganze K (5 → „1 K“) |
| 2 | `0x2587`/2 | Party/Heizpause Funktion | 0 Aus, 1 Ein |
| 2 | `0x2589`/2 | Party Raumsoll-Stufe | 1 Absenk, 3 Komfort |
| 2 | `0x2593`/2, /3 | Party Beginn Stunde, Minute | 1 |
| 2 | `0x2588`/2, /3 | Party Ende Stunde, Minute | 1 |
| 2 | `0x258A`/2 | Urlaub Funktion | 0 Aus, 1 Ein |
| 2 | `0x2586`/2 | Urlaub Raumsoll | 0 Frost, 1 Absenk |
| 2 | `0x2584`/2–4, `0x2585`/2–4 | Urlaub Beginn, Ende | Jahr − 2000, Monat, Tag |
| 3 | `0x2539`/2 | Warmwasser-Soll Normal | 0,1 °C |
| 3 | `0x2520`/2 | Betriebsart Warmwasser | 1 Ein, 2 Aus |
| 3 | `0x2549`/2 | Warmwasser-Push | 1 = auslösen; ob der WEM den Wert zurücksetzt, ist offen |

Jeder Wert wurde am RG2 verstellt, an der Quittung des WEM am Bus kontrolliert (Betriebsarten zusätzlich an der
Anzeige der Firmware) und danach zurückgestellt. `0x2539` ist bei MI 2 das Raumsoll Absenk, bei MI 3 das Warmwasser-Soll. Das Party-Menü schlägt als
Beginn die abgerundete Uhrzeit und als Ende drei Stunden später vor; im WEM kann anderes gespeichert sein.

**Die Grenzen der Steilheit hängen vom Heizkreistyp ab** – Hinweis von
[ChristophKomenda](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/issues/8#issuecomment-6763314919): Nach der Weishaupt-Unterlage
gilt für „Universal“ 1,5–40,0, für „Heizkörper 60“ 8,0–20,0 und für „Heizkörper 70“ 10,0–25,0. Die hier gelesenen Grenzen
100–250 (`0x254B`/3 und /4) passen zu „Heizkörper 70“; welcher Typ am gemessenen WEM eingestellt ist, ist noch nicht
nachgesehen. An seiner WTC-GW 15-B (Heizkreistyp „Universal“, Steilheit 8,5) hat er beide Objekte über die
JSON-Schnittstelle gelesen und [mit der Anzeige am Gerät verglichen](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/issues/8#issuecomment-6708250969):
`02 00 254B 02` (Steilheit) und `02 00 25AD 02` (Parallelverschiebung, dreimal hin und zurück verstellt). Die Objekte
sind damit an zwei Anlagen belegt, die Grenzen gelten je Einstellung.

**Warmwasser-Programm (MI 3, `0x2580`):** gleicher Aufbau wie die Heiz-Zeitprogramme, das obere Wort war hier `0x3288`.
Die Kennwerte vor jedem Tagesblock (Sub 3, 19, 35, …) waren `01` für die Gruppe Montag–Freitag und `02` für Samstag/Sonntag;
das RG2 las nur den Montags- (Sub 4–13) und den Samstagsblock (Sub 84–93).

### Geräteliste, Ausfall, Neustart

- **Geräteliste** am Therme-Display öffnen: Der WEM schreibt jedes Mal `604: 23 F0 20 01 01 00 00 04` (`0x20F0`/1 =
  `0x04000001`), das RG2 quittiert, und sein Leuchtring blinkt grün (Deutung: Gerät identifizieren). Dazu sendet der WEM
  `011: 01 F0 20 00 00 00 00 04`.
- **Start des RG2:** Es sendet `011: 01 F0 20 00 00 00 00 1C` und läuft `704:00` → `704:7F` → `704:05` in rund 20 s hoch.
  Bei laufendem WEM behielt es seinen Knoten 4 ohne Eingriff – direkt am Bus wie über den CAN-Tunnel.
- **Ausfall des RG2:** 30 s nach dem letzten Lebenszeichen meldet der WEM `081: FF FF 01 02 05 DD 01 00` (`0x05DD` = **Warnung 1501**, Verbindung zu RG2 Nr. 1 unterbrochen, s. [PROTOKOLL.md, Abschnitt 10c](PROTOKOLL.md#10c-störmeldungen-am-bus-und-fehlercodes))
  und löscht die Meldung, sobald das RG2 wieder da ist. In der Geräteliste steht es dann unter „Nicht aktive Geräte“.

## 10b. CAN-Tunnel über WLAN (Versuch, Zweig `can-tunnel`)

> **Abgrenzung:** Das Projekt als Ganzes ist die „ESP-Brücke“ – ein Board vermittelt zwischen CAN-Bus und Heimnetz.
> Der **CAN-Tunnel** ist etwas anderes: Zwei Boards verlängern den CAN-Bus selbst über das WLAN.

Zwei Boards dieses Projekts verbinden zwei CAN-Busse über das WLAN: Board A hängt an der Therme am echten Bus, Board B
steht mit dem Raumgerät am Schreibtisch und bildet mit ihm einen kleinen eigenen Bus. So lässt sich ein RG2 bedienen und
mitschneiden, ohne ein Kabel in den Heizraum zu ziehen. Das RG2 braucht dort eine eigene Versorgung (DC 15–30 V, bei uns
ein Aufwärtswandler auf 24 V).

> **Achtung – erst lesen, dann einschalten.** Der Tunnel **filtert nicht**: Alles, was das Gerät am anderen Ende
> sendet, kommt am Heizungsbus an, auch Schreibaufträge. Die UDP-Pakete sind unverschlüsselt und nur über die
> Absenderadresse gesichert. Deshalb: nur für Versuche, nur im eigenen Heimnetz, nur unter Aufsicht – und Board B
> stromlos machen, wenn niemand dabei ist. In `main` und in den fertigen Firmware-Dateien ist der Tunnel nicht enthalten.

**Warum CAN und nicht JSON.** Ein Raumgerät oder Raumfühler ist für den WEM ein eigener Teilnehmer am CAN-Bus: Es wird
vom WEM aufgenommen, sendet ein Lebenszeichen und liefert den Raumwert von sich aus (Abschnitt 10a). Die
JSON-Schnittstelle des WEM beantwortet nur einzelne Anfragen nach seinen eigenen Werten – darüber kann sich kein Gerät
anmelden und nichts laufend senden, und regelmäßige Anfragen verträgt der WEM nicht. Einen JSON-Weg, dem WEM einen
Raumwert zu liefern, kennen wir nicht. Wer ein Raumgerät oder einen Raumfühler am WEM haben will – echt, über den
Tunnel oder später nachgebildet –, braucht deshalb ein Board **am CAN-Bus** (WeAct CAN485; für Ethernet die
[Olimex-Variante von cg089](OLIMEX-ESP32-EVB.md)). Ein Board ohne CAN-Kabel reicht dafür nicht.

**Was man braucht.**
- Zwei Boards mit der Firmware aus dem Zweig `can-tunnel`, beide im selben WLAN. Erprobt ist der Tunnel nur mit zwei
  WeAct CAN485; das Paket setzt WLAN voraus (Paket `netz-wlan`).
- Für beide Boards eine **feste Adresse** (Reservierung im Router oder Paket `feste-ip`): Jedes Board nimmt nur Pakete
  von der eingetragenen Adresse des anderen an.
- Am Raumgerät: eigene Versorgung (s. oben) und ein Busabschluss am kleinen Bus – Schalter „120 Ω CAN“ an Board B
  **ein** (Ende eines eigenen kurzen Busses), an Board A wie immer **aus** ([INSTALL.md](INSTALL.md)).
- Auf Wunsch ein Rechner im Heimnetz, der den Mitschnitt annimmt.

**Einrichten** – für beide Boards, jeweils mit der Adresse des anderen:

1. In `secrets.yaml` die Adressen eintragen (Vorlage: `secrets.yaml.example`). Sie stehen nur dort, nicht in der
   Hauptdatei:
   ```yaml
   tunnel_ziel: "192.0.2.54"   # IP des ANDEREN Boards (Beispieladresse)
   tunnel_log: "192.0.2.30"    # nur für den Mitschnitt: IP des Rechners
   ```
2. In `weishaupt-wem-can.yaml` die vorbereiteten Zeilen einkommentieren und anpassen:
   ```yaml
   substitutions:
     tunnel_ziel: !secret tunnel_ziel
     tunnel_log: !secret tunnel_log       # Zeile weglassen = kein Mitschnitt
     tunnel_name: "A"                     # am anderen Board "B"
     tunnel_nur_bei_bus: "true"           # am Board beim Raumgerät "false"

   packages:
     can_tunnel: !include pakete/can-tunnel.yaml
   ```
3. Bauen und flashen wie in [INSTALL.md](INSTALL.md).
4. An **beiden** Boards in der Weboberfläche den Schalter „CAN-Tunnel (Experten)“ einschalten. Ausschalten an einem
   der beiden trennt die Verbindung.

| Einstellung | Vorgabe | Bedeutung |
|---|---|---|
| `tunnel_ziel` | – (Pflicht) | IPv4-Adresse des anderen Boards. Fehlt sie oder ist sie keine Adresse, bricht der Bau mit einer Meldung ab. |
| `tunnel_log` | `""` | Adresse eines Rechners für den Mitschnitt; leer = kein Mitschnitt |
| `tunnel_name` | `"A"` | Kürzel dieses Boards im Mitschnitt und im Log |
| `tunnel_nur_bei_bus` | `"true"` | `"true"`: nur auf den Bus senden, wenn in den letzten 3 s etwas vom Bus kam (Board an der Therme). `"false"`: immer senden (Board am Raumgerät). |
| `tunnel_selbst_ein` | `"false"` | `"false"`: Der Tunnel ist nach jedem Neustart aus. `"true"`: Er schaltet sich nach jedem Neustart einmal selbst ein, sobald das WLAN steht. |

Die Einstellungen stehen fest in der Firmware; zur Laufzeit lässt sich nur der Schalter bedienen. Die Vorgaben und ihre
Beschreibung stehen im Kopf von [`pakete/can-tunnel.yaml`](https://github.com/jvejmelka/weishaupt-wem-can-esphome/blob/can-tunnel/pakete/can-tunnel.yaml), die UDP-Ports in
[`can_tunnel.h`](https://github.com/jvejmelka/weishaupt-wem-can-esphome/blob/can-tunnel/components/weishaupt_can/can_tunnel.h) (wer sie ändert, baut beide Boards neu).

**Ablauf.** Jedes Board schickt jeden Frame, den es auf seinem eigenen Bus empfängt, per UDP (Port 47011) an die
eingetragene Gegenstelle und sendet, was von dort kommt, auf seinen Bus. Angenommen werden nur Pakete von der Adresse der
Gegenstelle. Ein Paket ist `'W' 'B' Folge id_lo id_hi(+0x80 = RTR) Länge d0…d7`; aus der Folgenummer zählt das Board
verlorene Pakete. Der Rundlauf Bestellung → Lieferung lag bei 25–35 ms.

**Schutz.** Board A sendet nur, wenn in den letzten 3 s etwas vom Bus kam – bei ausgeschalteter Therme
stieg sonst der Fehlerzähler bis zum Bus-off. Board B nutzt diese Sperre nicht, damit Anfragen des WEM das stumme RG2
erreichen. Beide versuchen alle 10 s einen Wiederanlauf, falls der CAN-Baustein im Bus-off steht. Ohne gültige Adresse
der Gegenstelle bleibt der Kanal zu.

**Schalter** „CAN-Tunnel (Experten)“: Nach einem Neustart ist er aus. Mit `tunnel_selbst_ein: "true"` schaltet er sich
nach jedem Neustart einmal selbst ein, sobald das WLAN steht; von Hand ausgeschaltet bleibt er dann bis zum nächsten
Neustart aus. Das ist bequem für lange Mitschnitte, heißt aber auch: Das Board schreibt nach jedem Stromausfall wieder
von selbst auf den Bus.

**Mitschnitt.** Ist `tunnel_log` gesetzt, schickt jedes Board seine Frames als Textzeilen per UDP (Port 47012) an diesen
Rechner, dazu alle 10 s eine Statuszeile (vom/zum Bus, verloren, Fehler, gesperrt, Bus-off). Annehmen mit
[`werkzeuge/tunnel_mitschnitt.py`](https://github.com/jvejmelka/weishaupt-wem-can-esphome/blob/can-tunnel/werkzeuge/tunnel_mitschnitt.py) (nur Python 3):

```
python3 werkzeuge/tunnel_mitschnitt.py --datei tunnel.txt

14:03:08.009 A 1234 R 184:FA00C201      A hat den Frame auf seinem Bus empfangen und weitergeschickt
14:03:08.021 B 1250 T 184:FA00C201      B hat ihn erhalten und auf seinen Bus gesendet (X = Senden abgelehnt)
```

Die Zahl hinter dem Kürzel sind Millisekunden seit dem Start des Boards. Mitschnitte können Seriennummern und
Gerätekennungen enthalten – nicht roh veröffentlichen.

**Was das Paket sonst ändert:** größerer Eingangspuffer für UDP (64 statt der ESP-IDF-Vorgabe 6), WLAN-Stromsparen aus,
längere CAN-Warteschlangen (Empfang 64, Senden 32) und ein Anhalten des CAN-Bausteins während eines Firmware-Updates.
Ohne das Paket verhält sich die Firmware wie in `main`.

**Was geklappt hat:** Aufnahme des RG2, Lesen und Schreiben aller Werte aus Abschnitt 10a, Neustart des RG2 und Neustart
von Board B jeweils ohne Eingriff.

**Bekannte Probleme:**
- Kaltstart der Therme bei laufendem Tunnel (05.11.2026, ältere Firmware): Das RG2 lief weiter, antwortete nicht auf
  `0x1018/1` und verlor seine Knotenadresse (LSS: `FF`). Direkt am Bus, mit gemeinsamem Kaltstart, ging es. Mit dem
  heutigen Stand noch nicht wiederholt.
- Firmware-Update: Am Board an der Therme brach `esphome upload` (Port 3232) mehrfach mit „receiving chunk result:
  Unknown error“ ab; das Log zeigte `canbus took a long time … (3–5 s)`. Das Hochladen im Browser (`/update`) klappte
  jedes Mal. Ein Anhalten des CAN-Bausteins während des Updates (im Zweig) hat die Warnung nicht beseitigt; die Ursache
  ist offen.
