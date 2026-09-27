# Protokoll: CAN-Bus und WEM-JSON

Was auf dem CAN-Bus einer Weishaupt-Brennwertheizung mit WEM-Systemgerät passiert, was diese
Firmware davon mithört, was sie selbst anfragt und wie sie über die JSON-Schnittstelle des WEM
schaltet. Gemessen an einer **WTC-GW 15-B** mit WEM-Systemgerät; wo eine Angabe nur aus fremden
Quellen stammt oder nur plausibel ist, steht das dabei.

> Die Firmware schreibt **nie** auf den Bus. Am Bus stellt sie ausschließlich Lese-Anfragen
> (SDO, Kommandobyte `0x40`). Geschaltet wird nur über die JSON-Schnittstelle des WEM.

- [1. Überblick](#1-überblick)
- [2. Mithören und Lesen auf Anforderung](#2-mithören-und-lesen-auf-anforderung)
- [3. Was mitgehört wird](#3-was-mitgehört-wird)
- [4. Was auf Anforderung gelesen wird](#4-was-auf-anforderung-gelesen-wird)
- [5. Schreiben über den WEM (JSON)](#5-schreiben-über-den-wem-json)
- [6. Weboberfläche: lesen und schreiben](#6-weboberfläche-lesen-und-schreiben)
- [7. Neue Objekte finden: Scan und Differenz](#7-neue-objekte-finden-scan-und-differenz)
- [8. Wann sich der WEM aufhängt](#8-wann-sich-der-wem-aufhängt)
- [9. Beispiel: Heizkreis auf „Zeitprogramm 1“](#9-beispiel-heizkreis-auf-zeitprogramm-1)
- [10. Beispiel: Warmwasser aus](#10-beispiel-warmwasser-aus)
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
| `40` | Lese-Anfrage (Upload). **Das einzige, das diese Firmware sendet.** |
| `4F` / `4B` / `47` / `43` | Antwort mit 1 / 2 / 3 / 4 Datenbytes |
| `2F` / `2B` / `23` | Schreib-Anfrage mit 1 / 2 / 4 Datenbytes (sendet nur der WEM) |
| `60` | Bestätigung eines Schreibvorgangs |
| `80` | Abbruch; Bytes 4–7 = Abbruchcode, z. B. `06020000` Objekt existiert nicht, `06090011` Subindex existiert nicht |

Beispiel: Lese-Anfrage an Knoten 1, Objekt `0x2933` Subindex 2:

```
CAN-ID 0x601   40 33 29 02 00 00 00 00
Antwort 0x581  4F 33 29 02 02 00 00 00     -> 1 Byte, Wert 2
```

## 2. Mithören und Lesen auf Anforderung

Das Board kommt auf zwei Wegen an Werte:

| | Mithören (passiv) | Lesen auf Anforderung (aktiv) |
|---|---|---|
| Was | PDOs, dazu die SDO-Antworten auf Fragen, die **der WEM selbst** an den Kessel stellt | eigene SDO-Lese-Anfragen `40 …` |
| Last | keine – das Board sendet nichts | eine Anfrage plus eine Antwort je Wert |
| Abschaltbar | nein, läuft immer | ja, Schalter **„Eigene CAN-Anfragen“** (Vorgabe: aus) |

Der WEM fragt den Kessel ohnehin ständig ab – an der gemessenen Anlage rund drei SDO-Anfragen pro
Sekunde. Deshalb kommt ein großer Teil der Kesselwerte beim Board an, ohne dass es selbst fragt.
Welche das sind, steht in [Abschnitt 3](#3-was-mitgehört-wird).

### Takt der eigenen Anfragen

Das WEM-Portal lässt für seinen Datenlogger höchstens alle 40 s je Wert abfragen; die
Datenpunktliste des WEM-Modbus-Gateways nennt Mindestabstände von 30 s / 60 s / 10 min. Die
Firmware fragt deshalb keinen Wert öfter als alle 40 s:

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
- wenn sich die Statusbits ändern (PDO `0x1C1`, etwa bei einer Umstellung am Display oder im Portal),
- nach einem eigenen Warmwasser-Schaltbefehl,
- auf Wunsch: Knopf „Status lesen“ (Weboberfläche, Handy-App) bzw. MQTT `<gerät>/cmd/status`.
  Wünsche werden höchstens alle 10 s angenommen, gesendet wird nur bei lebendem Bus.

Die Heizkreis-Betriebsart wird dabei höchstens alle 2 min gelesen – außer auf ausdrücklichen Wunsch. Bekannte Lücke: ein Wechsel
zwischen zwei Zeitprogrammen am Display ändert die Statusbits nicht und bleibt bis zum nächsten
Anlass unbemerkt.

### Warum nie über JSON gepollt wird

Die JSON-Schnittstelle des WEM ist ein Übersetzer zwischen HTTP und dem CAN-Bus – und der ist
empfindlich (s. [Abschnitt 8](#8-wann-sich-der-wem-aufhängt)). Regelmäßiges Abfragen über JSON hat
sie an der gemessenen Anlage fünfmal bis zum Stromlos-Machen lahmgelegt. Der Bus selbst hat damit
kein Problem: Der WEM fragt den Kessel dort dreimal pro Sekunde ab. **Lesen deshalb immer am Bus,
den WEM nur zum Schalten ansprechen.**

### Was bleibt, wenn der WEM ausfällt

| Ausfall | Folge |
|---|---|
| nur die JSON-Schnittstelle hängt (der bisher beobachtete Fall) | am Bus läuft alles weiter; das Board liest unverändert, nur Schalten geht nicht |
| der WEM ist ganz weg | es fehlen alle Werte, die nur als Antwort auf **WEM-Anfragen** mitgehört werden, und alles, was der WEM sendet |

Nach CANopen-Konvention stammen PDOs auf `0x180 + n` vom Knoten *n*; `0x182` ist damit vom Kessel,
`0x1C1` vom WEM. Welche Werte bei einem vollständigen WEM-Ausfall tatsächlich weiterlaufen, ist
**nicht getestet**. Mit eingeschalteten eigenen Anfragen liest das Board die Kesselwerte aus
[Abschnitt 4](#4-was-auf-anforderung-gelesen-wird) selbst und ist damit vom WEM unabhängig.

## 3. Was mitgehört wird

**Stand:** *belegt* = an der Anlage gegen Kesseldisplay oder eine unabhängige Quelle geprüft;
*plausibel* = Werte passen, aber nicht gegengeprüft; *unbestätigt* = Deutung aus fremden Quellen.

### PDOs und Schreibtelegramme

| CAN-ID | Bytes | Inhalt | Faktor | Name in der Firmware | Stand |
|---|---|---|---|---|---|
| `0x201` | 1–2 | Außentemperatur, int16 | 0,1 °C | Aussentemperatur | **belegt** (Display, Wetterdienst) |
| `0x201` | 0 | Systembetriebsart: 0 Aus, 1 Standby, 2 Sommer, 3 Automatik | – | Systembetriebsart | plausibel |
| `0x241` | 0–1 | Heizanforderung, int16 | 0,1 °C | Heizanforderung | plausibel (0 bei „Heizkreise inaktiv“) |
| `0x241` | 2–3 | Kesseltemperatur (gleich `0x2532`) | 0,1 °C | Kesseltemperatur | plausibel |
| `0x241` | 6–7 | Warmwassertemperatur | 0,1 °C | Warmwasser | **belegt** (Display) |
| `0x181` | 0–5 | Stunde, Minute, Jahr − 2000, Monat, Tag, Wochentag | – | Uhrzeit Heizung | **belegt** |
| `0x182` | 0 | Kesselstatus: 0 Standby, 1 Aus, 10 Heizbetrieb, 15 Warmwasserbetrieb, 101 Kaminfeger, 104 Wartung | – | Kesselstatus, Warmwasser aktiv (Status 15) | **belegt** für den Ruhezustand |
| `0x1C1` | 2–3 | Statusbits Knoten 1 (Objekt `0x274D`): `0x1000` Heizkreis Standby, `0x0040` Heizbetrieb, `0x0010` Warmwasser-Ladung | – | Heizkreis Status | plausibel |
| `0x6C2` | SDO-Schreibtelegramm, Objekt `0x2699`/1 | Rücklauftemperatur VPT | 0,1 °C | Ruecklauf | plausibel |
| `0x6C2` | Objekt `0x2697`/1 | Vorlauftemperatur VPT | 0,1 °C | Vorlauf VPT | plausibel |
| `0x6C2` | Objekt `0x2698`/1 | Sollleistung | 0,01 % | Sollleistung | plausibel |

Zu `0x6C2`: Auf dieser CAN-ID liegen alle paar Sekunden Schreibtelegramme (Kommando `2B`/`2F`/`23`),
bestätigt auf `0x682`. Die Objektbedeutung stammt aus
[geronet1/wem-python](https://github.com/geronet1/wem-python); die Werte passen an der Anlage
(Rücklauf 17,9 °C bei 18,5 °C Vorlauf, Brenner aus). Dort liegen außerdem `0x261E`/1–2, `0x2A29`/1–2,
`0x2694`/1 und `0x2695`/1 – nicht ausgewertet.

Nicht ausgewertet werden außerdem die PDOs `0x1C2` und `0x082` sowie die Heartbeats `0x701`/`0x702`.

### SDO-Antworten auf Fragen des WEM

Das Board wertet jede SDO-Antwort auf `0x581` und `0x582` aus, egal wer gefragt hat. Im
Mitschnitt fragte der WEM (mit dem Kommandobyte `A4`) unter anderem diese Kesselobjekte selbst ab –
deren Werte kommen also auch bei ausgeschalteten eigenen Anfragen:

| Objekt (Knoten 2) | Name in der Firmware | vom WEM abgefragt |
|---|---|---|
| `0x2532`/0 | Kesseltemperatur | ja |
| `0x2534`/0 | Leistung | ja |
| `0x2537`/0 | Abgastemperatur | ja |
| `0x2541`/0 | Brenner, Brennerphase, Brennerstarts | ja, selten |
| `0x2545`/0 | Vorlauf Soll | ja, selten |
| `0x2713`/2 | Volumenstrom | ja, selten |
| `0x2714`/2 | Anlagendruck | ja |
| `0x2726`/2, `0x2727`/2, `0x2728`/2 | Wärmemengen Vortag | ja – **nur** so, die Firmware fragt sie nie selbst |
| `0x2536`/0 | Vorlauf | **nein** – kommt nur mit eigenen Anfragen |
| `0x2540`/0 | Drehzahl | **nein** – kommt nur mit eigenen Anfragen |

Der WEM fragt noch weitere Objekte ab (u. a. `0x2530`, `0x2531`, `0x2533`/2, `0x2731`/2, `0x2739`,
`0x2753`/2), die die Firmware nicht auswertet. `0x2533`/2 ist laut Weishaupt-Registertabelle und
geronet1 die Rücklauftemperatur VPT – an der Anlage **noch nicht geprüft**.

Wie oft der WEM ein Objekt abfragt, schwankt stark; „selten“ heißt: deutlich seltener als einmal
pro Minute. Die Liste stammt aus einem Mitschnitt im Sommerbetrieb und ist nicht vollständig.

## 4. Was auf Anforderung gelesen wird

Alle Anfragen mit Kommandobyte `0x40`. Die Größe ergibt sich aus der Antwort (`4F` 1 Byte, `4B`
2 Byte, `43` 4 Byte); die Firmware liest bei Knoten 2 bis zu 4 Byte, bei Knoten 1 meist 2 Byte mit
Vorzeichen (Betriebsarten: 1 Byte). Der Wert `0x8000` bedeutet „kein Wert“ und wird verworfen.

| Knoten | Objekt | Faktor | Takt | Name in der Firmware | Stand |
|---|---|---|---|---|---|
| 2 | `0x2532`/0 | 0,1 °C | 40 s | Kesseltemperatur | **belegt** (Display) |
| 2 | `0x2537`/0 | 0,1 °C | 40 s | Abgastemperatur | **unbestätigt** – in manchen Vorlagen „Rücklauf“ genannt |
| 2 | `0x2536`/0 | 0,1 °C | 40 s | Vorlauf | **unbestätigt** – antwortet, Deutung erst bei laufendem Brenner prüfbar (Vorlauf muss dann über Kessel liegen) |
| 2 | `0x2534`/0 | 0,01 % | 40 s | Leistung | aus der Vorlage übernommen, bei laufendem Brenner prüfen |
| 2 | `0x2540`/0 | 1 U/min | 40 s | Drehzahl | aus der Vorlage übernommen, bei laufendem Brenner prüfen |
| 2 | `0x2541`/0 | Phase: 0 aus, 1 Vorbelüftung, 2 Steuerbetrieb, 3 Regelbetrieb, 4 Nachbelüftung | 40 s | Brenner Status, Brenner, Brennerphase, Brennerstarts | **belegt** (Display „Heizkreise inaktiv“) |
| 2 | `0x2545`/0 | 0,1 °C | 60 s | Vorlauf Soll | **unbestätigt** |
| 2 | `0x2713`/2 | 1 l/h | 60 s | Volumenstrom | aus der Vorlage übernommen, bei laufendem Brenner prüfen |
| 2 | `0x2714`/2 | 0,01 bar | 60 s | Anlagendruck | **belegt** (Display) |
| 1 | `0x2907`/2 | 0,1 °C | 60 s | Vorlauf Heizkreis | plausibel (JSON MI 02 `0x2507` Vorlaufisttemperatur, +0x400) |
| 1 | `0x2640`/3 | 0,1 °C | 60 s | Vorlaufsoll Anforderung | plausibel (50 °C bei Warmwasser Ein, 23 °C bei Aus beobachtet) |
| 1 | `0x2958`/2 | 0,1 °C | 5 min | Raumsoll aktuell | plausibel (JSON MI 02 `0x2558`, +0x400) |
| 1 | `0x2933`/2 | Code 1–8, s. [Abschnitt 5](#5-schreiben-über-den-wem-json) | Anlass, höchstens alle 2 min | Heizkreis Betriebsart | **belegt** für Standby, Zeitprogramm 1–3, Sommer. **Komfort, Normal, Absenk nie beobachtet** |
| 1 | `0x2A20`/2 | 1 Ein, 2 Aus | Anlass | Warmwasser Betriebsart | **belegt** – geschaltet und am Display gesehen |
| 1 | `0x2A2C`/2 | 0,1 °C | Anlass | Warmwasser Soll aktuell | plausibel (JSON MI 03 `0x252C`, +0x500; 8,0 °C bei Warmwasser Aus) |
| 1 | `0x2A39`/2 | 0,1 °C | Anlass | Warmwasser Soll normal | plausibel (JSON MI 03 `0x2539`, +0x500) |

„Brenner“ (0/1) und der Zähler „Brennerstarts“ zählen nur mit Flamme (Phase 2 oder 3).
Schaltet man „Eigene CAN-Anfragen“ aus, gehen die Werte, die nur durch eigene Anfragen aktuell
blieben, auf „unbekannt“; fragt der WEM einen davon selbst ab, kommt er über das Mithören zurück.

Rückmeldungen von anderen Anlagen sind willkommen – besonders Messungen bei laufendem Brenner.

## 5. Schreiben über den WEM (JSON)

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

Die Firmware sendet nur `CM 03` und nie `CM 01`. Sie wertet die ersten zwei Zeichen der Antwort
aus: `04` oder `02` gilt als bestätigt, `05` als abgelehnt.

### Was die Firmware schreibt

| Ziel | MI | MX | OX | OS | VS | Werte | Beobachtet |
|---|---|---|---|---|---|---|---|
| Heizkreis-Betriebsart | `02` | `00` | `0x2533` | `02` | `0001` | 1 Standby, 2 Zeitprogramm 1, 3 Zeitprogramm 2, 4 Zeitprogramm 3, 5 Sommer, 6 Komfort, 7 Normal, 8 Absenk | 1–5; **6–8 nie** |
| Warmwasser-Betriebsart | `03` | `00` | `0x2520` | `02` | `0001` | 1 Ein, 2 Aus | beide |

Die Heizkreis-Codes stehen so in der Weishaupt-Registertabelle. Die Warmwasser-Betriebsart steht
dort nicht; sie wurde per Scan und Differenz gefunden (s. [Abschnitt 7](#7-neue-objekte-finden-scan-und-differenz)).

Über das Schreibfeld der Weboberfläche lassen sich weitere Objekte schreiben, aber nur in den
Modulen MI 01, 02 und 03 (s. [Abschnitt 6](#6-weboberfläche-lesen-und-schreiben)).

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

**Direkt per SDO schreibt die Firmware nie** – weder auf den Kessel noch auf den WEM. Schreiben
läuft ausschließlich über JSON; der Bus dient nur zur Kontrolle.

### Ablauf in der Firmware

1. Ein Wunsch kommt über die Weboberfläche, Home Assistant oder MQTT (`<gerät>/cmd/heizkreis`,
   `<gerät>/cmd/warmwasser`) und landet in der **Warteschlange**: höchstens ein Eintrag je Ziel,
   der neueste Wunsch ersetzt den alten; Heizkreis vor Warmwasser.
2. Einmal pro Sekunde prüft die Firmware die Warteschlange. Gesendet wird höchstens **ein Befehl
   pro Minute** (Sperrminute ab dem letzten Senden).
3. Die Antwort des WEM wird angezeigt. Bei `CM 05` ist der Vorgang beendet („abgelehnt“).
4. Sonst liest die Firmware **3 s** nach der Antwort das Zielobjekt am Bus (Knoten 1, `0x2933`/2
   bzw. `0x2A20`/2). Diese eine Kontroll-Lesung geht auch bei ausgeschalteten eigenen Anfragen raus.
5. **Der Bus entscheidet, nicht die WEM-Antwort:** Stimmt der gelesene Wert mit dem Wunsch überein,
   heißt es „OK“, sonst „NICHT übernommen“. Kommt binnen 10 s keine Antwort, „keine Rückmeldung
   vom Bus – Ergebnis unbekannt“. Auch nach einer leeren oder unklaren WEM-Antwort wird am Bus
   geprüft – es wurde schon beobachtet, dass trotz leerer Antwort geschaltet war.
6. Ergebnis steht in **„Ergebnis letzter Schaltbefehl“** und im **Schaltprotokoll**.

## 6. Weboberfläche: lesen und schreiben

Die Weboberfläche des Boards (`http://<Board>`, Benutzer `admin`) ist in Gruppen geteilt. Die
Namen hier sind die aus der Firmware.

### Lesebefehl (nur lesen)

| Element | Funktion |
|---|---|
| **Lesebefehl (KN IDX SUB hex)** | Eingabe `KNOTEN INDEX SUB` in hex, z. B. `01 2933 02` |
| **Lesen** | sendet eine Lese-Anfrage (`0x40`) an Knoten `0x600 + KN` – nur, wenn der Bus lebt |
| **Lesebefehl Antwort** | `01 2933/02 = 2 (0x02, 1 Byte)` oder `… Abbruch 06020000 (Objekt fehlt?)`; bei totem Bus „Bus tot - nicht gesendet“ |

Dasselbe geht per MQTT: `<gerät>/cmd/lesen` mit `01 2933 02`.

### Betriebsarten: Status lesen

| Element | Funktion |
|---|---|
| **Status lesen** (Knopf) | liest Heizkreis-Betriebsart (`0x2933`/2) und – mit Paket warmwasser – `0x2A20`, `0x2A2C`, `0x2A39` (je Subindex 2) einmal von Knoten 1. Nur Leseanfragen, kein WEM-JSON |
| **Status gelesen** | `27.09. 09:32:51 gelesen`, während der Anforderung `09:32:46 angefordert ...`, bei totem Bus `… Bus schweigt - nicht gelesen` |

Per MQTT: `<gerät>/cmd/status` mit beliebigem Inhalt. Höchstens ein Wunsch alle 10 s.

### Schalten (über WEM, Kontrolle am Bus)

| Element | Funktion |
|---|---|
| **Heizkreis Betriebsart setzen** | Auswahl Standby … Absenk; zeigt nach jeder Kontroll-Lesung den Wert am Bus |
| **Warmwasser setzen** | Auswahl Ein / Aus |
| **Warteschlange** | vorgemerkte Befehle mit Quelle, z. B. `1. Heizkreis -> Zeitprogramm 1 (MQTT) - naechster Befehl ab 14:03:12`, sonst `leer` |
| **Ergebnis letzter Schaltbefehl** | Fortschritt und Ergebnis, s. [Abschnitt 5](#ablauf-in-der-firmware) |
| **Warteschlange leeren** | verwirft alle vorgemerkten Befehle (wird protokolliert) |
| **Schaltprotokoll** | die letzten zehn Befehle mit Uhrzeit, Quelle und Ergebnis; per MQTT retained unter `<gerät>/schaltprotokoll`. Nach einem Neustart leer |

### Schreibbefehl über WEM (Experten)

| Element | Funktion |
|---|---|
| **Schreibbefehl (MI MX OX OS WERT hex)** | z. B. `03 00 2520 02 02` = Warmwasser Aus. WERT 1–4 Hexziffern; die Länge (VS) ergibt sich daraus |
| **Schreiben** | baut `CM 03` und sendet über JSON. **Nur MI 01, 02, 03** – der Kessel ist gesperrt. Es gilt dieselbe Sperrminute wie beim Schalten |
| **Schreibbefehl Antwort** | `OK, WEM bestaetigt (VG …)`, `abgelehnt (CM=05): Objekt unbekannt oder nicht schreibbar - VG …`, `unklare Antwort, HTTP …` oder `WEM nicht erreichbar` |

Das Schreibfeld prüft **nicht** am Bus nach und kennt keine Warteschlange. Vor dem Schreiben das
Objekt über den Lesebefehl am Bus ansehen und die Abbildung aus
[Abschnitt 5](#abbildung-json-objekt--can-objekt) anwenden. Jede `CM 05`-Antwort kostet einen der
zehn Plätze aus [Abschnitt 8](#8-wann-sich-der-wem-aufhängt).

### Einstellungen

| Element | Funktion |
|---|---|
| **Eigene CAN-Anfragen** | an = Leseanfragen im Takt aus [Abschnitt 2](#takt-der-eigenen-anfragen); aus = nur mithören. Bleibt über Neustarts erhalten; Startzustand über `can_anfragen_start` in der Hauptdatei |
| **CAN-Rohmitschnitt nach MQTT** | jeder empfangene Frame als `ID:DATEN` nach `<gerät>/canraw` (Paket `mqtt`); während eines Scans immer an |
| **Heizung: IP-Adresse** | Adresse des WEM für die JSON-Befehle |
| **Waechter: Neustart nach Minuten ohne Bus (0 = aus)** | startet das Board neu, wenn so lange kein Frame kam (Bus-off-Falle) |

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

> **Nur am Bus scannen, nie über JSON.** Am Bus haben Lesescans mit vielen Abbruchantworten an der
> gemessenen Anlage keine Beeinträchtigung gezeigt. Über JSON dagegen verbraucht jede Anfrage an
> ein nicht vorhandenes Objekt einen von zehn Plätzen bis zum nächsten Stromlos-Machen
> (s. [Abschnitt 8](#8-wann-sich-der-wem-aufhängt)). Ein JSON-Scan legt die Schnittstelle sicher lahm.

Ein Scan erzeugt viel Busverkehr (40 Anfragen pro Sekunde). Bereiche klein halten und nur scannen,
wenn nötig.

## 8. Wann sich der WEM aufhängt

### Symptome

- HTTP 200 mit leerem Körper statt JSON („Leerantwort“),
- oder `CM 05` auf **alle** Register, auch auf solche, die vorher antworteten,
- das WEM-Portal zeigt eingefrorene Werte, obwohl sein Zeitstempel frisch ist,
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

### Regeln, die daraus folgen

1. **Nur Objekte ansprechen, deren Existenz belegt ist.** Unbekannte Objekte vorher am Bus lesen.
2. **Mindestens eine Minute zwischen zwei JSON-Befehlen**, besser mehr.
3. **Kein Polling über JSON.** Werte am Bus lesen.
4. **Ein Client** an der JSON-Schnittstelle. Diese Firmware spricht den WEM nur an, wenn jemand
   schaltet.
5. Nach einer Leerantwort **nicht sofort wiederholen**.

## 9. Beispiel: Heizkreis auf „Zeitprogramm 1“

**1. Befehl.** Der Knopf „ZP 1“ in der Handy-App schickt über die App

```
MQTT  <gerät>/cmd/heizkreis   2
```

Gleichwertig: `Zeitprogramm 1` als Text per MQTT, oder „Heizkreis Betriebsart setzen“ in der
Weboberfläche bzw. in Home Assistant. Das Board meldet in „Ergebnis letzter Schaltbefehl“:

```
vorgemerkt (MQTT): Heizkreis Zeitprogramm 1
```

**2. Warteschlange.** Läuft noch die Sperrminute, steht in „Warteschlange“ etwa
`1. Heizkreis -> Zeitprogramm 1 (MQTT) - naechster Befehl ab 14:03:12`. Kommt vorher ein
anderer Heizkreis-Wunsch, ersetzt er diesen.

**3. JSON an den WEM.** Nach Ablauf der Sperrminute:

```
POST http://<WEM>/ajax/CanApiJson.json
{"ID":"…","SRC":"DDC","CAPI":{"NN":1,"N01":{"VG":"030200253302000102"}}}

VG = 03 02 00 2533 02 0001 02
     CM MI MX OX   OS VS   VA      schreiben, HZK0, Betriebsart, 1 Byte, Wert 2
```

Status: `sende an WEM ...`, dann bei einer Antwort mit `CM 04`:
`WEM hat bestaetigt, pruefe am Bus ...`.

**4. Kontrolle am Bus.** 3 s später liest das Board:

```
CAN-ID 0x601   40 33 29 02 00 00 00 00      Lese-Anfrage Knoten 1, 0x2933/2
Antwort 0x581  4F 33 29 02 02 00 00 00      1 Byte, Wert 2 = Zeitprogramm 1
```

**5. Ergebnis.**

| Fall | „Ergebnis letzter Schaltbefehl“ |
|---|---|
| Bus meldet 2 | `14:03:16 OK: Heizkreis steht auf Zeitprogramm 1` |
| Bus meldet anderen Wert | `14:03:16 NICHT uebernommen, Heizkreis steht auf Standby` |
| WEM antwortet `CM 05` | `14:03 abgelehnt (CM=05) - VG 05…` – keine Kontroll-Lesung |
| binnen 10 s keine Antwort am Bus | `keine Rueckmeldung vom Bus - Ergebnis unbekannt` |
| WEM nicht erreichbar | `WEM nicht erreichbar, pruefe trotzdem am Bus ...`, danach wie oben |

Das Schaltprotokoll bekommt eine Zeile wie
`27.09. 14:03 Heizkreis -> Zeitprogramm 1 (MQTT): ok`. Die Handy-App zeigt den Text drei Minuten
lang in der Statuszeile – grün bei „OK“, rot bei „NICHT übernommen“, „abgelehnt“, „keine
Rückmeldung“ oder „nicht erreichbar“.

Am Bus ändern sich danach die Statusbits im PDO `0x1C1` (Bit `0x1000` Heizkreis Standby fällt weg);
„Heizkreis Status“ springt von „Standby“ auf „Zeitprogramm“.

## 10. Beispiel: Warmwasser aus

**1. Befehl.** Knopf „AUS“ in der Handy-App:

```
MQTT  <gerät>/cmd/warmwasser   Aus
```

Angenommen werden auch `aus`, `2`, `OFF`, `off`; in der Weboberfläche „Warmwasser setzen“ → Aus.
Status: `vorgemerkt (MQTT): Warmwasser Aus`. Warteschlange und Sperrminute wie beim Heizkreis;
steht gleichzeitig ein Heizkreis-Wunsch an, geht dieser zuerst.

**2. JSON an den WEM.**

```
{"ID":"…","SRC":"DDC","CAPI":{"NN":1,"N01":{"VG":"030300252002000102"}}}

VG = 03 03 00 2520 02 0001 02
     CM MI MX OX   OS VS   VA      schreiben, WW0, Betriebsart, 1 Byte, Wert 2 = Aus
```

Erwartet wird `CM 04`.

**3. Kontrolle am Bus**, 3 s später:

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

## 11. Quellen

- [MenkeC/Weishaupt-C3supermini](https://github.com/MenkeC/Weishaupt-C3supermini) – 50 kbit/s,
  Kessel = Knoten 2, SDO `0x602`/`0x582`, PDOs `0x201`/`0x241`.
- [geronet1/wem-python](https://github.com/geronet1/wem-python) – Objektbedeutungen, u. a. die
  Schreibtelegramme auf `0x6C2`.
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
