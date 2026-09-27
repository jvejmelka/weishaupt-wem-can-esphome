# Regeln: Betriebsarten lesen, wenn sich etwas geändert haben könnte

Paket `pakete/verdacht.yaml`, ab Firmware v22, in dieser Form ab **v25**; seit **v30** sind R1–R5
Datenregeln im selben Format wie die eigenen Regeln (Verhalten unverändert, gegen v29 geprüft). Diese Seite erklärt, was
die Regeln tun, woran sie eine Änderung erkennen, wie sicher das ist und wie man sie einstellt.

- [1. Worum es geht](#1-worum-es-geht)
- [2. Übersicht R1–R5](#2-übersicht-r1r5)
- [3. Die Regeln im Einzelnen](#3-die-regeln-im-einzelnen)
- [4. Schattenmodus: ausgeschaltete Regeln beobachten](#4-schattenmodus-ausgeschaltete-regeln-beobachten)
- [5. Eigene Regeln (experimentell)](#5-eigene-regeln-experimentell)
- [6. Einstellen](#6-einstellen)
- [7. Per MQTT schalten](#7-per-mqtt-schalten)
- [8. Protokoll, Log und Ereignisse](#8-protokoll-log-und-ereignisse)
- [9. Geprüft an Mitschnitten](#9-geprüft-an-mitschnitten)
- [10. Grenzen und offene Fragen](#10-grenzen-und-offene-fragen)

## 1. Worum es geht

Die Betriebsarten – Heizkreis (Standby, Zeitprogramm 1–3, Sommer, Komfort, Normal, Absenk) und
Warmwasser (Ein/Aus) – liegen im **WEM-Systemgerät**, nicht im Kessel. Der WEM meldet sie **nicht
von selbst** über den Bus. Wer am Display, im WEM-Portal oder in der Weishaupt-App umschaltet,
hinterlässt am Bus nur **indirekte Spuren**: der WEM fragt den Kessel bestimmte Werte ab, seine
Statusbits ändern sich, der Kessel beginnt eine Warmwasserladung.

Das Board könnte die Betriebsarten periodisch abfragen – das wollen wir nicht. Stattdessen werten
die Regeln diese Spuren aus und **lesen nur dann nach, wenn eine Änderung zu vermuten ist**.

**Was eine Regel darf – und was nicht:**

- Eine Regel **schaltet nie**. Sie löst höchstens **eine Leseanfrage** aus.
- Gelesen wird ausschließlich am Bus, mit SDO-Leseanfragen (Kommandobyte `0x40`) an **Knoten 1**
  (`0x601`): Heizkreis-Betriebsart `0x2933/2`, Warmwasser `0x2A20/2`, `0x2A2C/2`, `0x2A39/2`.
- **Nie** eine Anfrage an den Kessel (Knoten 2), **nie** eine Anfrage an die JSON-Schnittstelle
  des WEM, **nie** ein Raster.
- Gelesen wird nur bei lebendem Bus und außerhalb der Anlaufpause (nach Board-Start 1 min, nach
  einem Busausfall 10 min).
- Jede Regel hat einen **Mindestabstand**; alle zusammen haben eine **Obergrenze pro Stunde**
  (Vorgabe 6).
- **2 Minuten nach einem eigenen Schaltbefehl ruhen alle Regeln** – der Befehl liest ohnehin selbst
  nach, und seine Spuren am Bus wären sonst ein Fehlalarm.

Sind alle Regeln aus, liest das Board die Betriebsarten nur beim Start, nach eigenen Schaltbefehlen
und auf Knopfdruck („Status lesen“ bzw. `<gerät>/cmd/status`).

## 2. Übersicht R1–R5

| Regel | Auslöser am Bus | liest | Vorgabe | Abstand | typische Häufigkeit |
|---|---|---|---|---|---|
| **R1** Warmwasser umgeschaltet | WEM fragt den Kessel `2101/0A`, `2102/0D`, `2102/01` ab | Warmwasser | **an** | 1 min | einmal je Warmwasser-Umschaltung, selten beim Blättern im Display-Menü |
| **R2** Warmwasserbetrieb trotz Aus | Kessel beginnt eine Warmwasserladung, bekannt ist „Aus“ | Warmwasser | **an** | 10 min | nur, wenn Warmwasser unbemerkt eingeschaltet wurde |
| **R3** Heizung ein ↔ Standby | Standby-Bit `0x1000` der WEM-Statusbits ändert sich | Heizkreis | **an** | 1 min | einmal je Umschaltung Standby ↔ (Zeitprogramm, Sommer, Komfort …) |
| **R4** Heizanforderung passt nicht | Heizanforderung bei bekanntem Standby/Sommer, oder Vorlaufsoll passt nicht zur Raumsoll-Stufe | Heizkreis | aus | 30 min | selten; nur bei milder Außentemperatur geeicht |
| **R5** übrige Statusbits | andere WEM-Statusbits ändern sich (ohne Standby, WW-Ladung, Heizbetrieb) | Heizkreis | aus | 5 min | bei jedem Wechsel des Heizbedarfs, also oft |

Ausgeschaltete Regeln laufen im [Schattenmodus](#4-schattenmodus-ausgeschaltete-regeln-beobachten)
weiter: sie melden, dass sie angeschlagen **hätten**, lesen aber nicht.

## 3. Die Regeln im Einzelnen

### R1 – Warmwasser umgeschaltet

**Was am Bus passiert.** Bei **jeder** Umschaltung der Warmwasser-Betriebsart fragt der WEM den
Kessel (`0x602`) nacheinander ab:

```
2101/0A   2102/0D   2102/01   2101/0A   273F/01
```

und 4–13 s später noch einmal kurz `2101/0A`, `273F/01`. Die Abfragen kommen als **SDO
Block-Upload** (Kommandobyte `0xA4`), nicht als gewöhnlicher Upload (`0x40`). Index und Subindex
stehen bei beiden an derselben Stelle (Bytes 1–2 little endian, Byte 3), z. B.

```
602:A401210A40400000   -> 2101/0A
602:A402210D40400000   -> 2102/0D
602:A402210140400000   -> 2102/01
```

**Auslöser.** Die drei Abfragen `2101/0A` → `2102/0D` → `2102/01` in dieser Reihenfolge, die letzte
höchstens 10 s nach der mittleren und 15 s nach der ersten. Erst die **volle Folge mit `2102/0D`
und `2102/01`** ist spezifisch; die Kurzform `2101/0A` + `273F/01` kommt auch bei jedem Ladebeginn
und zählt nicht.

**Warum das ein Zeichen ist.** In allen Mitschnitten kam die volle Folge bei jeder
Warmwasser-Umschaltung, am Display wie per JSON, und bei **keiner** Umschaltung des Heizkreises.

**Sicherungen.** Ruht 2 min nach eigenem Schaltbefehl und **5 min nach einem Neustart des WEM**
(Bootup-Meldung `0x701` = `00`) – dabei fragt der WEM die volle Folge einmal ab, ohne dass jemand
umgeschaltet hat. Mindestabstand 1 min (die Folge kommt genau einmal je Umschaltung; zwei
Umschaltungen binnen weniger Minuten kommen vor).

**Was R1 nicht erkennt.** Die **Richtung**: die Antworten des Kessels sind bei Ein und Aus gleich.
Deshalb liest R1 nach. Ob Umschaltungen im **WEM-Portal oder der Weishaupt-App** dieselbe Folge
erzeugen, ist nicht mitgeschnitten.

**Wie sicher.** Hoch. Die Folge kam auch beim **Blättern im Menü am Display** – harmlos, dann liest
das Board einmal nach und meldet „geändert: nein“.

> **Behoben in v25:** Bis v24 hat R1 nur Abfragen mit Kommandobyte `0x40` ausgewertet. Der WEM
> fragt aber mit `0xA4`. **R1 hat deshalb in v22–v24 nie ausgelöst.**

### R2 – Warmwasserbetrieb trotz bekanntem „Aus“

**Auslöser.** Der Kesselstatus (PDO `0x182`, Byte 0) springt auf **15** (Warmwasserbetrieb), oder
der WEM schreibt `0x252B` = `0F` an den Kessel (`0x602`, Download mit Kommandobyte `0x2F`, Wert in
Byte 4) – und die zuletzt gelesene Warmwasser-Betriebsart ist **Aus**. Nur die Flanke zählt, nicht
jeder Frame. `0x252B` schreibt der WEM nur über `0x602`, nie über `0x6C2`.

**Warum das ein Zeichen ist.** Der Kessel lädt Warmwasser, obwohl es aus sein sollte – vermutlich
wurde es eingeschaltet. In den Mitschnitten begann eine Ladung jeweils kurz nach dem Einschalten.

**Sicherungen.** 2 min nach eigenem Schaltbefehl, Mindestabstand 10 min.

**Was R2 nicht erkennt.** Ein Einschalten ohne anschließende Ladung (Speicher ist warm genug) und
jedes Ausschalten. Dafür ist R1 da.

### R3 – Heizung ein ↔ Standby

**Auslöser.** Das Bit **`0x1000`** der WEM-Statusbits ändert sich. Die Statusbits (Objekt
`0x274D`) sendet der WEM als PDO **`0x1C1`, nur bei Änderung**; der Wert ist
`m = x[3]<<8 | x[2]` (Rohbytes `00 00 00 10` = `m` `0x1000` = Standby). Der letzte Stand wird
dauerhaft gespeichert, damit auch eine Änderung während eines Board-Neustarts auffällt.

**Warum das ein Zeichen ist.** Das Bit ist gesetzt, solange der Heizkreis auf **Standby** steht,
und in jeder anderen Betriebsart gelöscht. Jede Änderung ist eine echte Umschaltung.

**Sicherungen.** 2 min nach eigenem Schaltbefehl, Mindestabstand 1 min.

**Was R3 nicht erkennt.** Wechsel zwischen **Zeitprogramm 1/2/3, Sommer, Komfort, Normal und
Absenk** – das Bit bleibt dabei gelöscht. Diese Wechsel sind am Bus **gar nicht** zu sehen, auch
nicht, wenn sie über die WEM-App kommen; ein Wechsel ZP 2 → ZP 3 per JSON erzeugte keinen einzigen
Frame. Dafür gibt es den Knopf **„Status lesen“**.

**Wie sicher.** Hoch: in allen Mitschnitten gehörte jede Änderung des Bits zu einer Umschaltung
Standby ↔ andere Betriebsart.

> **Geändert in v25:** Bis v24 reagierte R3 auf **jede** Änderung der Statusbits (ohne WW-Ladung und
> Heizbetrieb). Das ist jetzt R5; R3 reagiert nur noch auf das Standby-Bit.

### R4 – Heizanforderung passt nicht zur Betriebsart

**Auslöser**, zwei Fälle:

1. Der WEM fordert Heizung an – er schreibt `0x252B` = `0A` an den Kessel oder der Kesselstatus
   springt auf **10** (Heizbetrieb) –, bekannt ist aber **Standby** oder **Sommer**.
2. Bei Komfort, Normal oder Absenk: aus dem Vorlaufsoll (PDO `0x241`, Bytes 0–1, ×0,1) und der
   Außentemperatur (PDO `0x201`, Bytes 1–2, ×0,1) wird die Raumsoll-Stufe zurückgerechnet,
   `RT ≈ (VL − 1,4 + 1,1 · AT) / 2,1`, eingeordnet in 18 / 20 / 21 °C, und mit der bekannten
   Betriebsart verglichen (Komfort 21, Normal 20, Absenk 18). Nur bei Vorlaufsoll > 0; derselbe
   Widerspruch löst nicht zweimal hintereinander aus.

**Warum das ein Zeichen ist.** Heizbetriebsarten haben kein eigenes Signal, die Heizanforderung
verrät sie aber teilweise. In den Mitschnitten zeigte der Vorlaufsoll einen Wechsel Komfort →
Normal einige Sekunden vor der Bestätigung durch die Lesung.

**Sicherungen.** 2 min nach eigenem Schaltbefehl, Mindestabstand 30 min.

**Was R4 nicht erkennt.** Zeitprogramme werden nicht geprüft (sie unterscheiden sich nur durch
ihre Schaltzeiten). Absenk fordert erst bei tieferer Außentemperatur an; Sommer sieht bei mildem
Wetter aus wie Absenk.

**Wie sicher.** Eingeschränkt. Die Rückrechnung ist **nur für Außentemperaturen von etwa
13–15 °C geeicht**; die eigene Heizkurve kann andere Werte liefern. Deshalb Vorgabe **aus** – erst
im [Schattenmodus](#4-schattenmodus-ausgeschaltete-regeln-beobachten) beobachten.

### R5 – übrige Statusbits geändert

**Auslöser.** Die WEM-Statusbits (`0x1C1`) ändern sich **ohne** das Standby-Bit `0x1000`, das
WW-Ladebit `0x0010` und das Heizbetrieb-Bit `0x0040` – also `m & ~0x1050`. Ändert sich im selben
Frame auch das Standby-Bit und R3 liest ohnehin, liest R5 nicht zusätzlich.

**Warum das ein Zeichen sein kann.** Manche Zustände zeigen sich nur hier, z. B. der
Schornsteinfeger-Betrieb. Die meisten Änderungen gehören aber zum **Heizbedarf** (Bits `0x0004`,
`0x0400`), der mehrmals pro Stunde kommen und gehen kann.

**Sicherungen.** 2 min nach eigenem Schaltbefehl, Mindestabstand 5 min.

**Wie sicher.** Gering als Hinweis auf eine Umschaltung: R5 schlägt oft an und bestätigt meist nur
den bekannten Stand. Deshalb Vorgabe **aus**. Das entspricht dem Verhalten von R3 bis v24.

## 4. Schattenmodus: ausgeschaltete Regeln beobachten

Eine **ausgeschaltete** Regel prüft ihre Bedingung trotzdem weiter. Trifft sie zu, wird **nicht
gelesen**, aber vermerkt:

- im Log (INFO): `R4 waere ausgeloest (Regel aus) - nicht gelesen`,
- als Ereignis an `<gerät>/verdacht/ereignis` mit `"phase":"waere"`.

Das gilt für R1–R5 und für eigene Regeln (einzeln aus **oder** Hauptschalter der eigenen Regeln
aus). Damit es nicht flutet, gelten die übrigen Sperren sinngemäß: 2 min nach eigenem
Schaltbefehl, bei R1 5 min nach WEM-Neustart, und je Regel der Mindestabstand (eigene Zeitmarke,
unabhängig von den echten Auslösungen). Schatten-Meldungen zählen **nicht** gegen die Obergrenze
pro Stunde und stehen **nicht** im Protokoll der letzten zehn Auslösungen – das bleibt für echte
Lesungen reserviert.

**Wozu:** eine Regel erst **beobachten**, dann einschalten. Wer die Ereignisse in eine Datenbank
schreibt (siehe [Abschnitt 8](#8-protokoll-log-und-ereignisse)), sieht in Grafana, wie oft eine
ausgeschaltete Regel angeschlagen hätte – und kann das gegen die bekannten Umschaltungen halten.

## 5. Eigene Regeln (experimentell)

Eigene Auslöser ohne Programmierung. Eigener Block „Eigene Regeln (experimentell)“ mit **eigenem
Hauptschalter, Vorgabe aus**. Höchstens 8 Regeln, dauerhaft im Flash gespeichert (überleben einen
Neustart auch ohne Broker).

**Seit v30 sind die festen Regeln R1–R5 selbst Datenregeln in genau diesem Format** (hinterlegt in
[`regelwerk.h`](components/weishaupt_can/regelwerk.h), Abschnitt „die festen Regeln als Daten“) und
laufen durch denselben Auswerter wie die eigenen. Was R1–R5 können – Folgen mit Zeitfenstern,
Flanken, Bedingungen auf den bekannten Stand, Ruhe nach einem Frame, „nicht zusätzlich, wenn eine
andere Regel schon liest“ –, können damit auch eigene Regeln.

### Kurzform (wie bis v29)

*Kommt ein Frame mit dieser CAN-ID, dessen Datenbytes UND Maske gleich Muster UND Maske sind, dann
nach dem Mindestabstand lesen.* Alte Regeln gelten unverändert und werden byteweise gleich
gespeichert und gemeldet.

| Feld | Inhalt |
|---|---|
| `name` | 1–24 Zeichen `A-Z a-z 0-9 - _ .`, eindeutig, nicht `R1`–`R5`, `eigene`, `verdacht` |
| `an` | `true`/`false` (Vorgabe `true`) |
| `can_id` | `"0x602"` oder Zahl. **`0x601` und `0x581` sind gesperrt** – das sind die eigenen Anfragen des Boards und ihre Antworten; eine Regel darauf würde sich selbst auslösen |
| `muster` | 1–8 Bytes hex, z. B. `"A4 3E 22"` – ab v30 auch eine **Liste** mit 1–4 Mustern (eines muss passen) |
| `maske` | 1–8 Bytes hex; fehlt sie, zählen genau die Bytes, die im Muster stehen (bei einer Liste müssen die Muster dann gleich lang sein) |
| `laenge_min` | ab v30, optional: der Frame muss mindestens so viele Datenbytes haben (0–8) |
| `wenn` | ab v30, optional: nur wenn der zuletzt gelesene Stand passt, z. B. `{"ww":[2]}` oder `{"hk":[1,5]}` (Werte wie in [Abschnitt 3](#3-die-regeln-im-einzelnen)) |
| `lesen` | `"hk"`, `"ww"` oder `"beide"` (Vorgabe) |
| `abstand_min` | 1–1440 (Vorgabe 10) |
| `beschreibung` | warum es die Regel gibt, bis 120 Zeichen |

**Beispiel:** der WEM fragt den Kessel `0x223E` ab (kam einmal beim Öffnen einer Portalseite –
Zusammenhang unbelegt). Byte 0 ist ausmaskiert, damit `0x40` und `0xA4` beide passen:

```json
{"name": "Portal-Seitenaufruf", "an": true, "can_id": "0x602",
 "maske": "00 FF FF 00 00 00 00 00", "muster": "00 3E 22 00 00 00 00 00",
 "lesen": "beide", "abstand_min": 30}
```

### Langform (ab v30): mehrere Auslöser, Folgen, Flanken

Statt `can_id`/`maske`/`muster` eine Liste `ausloeser` (1–4). Jeder Auslöser hat **genau eine**
Art; trifft einer zu, wird gelesen (Mindestabstand, Obergrenze, Schattenmodus und Ruhe nach eigenem
Schaltbefehl gelten wie immer):

| Art | Schreibweise | trifft zu, wenn … |
|---|---|---|
| Frame | `{"frame": {can_id, maske, muster, laenge_min}}` | ein passender Frame kommt |
| Folge | `{"folge": [Glied, Glied, …]}` (2–4 Glieder, je `{can_id, maske, muster, laenge_min, fenster_ms}`) | die Glieder in dieser Reihenfolge kamen und beim letzten jedes frühere höchstens `fenster_ms` alt ist (fehlt `fenster_ms`: egal). Danach beginnt die Folge von vorn |
| Flanke | `{"flanke": {can_id, maske, muster, laenge_min}, "byte": 2, "bytes": 2, "bits": "0x1000", "erster": false, "gleich": [15]}` | sich ein Wert im Frame ändert: `bytes` (1–2) Bytes ab `byte`, little-endian, UND `bits`. Hat `bits` genau ein Bit, zählt 0/1. `erster: true` = schon der erste gesehene Wert gilt als Änderung. `gleich`: nur bei diesen neuen Werten |
| Baustein | `{"baustein": "r4_vorlauf"}` | eine Rechnung im Code zutrifft (siehe unten) |

Zusätzlich je Auslöser: `wenn` (wie oben), `text` (Kennung im Log, bis 40 Zeichen),
`meldung_ruht` und `meldung_unterdrueckt` (eigener Logtext, Platzhalter `{regel}`, `{grund}`,
`{text}`, `{alt}`, `{neu}`, `{althex}`, `{neuhex}`, `{durch}`).

Auf Ebene der Regel:

| Feld | Inhalt |
|---|---|
| `ruhe_nach` | `{can_id, maske, muster, laenge_min, "ms": 300000, "grund": "WEM-Neustart"}` – nach so einem Frame ruht die ganze Regel `ms` lang (1–86 400 000); im Log steht `grund` |
| `unterdrueckt_durch` | Name einer anderen Regel: liest die im selben Frame schon (eingeschaltet), wird diese nicht zusätzlich gerufen |

**Beispiel:** die R1-Folge als eigene Regel, zusätzlich „Kesselstatus springt auf 15, bekannt ist
Warmwasser Aus“, beides 5 min Ruhe nach einem Neustart des WEM:

```json
{"name": "ww-folge", "an": false, "lesen": "ww", "abstand_min": 1,
 "ausloeser": [
   {"folge": [
      {"can_id": "0x602", "maske": "FF FF FF FF", "muster": ["A4 01 21 0A", "40 01 21 0A"], "laenge_min": 5, "fenster_ms": 15000},
      {"can_id": "0x602", "maske": "FF FF FF FF", "muster": ["A4 02 21 0D", "40 02 21 0D"], "laenge_min": 5, "fenster_ms": 10000},
      {"can_id": "0x602", "maske": "FF FF FF FF", "muster": ["A4 02 21 01", "40 02 21 01"], "laenge_min": 5}],
    "text": "WW-Folge"},
   {"flanke": {"can_id": "0x182", "laenge_min": 1}, "byte": 0, "gleich": [15], "wenn": {"ww": [2]}}],
 "ruhe_nach": {"can_id": "0x701", "muster": "00", "laenge_min": 1, "ms": 300000, "grund": "WEM-Neustart"}}
```

Die Langform braucht mehr Speicher: alle eigenen Regeln zusammen dürfen in der Speicherform
höchstens 2400 Zeichen lang sein, sonst lehnt das Board die Datei ab („eigene Regeln zu lang“).

### R1–R5 im Regelformat

So stehen die festen Regeln im Code (als JSON geschrieben; `an` und `abstand_min` kommen bei ihnen
aus den Schaltern und Mindestabständen der Weboberfläche):

| Regel | Auslöser |
|---|---|
| R1 | Folge `0x602` `A4/40 01 21 0A` (≤ 15 s) → `A4/40 02 21 0D` (≤ 10 s) → `A4/40 02 21 01`, je mindestens 5 Bytes; `ruhe_nach` `0x701` = `00` 300 000 ms „WEM-Neustart“; lesen ww |
| R2 | Flanke `0x602` Maske `E0 FF FF` Muster `20 2B 25` (WEM schreibt 0x252B), Byte 4, `erster: true`, `gleich [15]`, `wenn {"ww":[2]}`; Flanke `0x182` Byte 0, `gleich [15]`, `wenn {"ww":[2]}`; lesen ww |
| R3 | Flanke `0x1C1` Bytes 2–3, `bits 0x1000` (Standby-Bit → 0/1); lesen hk |
| R4 | wie R2 mit `gleich [10]`/`[10]` und `wenn {"hk":[1,5]}`, dazu Baustein `r4_vorlauf`; lesen hk |
| R5 | Flanke `0x1C1` Bytes 2–3, `bits 0xEFAF` (ohne Standby, WW-Ladung, Heizbetrieb), `unterdrueckt_durch: "R3"`; lesen hk |

Der Test `aequivalenz_feste_regeln_als_json` schreibt R1–R5 genau so als JSON, liest sie wieder ein
und vergleicht sie mit v29 (siehe [Abschnitt 9](#9-geprüft-an-mitschnitten)).

**Warum `r4_vorlauf` ein Baustein bleibt:** R4 rechnet aus Vorlaufsoll (PDO `0x241`) und
Außentemperatur (PDO `0x201`, ein anderer Frame!) eine Raumsoll-Stufe zurück –
`RT = (VL − 1,4 + 1,1 · AT) / 2,1`, dann Stufe 18/20/21 – und vergleicht sie mit der bekannten
Betriebsart (Komfort 21, Normal 20, Absenk 18). Das ist Gleitkomma-Rechnung über zwei Frames mit
Stufen und eigenem Gedächtnis („derselbe Widerspruch nicht zweimal“); als Byte-Muster lässt sich das
nicht sinnvoll schreiben, und die Formel ist nur bei milder Außentemperatur geeicht. Der Baustein
ist benannt und kann auch in eigenen Regeln benutzt werden; sein Zustand gehört dem jeweiligen
Auslöser, R4 wird davon nicht berührt. Ebenfalls nur im Code (nicht im Dateiformat): wo die
Vorwerte von R3/R5 dauerhaft gespeichert werden – damit sie über ein Update hinweg dieselben
Schlüssel behalten.

### Hinzufügen, anzeigen, schalten

**Hinzufügen oder ändern:** Liste `eigene` in der Datei (siehe [6](#6-einstellen)). Steht `eigene`
in der Datei, ersetzt sie die ganze Liste (Zähler und „zuletzt“ gleichnamiger Regeln bleiben);
fehlt der Schlüssel, bleiben die eigenen Regeln unverändert.

**Anzeigen:** Weboberfläche, Gruppe **Eigene Regeln (experimentell)**, z. B.
`Portal-Seitenaufruf an: 602 [.. 3E 22] -> beide, 30 min, zuletzt …, 3 x`, in der Langform
`ww-folge an: folge 602 x3 + flanke 182 -> ww, 1 min, …`; vollständig unter `<gerät>/regeln/stand`
(Kurzform-Regeln mit denselben Feldern wie bisher, Langform mit `ausloeser`).

**Schalten und löschen:** `NAME an`, `NAME aus`, `NAME loeschen` – per MQTT (siehe
[7](#7-per-mqtt-schalten)) oder im Feld **Regel-Befehl** der Weboberfläche.

**Sicherungen:** wie die festen Regeln (eigener Befehl 2 min, Mindestabstand, gemeinsame
Obergrenze pro Stunde), dazu die Sperre für `0x601`/`0x581` in jedem Glied.

Was darüber hinausgeht (Uhrzeiten, Werte anderer Geräte), baut man besser in Home Assistant oder
Node-RED und schickt `<gerät>/cmd/status` – siehe
[IDEEN.md, Abschnitt 7](IDEEN.md#7-eigene-lese-regeln-in-home-assistant-oder-node-red).

## 6. Einstellen

Drei Wege, die dieselben Werte ändern; **zuletzt geschrieben gilt**, alles wird dauerhaft
gespeichert.

**Beim Bauen** – Startwerte in der Hauptdatei unter `substitutions:` (gelten nur, solange noch
nichts gespeichert ist):

```yaml
  regel_r1_start: RESTORE_DEFAULT_ON
  regel_r2_start: RESTORE_DEFAULT_ON
  regel_r3_start: RESTORE_DEFAULT_ON
  regel_r4_start: RESTORE_DEFAULT_OFF
  regel_r5_start: RESTORE_DEFAULT_OFF
  regel_abstand_r1: "1"      # ... bis regel_abstand_r5
  regeln_max_h: "6"
  eigene_regeln_start: RESTORE_DEFAULT_OFF
```

**In der Weboberfläche** – Gruppe **Regeln (R1-R5)**: je Regel Schalter, Mindestabstand, „was und
warum“, „zuletzt ausgelöst“; dazu Obergrenze pro Stunde, „Regeln aktiv“, „Regeln Protokoll“ und
„Regeln-Datei“ (Ergebnis der letzten Datei).

**Zur Laufzeit per Datei** – JSON an `<gerät>/cmd/regeln`, am besten retained. Vorlage:
[`regeln.json.example`](regeln.json.example).

```json
{
  "version": 1,
  "R1": {"an": true,  "abstand_min": 1},
  "R2": {"an": true,  "abstand_min": 10},
  "R3": {"an": true,  "abstand_min": 1},
  "R4": {"an": false, "abstand_min": 30},
  "R5": {"an": false, "abstand_min": 5},
  "max_pro_stunde": 6,
  "eigene_regeln_an": false,
  "eigene": []
}
```

```
mosquitto_pub -h <broker> -u <konto> -P <passwort> -r -f regeln.json -t <gerät>/cmd/regeln
```

| Feld | Inhalt |
|---|---|
| `version` | ganze Zahl ≥ 0, frei wählbar; das Board meldet sie im Stand zurück |
| `R1` … `R5` | je `{"an": true/false, "abstand_min": 1–1440}` |
| `max_pro_stunde` | 1–60, gilt für alle Regeln zusammen |
| `eigene_regeln_an` | Hauptschalter der eigenen Regeln |
| `eigene` | Liste der eigenen Regeln |
| `kommentar`, `_…` | werden ignoriert |

Jedes Feld ist optional; was fehlt, bleibt, wie es ist. **Ungültiges JSON, unbekannte Felder und
Werte außerhalb der Grenzen lehnt das Board ganz ab** („Regeln-Datei: ABGELEHNT: …“) und ändert
nichts. Das Feld `verdacht` (bis v22) wird mit Hinweis abgelehnt.

**Retained und Prüfsumme:** Das Board merkt sich eine Prüfsumme der zuletzt übernommenen Datei.
Dieselbe Datei wird nach Neustart oder Wiederverbindung **nicht erneut** angewandt – sonst würde
sie jede spätere Änderung in der Weboberfläche überschreiben. Soll eine unveränderte Datei bewusst
erneut gelten, `version` hochzählen.

**Rückkanal:** den aktiven Stand meldet das Board retained unter `<gerät>/regeln/stand` (beim Start
und bei jeder Änderung), im selben Format plus `firmware` und Laufzeitwerten der eigenen Regeln:

```json
{"firmware":"v25 vom 27.09.2026","version":1,
 "R1":{"an":true,"abstand_min":1},"R2":{"an":true,"abstand_min":10},
 "R3":{"an":true,"abstand_min":1},"R4":{"an":false,"abstand_min":30},
 "R5":{"an":false,"abstand_min":5},
 "max_pro_stunde":6,"eigene_regeln_an":false,"eigene":[]}
```

## 7. Per MQTT schalten

Einzelne Regeln schaltet man ohne Datei mit `<gerät>/cmd/regel`:

```
mosquitto_pub -h <broker> -u <konto> -P <passwort> -t <gerät>/cmd/regel -m "R4 an"
mosquitto_pub -h <broker> -u <konto> -P <passwort> -t <gerät>/cmd/regel -m "R5 aus"
mosquitto_pub -h <broker> -u <konto> -P <passwort> -t <gerät>/cmd/regel -m "eigene an"
mosquitto_pub -h <broker> -u <konto> -P <passwort> -t <gerät>/cmd/regel -m "Portal-Seitenaufruf loeschen"
```

`R1`–`R5` und `eigene` (Hauptschalter der eigenen Regeln) lassen sich nur an- und ausschalten,
eigene Regeln auch löschen. Unbekannte Namen oder Befehle werden mit Meldung abgelehnt; die
Antwort steht in der Weboberfläche unter **Regel-Befehl Antwort**, das Ergebnis unter
`<gerät>/regeln/stand`.

**Broker-Rechte:** Wer `cmd/regel` oder `cmd/regeln` schreiben darf, kann Regeln schalten und damit
Leseanfragen auslösen – nur Lesen an Knoten 1 und gedrosselt, aber Busverkehr. Diese Rechte nur
einem Verwaltungskonto geben; das Konto der Handy-App hat sie **nicht**.

## 8. Protokoll, Log und Ereignisse

**Protokoll** der letzten zehn **echten** Auslösungen seit dem Neustart – Weboberfläche („Regeln
Protokoll“) und retained unter `<gerät>/verdacht/protokoll`, eine Zeile je Auslösung:

```
27.09. 09:52 R1: Warmwasser Ein (vorher Aus), geaendert: JA
27.09. 10:03 R3: Heizkreis Standby, geaendert: nein
```

„geändert“ vergleicht den gelesenen Wert mit dem vorher bekannten; war der unbekannt, steht `?`.
Kommt binnen 3 min keine Antwort (etwa weil der Bus in die Anlaufpause fiel), steht dort
`keine Antwort`. Haben mehrere Regeln dieselbe Lesung ausgelöst, stehen sie zusammen (`R3+R5`).

**Log** (Tag `verdacht`): jede Auslösung und jedes Ergebnis als **INFO**, jede **unterdrückte**
Auslösung (eigener Befehl, WEM-Neustart, Mindestabstand, Obergrenze, Bus nicht bereit) als
**DEBUG**, Schatten-Meldungen als INFO.

**Ereignisse** – jede Auslösung, jede Schatten-Meldung und jedes Ergebnis als JSON an
**`<gerät>/verdacht/ereignis`** (**nicht** retained):

| `phase` | Bedeutung | weitere Felder |
|---|---|---|
| `ausgeloest` | Regel hat eine Lesung ausgelöst | `regel`, `ziel` (`hk`, `ww`, `beide`) |
| `waere` | Bedingung traf zu, Regel war aus – **nicht** gelesen | `regel`, `ziel` |
| `ergebnis` | Antwort der Lesung | `regel`, `ziel`, `wert` (z. B. `Standby`, `Ein`), `geaendert` (`true`, `false`, `null` = vorher unbekannt) |
| `keine_antwort` | binnen 3 min keine Antwort | `regel`, `ziel` |

```json
{"regel":"R3","phase":"ausgeloest","ziel":"hk","zeit":"2026-09-27T13:20:05"}
{"regel":"R3","phase":"ergebnis","ziel":"hk","wert":"Standby","geaendert":false,"zeit":"2026-09-27T13:20:06"}
{"regel":"R4","phase":"waere","ziel":"hk","zeit":"2026-09-27T13:31:40"}
```

`zeit` fehlt, solange die Uhr des Boards noch nicht gestellt ist.

**In eine Datenbank schreiben** (Telegraf → InfluxDB), eine Messreihe mit den Tags `regel`,
`phase`, `ziel` und den Feldern `wert`, `geaendert` (0/1), `anzahl` (=1, zum Zählen):

```toml
[[inputs.mqtt_consumer]]
  servers   = ["tcp://<broker>:1883"]
  username  = "telegraf"
  password  = "${MQTT_PASS}"
  client_id = "telegraf-heizung-regel"      # eigene ID je Eingang
  topics    = ["<gerät>/verdacht/ereignis"]
  data_format = "json_v2"
  [[inputs.mqtt_consumer.json_v2]]
    measurement_name = "heizung_regel"
    [[inputs.mqtt_consumer.json_v2.tag]]
      path = "regel"
    [[inputs.mqtt_consumer.json_v2.tag]]
      path = "phase"
    [[inputs.mqtt_consumer.json_v2.tag]]
      path = "ziel"
      optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "phase"
      rename = "ereignis"
      type = "string"
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "wert"
      type = "string"
      optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "geaendert"
      optional = true

[[processors.starlark]]
  namepass = ["heizung_regel"]
  source = '''
def apply(metric):
    metric.fields["anzahl"] = 1
    g = metric.fields.get("geaendert")
    if type(g) == "bool":
        metric.fields["geaendert"] = 1 if g else 0
    elif g != None and type(g) != "int":
        metric.fields.pop("geaendert")     # null: weglassen
    return metric
'''
```

> Zum Testen einer Kopie mit `telegraf --test`: laufen dabei Eingänge mit **denselben
> `client_id`** wie im laufenden Dienst, werfen sich beide am Broker gegenseitig hinaus. Eine
> Kopie nur mit dem neuen Eingang testen.

In **Grafana** passen dazu eine Tabelle der letzten Ereignisse (Zeit, Regel, Phase, Ziel, Wert,
geändert), ein Balken „Auslösungen je Regel“ getrennt nach `ausgeloest` und `waere` und
Annotationen für `phase = ausgeloest` in einem Heizungs-Diagramm. Beispiel-Abfrage für den Balken:

```
from(bucket: "<bucket>")
  |> range(start: -7d)
  |> filter(fn: (r) => r._measurement == "heizung_regel" and r._field == "anzahl")
  |> filter(fn: (r) => r.phase == "ausgeloest" or r.phase == "waere")
  |> group(columns: ["regel", "phase"])
  |> sum()
  |> map(fn: (r) => ({name: r.regel + " " + r.phase, _value: r._value}))
  |> group()
```

## 9. Geprüft an Mitschnitten

Die Regel-Logik wurde Byte für Byte in Python nachgebildet (gleiche Bitmasken, Byte-Reihenfolge,
Zeitfenster und Sperren) und über alle Rohmitschnitte eines Abends mit bekannten Umschaltungen
laufen gelassen (rund 295 000 Frames, Heizkreis und Warmwasser je mehrfach am Display und per JSON):

| Regel | Treffer | Fehlalarm | verpasst | Anmerkung |
|---|---|---|---|---|
| R1 (nur `0x40`, bis v24) | 0 | 0 | **alle** | der Fehler, den v25 behebt |
| R1 (v25) | alle Warmwasser-Umschaltungen | 0 | 0 | dazu zwei Treffer beim Blättern im Display-Menü; nie bei Heizkreis-Umschaltungen. Mit dem früheren Abstand von 10 min wären zwei schnelle Umschaltungen hintereinander verpasst worden – daher jetzt 1 min |
| R2 | – | 0 | 0 | die Bedingung trat genau zweimal auf, jeweils am Ladebeginn nach einem Einschalten; die Warmwasser-Betriebsart war im Mitschnitt da noch nicht bekannt |
| R3 | alle Standby-Umschaltungen | 0 | 0 | mit dem früheren Abstand von 5 min wäre eine schnelle Rückschaltung verpasst worden – daher jetzt 1 min |
| R4 | 2 | 0 | 0 | einmal Sommer → Zeitprogramm (Heizanforderung), einmal Komfort → Normal (Vorlaufsoll), beide vor der Bestätigung durch eine Lesung; der zweite fiel in den Mindestabstand |
| R5 | 5 Lesungen | – | – | 26-mal angeschlagen, fast immer Heizbedarf an/aus; einmal Schornsteinfeger-Betrieb |

Byte-Positionen wurden dabei gegengeprüft: Kesselstatus `0x182` Byte 0; Vorlaufsoll `0x241`
Bytes 0–1; Außentemperatur `0x201` Bytes 1–2; `0x252B` schreibt der WEM als 1-Byte-Download
(`0x2F`, Wert in Byte 4) über `0x602` mit den Werten `01` (keine Anforderung), `0A` (Heizung) und
`0F` (Warmwasser).

**Umbau auf Datenregeln (v30), gegen v29 geprüft:** die Logik von v29 liegt eingefroren in
[`tests/referenz_v29/`](tests/referenz_v29/). Der Test `aequivalenz_zufall_v29_gleich_neu` spielt
400 Zufallsläufe zu je 1500 Frames (die Hälfte über den Überlauf von `millis()`, mit eingestreuten
R1-Folgen, Umschalt-Szenen, Antworten, WEM-Neustarts, eigenem Schaltbefehl, wechselnden Schaltern,
Abständen und Obergrenzen) durch beide Fassungen und verlangt nach **jedem** Frame dieselben
Logzeilen, dieselben Aufrufe mit gleichem Ergebnis (Auslösung, Schatten, Abstand, Obergrenze,
Bus), dieselben Ereignisse und denselben gespeicherten Zustand. Er prüft auch, dass jeder Fall
vorkam (jede Regel ausgelöst, jede „ruht“-Meldung, „R3 liest schon“, Baustein R4, Schatten,
Obergrenze …). Dazu feste Szenarien genau an den Zeitfenstern von R1 und eine Gegenprobe: neun
absichtlich eingebaute Fehler (Fenster, Kommandobyte, WEM-Neustart, Bedingung, Bits, Ziel, Logtext
…) machen den Vergleich jeweils rot.

## 10. Grenzen und offene Fragen

- **Umschaltungen über WEM-Portal und Weishaupt-App** wurden nie mitgeschnitten. Ob sie dieselben
  Spuren hinterlassen wie am Display, ist offen – Rückmeldungen willkommen.
- **Zeitprogramm 1 ↔ 2 ↔ 3, Sommer, Komfort, Normal, Absenk** untereinander sind am Bus unsichtbar
  (R4 sieht nur einen Teil davon, wenn gerade Heizbedarf besteht). Dafür: **„Status lesen“**.
- **Die Rückrechnung von R4** braucht Winterdaten (Außentemperatur 0–8 °C) und ist anlagenabhängig
  (Heizkurve).
- **R5** ist als Hinweis auf Umschaltungen schwach; nützlich vor allem im Schattenmodus, um
  seltene Zustände (Schornsteinfeger, Störungen) zu sehen.
- Gemessen an einer **WTC-GW 15-B** mit WEM-Systemgerät. Andere Kessel oder WEM-Stände können
  andere Folgen senden – dann zuerst im Schattenmodus beobachten.

Mehr zum Busprotokoll: [PROTOKOLL.md](PROTOKOLL.md), insbesondere
[„Passiv erkennen: was geht und was nicht“](PROTOKOLL.md#passiv-erkennen-was-geht-und-was-nicht).
