# Protokoll: CAN-Bus und WEM-JSON

> 🇬🇧 English version: [PROTOKOLL.en.md](PROTOKOLL.en.md)

Was auf dem CAN-Bus einer Weishaupt-Brennwertheizung mit WEM-Systemgerät passiert, was diese
Firmware davon mithört, was sie selbst anfragt und wie sie am WEM schaltet – per CAN und über dessen
JSON-Schnittstelle. Gemessen an einer **WTC-GW 15-B** mit WEM-Systemgerät; wo eine Angabe nur aus fremden
Quellen stammt oder nur plausibel ist, steht das dabei.

Was die Firmware zum Bedienen anbietet (Experten-Oberfläche, alle Einstellungen, Regeln, Zusatzanzeigen), steht in
[FIRMWARE.md](FIRMWARE.md); die Mitschnitte zum Raumgerät WEM-RG2 und der CAN-Tunnel in [VERSUCHE.md](VERSUCHE.md).

## Wege zur Heizung

Auf welchen Wegen die Firmware mit der Heizung spricht und was davon ab Werk an ist (Stand v42 – mit jeder Fassung
kam ein Weg dazu). **Diese Tabelle ist die eine Stelle dafür**; README, INSTALL und die übrigen Abschnitte verweisen
hierher. Die Angaben „an“/„aus“ in der Spalte „Ab Werk“ werden aus dem Code erzeugt.

>
> | Weg | Was | Ab Werk |
> |---|---|---|
> | **CAN mithören** | PDOs und die Antworten auf Fragen, die der WEM dem Kessel stellt. Das Board sendet dabei nichts. | immer an |
> | **CAN lesen** (SDO, Kommandobyte `0x40`) | eigene Leseanfragen an den WEM (Knoten 1) und den Kessel (Knoten 2): im Takt, bei Anlass (Betriebsarten, nur WEM), zur Kontrolle nach dem Schalten, von Hand | Takt <!--ab-werk:can_anfragen-->aus<!--/ab-werk--> (Schalter „Eigene CAN-Anfragen“), alles andere an |
> | **CAN schreiben an den WEM** (SDO, Knoten 1) | Heizkreis (`0x2933`/2) und Warmwasser (`0x2A20`/2) schalten – direkt am Bus, ohne JSON | **<!--ab-werk:can_schalten-->an<!--/ab-werk-->** seit v42 (v35–v41 einschaltbar) |
> | **Freies CAN-Schreiben** (Experten) | einzelne Objekte von Hand, nur am WEM (Knoten 1), nur `0x2000`–`0x5FFF` | <!--ab-werk:can_frei-->aus<!--/ab-werk--> |
> | **Warmwasser-Push** (nur Olimex-Variante, Paket `kraiz-per-can` von cg089) | ein Knopf schreibt `1` auf `0x2A49`/2 des WEM (Knoten 1): festes Ziel, fester Wert, mit Mindestabstand und Kontrolle am Bus. Der Schalter „Freies CAN-Schreiben“ gilt für diesen Knopf nicht ([OLIMEX-ESP32-EVB.md](OLIMEX-ESP32-EVB.md#warmwasser-push)). | in der Hauptdatei (WeAct) nicht eingebunden; in der Olimex-Variante nur auf Knopfdruck |
> | **JSON schreiben** (HTTP an den WEM) | Rückweg fürs Schalten, wenn „Schalten per CAN statt JSON“ aus ist. Bis v34 war das der einzige Weg. Dazu das Feld „Schreibbefehl über WEM (Experten)“, das immer über JSON geht. | ungenutzt, solange niemand den Rückweg oder das Schreibfeld benutzt |
> | **JSON lesen** (HTTP an den WEM) | nur von Hand: „Werte per JSON abrufen“ (eine Anfrage, ab v42), Dateien der SD-Karte. Nie im Takt – der WEM verträgt kein Polling. | nur auf Knopfdruck |
> | **CAN-Tunnel** (Versuch, Zweig `can-tunnel`) | reicht ungefiltert weiter, was das Gerät am anderen Ende sendet – auch die Schreibaufträge eines Raumgeräts ([VERSUCHE.md, Abschnitt 10b](VERSUCHE.md#10b-can-tunnel-über-wlan-versuch-zweig-can-tunnel)) | nicht enthalten |
>
> **In den Kessel (Knoten 2) schreibt die Firmware nie** – dorthin gehen nur Leseanfragen. Geschrieben wird
> am WEM. Einzelheiten: [Mithören und Lesen](#2-mithören-und-lesen-auf-anforderung),
> [Schreiben per CAN](#schreiben-per-can-ab-v35-seit-v42-vorgabe). Die gemessene Anlage schaltet seit
> dem 26.10.2026 per CAN.

## Inhalt

- [Wege zur Heizung](#wege-zur-heizung)
- [1. Überblick](#1-überblick)
- [2. Mithören und Lesen auf Anforderung](#2-mithören-und-lesen-auf-anforderung)
- [3. Was mitgehört wird](#3-was-mitgehört-wird)
- [4. Was auf Anforderung gelesen wird](#4-was-auf-anforderung-gelesen-wird)
- [5. Schreiben am WEM: über JSON und per CAN](#5-schreiben-am-wem-über-json-und-per-can)
- [6. Weboberfläche: lesen und schreiben](FIRMWARE.md#6-weboberfläche-lesen-und-schreiben) → FIRMWARE.md
- [6a. Regeln: Betriebsarten bei Verdacht lesen](FIRMWARE.md#6a-regeln-betriebsarten-bei-verdacht-lesen) → FIRMWARE.md
- [6b. Zusatzanzeigen für die Handy-App](FIRMWARE.md#6b-zusatzanzeigen-für-die-handy-app) → FIRMWARE.md
- [7. Neue Objekte finden: Scan und Differenz](#7-neue-objekte-finden-scan-und-differenz)
- [7a. Übernommene Objekte aus anderen Projekten (ungeprüft)](#7a-übernommene-objekte-aus-anderen-projekten-ungeprüft)
- [8. Wann sich der WEM aufhängt](#8-wann-sich-der-wem-aufhängt)
- [9. Beispiel: Heizkreis auf „Zeitprogramm 1“](#9-beispiel-heizkreis-auf-zeitprogramm-1)
- [10. Beispiel: Warmwasser aus](#10-beispiel-warmwasser-aus)
- [10a. Raumgerät WEM-RG2 am Bus](VERSUCHE.md#10a-raumgerät-wem-rg2-am-bus-mitschnitt-november-2026) → VERSUCHE.md
- [10b. CAN-Tunnel über WLAN](VERSUCHE.md#10b-can-tunnel-über-wlan-versuch-zweig-can-tunnel) → VERSUCHE.md
- [10c. Störmeldungen am Bus und Fehlercodes](#10c-störmeldungen-am-bus-und-fehlercodes)
- [11. Quellen](#11-quellen)

## 1. Überblick

Wie das an einer konkreten Anlage mit Wärmepumpe und PV aussieht (Sollwerte, Messungen):
[ANLAGE.md](ANLAGE.md).

### Busparameter

| | |
|---|---|
| Geschwindigkeit | **50 kbit/s** |
| Identifier | 11 Bit (Standard-Frames) |
| Klemme am Kessel | H, L, −, + (nur H und L werden gebraucht) |
| Protokoll | CANopen-ähnlich: PDOs, SDOs, Heartbeats |

### Knoten und Module

Welche Module auf welchem Knoten liegen, steht in der Systemtabelle auf der SD-Karte des WEM
(`/sd/systable.csv`, über die Weboberfläche des WEM abrufbar). An der gemessenen Anlage:

| Knoten | Gerät | Module (JSON-Modulindex MI/MX) |
|---|---|---|
| **1** | WEM-Systemgerät | u. a. SYSTEM0 (MI 01), Heizkreis **HZK0 (MI 02 / MX 00)**, Warmwasser **WW0 (MI 03)**, TERMINAL0 (MI 05), GATEWAY0 (MI 06), Gerätemodul MI 09 / MX 00 |
| **2** | Kessel (WTC) | WE0 (MI 07), Gerätemodul MI 09 / MX 01 |

Weitere Knoten gibt es an dieser Anlage nicht. Anlagen mit zweitem Heizkreis, Solar oder
Erweiterungsmodulen haben mehr Einträge – vorher die eigene `systable.csv` ansehen.

**Anlage mit Erweiterungsmodulen** (WTC-GW 25-B mit WEM-EM-HK und WEM-EM-Sol, gleiche WEM-Firmware
428CC226; `systable.csv` und Tabelle von [geronet1](https://github.com/geronet1/wem-python), danke!):

| Knoten | Gerät | Firmware | Module (MI/MX) | Heartbeat | Schreibkanal in den WEM |
|---|---|---|---|---|---|
| 1 | WEM-Systemgerät | 428CC226 | wie oben | `0x701` | – |
| 2 | Kessel (WTC) | 334EE004 | WE0 (MI 07), MI 09/01, MI 0A/01 | `0x702` | `0x6C2` → `0x682` |
| **3** | **WEM-EM-HK** (Heizkreis 2, gemischt) | 451CC220 | Heizkreis MI 02 / **MX 01**, MI 09/02, MI 0A/02 | `0x703` | `0x6C3` → `0x683` |
| **4** | **WEM-EM-Sol** | 452CC210 | SOLAR0 (MI 04), MI 09/03, MI 0A/03 | `0x704` | `0x6C4` → `0x684` |

Daraus folgt die Regel: **Knoten n meldet sich mit Heartbeat `0x700 + n` und schreibt seine Messwerte
über `0x6C0 + n` in den WEM** (Bestätigung `0x680 + n`). Der Versatz JSON → CAN gilt auch auf den
Modulknoten: Vorlauf des EM-HK = JSON `MI 02 / MX 01 / 0x2507 / 02` = Knoten 3 `0x2607`/2 (+0x100).
Die Betriebsart von Heizkreis 2 läge nach diesem Muster auf Knoten 3 `0x2633`/2 – **nicht geprüft**.

Weitere Dateien der SD-Karte liefert der WEM ebenfalls unter `/sd/<datei>` aus (Anmeldung wie für
JSON; **einzeln abrufen, nie im Takt**):

| Datei | Inhalt |
|---|---|
| `systable.csv` | Systemtabelle: Knoten, Module (MI/MX), Firmware-Dateien je Modul |
| `DLOG_ACT.CSV` | letzte Aufzeichnung des Datenloggers am Display: Kopfzeilen `DP-Nr`, `MI`, `MX`, `OX`, `OS`, `INT` (Takt in s, hex), `UNIT`, `FF` (Teiler), dann `Datum;Werte` – an der gemessenen Anlage eine alte Aufzeichnung, nicht die des Portals |
| `JJMMTTxx.CSV` | Tagesdateien desselben Formats (laut BorgNumberOne) |
| `FAC_TEST.TXT` | Werkstest der SD-Karte (laut BorgNumberOne) |

Ein Inhaltsverzeichnis liefert der WEM nicht (`/sd/` → 404, Webserver „SEVENSTAX“); die Namen muss man
kennen. Ab v36 holt das Board eine Datei auf Knopfdruck oder per `cmd/sd_datei` (Abstand wie beim
Schalten über JSON, ab Werk <!--ab-werk:abstand_json-->60<!--/ab-werk--> s) und wertet `systable.csv` aus, z. B. „Knoten 01 (FW 428CC226): MI 01 SYSTEM0, MI 02 HZK0, …“.

### CANopen in drei Sätzen

- **PDO** (Process Data Object): ein Gerät sendet Werte von selbst, zyklisch oder bei Änderung.
  Kein Index, kein Subindex – die Bedeutung hängt allein an der CAN-ID und der Byteposition.
- **SDO** (Service Data Object): Frage und Antwort auf ein Objekt (Index 16 Bit, Subindex 8 Bit).
  Anfrage an Knoten *n* auf CAN-ID **`0x600 + n`**, Antwort auf **`0x580 + n`**.
- **Heartbeat** auf `0x700 + n`: „Knoten *n* lebt“.

### Aufbau eines SDO-Frames (8 Byte)

| Byte | 0 | 1 | 2 | 3 | 4–7 |
|---|---|---|---|---|---|
| Inhalt | Kommando | Index low | Index high | Subindex | Daten, little endian |

| Kommandobyte | Bedeutung |
|---|---|
| `40` | Lese-Anfrage (Upload). Für lange Objekte sendet die Firmware außerdem die Lese-Kommandos `60`/`70` und `A4`/`A3`/`A2`/`A1` (siehe unten). |
| `41` | Ankündigung: Objekt ist länger als 4 Byte, Länge in Byte 4–7 |
| `4F` / `4B` / `47` / `43` | Antwort mit 1 / 2 / 3 / 4 Datenbytes |
| `2F` / `2B` / `23` | Schreib-Anfrage (Download) mit 1 / 2 / 4 Datenbytes. Sendet der WEM – und die Firmware an den WEM, wenn sie per CAN schaltet ([Schreiben per CAN](#schreiben-per-can-ab-v35-seit-v42-vorgabe)) |
| `60` | Bestätigung eines Schreibvorgangs |
| `80` | Abbruch; Bytes 4–7 = Abbruchcode, z. B. `06020000` Objekt existiert nicht, `06090011` Subindex existiert nicht |

Beispiel: Lese-Anfrage an Knoten 1, Objekt `0x2933` Subindex 2:

```
CAN-ID 0x601   40 33 29 02 00 00 00 00
Antwort 0x581  4F 33 29 02 02 00 00 00     -> 1 Byte, Wert 2
```

### Lange Objekte (mehr als 4 Byte): Teilstücke und Block

Eine einfache Antwort trägt höchstens 4 Byte. Längere Objekte (z. B. der Fehlerblock des WEM, 8 Byte,
oder Texte) kündigt die Gegenstelle mit `41` und der Länge an; dann gibt es zwei Lese-Verfahren:

```
Teilstücke (segmentiert)                     Block
>601 40 3F 27 00 …     Frage                 >601 A4 3F 27 00 08 …   Frage als Block, 8 Stücke je Block
<581 41 3F 27 00 08 …  „8 Byte“              <581 C6 3F 27 00 08 …   „ok, 8 Byte“
>601 60 …              Stück 1 bitte         >601 A3 …               „los“
<581 00 xx xx xx xx xx xx xx                 <581 01 xx … / 82 xx …  Stücke 1, 2 (0x80 = letztes)
>601 70 …              Stück 2 bitte         >601 A2 02 08 …         „bis 2 erhalten“
<581 1D xx …           letztes Stück         <581 D9 …               „Ende“ (+ Prüfsumme)
                                             >601 A1 …               „Ende quittiert“
```

- **Der WEM braucht eine kurze Pause** zwischen Ankündigung und erster Teilstück-Anfrage. Kommt `60`
  im selben Augenblick, antwortet er mit Abbruch `05040001` (Kommando unbekannt). Mit 50 ms Pause
  liefert er (ab v33).
- **Block** beherrscht der WEM auch als Antwortender (ab v34: Lesebefehl mit Zusatz `B`). Er selbst
  fragt den Kessel fast nur per Block ab (`602: A4 …`).
- Block und Teilstücke sendet die Firmware am WEM (Knoten 1) immer, am Kessel (Knoten 2) nur, solange
  „Lange Lesebefehle auch am Kessel“ an ist (ab Werk <!--ab-werk:lang_am_kessel-->an<!--/ab-werk-->) – siehe nächster Abschnitt.

### SDO-Kanäle: wer fragt wen

| Kanal (Frage → Antwort) | Gegenstelle | wer fragt |
|---|---|---|
| `0x601` → `0x581` | WEM | nur wir (Lesebefehle, Anlass-Lesungen, Schalten per CAN, Kontrolle nach dem Schalten) |
| `0x602` → `0x582` | Kessel | **der WEM, etwa 3× pro Sekunde, meist per Block** – und gegebenenfalls wir |
| `0x6C2` → `0x682` | WEM | der Kessel: schreibt seine Messwerte in den WEM (z. B. `0x2A29` Warmwassertemperatur, `0x2694`–`0x2699`) |
| `0x6C0 + n` → `0x680 + n` | WEM | weitere Module (n = Knoten, z. B. `0x6C3` EM-HK, `0x6C4` EM-Sol, siehe [Knoten und Module](#knoten-und-module)) |

CAN-Nachrichten haben keinen Absender: Eine Frage von uns auf `0x602` sieht für den Kessel genauso aus
wie eine des WEM. Mitten in einem Block-Dialog des WEM kann sie dessen Abfrage stören. Darum:
am Kessel nur **einzelne** Lesebefehle, keine Serien. An der gemessenen
Anlage ließen Scans über `0x602` das WEM-Portal stocken, bis der Scan aufhörte. Lange Objekte
(Teilstücke, Block) liest der Lesebefehl seit v35 auch am Kessel; abschalten lässt sich das mit
„Lange Lesebefehle auch am Kessel“ (ab Werk <!--ab-werk:lang_am_kessel-->an<!--/ab-werk-->). Am WEM (`0x601`) fragt
sonst niemand – dort sind auch lange Dialoge unkritisch.

Umgekehrt schreibt **nur der WEM** in den Kessel: alle ~5 s sieben Werte (`0x2709`, `0x270A`,
`0x252B`–`0x252F`: Anforderung, Vorlauf- und Warmwasser-Soll). Das Board schreibt nie in den Kessel;
es schreibt an den WEM, wenn es schaltet (ab v35, seit v42 Vorgabe, siehe [Schreiben per CAN](#schreiben-per-can-ab-v35-seit-v42-vorgabe)).

### Anlauf, Systemtabelle und Kennung

Mitschnitt eines Kaltstarts (Therme stromlos, wieder an), WTC-GW 15-B:

```
t = 0 s     702: 7F, 701: 7F         Kessel und WEM melden sich („pre-operational“)
t = 5–52 s  603…63C: 40 18 10 01 …   der WEM fragt Knoten 3 bis 60 je ~1 s nach 0x1018/1 (Kennung)
            582: 43 18 10 01 9C 03 …  nur der Kessel antwortet (924)
            7E5 …                    LSS: Suche nach unkonfigurierten Geräten
                                     -> Display: „Systemtabelle wird erstellt“
t = 74 s    000: 01 00               NMT: alle Knoten starten (danach alle 10 s)
            602: 2F/2B …             der WEM schreibt alle ~5 s Sollwerte in den Kessel
```

Wer auf den Rundruf antwortet, kommt in die Systemtabelle. Das Board antwortet nie – es ist kein
CANopen-Teilnehmer, braucht keine Knotennummer und fehlt dem WEM nicht. Ein Raumfühler oder
Raumgerät (WEM-RF/RG) ist dagegen ein eigener Knoten und sendet einen eigenen Heartbeat
`0x700 + n`.

Standard-Objekte der Kennung (lesbar mit dem Lesebefehl; die Texte am Kessel sind hier nicht gelesen – sie
brauchen Teilstücke auf `0x602`, die der Lesebefehl dort erst seit v35 sendet):

| Objekt | WEM (Knoten 1) | Kessel (Knoten 2) |
|---|---|---|
| `0x1008` Gerätename | „Systemgeraet“ | – |
| `0x1009` Hardware | „69503511“ | – |
| `0x100A` Software | „428.226.000“ (Systemtabelle: `428CC226.FWU`) | – |
| `0x1018`/1 Hersteller | 924 (`0x39C`, Weishaupt) | 924 |
| `0x1018`/2 Produkt | 5500 | 11 |

## 2. Mithören und Lesen auf Anforderung

Das Board kommt auf zwei Wegen an Werte:

| | Mithören (passiv) | Lesen auf Anforderung (aktiv) |
|---|---|---|
| Was | PDOs, dazu die SDO-Antworten auf Fragen, die **der WEM selbst** an den Kessel stellt | eigene SDO-Lese-Anfragen `40 …` |
| Last | keine – das Board sendet nichts | eine Anfrage plus eine Antwort je Wert |
| Abschaltbar | nein, läuft immer | ja, Schalter **„Eigene CAN-Anfragen“** (Vorgabe: <!--ab-werk:can_anfragen-->aus<!--/ab-werk-->) |

Der WEM fragt den Kessel ohnehin ständig ab – an der gemessenen Anlage rund drei SDO-Anfragen pro
Sekunde. Deshalb kommt ein großer Teil der Kesselwerte beim Board an, ohne dass es selbst fragt.
Welche das sind, steht in [Abschnitt 3](#3-was-mitgehört-wird).

### Takt der eigenen Anfragen

Die Zahlen stammen von Weishaupt selbst:

| Quelle | Angabe |
|---|---|
| WEM-Portal, Datenlogger | kürzestes wählbares Intervall **40 s** je Wert |
| Registertabelle (Datenpunktliste des WEM-Modbus-Gateways) | Aktualisierungsklassen **s / m / l = 30 s / 60 s / 10 min** je Objekt |
| WEM-Modbus-Gateway | fragt den WEM selbst im **30-s-Takt** ab – schließt dafür aber das WEM-Portal aus |

Regelmäßige Abfragen sind also nicht an sich das Problem, wohl aber mehrere Abfrager zugleich (s.
[Abschnitt 8](#8-wann-sich-der-wem-aufhängt)). Die Firmware fragt keinen Wert öfter als alle 40 s:

| Werte | Takt |
|---|---|
| Kessel: Temperatur, Abgas, Vorlauf, Leistung, Drehzahl, Brenner | 40 s |
| Kessel: Vorlauf-Soll, Volumenstrom, Druck; WEM: Vorlauf Heizkreis, Vorlaufsoll-Anforderung | 60 s |
| WEM: Raumsoll aktuell | 5 min |
| WEM: Heizkreis-Betriebsart, Warmwasser-Betriebsart und -Sollwerte | nur bei Anlass (s. u.) |

Zwischen zwei Anfragen eines Takts liegen 150 ms; die 60-s-Gruppe startet 7 s, die 5-min-Gruppe
13 s versetzt.

**Gesendet wird nur, wenn:**

- der Schalter „Eigene CAN-Anfragen“ an ist (gilt für die Takte, nicht für die Anlass-Lesungen),
- der Bus lebt (letzter empfangener Frame jünger als 30 s),
- die **Anlaufpause** vorbei ist: 1 min nach einem Neustart des Boards; **10 min**
  (`anlaufpause_min`), wenn der Bus während des Betriebs weg war und wiederkommt – die Heizung war
  dann stromlos, und der WEM bekommt beim Hochfahren Ruhe.

**Anlass-Lesungen:** Betriebsarten und Sollwerte liegen im WEM und kommen nie von selbst über den
Bus. Sie werden gelesen, auch wenn „Eigene CAN-Anfragen“ aus ist, aber nur

- einmal nach der Anlaufpause,
- wenn sich die Statusbits ändern (PDO `0x1C1`, etwa bei einer Umstellung am Display oder im Portal) –
  mit Paket `verdacht` (in der Hauptdatei eingebunden) stattdessen über die Regeln R3 und R5,
- wenn eine andere Regel auslöst ([FIRMWARE.md, Abschnitt 6a](FIRMWARE.md#6a-regeln-betriebsarten-bei-verdacht-lesen)),
- nach einem eigenen Warmwasser-Schaltbefehl,
- auf Wunsch: Knopf „Status lesen“ (Experten-Oberfläche, Handy-App) bzw. MQTT `<gerät>/cmd/status`.
  Wünsche werden höchstens alle 10 s angenommen.

Auch Anlass-Lesungen gehen nur bei lebendem Bus und nach der Anlaufpause hinaus. Ein Anlass in der
Pause bleibt stehen („angefordert – Anlaufpause, liest danach“) und wird danach einmal gelesen –
seit v42 auch beim Warmwasser; bis v41 las der Warmwasser-Teil schon in der Pause.

Die Heizkreis-Betriebsart wird dabei höchstens alle 2 min gelesen – außer auf ausdrücklichen Wunsch und wenn eine Regel auslöst (dann gilt der Mindestabstand der Regel). Bekannte Lücke: ein Wechsel
zwischen zwei Zeitprogrammen am Display ändert die Statusbits nicht und bleibt bis zum nächsten
Anlass unbemerkt.

### Warum nie über JSON gepollt wird

Die JSON-Schnittstelle des WEM ist ein Übersetzer zwischen HTTP und dem CAN-Bus – und der ist
empfindlich (s. [Abschnitt 8](#8-wann-sich-der-wem-aufhängt)). Regelmäßiges Abfragen über JSON hat
sie an der gemessenen Anlage fünfmal bis zum Stromlos-Machen lahmgelegt. Der Bus selbst hat damit
kein Problem: Der WEM fragt den Kessel dort dreimal pro Sekunde ab. **Lesen deshalb immer am Bus –
und seit v42 auch Schalten: per CAN direkt am WEM. Die JSON-Schnittstelle ist nur noch der Rückweg
und wird nie von selbst angesprochen.**

Ausnahme ab v42: **Abruf von Hand** (Paket `json-lesen`: Knopf „Werte per JSON abrufen“ in der Experten-Oberfläche, „Aktualisieren“ in der App auf dem Board) für Boards ohne CAN-Kabel.
Dabei am 28.10.2026 beobachtet:
- Der WEM beantwortet **höchstens 7 Objekte je Anfrage**; das achte bekommt immer CM 05 – egal welches Objekt dort steht.
- Sechs Abrufe in 7 Minuten (je mindestens 60 s Abstand) – danach kamen nur noch leere Antworten; nach 10 Minuten
  Pause wieder alle Werte. Der Mindestabstand ist deshalb ab Werk <!--ab-werk:json_abstand-->300<!--/ab-werk--> Sekunden (mindestens 120), automatisch wird nie abgefragt.

### Was bleibt, wenn der WEM ausfällt

| Ausfall | Folge |
|---|---|
| nur die JSON-Schnittstelle hängt (der bisher beobachtete Fall) | am Bus läuft alles weiter; das Board liest unverändert. Schalten über JSON geht nicht; ob der WEM in diesem Zustand per CAN schaltet (Vorgabe seit v42), ist noch nicht gemessen |
| nur Cloud-Verbindung und Portal-Logger hängen (Oktober 2026) | Regelung, Heizkurve und Bus laufen normal; es fehlen nur die mitgehörten Logger-Abfragen des WEM (Abgas, Volumenstrom) |
| der WEM ist ganz weg | es fehlen alle Werte, die nur als Antwort auf **WEM-Anfragen** mitgehört werden, und alles, was der WEM sendet |

Nach CANopen-Konvention stammen PDOs auf `0x180 + n` vom Knoten *n*; `0x182` ist damit vom Kessel,
`0x1C1` vom WEM. Welche Werte bei einem vollständigen WEM-Ausfall tatsächlich weiterlaufen, ist
**nicht getestet**. Die Erwartung:

| bleiben voraussichtlich (PDOs, s. [Abschnitt 3](#3-was-mitgehört-wird)) | fallen weg |
|---|---|
| Außentemperatur, Vorlaufsoll/„Heizanforderung“, Warmwasser, Uhrzeit, Kesselstatus | alles, was nur als Antwort auf **Fragen des WEM** mitgehört wird (Druck, Leistung, Wärmemengen …) und die WEM-Schreibtelegramme |

Laut Weishaupt-Anleitung WTC-GW 15…32-B (83307201) meldet der Kessel bei fehlender Verbindung zum
Systemgerät **F 91** „Kommunikationsfehler Systemgerät/Feuerungsautomat“ (Fehler, Anlage verriegelt,
wenn die Betriebssicherheit nicht gewährleistet ist); einen Notbetrieb beschreibt die Anleitung nicht.
Die Heizkurve liegt im WEM – ohne ihn bekommt der Kessel keinen Vorlauf-Sollwert.

Die Kesseltemperatur steht zusätzlich in PDO `0x241`, Bytes 2–3 – sie lässt sich also auch ohne
WEM-Anfragen mithören. Wer welches der PDOs sendet, ist nicht in jedem Fall geklärt. Mit eingeschalteten eigenen Anfragen liest das Board die Kesselwerte aus
[Abschnitt 4](#4-was-auf-anforderung-gelesen-wird) selbst und ist damit vom WEM unabhängig.

## 3. Was mitgehört wird

Die Tabellen in den Abschnitten 3–5 werden aus [`register.yaml`](register.yaml) erzeugt
([INSTALL.md, Abschnitt 14](INSTALL.md#14-register-ändern)); Ergänzungen bitte dort eintragen.

**Stand:** *belegt* = an der Anlage gegen Kesseldisplay oder eine unabhängige Quelle geprüft;
*plausibel* = Werte passen, aber nicht gegengeprüft; *unbestätigt* = Deutung aus fremden Quellen.

> **Was als Gegenprobe zählt – und was nicht:** Eine App oder das Portal, die dieselben Register
> lesen, bestätigen nur die gemeinsame Annahme (Zirkelschluss). Unabhängig sind das
> **Kesseldisplay**, ein **Wetterdienst** für die Außentemperatur und physikalische Kriterien,
> etwa **Vorlauf > Kessel > Rücklauf** bei laufendem Brenner.

### PDOs und Schreibtelegramme

<!-- REGISTER:BEGIN mithoeren -->
<!-- erzeugt aus register.yaml von werkzeuge/register_erzeugen.py - nicht von Hand aendern -->

| CAN-ID | Bytes | Inhalt | Faktor | Name in der Firmware | Stand |
|---|---|---|---|---|---|
| `0x201` | 1–2 | Außentemperatur, int16 | 0,1 °C | Aussentemperatur | **belegt** (Display, Wetterdienst) |
| `0x201` | 0 | Systembetriebsart: 0 Aus, 1 Standby, 2 Sommer, 3 Automatik | – | Systembetriebsart | plausibel |
| `0x241` | 0–1 | **Vorlaufsoll Heizkreis**, int16 – derselbe Wert, den der WEM in `0x252C` schreibt (s. `0x602`); 0 = keine Anforderung | 0,1 °C | Heizanforderung *(Name historisch)* | plausibel (0 bei „Heizkreise inaktiv“) |
| `0x241` | 2–3 | Kesseltemperatur (gleich `0x2532`) | 0,1 °C | Kesseltemperatur | plausibel |
| `0x241` | 6–7 | Warmwassertemperatur | 0,1 °C | Warmwasser | **belegt** (Display) |
| `0x181` | 0–5 | Stunde, Minute, Jahr − 2000, Monat, Tag, Wochentag | – | Uhrzeit Heizung | **belegt** |
| `0x182` | 0 | Kesselstatus (Objekt `0x2530`): 0 Standby, 1 Aus, 10 Heizbetrieb, 15 Warmwasserbetrieb, 101 Kaminfeger, 104 Wartung | – | Kesselstatus, Warmwasser aktiv (Status 15) | **belegt** (für den Ruhezustand) |
| `0x1C1` | 2–3 | Statusbits Knoten 1 (Objekt `0x274D`): `0x1000` Heizkreis Standby, `0x0040` Heizbetrieb, `0x0010` Warmwasser-Ladung ¹ | – | Heizkreis Status | plausibel |
| `0x602` | SDO-Schreibtelegramm des WEM an den Kessel, Objekt `0x252B`/0 | Anforderung an den Kessel: 01 keine Anforderung, 0A Heizen, 0F Warmwasser | – | Regeln R2/R4 | plausibel |
| `0x602` | SDO-Schreibtelegramm des WEM an den Kessel, Objekt `0x252C`/0 | Vorlaufsoll Heizkreis (erscheint auch in PDO `0x241` B0–1) | – | nicht ausgewertet | plausibel |
| `0x602` | SDO-Schreibtelegramm des WEM an den Kessel, Objekt `0x252D`/0 | steigt beim Warmwasserladen rampenförmig (auf 50 °C beobachtet) | – | nicht ausgewertet | plausibel |
| `0x602` | SDO-Schreibtelegramm des WEM an den Kessel, Objekt `0x2709`/0 | 0x64 Heizanforderung aktiv, 0x32 Nachlauf | – | nicht ausgewertet | plausibel |
| `0x6C2` | SDO-Schreibtelegramm, Objekt `0x2699`/1 | Rücklauftemperatur VPT | 0,1 °C | Ruecklauf | **belegt** (Vergleich mit dem WEM-Portal (Fachmann-Ansicht) bei laufendem Brenner, 26.10.2026: Portal 35,7 °C, Bus 35,6–36,7 °C) |
| `0x6C2` | SDO-Schreibtelegramm, Objekt `0x2697`/1 | Vorlauftemperatur VPT | 0,1 °C | Vorlauf VPT | **belegt** (Vergleich mit dem WEM-Portal (Fachmann-Ansicht) bei laufendem Brenner, 26.10.2026: Portal 49,9 °C, Bus 49,7–50,2 °C) |
| `0x6C2` | SDO-Schreibtelegramm, Objekt `0x2698`/1 | Sollleistung | 0,01 % | Sollleistung | **belegt** (Vergleich mit dem WEM-Portal (Fachmann-Ansicht) bei laufendem Brenner, 26.10.2026: Portal 66,98 %, Bus 66–67 %) |

<!-- REGISTER:END mithoeren -->

Zu `0x6C2`: Auf dieser CAN-ID liegen alle paar Sekunden Schreibtelegramme (Kommando `2B`/`2F`/`23`),
bestätigt auf `0x682`. Die Objektbedeutung stammt aus
[geronet1/wem-python](https://github.com/geronet1/wem-python); die Werte passen an der Anlage
(Rücklauf 17,9 °C bei 18,5 °C Vorlauf, Brenner aus). Dort liegen außerdem `0x261E`/1–2, `0x2A29`/1–2,
`0x2694`/1 und `0x2695`/1 – nicht ausgewertet.

Nicht ausgewertet werden außerdem die PDOs `0x1C2` und `0x082` sowie die Heartbeats `0x701`/`0x702`.

¹ `0x1C1` kommt **nur bei Änderung**, nicht zyklisch – die Firmware speichert den letzten Stand.
Vorsicht bei der Schreibweise: die Bytes 2–3 sind little-endian. Rohbytes `00 00 10 00` ergeben
m = `0x0010` (Warmwasser-Ladung), Rohbytes `00 00 00 10` ergeben m = `0x1000` (Standby).

### SDO-Antworten auf Fragen des WEM

Das Board wertet jede SDO-Antwort auf `0x581` und `0x582` aus, egal wer gefragt hat. Im
Mitschnitt fragte der WEM (mit dem Kommandobyte `A4`) unter anderem diese Kesselobjekte selbst ab –
deren Werte kommen also auch bei ausgeschalteten eigenen Anfragen:

<!-- REGISTER:BEGIN wem-antwort -->
<!-- erzeugt aus register.yaml von werkzeuge/register_erzeugen.py - nicht von Hand aendern -->

| Objekt (Knoten 2) | Name in der Firmware | vom WEM abgefragt |
|---|---|---|
| `0x2532`/0 | Kesseltemperatur | ja |
| `0x2537`/0 | Abgastemperatur | ja |
| `0x2534`/0 | Leistung | ja |
| `0x2541`/0 | Brenner Status, Brenner, Brennerphase, Brennerstarts | ja, selten |
| `0x2545`/0 | Vorlauf Soll | ja, selten |
| `0x2713`/2 | Volumenstrom | ja, selten |
| `0x2714`/2 | Anlagendruck | ja |
| `0x2726`/2 | Wärmemenge Vortag Heizung | ja – **nur** so, die Firmware fragt es nie selbst |
| `0x2727`/2 | Wärmemenge Vortag WW | ja – **nur** so, die Firmware fragt es nie selbst |
| `0x2728`/2 | Wärmemenge Vortag Gesamt | ja – **nur** so, die Firmware fragt es nie selbst |
| `0x2731`/2 | nicht ausgewertet (Wärmeleistung aktuell) | ja |
| `0x2533`/2 | nicht ausgewertet (Rücklauftemperatur VPT) | ja |
| `0x2536`/0 | Vorlauf | **nein** – kommt nur mit eigenen Anfragen |
| `0x2540`/0 | Drehzahl | **nein** – kommt nur mit eigenen Anfragen |

<!-- REGISTER:END wem-antwort -->

Der WEM fragt noch weitere Objekte ab (u. a. `0x2530`, `0x2531`, `0x2739`, `0x2753`/2), die die
Firmware nicht auswertet. `0x2533`/2 ist laut Weishaupt-Registertabelle und geronet1 die
Rücklauftemperatur VPT – am 26.10.2026 mit dem WEM-Portal **bestätigt** (Portal 35,7 °C, Bus 35,6–36,7 °C; derselbe Wert wie `0x2699`).

Wie oft der WEM ein Objekt abfragt, schwankt stark; „selten“ heißt: deutlich seltener als einmal
pro Minute. Die Liste stammt aus einem Mitschnitt im Sommerbetrieb und ist nicht vollständig.

### Passiv erkennen: was geht und was nicht

Betriebsarten liegen im WEM und kommen nie als eigener Wert über den Bus. Aus Mitschnitten
von Umschaltungen am Display und per JSON (Heizkreis und Warmwasser, je mehrfach) ergibt sich:

| Umschaltung | passiv erkennbar? | woran |
|---|---|---|
| Heizkreis Standby ↔ Zeitprogramm | **ja** | `0x1C1`, Bit `0x1000` |
| Zeitprogramm 1 ↔ 2 ↔ 3, Sommer, Komfort, Normal, Absenk untereinander | **nein** – kein eigenes Signal | sichtbar ist nur, ob gerade eine Heizanforderung besteht (`0x252B`, `0x252C`, `0x2709`). ZP 2 ↔ ZP 3 per JSON erzeugte **null** Busverkehr; Zeitprogramme unterscheiden sich nur über ihre Schaltzeiten |
| Raumsoll-Stufe (bei Heizbedarf) | eingeschränkt | Vorlaufsoll aus `0x241` B0–1: Komfort liegt etwa 2,2 K Vorlauf über Normal. Rückrechnung RT ≈ (VL − 1,4 + 1,1 · AT) / 2,1, nur für AT 12,9–15,2 °C geeicht. Absenk fordert erst unter AT ≈ RT − 4,5 an; Sommer sieht bei mildem Wetter aus wie Absenk |
| Warmwasser Ein ↔ Aus | **dass** umgeschaltet wurde: ja · **wohin**: nein | der WEM liest vom Kessel (als SDO Block-Upload, Kommandobyte `0xA4`) die Folge `2101/0A, 2102/0D, 2102/01, 2101/0A, 273F/01`, 4–13 s später noch einmal `2101/0A, 273F/01`. Spezifisch ist nur die volle Folge mit `2102/0D` + `2102/01` (die Kurzform kommt auch bei Ladebeginn; die volle Folge ohne `273F` einmal beim Neustart des WEM). Die Richtung zeigt sich nur, wenn eine Ladung einsetzt: `0x252B` = 0F, Rampe in `0x252D`, `0x1C1`-Bit `0x0010` |

**Offen:** Umschaltungen über das WEM-Portal oder die WEM-App wurden nie mitgeschnitten – ob dort
dieselben Folgen entstehen, ist unbekannt. Die Rückrechnung braucht Winterdaten (AT 0–8 °C).

## 4. Was auf Anforderung gelesen wird

Alle Anfragen mit Kommandobyte `0x40`. Die Größe steht im Kommandobyte der Antwort (`4F` 1 Byte, `4B`
2 Byte, `43` 4 Byte); die Firmware liest genau so viele Bytes, 2-Byte-Werte mit Vorzeichen.

**„Kein Wert“:** Bei 2-Byte-Werten bedeutet `0x8000` „kein Wert“. An der gemessenen Anlage antwortet der
Kessel so auf `0x2527`/0 und `0x273B`/2 (`582: 4B 27 25 00 00 80 00 00`), und einmal standen die
Außentemperatur im PDO `0x201` und das Warmwasser im PDO `0x241` für einen Augenblick auf `0x8000`. cg089
sah es nach einem Neustart der Anlage bei Anlagendruck und Temperatur ([Issue #2](https://github.com/jvejmelka/weishaupt-wem-can-esphome/issues/2)).
Die Firmware verwirft `0x8000` und `0x7FFF` bei allen 2-Byte-Werten aus SDO-Telegrammen und bei den
Temperaturen der PDOs; der letzte gültige Wert bleibt stehen. Frühere Stände lasen Antworten des Kessels
immer als 4 Byte, ohne auf die Länge zu achten – `0x8000` erschien dann als 327,68 bar oder 3276,8 °C. Ein
Bereichsfilter je Wert (etwa 0–3 bar) ist nicht eingebaut: Er würde echte Zustände wie Überdruck oder
Frost am Außenfühler verdecken.

<!-- REGISTER:BEGIN eigene-anfragen -->
<!-- erzeugt aus register.yaml von werkzeuge/register_erzeugen.py - nicht von Hand aendern -->

| Knoten | Objekt | Faktor | Takt | Klasse | Name in der Firmware | Stand |
|---|---|---|---|---|---|---|
| 2 | `0x2532`/0 | 0,1 °C | 40 s | s | Kesseltemperatur | **belegt** (Display) |
| 2 | `0x2537`/0 | 0,1 °C | 40 s | – | Abgastemperatur | **belegt** (Vergleich mit dem WEM-Portal (Fachmann-Ansicht) bei laufendem Brenner, 26.10.2026: Portal 49 °C, Bus 49,3 °C; Display 53 °C/Bus 53,9 °C am 26.09.) |
| 2 | `0x2536`/0 | 0,1 °C | 40 s | – | Vorlauf | **belegt** (Vergleich mit dem WEM-Portal (Fachmann-Ansicht) bei laufendem Brenner, 26.10.2026: Portal „Vorlauftemperatur“ 52,1 °C, Bus 52,7 °C (Kesselvorlauf, nicht VPT)) |
| 2 | `0x2534`/0 | 0,01 % | 40 s | – | Leistung | **belegt** (Vergleich mit dem WEM-Portal (Fachmann-Ansicht) bei laufendem Brenner, 26.10.2026: Portal „Istleistung“ 66,88 %, Bus 65,30 %) |
| 2 | `0x2540`/0 | 1 U/min | 40 s | – | Drehzahl | **belegt** (Vergleich mit dem WEM-Portal (Fachmann-Ansicht) bei laufendem Brenner, 26.10.2026: Portal „Gebläsedrehzahl“ 3478 U/min, Bus 3478 bzw. 3380 U/min) |
| 2 | `0x2541`/0 | Phase: 0 aus, 1 Vorbelüftung, 2 Steuerbetrieb, 3 Regelbetrieb, 4 Nachbelüftung | 40 s | s | Brenner Status, Brenner, Brennerphase, Brennerstarts | **belegt** (Display „Heizkreise inaktiv“) |
| 2 | `0x2545`/0 | 0,1 °C | 60 s | m | Vorlauf Soll | **belegt** (Vergleich mit dem WEM-Portal (Fachmann-Ansicht) bei laufendem Brenner, 26.10.2026: Portal „Vorlaufsolltemperatur“ 50 °C, Bus 50,0 °C) |
| 2 | `0x2713`/2 | 1 l/h | 60 s | m | Volumenstrom | **belegt** (Vergleich mit dem WEM-Portal (Fachmann-Ansicht) bei laufendem Brenner, 26.10.2026: Portal „Volumenstrom VPT“ 529 l/h, Bus 531 l/h) |
| 2 | `0x2714`/2 | 0,01 bar | 60 s | m | Anlagendruck | **belegt** (Display) |
| 1 | `0x2907`/2 | 0,1 °C | 60 s | s | Vorlauf Heizkreis | plausibel (JSON MI 02 `0x2507` Vorlaufisttemperatur, +0x400) |
| 1 | `0x2640`/3 | 0,1 °C | 60 s | – | Vorlaufsoll Anforderung | plausibel (50 °C bei Warmwasser Ein, 23 °C bei Aus beobachtet) |
| 1 | `0x2958`/2 | 0,1 °C | 5 min | m | Raumsoll aktuell | plausibel (JSON MI 02 `0x2558`, +0x400) |
| 1 | `0x2933`/2 | Code 1–8, s. [Abschnitt 5](#5-schreiben-am-wem-über-json-und-per-can) | Anlass, höchstens alle 2 min | m | Heizkreis Betriebsart | **belegt** (für Standby, Zeitprogramm 1–3, Sommer. **Komfort, Normal, Absenk nie beobachtet**) |
| 1 | `0x2A20`/2 | 1 Ein, 2 Aus | Anlass | – | Warmwasser Betriebsart | **belegt** (geschaltet und am Display gesehen) |
| 1 | `0x2A2C`/2 | 0,1 °C | Anlass | m | Warmwasser Soll aktuell | plausibel (JSON MI 03 `0x252C`, +0x500; 8,0 °C bei Warmwasser Aus) |
| 1 | `0x2A39`/2 | 0,1 °C | Anlass | m | Warmwasser Soll normal | plausibel (JSON MI 03 `0x2539`, +0x500) |

<!-- REGISTER:END eigene-anfragen -->

„Brenner“ (0/1) und der Zähler „Brennerstarts“ zählen nur mit Flamme (Phase 2 oder 3).
Schaltet man „Eigene CAN-Anfragen“ aus, gehen die Werte, die nur durch eigene Anfragen aktuell
blieben, auf „unbekannt“; fragt der WEM einen davon selbst ab, kommt er über das Mithören zurück.

Rückmeldungen von anderen Anlagen sind willkommen – besonders Messungen bei laufendem Brenner.

<a id="5-schreiben-über-den-wem-json"></a>

## 5. Schreiben am WEM: über JSON und per CAN

Zuerst der Weg über die JSON-Schnittstelle des WEM (bis v34 der einzige, heute der Rückweg und der Weg des Schreibfelds), danach die
[Abbildung JSON-Objekt → CAN-Objekt](#abbildung-json-objekt--can-objekt) und das
[Schreiben per CAN](#schreiben-per-can-ab-v35-seit-v42-vorgabe), mit dem die Firmware seit v42 ab Werk schaltet.

### Die Anfrage

```
POST http://<WEM>/ajax/CanApiJson.json
Authorization: Basic <Benutzer:Passwort>      Werkseinstellung, siehe secrets.yaml.example
Referer: http://<WEM>/
Content-Type: application/json

{"ID":"12345678","SRC":"DDC","CAPI":{"NN":1,"N01":{"VG":"030200253302000102"}}}
```

`ID` setzt die Firmware aus der Laufzeit in Millisekunden. Die Antwort hat denselben Aufbau mit
`"SRC":"SYS"`; ausgewertet wird das `VG`-Feld.

### Das VG-Feld

| Teil | CM | MI | MX | OX | OS | VS | VA |
|---|---|---|---|---|---|---|---|
| Bedeutung | Kommando | Modul | Modul-Instanz | Objekt | Subobjekt | Länge von VA in Byte | Wert |
| Beispiel | `03` | `02` | `00` | `2533` | `02` | `0001` | `02` |

| CM | Bedeutung |
|---|---|
| `01` | lesen (Anfrage) |
| `02` | Leseantwort |
| `03` | schreiben (Anfrage) |
| `04` | Schreibbestätigung |
| `05` | Fehler – Objekt unbekannt oder nicht schreibbar |

Zum Schalten und Schreiben sendet die Firmware `CM 03`; `CM 01` sendet sie nur beim Abruf von Hand
(Paket `json-lesen`, ab v42), nie von selbst. Sie wertet die ersten zwei Zeichen der Antwort
aus: `04` oder `02` gilt als bestätigt, `05` als abgelehnt.

### Was die Firmware schreibt

<!-- REGISTER:BEGIN schreiben -->
<!-- erzeugt aus register.yaml von werkzeuge/register_erzeugen.py - nicht von Hand aendern -->

| Ziel | MI | MX | OX | OS | VS | Werte | Beobachtet |
|---|---|---|---|---|---|---|---|
| Heizkreis-Betriebsart | `02` | `00` | `0x2533` | `02` | `0001` | 1 Standby, 2 Zeitprogramm 1, 3 Zeitprogramm 2, 4 Zeitprogramm 3, 5 Sommer, 6 Komfort, 7 Normal, 8 Absenk | 1–5; **6–8 nie** |
| Warmwasser-Betriebsart | `03` | `00` | `0x2520` | `02` | `0001` | 1 Ein, 2 Aus | beide |

<!-- REGISTER:END schreiben -->

Die Heizkreis-Codes stehen so in der Weishaupt-Registertabelle. Die Warmwasser-Betriebsart steht
dort nicht; sie wurde per Scan und Differenz gefunden (s. [Abschnitt 7](#7-neue-objekte-finden-scan-und-differenz)).

Über das Schreibfeld der Experten-Oberfläche lassen sich weitere Objekte schreiben, aber nur in den
Modulen MI 01, 02 und 03 (s. [FIRMWARE.md, Abschnitt 6](FIRMWARE.md#6-weboberfläche-lesen-und-schreiben)).

### Belegte Leseobjekte: Wärmemengen

Die Firmware liest diese Objekte nicht; ihr Abruf von Hand holt sieben andere Werte
([Warum nie über JSON gepollt wird](#warum-nie-über-json-gepollt-wird)). Wer sie über JSON liest: Diese Objekte antworten an der
WTC-GW 15-B mit `CM 02` (alle **MI 09 / MX 01**, OS `02`, 4 Byte):

<!-- REGISTER:BEGIN json-lesen -->
<!-- erzeugt aus register.yaml von werkzeuge/register_erzeugen.py - nicht von Hand aendern -->

| OX | Inhalt | Faktor | am Bus |
|---|---|---|---|
| `0x2626` | Wärmemenge Vortag Heizung | 0,01 kWh | Knoten 2 `0x2726`/2 |
| `0x2627` | Wärmemenge Vortag Warmwasser | 0,01 kWh | Knoten 2 `0x2727`/2 |
| `0x2628` | Wärmemenge Vortag gesamt | 0,01 kWh | Knoten 2 `0x2728`/2 |
| `0x2631` | Wärmeleistung aktuell | 0,01 kW | Knoten 2 `0x2731`/2 |

<!-- REGISTER:END json-lesen -->

Unter **MI 07 / MX 00** liefern dieselben OX `CM 05` – s. das Warnbeispiel in
[Abschnitt 8](#regeln-die-daraus-folgen).

### Abbildung JSON-Objekt → CAN-Objekt

Dasselbe Objekt, das über JSON adressiert wird, liegt am Bus auf einem festen Knoten mit einem
festen Versatz. Gemessen an der WTC-GW 15-B:

| JSON-Modul | CAN-Knoten | Versatz | Beispiel JSON → CAN |
|---|---|---|---|
| MI 07 (Kessel WE0) | 2 | gleich | `07 00 2532 00` → Knoten 2 `0x2532`/0 Kesseltemperatur |
| MI 09 / MX 01 (Kessel-Gerätemodul) | 2 | +0x100 | `09 01 2614 02` → Knoten 2 `0x2714`/2 Anlagendruck |
| MI 01 (SYSTEM0) | 1 | +0x100 | `01 00 261E 00` → Knoten 1 `0x271E` Systembetriebsart (Wert 3 = Automatik gelesen) |
| MI 02 (Heizkreis HZK0) | 1 | +0x400 | `02 00 2533 02` → Knoten 1 `0x2933`/2 Heizkreis-Betriebsart |
| MI 03 (Warmwasser WW0) | 1 | +0x500 | `03 00 2520 02` → Knoten 1 `0x2A20`/2 Warmwasser-Betriebsart |

Die Versätze gelten je Modul, nicht allgemein: für HZK0 und WW0 gilt +0x100 ausdrücklich **nicht**.
Andere Anlagen (zweiter Heizkreis, Solar) können andere Zuordnungen haben.

Am 26.10.2026 an weiteren Objekten bestätigt (JSON-Nummer aus der Tabelle von BorgNumberOne bzw. der
Gateway-Anleitung umgerechnet, am Bus einzeln gelesen):

| JSON | CAN | Bedeutung | gelesen |
|---|---|---|---|
| `01 00 259C 01` | Knoten 1 `0x269C`/1 | Wärmeanforderung Heizung | 35,3 °C = PDO `0x241` |
| `01 00 259D 01` | Knoten 1 `0x269D`/1 | Wärmeanforderung Warmwasser | 0 (keine Ladung) |
| `01 00 26B1 02` | Knoten 1 `0x27B1`/2 | Außentemperatur | 8,1 °C (PDO 8,0) |
| `01 00 263F 00` | Knoten 1 `0x273F`/0 | **Fehlerblock**, 8 Byte (Fehler/Warnung-Bits, Fehlernummer, Modul) | `00 00 00 00 00 00 00 00` – nichts anstehend |
| `02 00 2559 02` | Knoten 1 `0x2959`/2 | Vorlaufsoll Heizkreis aktuell | 35,3 °C |
| `03 00 2569 02` | Knoten 1 `0x2A69`/2 | „Status Warmwasser“ (Gateway: 0 Warnung / 1 Fehler anstehend) | 1 – passt nicht zum leeren Fehlerblock, **Bedeutung unklar** |

Vermutung zur Herkunft der Versätze: Die JSON-Objektnummer ist **modulrelativ** (`0x2533` ist im
Heizkreis die Betriebsart, im Kessel der Rücklauf VPT); im CAN-Objektverzeichnis eines Knotens liegt
jedes Modul um einen festen Betrag verschoben. Die Plätze +0x200/+0x300 am WEM gehören vermutlich zu
Modulen, die es an der gemessenen Anlage nicht gibt. **Mithilfe gesucht:** Wer einen zweiten
Heizkreis, Solar oder Raumgeräte hat, kann mit einzelnen Lesebefehlen am WEM (`0x601`) prüfen,
wo deren Objekte liegen.

<a name="schreiben-per-can-ab-v35-ab-werk-aus"></a>

### Schreiben per CAN (ab v35, seit v42 Vorgabe)

Bis v34 schrieb die Firmware nie per SDO auf den Bus: Geschaltet wurde über JSON, der Bus diente zur
Kontrolle. Seit v35 kann sie Heizkreis und Warmwasser direkt per CAN am WEM schalten, **seit v42 ist
das die Vorgabe** – der WEM nimmt den Befehl sofort an, und seine empfindliche JSON-Schnittstelle
bleibt unberührt. JSON ist der Rückweg (Schalter aus). Drei Schalter in der Experten-Oberfläche (die ersten beiden in
der Gruppe „Schreiben per CAN“, der dritte unter „Lesebefehl (nur lesen)“):

| Schalter | ab Werk | Wirkung |
|---|---|---|
| Schalten per CAN statt JSON (Heizkreis, Warmwasser) | **<!--ab-werk:can_schalten-->an<!--/ab-werk-->** (seit v42; v35–v41 aus) | App-Knöpfe, `cmd/heizkreis`, `cmd/warmwasser` schreiben per SDO-Download an den WEM: `0x2933`/2 bzw. `0x2A20`/2, 1 Byte. Warteschlange und Kontrolle am Bus wie beim JSON-Weg; Abstand zum vorigen Befehl ab Werk <!--ab-werk:abstand_can-->10<!--/ab-werk--> s (über JSON <!--ab-werk:abstand_json-->60<!--/ab-werk--> s); Abbruch des WEM = „abgelehnt“ |
| Freies CAN-Schreiben (Experten) | <!--ab-werk:can_frei-->aus<!--/ab-werk--> | Eingabefeld und `cmd/can_schreiben`: `KN IDX SUB WERT [LÄNGE]` oder `KN IDX SUB = B1 B2 …` (hex, bis 64 Byte, ab 5 Byte in Teilstücken mit 50 ms Pause). **Nur Knoten 01, nur 2000–5FFF** – nie der Kessel, nie Kommunikationsobjekte (0x1000–0x1FFF) |
| Lange Lesebefehle auch am Kessel | **<!--ab-werk:lang_am_kessel-->an<!--/ab-werk-->** | Teilstücke/Block beim Lesen auch an Knoten 2 (dort teilt sich das Board den Kanal mit dem WEM) |

**An der WTC-GW 15-B (WEM 428.226) am 26.10.2026 bestätigt:**

| Befehl | WEM | Folge |
|---|---|---|
| `601: 2F 33 29 02 02` (Heizkreis = ZP 1, gleicher Wert) | `60` bestätigt | nichts |
| `601: 2F 33 29 02 01` (Standby) | `60` | binnen 1 s Heizanforderung 0, Kessel aus; Display „Heizkreise inaktiv“, Betriebsart „Standby“, „Heizung aus“, „Warmwasser aus“; App Standby |
| `601: 2F 33 29 02 02` (zurück ZP 1) | `60` | Kessel heizt wieder, Display „Automatik/Heizbetrieb“ |
| `601: 2F 20 2A 02 02` / `01` (Warmwasser Aus/Ein, über den Schalter) | `60` | WW-Soll aktuell 8 °C bzw. 35 °C, Bus-Kontrolle bestätigt |

Der WEM übernimmt den per CAN geschriebenen Wert also wie einen per JSON geschriebenen (Regelung,
Display, App). Offen: Portal, Verhalten nach einem Neustart, andere Anlagen. Jeder geänderte Wert
landet vermutlich im EEPROM des WEM – nie im Takt schreiben. Andere Berichte, nach denen die
Geräte SDO-Schreiben ablehnen, betrafen vermutlich andere Objekte oder den Kessel.

### Ablauf in der Firmware

1. Ein Wunsch kommt über die App auf dem Board, die Experten-Oberfläche, Home Assistant oder MQTT
   (`<gerät>/cmd/heizkreis`, `<gerät>/cmd/warmwasser`) und landet in der **Warteschlange**: höchstens
   ein Eintrag je Ziel, der neueste Wunsch ersetzt den alten; gesendet wird in der Reihenfolge des
   Eingangs (bis v37: Heizkreis vor Warmwasser).
2. Einmal pro Sekunde prüft die Firmware die Warteschlange. Zwischen zwei Befehlen liegt ein
   **Mindestabstand** ab dem letzten Senden: per CAN ab Werk <!--ab-werk:abstand_can-->10<!--/ab-werk--> s, über JSON <!--ab-werk:abstand_json-->60<!--/ab-werk--> s (beide
   einstellbar, Grenzen in der Tabelle unter [Einstellungen](FIRMWARE.md#einstellungen)).
3. **Per CAN (Vorgabe):** ein SDO-Download an den WEM (`0x601`, `0x2933`/2 bzw. `0x2A20`/2, 1 Byte).
   Nach 1 s wertet die Firmware die Antwort aus: Quittung `60` = bestätigt; Abbruch `80` =
   „abgelehnt“, der Vorgang ist beendet; keine Antwort = es wird trotzdem am Bus geprüft.
   **Über JSON (Schalter aus):** `CM 03` an den WEM, die Antwort wird angezeigt; bei `CM 05` ist der
   Vorgang beendet („abgelehnt“).
4. Sonst liest die Firmware das Zielobjekt am Bus (Knoten 1, `0x2933`/2 bzw. `0x2A20`/2) – per CAN
   **3 s** nach dem Senden, über JSON **3 s** nach der Antwort. Diese eine Kontroll-Lesung geht auch
   bei ausgeschalteten eigenen Anfragen raus.
5. **Der Bus entscheidet, nicht die WEM-Antwort:** Stimmt der gelesene Wert mit dem Wunsch überein,
   heißt es „OK“, sonst „NICHT übernommen“. Kommt binnen 10 s keine Antwort, „keine Rückmeldung
   vom Bus – Ergebnis unbekannt“. Auch nach einer leeren oder unklaren WEM-Antwort wird am Bus
   geprüft – es wurde schon beobachtet, dass trotz leerer Antwort geschaltet war.
6. Ergebnis steht in **„Ergebnis letzter Schaltbefehl“** und im **Schaltprotokoll** (Klartext,
   veraltet) sowie ab v27 strukturiert auf **`<gerät>/befehl/status`** (jede Phase, mit Kennung)
   und in **`<gerät>/status/json`** → `schalten` – Aufbau in [TOPICS.md](TOPICS.md).

## 6. Weboberfläche: lesen und schreiben

Ausgelagert nach [FIRMWARE.md](FIRMWARE.md#6-weboberfläche-lesen-und-schreiben).

## 6a. Regeln: Betriebsarten bei Verdacht lesen

Ausgelagert nach [FIRMWARE.md](FIRMWARE.md#6a-regeln-betriebsarten-bei-verdacht-lesen).

## 6b. Zusatzanzeigen für die Handy-App

Ausgelagert nach [FIRMWARE.md](FIRMWARE.md#6b-zusatzanzeigen-für-die-handy-app).

## 7. Neue Objekte finden: Scan und Differenz

So wurden die Heizkreis-Betriebsart `0x2933`/2 und die Warmwasser-Betriebsart `0x2A20`/2 gefunden –
und daraus die Versätze aus [Abschnitt 5](#abbildung-json-objekt--can-objekt) abgeleitet.

1. **Systemtabelle ansehen.** `/sd/systable.csv` des WEM zeigt, welches Modul auf welchem Knoten
   liegt. Damit ist klar, wo gesucht werden muss (Heizkreis und Warmwasser: Knoten 1).
2. **Mithören, was der WEM selbst fragt.** Mit dem Schalter „CAN-Rohmitschnitt nach MQTT“ eine
   Weile `<gerät>/canraw` aufzeichnen. Die Anfragen des WEM auf `0x602` zeigen, welche Objekte
   er für wichtig hält.
3. **Zustand A scannen.** Per MQTT einen Bereich lesen, z. B.
   `<gerät>/cmd/scan` mit `01 2A00 2AFF 3` (Knoten 1, Index `0x2A00` bis `0x2AFF`, Subindex 0–3).
   Das Board sendet alle 25 ms eine Lese-Anfrage; die Antworten landen in `<gerät>/canraw`.
   Abbruch mit `<gerät>/cmd/stop`.
4. **Genau eine Sache ändern** – am Display oder mit einem einzigen JSON-Befehl, z. B. Heizkreis
   von Standby auf Zeitprogramm 1, oder Warmwasser von Ein auf Aus.
5. **Zustand B scannen**, derselbe Bereich.
6. **Differenz bilden:** Antworten beider Scans nach (Knoten, Index, Subindex) zusammenführen und
   nur die geänderten Werte ausgeben. Was sich ändert, ist Kandidat. Abbruchantworten (`80 …`)
   markieren Objekte, die es nicht gibt.
7. **Einzeln bestätigen:** den Kandidaten per Lesebefehl lesen, noch einmal umschalten, wieder lesen.
8. **Am Display gegenprüfen.**

Ergebnis an der gemessenen Anlage: Standby → Zeitprogramm 1 änderte Knoten 1 `0x2933`/2 von 1 auf
2 – der JSON-Wert `0x2533`/2 plus `0x400`. Warmwasser Ein → Aus im Bereich `0x2A00`–`0x2AFF`
änderte `0x2A20`/2 von 1 auf 2 – ein Objekt, das in keiner veröffentlichten Liste steht.

> **Nur am Bus scannen, nie über JSON – und am Bus nur am WEM (Knoten 1).** Dort haben Lesescans mit vielen
> Abbruchantworten an der gemessenen Anlage keine Beeinträchtigung gezeigt. Am Kessel (Knoten 2, `0x602`)
> stockte dagegen das Portal, solange ein Scan lief ([Ausfall Nr. 10](#beobachtete-ausfälle-wtc-gw-15-b)) –
> dort nur einzelne Lesebefehle. Über JSON verbraucht jede Anfrage an
> ein nicht vorhandenes Objekt einen von zehn Plätzen bis zum nächsten Stromlos-Machen
> (s. [Abschnitt 8](#8-wann-sich-der-wem-aufhängt)). Ein JSON-Scan legt die Schnittstelle sicher lahm.

Ein Scan erzeugt viel Busverkehr (40 Anfragen pro Sekunde). Bereiche klein halten und nur scannen,
wenn nötig.

## 7a. Übernommene Objekte aus anderen Projekten (ungeprüft)

Andere haben an ihren Anlagen Objekte gefunden, die in den Tabellen oben fehlen. Sie stehen hier, damit
niemand sie ein zweites Mal suchen muss – mit eigenen Bezeichnungen und der Quelle je Objekt.
**Stand aller Zeilen: übernommen, ungeprüft.** An der gemessenen Anlage ist keines davon gelesen und
verglichen; Skalierung und Bedeutung sind die der Quelle. Wird eines geprüft, wandert es mit Beleg in die
Tabellen der Abschnitte 3 und 4.

- Aufgenommen sind nur Objekte der Module, die es an der gemessenen Anlage gibt (Kessel, System,
  Heizkreis 1, Warmwasser). Objekte für einen zweiten Heizkreis, Erweiterungsmodule und Solar stehen in
  den Quellen – am vollständigsten in [`wem_codes.py`](https://github.com/geronet1/wem-python/blob/main/wem_codes.py) von geronet1
  (knapp 200 Objekte zum Erweiterungsmodul Heizkreis und zu Solar; hier an keiner Anlage geprüft).
- Nennt die Quelle eine JSON-Adresse, ist der CAN-Index mit den Versätzen aus
  [Abschnitt 5](#abbildung-json-objekt--can-objekt) umgerechnet. Die Versätze sind an einer WTC-GW 15-B
  gemessen; ob sie für jedes Objekt eines Moduls gelten, ist nur an den dort genannten Beispielen geprüft.
- Die Netzwerk-Objekte des WEM (Modul MI 06) sind nur als JSON-Adresse bekannt und stehen weiter unten in einer
  eigenen Tabelle – ohne Werte, denn die wären Angaben zum eigenen Heimnetz.
- Zum Systemmodul gehören auch Objekte für Pufferspeicher, Kaskade, Plattenwärmetauscher und Zirkulation.
  Ob sie an einer Anlage ohne diese Funktionen einen Wert liefern, ist ungeprüft.
- Viele Kesselobjekte sind Einstellwerte der Fachmann-Ebene. Hier geht es nur ums Lesen: Eigene
  Schreibbefehle an den Kessel (Knoten 2) hat die Firmware nicht, auch das Schalten per CAN geht an den WEM.
- Gepflegt wird die Liste in [`register-uebernommen.yaml`](register-uebernommen.yaml).

<!-- REGISTER:BEGIN uebernommen -->
<!-- erzeugt aus register-uebernommen.yaml von werkzeuge/register_erzeugen.py - nicht von Hand aendern -->

**Kessel (Knoten 2)**

| Objekt | Bedeutung | Skalierung | JSON (MI MX OX OS) | Modbus | Quelle | Hinweis |
|---|---|---|---|---|---|---|
| `0x2503`/2 | Schaltdifferenz des Reglers bei Warmwasserladung | 0,1 K | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x250A`/0 | Taktsperre des Brenners im Heizbetrieb | 1 min | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x250D`/2 | Schaltdifferenz des Reglers im Heizbetrieb | 0,1 K | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2513`/0 | Dauer der Zwangsteillast im Heizbetrieb | 0,1 s | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2516`/2 | Betriebsart der internen Pumpe bei Warmwasserladung | Code | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2523`/2 | Größte Leistung im Heizbetrieb | 0,01 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2528`/2 | Größte Leistung bei Warmwasserladung | 0,01 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x253D`/0 | Offset des Gasventils | 0,01 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2542`/0 | Sollleistung des Kessels | 0,01 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x256B`/2 | Kleinste Pumpenleistung im Heizbetrieb | 1 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x256C`/2 | Größte Pumpenleistung im Heizbetrieb | 1 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x256E`/2 | Kleinste Pumpenleistung bei Warmwasserladung | 1 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2579`/0 | Ionisationssignal, Sollwert | 1 Punkte | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x257A`/0 | Ionisationssignal beim Start | 1 Punkte | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x257D`/0 | Gas-Luft-Verhältnis, aktueller Wert | 0,01 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x257E`/0 | Ansteuersignal des Gebläses, aktueller Wert | 0,01 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2581`/2 | Korrektur der Gasmenge beim Start | 1 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x258B`/2 | Korrektur der kleinsten Leistung | 1 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x259B`/2 | Höchste Abgastemperatur (Grenzwert) | 1 °C | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2715`/2 | Vorlauftemperatur am VPT-Fühler | 0,1 °C | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2716`/2 | Anlagendruck, Schwelle für die Warnmeldung | 0,01 bar | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2717`/2 | Anlagendruck, Schwelle für die Brennersperre | 0,01 bar | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2719`/2 | Interne Pumpe, Sollleistung | 1 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x271B`/2 | Interne Pumpe, elektrische Leistung | 1 W | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2729`/2 | Größter Volumenstrom | 1 l/h | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x272A`/2 | Volumenstrom-Faktor im Heizbetrieb | 1 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x272B`/2 | Volumenstrom-Faktor bei Warmwasserladung | 1 % | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2741`/0 | Zeit bis zur nächsten Wartung | 1 h | `09 01 2641 00` | – | [kraiz](https://github.com/kraiz/hassio-weishaupt) | – |
| `0x2742`/0 | Wartungsintervall | 1 h | `09 01 2642 00` | – | [kraiz](https://github.com/kraiz/hassio-weishaupt) | – |
| `0x274E`/0 | Ausgangstest | Code | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x274F`/2 | Ausgang MFA1 | Code | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2763`/2 | Gasdruckwächter | Code | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x276A`/2 | Betriebsart der internen Pumpe im Heizbetrieb | Code | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2A01`/0 | Brennerstarts, Gesamtzähler | Zähler | – | – | [geronet1](https://github.com/geronet1/wem-python) | – |
| `0x2A21`/0 | Betriebsstunden des Brenners | 1 h | `09 01 2921 00` | – | [geronet1](https://github.com/geronet1/wem-python), [kraiz](https://github.com/kraiz/hassio-weishaupt) | geronet1: Zähler seit dem Rücksetzen; kraiz: gesamt |

**Heizkreis 1 (Knoten 1, JSON-Modul MI 02 + 0x400)**

| Objekt | Bedeutung | Skalierung | JSON (MI MX OX OS) | Modbus | Quelle | Hinweis |
|---|---|---|---|---|---|---|
| `0x291B`/2 | Untere Grenze des Vorlaufsolls | 0,1 °C | `02 00 251B 02` | – | [ChristophKomenda](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/pull/10) | – |
| `0x291C`/2 | Obere Grenze des Vorlaufsolls | 0,1 °C | `02 00 251C 02` | – | [ChristophKomenda](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/pull/10) | – |
| `0x292C`/2 | Vorlaufsoll im Sonderniveau | 0,1 °C | `02 00 252C 02` | 113 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | liest das Paket `kraiz-per-can` |
| `0x293A`/2 | Raumsoll Normal | 0,1 °C | `02 00 253A 02` | 107 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | JSON-Adresse am RG2 mitgeschnitten (VERSUCHE.md, Abschnitt 10a); liest das Paket `kraiz-per-can` |
| `0x293B`/2 | Raumsoll Komfort | 0,1 °C | `02 00 253B 02` | 106 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | JSON-Adresse am RG2 mitgeschnitten (VERSUCHE.md, Abschnitt 10a); liest das Paket `kraiz-per-can` |
| `0x2954`/2 | Frostschutzgrenze der Außentemperatur | 0,1 °C | `02 00 2554 02` | – | [ChristophKomenda](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/pull/10) | – |
| `0x2969`/2 | Vorlaufsoll Absenk | 0,1 °C | `02 00 2569 02` | 112 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | liest das Paket `kraiz-per-can` |
| `0x296A`/2 | Vorlaufsoll Normal | 0,1 °C | `02 00 256A 02` | 111 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | liest das Paket `kraiz-per-can` |
| `0x296B`/2 | Vorlaufsoll Komfort | 0,1 °C | `02 00 256B 02` | 110 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | liest das Paket `kraiz-per-can` |
| `0x296E`/2 | Bauweise des Gebäudes | Code | `02 00 256E 02` | – | [ChristophKomenda](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/pull/10) | – |
| `0x297C`/2 | Heizgrenze nach Raumsoll | Code | `02 00 257C 02` | – | [ChristophKomenda](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/pull/10) | – |
| `0x297E`/4 | Aktuelle Betriebsart des Heizkreises | Code | `02 00 257E 04` | 102 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | liest das Paket `kraiz-per-can` |
| `0x297E`/5 | Status des Heizkreises | Code | `02 00 257E 05` | 103 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | liest das Paket `kraiz-per-can` |
| `0x2981`/2 | Niveauanhebung nach Außentemperatur | 0,1 °C | `02 00 2581 02` | – | [ChristophKomenda](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/pull/10) | – |
| `0x29A3`/2 | Aufheizoptimierung | Code | `02 00 25A3 02` | – | [ChristophKomenda](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/pull/10) | – |
| `0x29C7`/2 | Heizgrenze nach Vorlauftemperatur | 0,1 °C | `02 00 25C7 02` | – | [ChristophKomenda](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/pull/10) | – |

**System (Knoten 1, JSON-Modul MI 01 + 0x100)**

| Objekt | Bedeutung | Skalierung | JSON (MI MX OX OS) | Modbus | Quelle | Hinweis |
|---|---|---|---|---|---|---|
| `0x2660`/2 | Pufferspeicher, Temperatur oben | 0,1 °C | `01 00 2560 02` | 118 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | liest das Paket `kraiz-per-can` |
| `0x2661`/2 | Pufferspeicher, Temperatur unten | 0,1 °C | `01 00 2561 02` | 119 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | liest das Paket `kraiz-per-can` |
| `0x2712`/0 | Kaskade, Sollwert Heizen | 0,1 °C | `01 00 2612 00` | 145 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | liest das Paket `kraiz-per-can` |
| `0x271C`/0 | Kaskade, Soll-Modulation Heizen | 0,01 % | `01 00 261C 00` | 143 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | liest das Paket `kraiz-per-can` |
| `0x271D`/0 | Kaskade, Soll-Modulation Warmwasser | 0,01 % | `01 00 261D 00` | 144 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | liest das Paket `kraiz-per-can` |
| `0x271F`/2 | Temperatur am Plattenwärmetauscher (Fühler B2) | 0,1 °C | `01 00 261F 02` | 117 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | liest das Paket `kraiz-per-can` |
| `0x27E4`/2 | Folgewechsel der Kessel | Code | `01 00 26E4 02` | 140 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | liest das Paket `kraiz-per-can` |
| `0x27E6`/2 | Wechselzeit der Kesselfolge | 1 h | `01 00 26E6 02` | 141 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | liest das Paket `kraiz-per-can` |
| `0x27FE`/2 | Kaskade, Abgleichtemperatur | 0,1 °C | `01 00 26FE 02` | 142 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | liest das Paket `kraiz-per-can` |
| `0x28F9`/2 | Leistungsvorgabe für Eingang N1 über Ethernet (0–100 %) | 0,01 % | `01 00 27F9 02` | 114 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | liest das Paket `kraiz-per-can` |

**Warmwasser (Knoten 1, JSON-Modul MI 03 + 0x500)**

| Objekt | Bedeutung | Skalierung | JSON (MI MX OX OS) | Modbus | Quelle | Hinweis |
|---|---|---|---|---|---|---|
| `0x2A38`/2 | Warmwasser-Soll Absenk | 0,1 °C | `03 00 2538 02` | 133 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | liest das Paket `kraiz-per-can` |
| `0x2A51`/2 | Warmwasserpumpe | 0 aus, 1 ein | `03 00 2551 02` | 137 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | liest das Paket `kraiz-per-can` |
| `0x2A57`/2 | Rücklauftemperatur der Zirkulation | 0,1 °C | `03 00 2557 02` | 136 | [kraiz](https://github.com/kraiz/hassio-weishaupt), [BorgNumberOne](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) | liest das Paket `kraiz-per-can` |

**Netzwerk des WEM (JSON-Modul MI 06, nur JSON-Adresse)**

Hier steht nur, **wo** der WEM seine Netzwerkeinstellungen ablegt. An der gemessenen Anlage ist nichts davon gelesen, und ein CAN-Objekt dazu ist nicht belegt: Für MI 06 ist kein Versatz gemessen ([Abschnitt 5](#abbildung-json-objekt--can-objekt)). Texte liest die Quelle mit dem Kommando `11` (Antwort `12`), nicht mit `01`. Die Werte beschreiben das eigene Heimnetz (Adressen, MAC-Adresse, Gerätename): Sie gehören nicht in Issues, Mitschnitte oder Bildschirmfotos. Diese Firmware liest und veröffentlicht sie nicht.

| JSON (MI MX OX OS) | Bedeutung | Länge und Art | Quelle |
|---|---|---|---|
| `06 00 2507 00` | Art der Adressvergabe | 1 Byte: 1 fest eingestellt, 3 automatisch (DHCP) | [JS-DE-Tech](https://github.com/JS-DE-Tech/hacs-weishaupt-lan) |
| `06 00 2508 00` | IP-Adresse | 4 Byte, IPv4 | [JS-DE-Tech](https://github.com/JS-DE-Tech/hacs-weishaupt-lan) |
| `06 00 2509 00` | Netzmaske | 4 Byte, IPv4 | [JS-DE-Tech](https://github.com/JS-DE-Tech/hacs-weishaupt-lan) |
| `06 00 250A 00` | Standardgateway | 4 Byte, IPv4 | [JS-DE-Tech](https://github.com/JS-DE-Tech/hacs-weishaupt-lan) |
| `06 00 250B 00` | DNS-Server | 4 Byte, IPv4 | [JS-DE-Tech](https://github.com/JS-DE-Tech/hacs-weishaupt-lan) |
| `06 00 250C 01` … `06` | MAC-Adresse, je Subobjekt eine der sechs Stellen | je 2 Byte | [JS-DE-Tech](https://github.com/JS-DE-Tech/hacs-weishaupt-lan) |
| `06 00 250E 00` | Gerätename im Netz | Text, bis 16 Zeichen | [JS-DE-Tech](https://github.com/JS-DE-Tech/hacs-weishaupt-lan) |
| `06 00 2511 00` | Name im Zertifikat (CN) | Text, bis 50 Zeichen | [JS-DE-Tech](https://github.com/JS-DE-Tech/hacs-weishaupt-lan) |

**In der Quelle ohne Bezeichnung**

- Knoten 2, Antworten des Kessels ([geronet1](https://github.com/geronet1/wem-python)): `0x2503`/1, `0x250D`/1, `0x2516`/1, `0x2523`/1, `0x2528`/1, `0x252E`/0, `0x2569`/1, `0x2569`/2, `0x256B`/1, `0x256C`/1, `0x256E`/1, `0x2574`/1, `0x2574`/2, `0x2581`/1, `0x258B`/1, `0x258C`/0, `0x2594`/1, `0x2594`/2, `0x259B`/1, `0x2716`/1, `0x2717`/1, `0x271A`/2, `0x2729`/1, `0x272A`/1, `0x272B`/1, `0x272C`/2, `0x2732`/2, `0x273B`/2, `0x273D`/1, `0x273D`/2, `0x2754`/1, `0x2754`/2, `0x2764`/2, `0x2765`/1, `0x2766`/2, `0x276A`/1, `0x2770`/2, `0x2772`/1, `0x2772`/2, `0x2778`/1, `0x2778`/2, `0x2A22`/0
- Schreibtelegramme auf `0x602` ([geronet1](https://github.com/geronet1/wem-python)): `0x2528`/2, `0x252E`/0, `0x2705`/1
- Schreibtelegramme auf `0x6C2` ([geronet1](https://github.com/geronet1/wem-python)): `0x251F`/1, `0x251F`/2

**Quellen**

- [geronet1/wem-python](https://github.com/geronet1/wem-python) – Objektliste des Mitschnitts (`wem_codes.py`, MIT-Lizenz)
- [BorgNumberOne/Weishaupt_CanApiJson](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) – Adresstabelle der JSON-Schnittstelle mit Modbus-Registern
- [kraiz/hassio-weishaupt](https://github.com/kraiz/hassio-weishaupt) – Sensorliste der Home-Assistant-Integration (MIT-Lizenz)
- [BorgNumberOne/Weishaupt_CanApiJson, Pull Request #10 (ChristophKomenda)](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/pull/10) – Heizkreis-Parameter der JSON-Schnittstelle
- [JS-DE-Tech/hacs-weishaupt-lan](https://github.com/JS-DE-Tech/hacs-weishaupt-lan) – Home-Assistant-Integration über die JSON-Schnittstelle (MIT-Lizenz); von dort die Netzwerk-Objekte des WEM

<!-- REGISTER:END uebernommen -->

## 8. Wann sich der WEM aufhängt

### Symptome

- HTTP 200 mit leerem Körper statt JSON („Leerantwort“),
- oder `CM 05` auf **alle** Register, auch auf solche, die vorher antworteten,
- das WEM-Portal zeigt eingefrorene Werte, obwohl sein Zeitstempel frisch ist. Portalwerte sind
  ohnehin ein **Cloud-Cache** mit Minuten Verzug, einzelne Werte bleiben auch im Normalbetrieb
  stehen. Echtheitstest: die Außentemperatur gegen einen Wetterdienst halten – folgt sie dem
  Tagesgang nicht, ist sie eingefroren,
- der CAN-Bus läuft währenddessen normal weiter: der WEM fragt den Kessel weiter ab, der Kessel
  antwortet, das Board liest unverändert.

### Zwei Schweregrade

| | kurzer Aussetzer | harte Sperre |
|---|---|---|
| Bild | einzelne Leerantwort oder kurz `CM 05` | `CM 05` auf alle Register, dauerhaft |
| Erholung | von selbst, nach etwa 2 min Ruhe beobachtet | **nur durch Stromlos-Machen der Heizung** |

### Bekannte Auslöser

| Auslöser | Quelle |
|---|---|
| **Anfragen an nicht vorhandene Objekte:** jede `CM 05`-Antwort belegt einen von zehn Plätzen, die bis zum Stromlos-Machen nicht frei werden; nach der zehnten sperrt die Schnittstelle alle Objekte | [kraiz/hassio-weishaupt #15](https://github.com/kraiz/hassio-weishaupt/issues/15) |
| **Regelmäßiges Pollen über JSON** – auch im Stundentakt | an der WTC-GW 15-B gemessen, fünf harte Sperren |
| **WEM-Portal und lokale Schnittstelle gleichzeitig** | [kraiz/hassio-weishaupt #13](https://github.com/kraiz/hassio-weishaupt/issues/13); Weishaupt: Gateway und Portal schließen sich aus |
| **Zwei Clients gleichzeitig** | [kraiz/hassio-weishaupt #9](https://github.com/kraiz/hassio-weishaupt/issues/9) |
| **Mehrere Schaltbefehle dicht hintereinander** | an der WTC-GW 15-B gemessen |

**Wiederkehrende Leerantwort-Fenster:** Mit eingeschaltetem Portal kamen Leerantworten etwa alle
zwei Stunden. Die Minute verschiebt sich mit dem Zeitpunkt, zu dem die Heizung eingeschaltet wurde –
ein festes Abfrageraster kann dem Fenster deshalb nicht dauerhaft ausweichen. Deutung (nicht
belegt): eine interne Aufgabe des WEM, vermutlich im Zusammenhang mit dem Portal.

Nicht jede harte Sperre ließ sich auf die Zehn-Platz-Grenze zurückführen: Einmal kam sie ohne eine
einzige vorherige `CM 05`-Antwort, nach einer Leerantwort und einer sofortigen Wiederholung.

### Beobachtete Ausfälle (WTC-GW 15-B)

| # | Art | Umstand |
|---|---|---|
| 1 | hart, tagelang unbemerkt | Polling im Minutentakt |
| 2 | hart | Polling alle 10 min |
| 3 | hart | Polling alle 15 min |
| 4 | hart | Leerantwort, dann sofortige Wiederholung nach 2,5 s |
| 5 | hart, Portalwerte eingefroren | Polling stündlich |
| 6 | zwei kurze Aussetzer, selbst erholt nach 2 min Ruhe | normale Bedienung in Portal und App zur selben Zeit |
| 7 | Leerantwort nach einem Schaltbefehl – geschaltet hatte er trotzdem | Befehle zu dicht hintereinander |
| 8 | hart | vier Schaltbefehle binnen fünf Minuten, dazu eine Leseanfrage |
| 9 | Portal eingefroren, Heizung regelt normal weiter | Cloud-Verbindung riss ab (Ursache unbekannt); drei Tage später fragte der WEM auch die Logger-Werte (Abgas, Volumenstrom alle ~32 s) nicht mehr ab. Erst Stromlos-Machen half – nach zwei Wochen |
| 10 | Portal stockt, erholt sich sofort nach Ende | SDO-Lese-Serien (Scan) auf `0x602`, auch mit 2,5 s und 6 s Abstand |

### Regeln, die daraus folgen

1. **Nur Objekte ansprechen, deren Existenz belegt ist.** Unbekannte Objekte vorher am Bus lesen.
   Warnbeispiel: die Wärmemengen „zur Sicherheit unter beiden Modulen“ abzufragen (MI 09/01 **und**
   MI 07/00, s. [Abschnitt 5](#belegte-leseobjekte-wärmemengen)) kostet bei jedem Aufruf vier
   `CM 05` – nach wenigen Aufrufen sind die zehn Plätze verbraucht.
2. **Mindestens eine Minute zwischen zwei JSON-Befehlen**, besser mehr.
3. **Kein Polling über JSON.** Werte am Bus lesen.
4. **Ein Client** an der JSON-Schnittstelle. Wer mehrere Nutzer hat (App, Automation), führt alle
   über **eine** Stelle mit einer Sperre (Mutex). Diese Firmware ruft die JSON-Schnittstelle
   nie von selbst auf, nur auf Bedienung: Schalten über JSON (Rückweg), Schreibfeld, Abruf von Hand.
5. **Wenige Register pro Anfrage.** An der gemessenen Anlage beantwortet der WEM höchstens sieben, das achte
   bekommt immer `CM 05` ([Warum nie über JSON gepollt wird](#warum-nie-über-json-gepollt-wird)); ein anderer
   Bericht nennt sporadische `CM 05` schon bei mehr als sechs
   ([kraiz #15](https://github.com/kraiz/hassio-weishaupt/issues/15)).
6. Nach einer Leerantwort oder reinem `CM 05` **nicht sofort wiederholen**, sondern gestuft
   pausieren: 2 → 4 → 8 → 16 → 30 min; die erste gesunde Antwort setzt zurück.
7. **Keep-Alive bringt nichts:** der WEM schließt jede TCP-Verbindung nach der Antwort selbst.
   Der einzige Hebel ist die Zahl der Anfragen.

## 9. Beispiel: Heizkreis auf „Zeitprogramm 1“

**1. Befehl.** Der Knopf „ZP 1“ in der Docker-App schickt

```
MQTT  <gerät>/cmd/heizkreis   {"id":"app-mg3k2-1","wert":2,"quelle":"App"}
```

Die App auf dem Board (Paket `app`) übergibt denselben Wunsch ohne MQTT direkt an die Firmware, Quelle
ebenfalls `App`. Gleichwertig: `2` oder `Zeitprogramm 1` als Text per MQTT (Kennung vergibt dann das Board), oder
„Heizkreis Betriebsart setzen“ in der Experten-Oberfläche bzw. in Home Assistant. Auf
`<gerät>/befehl/status` erscheinen `angenommen` und `vorgemerkt` (mit `warten_s`), später
`gesendet`, `pruefe_bus` und `bestaetigt` – siehe [TOPICS.md](TOPICS.md#gerätbefehlstatus).
Das Board meldet zusätzlich (veraltet) in „Ergebnis letzter Schaltbefehl“:

```
vorgemerkt (App): Heizkreis Zeitprogramm 1
```

**2. Warteschlange.** Läuft noch der Mindestabstand zum vorigen Befehl (per CAN ab Werk <!--ab-werk:abstand_can-->10<!--/ab-werk--> s, über JSON
<!--ab-werk:abstand_json-->60<!--/ab-werk--> s), steht in „Warteschlange“ etwa
`1. Heizkreis -> Zeitprogramm 1 (App) - naechster Befehl ab 14:03:12`. Kommt vorher ein
anderer Heizkreis-Wunsch, ersetzt er diesen.

**3. Befehl an den WEM.** Nach Ablauf des Abstands, per CAN (Vorgabe seit v42):

```
CAN-ID 0x601   2F 33 29 02 02 00 00 00      Schreib-Anfrage Knoten 1, 0x2933/2, 1 Byte, Wert 2
Antwort 0x581  60 33 29 02 …                Quittung
```

Status: `sende per CAN an WEM ...`, dann `WEM hat per CAN bestaetigt, pruefe am Bus ...`.

Über JSON (Schalter „Schalten per CAN statt JSON“ aus) stattdessen:

```
POST http://<WEM>/ajax/CanApiJson.json
{"ID":"…","SRC":"DDC","CAPI":{"NN":1,"N01":{"VG":"030200253302000102"}}}

VG = 03 02 00 2533 02 0001 02
     CM MI MX OX   OS VS   VA      schreiben, HZK0, Betriebsart, 1 Byte, Wert 2
```

Status: `sende an WEM ...`, dann bei einer Antwort mit `CM 04`:
`WEM hat bestaetigt, pruefe am Bus ...`.

**4. Kontrolle am Bus.** 3 s nach dem Senden (über JSON: 3 s nach der Antwort) liest das Board:

```
CAN-ID 0x601   40 33 29 02 00 00 00 00      Lese-Anfrage Knoten 1, 0x2933/2
Antwort 0x581  4F 33 29 02 02 00 00 00      1 Byte, Wert 2 = Zeitprogramm 1
```

**5. Ergebnis.**

| Fall | „Ergebnis letzter Schaltbefehl“ |
|---|---|
| Bus meldet 2 | `14:03:16 OK: Heizkreis steht auf Zeitprogramm 1` |
| Bus meldet anderen Wert | `14:03:16 NICHT uebernommen, Heizkreis steht auf Standby` |
| WEM bricht per CAN ab (`80`) | `14:03 per CAN 01 2933/02: abgelehnt, Abbruch …` – keine Kontroll-Lesung |
| über JSON: WEM antwortet `CM 05` | `14:03 abgelehnt (CM=05) - VG 05…` – keine Kontroll-Lesung |
| binnen 10 s keine Antwort auf die Kontroll-Lesung | `keine Rueckmeldung vom Bus - Ergebnis unbekannt` |
| WEM quittiert per CAN nicht | `keine CAN-Antwort vom WEM, pruefe trotzdem am Bus ...`, danach wie oben |
| über JSON: WEM nicht erreichbar | `WEM nicht erreichbar, pruefe trotzdem am Bus ...`, danach wie oben |

Das Schaltprotokoll bekommt eine Zeile wie
`27.09. 14:03 Heizkreis -> Zeitprogramm 1 (App): ok`. Die Handy-App wertet ab v27 nur noch die
Phase aus `befehl/status` bzw. `status/json` aus und zeigt sie drei Minuten lang – grün bei
`bestaetigt`, rot bei `gescheitert` (nicht übernommen, vom WEM abgelehnt, keine Rückmeldung) oder
`abgelehnt` (ungültiger Wert).

Am Bus ändern sich danach die Statusbits im PDO `0x1C1` (Bit `0x1000` Heizkreis Standby fällt weg);
„Heizkreis Status“ springt von „Standby“ auf „Zeitprogramm“.

## 10. Beispiel: Warmwasser aus

**1. Befehl.** Als Klartext per MQTT:

```
MQTT  <gerät>/cmd/warmwasser   Aus
```

Angenommen werden auch `aus`, `2`, `OFF`, `off`, `false`; in der Experten-Oberfläche „Warmwasser setzen“ → Aus.
Der Knopf „AUS“ in der App schickt dasselbe als JSON mit Kennung (wie in Abschnitt 9, Quelle `App`).
Status: `vorgemerkt (MQTT): Warmwasser Aus`. Warteschlange und Mindestabstand wie beim Heizkreis;
steht gleichzeitig ein Heizkreis-Wunsch an, geht der zuerst, der zuerst kam (bis v37: immer der Heizkreis).

**2. Befehl an den WEM.** Per CAN (Vorgabe seit v42):

```
CAN-ID 0x601   2F 20 2A 02 02 00 00 00      Schreib-Anfrage Knoten 1, 0x2A20/2, 1 Byte, Wert 2 = Aus
Antwort 0x581  60 20 2A 02 …                Quittung
```

Über JSON (Schalter „Schalten per CAN statt JSON“ aus) stattdessen:

```
{"ID":"…","SRC":"DDC","CAPI":{"NN":1,"N01":{"VG":"030300252002000102"}}}

VG = 03 03 00 2520 02 0001 02
     CM MI MX OX   OS VS   VA      schreiben, WW0, Betriebsart, 1 Byte, Wert 2 = Aus
```

Erwartet wird `CM 04`.

**3. Kontrolle am Bus**, 3 s nach dem Senden (über JSON: 3 s nach der Antwort):

```
CAN-ID 0x601   40 20 2A 02 00 00 00 00      Lese-Anfrage Knoten 1, 0x2A20/2
Antwort 0x581  4F 20 2A 02 02 00 00 00      Wert 2 = Aus
```

Ergebnis: `14:05:04 OK: Warmwasser Aus`, im Protokoll
`27.09. 14:05 Warmwasser -> Aus (MQTT): ok`.

**4. Folgen am Bus.** Nach dem Warmwasser-Befehl löst die Firmware eine Anlass-Lesung aus und liest
8 s später die Warmwasserwerte nach:

```
0x601  40 20 2A 02 …   Warmwasser Betriebsart   -> 2 (Aus)
0x601  40 2C 2A 02 …   Warmwasser Soll aktuell  -> 80 = 8,0 °C
0x601  40 39 2A 02 …   Warmwasser Soll normal   -> unverändert
```

„Warmwasser Soll aktuell“ fällt bei Aus auf **8,0 °C** – an der Anlage beobachtet, das Umschalten
selbst am Display bestätigt. „Vorlaufsoll Anforderung“ (`0x2640`/3) stand im Sommerbetrieb bei
Warmwasser Ein auf 50 °C, bei Aus auf 23 °C. „Warmwasser aktiv“ (Kesselstatus 15 im PDO `0x182`) zeigt weiter, ob gerade geladen wird.

Wie das Objekt `0x2A20`/2 gefunden wurde, steht in [Abschnitt 7](#7-neue-objekte-finden-scan-und-differenz).

## 10a. Raumgerät WEM-RG2 am Bus (Mitschnitt, November 2026)

Ausgelagert nach [VERSUCHE.md](VERSUCHE.md#10a-raumgerät-wem-rg2-am-bus-mitschnitt-november-2026).

## 10b. CAN-Tunnel über WLAN (Versuch, Zweig `can-tunnel`)

Ausgelagert nach [VERSUCHE.md](VERSUCHE.md#10b-can-tunnel-über-wlan-versuch-zweig-can-tunnel).

## 10c. Störmeldungen am Bus und Fehlercodes

Der WEM meldet Warnungen und Fehler **von sich aus** als CANopen-Notfalltelegramm (EMCY) auf `0x081`, der Kessel auf
`0x082`. Die Firmware hört nur zu und zeigt die Meldung als „Meldung WEM“ bzw. „Meldung Kessel“ an, zum Beispiel
„W 1501: Verbindung zum Raumgerät RG2 unterbrochen (Teilnehmer 1)“. Angefragt wird dafür nichts.

```
081: FF FF 01 02 05 DD 01 00   Meldung: Fehlercode FFFF, Register 01, Art 02 = Warnung, Nummer 0x05DD = 1501
081: 00 00 01 00 00 00 01 00   erledigt (Fehlercode 0000)
```

Byte 0–1 Fehlercode (`0000` = keine Meldung), Byte 3 die Art, Byte 4–5 die Nummer (big endian). Belegt ist bisher nur
die Warnung 1501 (RG2 abgezogen, mehrfach). Dass Art `01` ein Fehler ist, ist eine **Annahme** nach dem Fehlerblock
`0x273F` (so liest ihn kraiz/hassio-weishaupt); einen echten Fehler haben wir am Bus noch nicht gesehen. Die
Bedeutung von Byte 6 ist offen. Vom Kessel kamen bisher nur `082: 00 … 01` und `082: 00 … 88` ohne Fehlercode.

**Kommunikationswarnungen:** Die Nummern 1101–1925 stehen für eine unterbrochene Verbindung zu einem Busteilnehmer, die
letzten beiden Stellen sind seine Nummer: 1101–1112 Systemgerät, 1201–1212 Feuerungsautomat, 1302–1325 EM Heizkreis,
1401 EM Solar, **1501–1532 Raumgerät RG2**, 1601–1632 Raumfühler RF, 1701–1732 Raumgerät RG1, 1800 EM Kaskade,
1902–1925 EM Warmwasser. Ein WEM ist also auf bis zu 32 RG2 und 32 Raumfühler ausgelegt.

**Texte und Urheberrecht.** Nummern und Bedeutung stammen aus Kapitel 10 der Montage- und Betriebsanleitung
„Gas-Brennwertgerät WTC-GW 15 … 32-B“, die Weishaupt öffentlich bereitstellt:
[Deutsch, 83307201](https://www.weishaupt.de/uploads/tx_weishaupt_documents/documents/83307201.pdf),
[Englisch, 83307202](https://www.weishaupt.de/uploads/tx_weishaupt_documents/documents/83307202.pdf).
Die Texte der Anleitung sind urheberrechtlich geschützt (Rechte bei Max Weishaupt SE). Deshalb enthält dieses Repo
**keine Originaltexte**, sondern in [`fehlercodes.yaml`](fehlercodes.yaml) eigene Kurztexte auf Deutsch und Englisch,
die nur die Ursache beschreiben. Was zu tun ist, steht in der Anleitung; die meisten Punkte darf nur eine Fachkraft
beheben.

**Originaltexte für die eigene Anlage:** Wer lieber den Wortlaut der Anleitung sieht, lädt sie selbst herunter und
erzeugt damit lokale Dateien:

```
python3 werkzeuge/fehlertexte_aus_handbuch.py --header 83307201.pdf
```

Das Werkzeug (braucht `pdftotext` aus poppler-utils) liest die Tabellen „Warncode“ und „Fehlercode“ aus und schreibt
`fehlertexte-original.json`, mit `--header` zusätzlich `components/weishaupt_can/fehlertexte_original.h`. Zwei Wege:

- **Ohne eigenen Bau** (auch mit dem Release-Image): Einstellungsseite → „Störmeldungen“ → `fehlertexte-original.json`
  mit Experten-Passwort einspielen (nur im Heimnetz). Das Board prüft die Datei, legt sie ins Dateisystem und nimmt sofort
  die Originaltexte; „Zurück auf eigene Texte“ löscht sie wieder. Die Infoseite zeigt, welche Texte gelten.
- **Beim Bau:** mit `--header` erzeugen und die Firmware bauen.

Was in der Datei fehlt, zeigt das Board weiter mit den eigenen Texten. Englisch: in der Haupt-YAML `meldungen_englisch: "true"` setzen und die
englische Anleitung nehmen. Die englische Ausgabe (2018) kennt sechs neuere Codes nicht (W 7, W 8, W 1101, W 1302,
W 1800, W 1902); für sie bleibt der eigene englische Text. Beide Dateien stehen in `.gitignore` und gehören **nicht** in ein
öffentliches Repo.

Dank an [cg089](https://github.com/cg089/weishaupt-wem-can-esphome/tree/olimex-esp32-evb) für die erste Zuordnung der
Kommunikationswarnungen und an [kraiz](https://github.com/kraiz/hassio-weishaupt) für den Aufbau des Fehlerblocks.

## 11. Quellen

- [MenkeC/Weishaupt-C3supermini](https://github.com/MenkeC/Weishaupt-C3supermini) – 50 kbit/s,
  Kessel = Knoten 2, SDO `0x602`/`0x582`, PDOs `0x201`/`0x241`.
- [geronet1/wem-python](https://github.com/geronet1/wem-python) – Objektbedeutungen, u. a. die
  Schreibtelegramme auf `0x6C2`; weitere Kesselobjekte in
  [Abschnitt 7a](#7a-übernommene-objekte-aus-anderen-projekten-ungeprüft).
- [BorgNumberOne/Weishaupt_CanApiJson](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) –
  Registertabelle der JSON-Schnittstelle mit MI/MX/OX/OS, Faktoren und Aktualisierungsklassen;
  Hinweis auf `/sd/systable.csv`.
- [kraiz/hassio-weishaupt](https://github.com/kraiz/hassio-weishaupt) – Issues
  [#15](https://github.com/kraiz/hassio-weishaupt/issues/15) (zehn Plätze),
  [#13](https://github.com/kraiz/hassio-weishaupt/issues/13) (Portal),
  [#9](https://github.com/kraiz/hassio-weishaupt/issues/9) (zwei Clients).
- [Home-Assistant-Forum: Weishaupt WTC, CAPI VG, CanApiJson](https://community.home-assistant.io/t/weishaupt-wtc-weishaupt-capi-vg-canapijson/997400) –
  Aufbau des VG-Felds.
- [Handbuch WEM-Modbus TCP (PDF)](https://www.loebbeshop.de/media/67944/file/static/pdf/weishaupt/manual-wem-modbustcp.pdf) –
  Datenpunktliste mit Aktualisierungsklassen; Gateway und Portal schließen sich aus.
- [Montage- und Betriebsanleitung WTC-GW 15–32-B (PDF)](https://www.intec-heizung.de/media/pdf/9f/c7/72/Weishaupt-Thermo-Condens-WTC-GW_15-32-B-Montage-u-Betriebsanleitung.pdf) –
  Parameter 10.8.1 (JSON-Schnittstelle), Klemme H/L/−/+.
- [WEM-Portal-FAQ (PDF)](https://www.wemportal.com/Web/Documents/FAQ/FAQ.de.pdf?lang=de)
