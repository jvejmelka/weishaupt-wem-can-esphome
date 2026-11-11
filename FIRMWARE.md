# Firmware: Bedienung, Einstellungen, Regeln, Zusatzanzeigen

> 🇬🇧 English version: [FIRMWARE.en.md](FIRMWARE.en.md)

Was die Firmware zum Bedienen anbietet: Lesen und Schreiben von Hand in der Experten-Oberfläche, Schalten, alle
Einstellungen mit ihren Werkswerten, die Regeln zum Nachlesen der Betriebsarten und die Zusatzanzeigen der App.
Was auf dem Bus passiert und wie der WEM antwortet, steht in [PROTOKOLL.md](PROTOKOLL.md); auf welchen Wegen die
Firmware mit der Heizung spricht und was ab Werk an ist, in der Tabelle
[Wege zur Heizung](PROTOKOLL.md#wege-zur-heizung).

Die Abschnitte standen bis November 2026 in PROTOKOLL.md und behalten ihre Nummern von dort (6, 6a, 6b), damit
ältere Verweise weiter stimmen.

- [6. Weboberfläche: lesen und schreiben](#6-weboberfläche-lesen-und-schreiben)
- [6a. Regeln: Betriebsarten bei Verdacht lesen](#6a-regeln-betriebsarten-bei-verdacht-lesen)
- [6b. Zusatzanzeigen für die Handy-App](#6b-zusatzanzeigen-für-die-handy-app)

## 6. Weboberfläche: lesen und schreiben

Gemeint ist die **Experten-Oberfläche** des Boards (die ESPHome-Weboberfläche): ab v42 unter
`http://<Board>/experten`, Benutzer `admin` mit dem Experten-Passwort; unter `http://<Board>/` liegt
mit Paket `app` die App. Sie ist in Gruppen geteilt. Die Namen hier sind die aus der Firmware.

### Lesebefehl (nur lesen)

| Element | Funktion |
|---|---|
| **Lesebefehl (KN IDX SUB hex)** | Eingabe `KNOTEN INDEX SUB` in hex, z. B. `01 2933 02` |
| **Lesen** | sendet eine Lese-Anfrage (`0x40`) an Knoten `0x600 + KN` – nur, wenn der Bus lebt |
| **Lesebefehl Antwort** | `01 2933/02 = 2 (0x02, 1 Byte)` oder `… Abbruch 06020000 (Objekt fehlt?)`; bei totem Bus „Bus tot - nicht gesendet“ |
| **Lesebefehl Rohdaten** (ab v33) | jede Nachricht des letzten Lesebefehls, `>` gesendet, `<` empfangen, z. B. `>403F270000000000 <413F270008000000 …` |

Lange Objekte liest der Lesebefehl automatisch in Teilstücken (ab v33), mit Zusatz `B` (z. B.
`01 273F 00 B`) per Block – am Kessel (Knoten 02) nur, solange „Lange Lesebefehle auch am Kessel“ an ist.

Dasselbe geht per MQTT: `<gerät>/cmd/lesen` mit `01 2933 02`.

### Betriebsarten: Status lesen

| Element | Funktion |
|---|---|
| **Status lesen** (Knopf) | liest Heizkreis-Betriebsart (`0x2933`/2) und – mit Paket warmwasser – `0x2A20`, `0x2A2C`, `0x2A39` (je Subindex 2) einmal von Knoten 1, nach einem Neustart des Boards dazu einmal die Statusbits (`0x274D`). Nur Leseanfragen, kein WEM-JSON. Gruppe „Betriebsarten und Sollwerte“ |
| **Status gelesen** | `27.09. 09:32:51 gelesen`, während der Anforderung `09:32:46 angefordert ...`, bei totem Bus `… Bus schweigt - nicht gelesen` |

Per MQTT: `<gerät>/cmd/status` mit beliebigem Inhalt. Höchstens ein Wunsch alle 10 s.

### Schalten (über WEM, Kontrolle am Bus)

| Element | Funktion |
|---|---|
| **Heizkreis Betriebsart setzen** | Auswahl Standby … Absenk; zeigt nach jeder Kontroll-Lesung den Wert am Bus |
| **Warmwasser setzen** | Auswahl Ein / Aus |
| **Warteschlange** | vorgemerkte Befehle mit Quelle, z. B. `1. Heizkreis -> Zeitprogramm 1 (MQTT) - naechster Befehl ab 14:03:12`, sonst `leer` |
| **Ergebnis letzter Schaltbefehl** | Fortschritt und Ergebnis als Klartext, s. [PROTOKOLL.md, Abschnitt 5](PROTOKOLL.md#ablauf-in-der-firmware) (veraltet – strukturiert: `befehl/status`, [TOPICS.md](TOPICS.md)) |
| **Warteschlange leeren** | verwirft alle vorgemerkten Befehle (wird protokolliert) |
| **Schaltprotokoll** | die letzten zehn Befehle mit Uhrzeit, Quelle und Ergebnis; per MQTT retained unter `<gerät>/schaltprotokoll`. Nach einem Neustart leer |
| **Schreibvorgaenge letzte Stunde** | wie oft das Board in den letzten 60 Minuten am WEM geschrieben hat (ab v42): Schalten, freies CAN-Schreiben, Schreibfeld, Warmwasser-Push |
| **Schreibvorgaenge gesamt** | derselbe Zähler ohne Zeitfenster; übersteht den Neustart, zählt ab dem Update auf v42 |
| **Schreibvorgaenge je Stunde hoechstens** | Obergrenze, ab Werk <!--ab-werk:schreib_max-->30<!--/ab-werk--> (1–120, Experten). Ist sie erreicht, sendet das Board nicht – [PROTOKOLL.md, Schreibschutz](PROTOKOLL.md#schreibschutz-so-wenig-wie-möglich-schreiben-ab-v42) |

### Schreibbefehl über WEM (Experten)

| Element | Funktion |
|---|---|
| **Schreibbefehl (MI MX OX OS WERT hex)** | z. B. `03 00 2520 02 02` = Warmwasser Aus. WERT 1–4 Hexziffern; die Länge (VS) ergibt sich daraus |
| **Schreiben** | baut `CM 03` und sendet über JSON. **Nur MI 01, 02, 03** – der Kessel ist gesperrt. Es gilt der Abstand für Befehle über JSON (ab Werk <!--ab-werk:abstand_json-->60<!--/ab-werk--> s) |
| **Schreibbefehl Antwort** | `OK, WEM bestaetigt (VG …)`, `abgelehnt (CM=05): Objekt unbekannt oder nicht schreibbar - VG …`, `unklare Antwort, HTTP …` oder `WEM nicht erreichbar` |

Das Schreibfeld prüft **nicht** am Bus nach und kennt keine Warteschlange; es liest auch nicht
vorab, und jeder Schreibvorgang zählt für die Obergrenze je Stunde (ab v42). Vor dem Schreiben das
Objekt über den Lesebefehl am Bus ansehen und die Abbildung aus
[PROTOKOLL.md, Abschnitt 5](PROTOKOLL.md#abbildung-json-objekt--can-objekt) anwenden. Jede `CM 05`-Antwort kostet einen der
zehn Plätze aus [PROTOKOLL.md, Abschnitt 8](PROTOKOLL.md#8-wann-sich-der-wem-aufhängt).

### Weitere Gruppen

- **Schreiben per CAN:** die Schalter „Schalten per CAN statt JSON“ und „Freies CAN-Schreiben“, das
  Feld für den freien Schreibbefehl, die Liste der dafür erlaubten Knoten (ab Werk nur `01`) und der Abstand per CAN – beschrieben unter [Schreiben per CAN](PROTOKOLL.md#schreiben-per-can-ab-v35-seit-v42-vorgabe).
- **SD-Karte des WEM (Dateien lesen):** Datei holen und Systemtabelle – beschrieben unter
  [Knoten und Module](PROTOKOLL.md#knoten-und-module).
- Paket `json-lesen` (Gruppe „Einstellungen“): Knopf „Werte per JSON abrufen“, Meldung „JSON-Abruf“
  und „Mindestabstand JSON-Abruf (s)“ – beschrieben unter
  [Warum nie über JSON gepollt wird](PROTOKOLL.md#warum-nie-über-json-gepollt-wird).

### Einstellungen

| Element | Funktion |
|---|---|
| **Eigene CAN-Anfragen** | an = Leseanfragen im Takt aus [PROTOKOLL.md, Abschnitt 2](PROTOKOLL.md#takt-der-eigenen-anfragen); aus = nur mithören. Bleibt über Neustarts erhalten; Startzustand über `can_anfragen_start` in der Hauptdatei |
| **CAN-Rohmitschnitt nach MQTT** | jeder empfangene Frame als `ID:DATEN` nach `<gerät>/canraw` (Paket `mqtt`); während eines Scans immer an |
| **Heizung: IP-Adresse** | Adresse des WEM für alles, was über HTTP läuft: Schalten über JSON, Schreibbefehl, SD-Dateien, Abruf von Hand |
| **Waechter: Neustart nach Minuten ohne Bus (0 = aus)** | startet das Board neu, wenn so lange kein Frame kam (Bus-off-Falle) |

**Alle Einstellungen mit ihrem Wert ab Werk.** Die Tabelle wird aus dem Code erzeugt (`werkzeuge/einstellungen_erzeugen.py`: `restore_mode` und `initial_value` in `pakete/*.yaml`, Liste der Einstellungsseite in `app_server.cpp`); die CI schlägt fehl, wenn sie nicht zum Code passt. Dasselbe gilt für jede Angabe „ab Werk“ an anderer Stelle der Doku.

<!-- EINSTELLUNGEN:BEGIN -->
| Einstellung (Name auf dem Board) | Ab Werk | Bereich | Paket | Gruppe der Einstellungsseite | Ändern braucht |
|---|---|---|---|---|---|
| Schalten per CAN statt JSON (Heizkreis, Warmwasser) | **an** | – | `wem-schalten` | Schalten | Admin-Anmeldung |
| Abstand zwischen Befehlen per CAN (s) | 10 | 5–300 | `wem-schalten` | Schalten | Admin-Anmeldung |
| Abstand zwischen Befehlen per JSON (s) | 60 | 60–600 | `wem-schalten` | Schalten | Admin-Anmeldung |
| Mindestabstand JSON-Abruf (s) | 300 | 120–3600 | `json-lesen` | Schalten | Admin-Anmeldung |
| Eigene CAN-Anfragen | **aus** | – | `kessel` | Lesen | Admin-Anmeldung und Experten-Passwort |
| Lange Lesebefehle auch am Kessel | **an** | – | `kessel` | Lesen | Admin-Anmeldung und Experten-Passwort |
| SD-Dateien abrufen | **an** | – | `wem-schalten` | Lesen | Admin-Anmeldung und Experten-Passwort |
| R1 Warmwasser am Display oder Portal umgeschaltet | **an** | – | `verdacht` | Regeln | Admin-Anmeldung |
| R2 Warmwasserbetrieb trotz Aus | **an** | – | `verdacht` | Regeln | Admin-Anmeldung |
| R3 Heizung ein oder Standby | **an** | – | `verdacht` | Regeln | Admin-Anmeldung |
| R4 Heizanforderung passt nicht | **aus** | – | `verdacht` | Regeln | Admin-Anmeldung |
| R5 uebrige Statusbits geaendert | **aus** | – | `verdacht` | Regeln | Admin-Anmeldung |
| Eigene Regeln (Hauptschalter) | **aus** | – | `verdacht` | Regeln | Admin-Anmeldung |
| Freies CAN-Schreiben (Experten) | **aus** | – | `wem-schalten` | Experten | Admin-Anmeldung und Experten-Passwort |
| Schreibvorgaenge je Stunde hoechstens | 30 | 1–120 | `wem-schalten` | Experten | Admin-Anmeldung und Experten-Passwort |
| CAN-Rohmitschnitt nach MQTT | **aus** | – | `mqtt` | Experten | Admin-Anmeldung und Experten-Passwort |
| Waechter: Neustart nach Minuten ohne Bus (0 = aus) | 5 | 0–120 | `kessel` | Experten | Admin-Anmeldung und Experten-Passwort |
| R1 Mindestabstand (min) | 1 | 1–1440 | `verdacht` | – | Experten-Oberfläche |
| R2 Mindestabstand (min) | 10 | 1–1440 | `verdacht` | – | Experten-Oberfläche |
| R3 Mindestabstand (min) | 1 | 1–1440 | `verdacht` | – | Experten-Oberfläche |
| R4 Mindestabstand (min) | 30 | 1–1440 | `verdacht` | – | Experten-Oberfläche |
| R5 Mindestabstand (min) | 5 | 1–1440 | `verdacht` | – | Experten-Oberfläche |
| Regeln: hoechstens pro Stunde (alle zusammen) | 6 | 1–60 | `verdacht` | – | Experten-Oberfläche |
<!-- EINSTELLUNGEN:END -->

„Ändern braucht“ gilt mit dem Paket `app`: Die Einstellungsseite verlangt die Admin-Anmeldung, in den Gruppen „Lesen“ und „Experten“ zusätzlich das Experten-Passwort. Was dort keine Gruppe hat, steht nicht auf der Einstellungsseite, sondern in der Experten-Oberfläche (`/experten`); die Werte der Regeln lassen sich auch per Datei und MQTT setzen ([REGELN.md](REGELN.md)). „(wahlweise)“ heißt: Das Paket ist in `weishaupt-wem-can.yaml` nicht eingebunden.

## 6a. Regeln: Betriebsarten bei Verdacht lesen

Paket `verdacht.yaml` liest die Betriebsarten **nur dann**, wenn das Mithören eine Änderung
vermuten lässt – ausschließlich Leseanfragen (`0x40`) an Knoten 1, nie JSON, kein Raster, mit
Mindestabstand je Regel und Obergrenze pro Stunde. **Ausführlich: [REGELN.md](REGELN.md).**

| Regel | Auslöser am Bus | liest | Vorgabe |
|---|---|---|---|
| R1 | WEM fragt den Kessel `2101/0A`, `2102/0D`, `2102/01` (SDO Block-Upload `0xA4` oder Upload `0x40`) | Warmwasser | <!--ab-werk:vd_r1-->an<!--/ab-werk--> |
| R2 | Kesselstatus 15 bzw. `0x252B` = `0F` bei bekanntem Warmwasser „Aus“ | Warmwasser | <!--ab-werk:vd_r2-->an<!--/ab-werk--> |
| R3 | Standby-Bit `0x1000` in `0x1C1` ändert sich | Heizkreis | <!--ab-werk:vd_r3-->an<!--/ab-werk--> |
| R4 | Heizanforderung passt nicht zur bekannten Heizkreis-Betriebsart | Heizkreis | <!--ab-werk:vd_r4-->aus<!--/ab-werk--> |
| R5 | übrige Statusbits in `0x1C1` ändern sich | Heizkreis | <!--ab-werk:vd_r5-->aus<!--/ab-werk--> |

Ausgeschaltete Regeln laufen im **Schattenmodus** weiter (melden „wäre ausgelöst“, lesen nicht).
Alle Regeln ruhen 2 min nach einem eigenen Schaltbefehl; R1 ruht außerdem 5 min nach einem Neustart
des WEM (Heartbeat `0x701` = `00`), und R5 liest nicht, wenn R3 im selben Augenblick schon liest.
Eigene Regeln (Kurzform „CAN-ID + Maske + Muster“, ab v30 auch Folgen, Flanken und Bedingungen)
sind experimentell und haben einen eigenen Hauptschalter. Einstellen per Experten-Oberfläche, Datei `<gerät>/cmd/regeln` oder Befehl
`<gerät>/cmd/regel`; Stand unter `<gerät>/regeln/stand`, Protokoll unter
`<gerät>/verdacht/protokoll`, Ereignisse unter `<gerät>/verdacht/ereignis`.

## 6b. Zusatzanzeigen für die Handy-App

Paket `zusatz.yaml` (ab v24) hat mit dem Kesselbus nichts zu tun: es verwaltet nur eine **Liste
fremder MQTT-Topics** (bis 6), die die Handy-App zusätzlich anzeigt – z. B. Raumtemperaturen oder ob
eine Wärmepumpe läuft. Das Board speichert die Liste dauerhaft und
veröffentlicht sie retained unter `<gerät>/app/zusatz`. **Wer die Topics liest, hängt von der App ab:**
Die Docker-App abonniert sie selbst, das Board liest sie dann nicht. Mit Paket `app` (App auf dem
Board, ab v38) abonniert das Board die Topics der Liste selbst – sein MQTT-Konto braucht dafür Leserecht.

| Feld | Bedeutung |
|---|---|
| `name` | Anzeigename, 1–24 Byte |
| `topic` | MQTT-Topic ohne Platzhalter `+`/`#` |
| `feld` | JSON-Feld im Payload, verschachtelt mit Punkt (`aenergy.total`); fehlt es, ist der Payload selbst die Zahl |
| `einheit` | bis 8 Byte, z. B. `°C` (3 Byte) |
| `art` | `wert` (Vorgabe) zeigt die Zahl; `laeuft` zeigt „läuft“ ab `schwelle` (Vorgabe 1), sonst „aus“ |

Einstellen: JSON `{"version":N,"eintraege":[…]}` an `<gerät>/cmd/zusatz` (retained möglich,
Prüfsumme wie bei `cmd/regeln`) auf der Einstellungsseite der App (Paket `app`) oder in der Experten-Oberfläche, Feld **Zusatz-Befehl**:

```
Wohnzimmer topic=sensoren/wz/temp feld=tC einheit=°C
WP topic=sensoren/wp/status feld=apower einheit=W art=laeuft schwelle=50
WP loeschen
alle loeschen
```

Der Name ist alles vor dem ersten Wort mit `=` (darf Leerzeichen enthalten); Werte ohne
Leerzeichen. Ein vorhandener Eintrag gleichen Namens wird ersetzt. Ergebnis in **Zusatz Meldung**,
die Liste in **Zusatzanzeigen**. Die App (auf dem Board und in Docker gleich) zeigt Werte, die älter als
10 min sind, gedimmt mit ihrem Alter („20,6 °C (35 min)“) – Batteriegeräte senden oft nur bei Änderung –
und nach 2 Stunden als „--“ (Docker-App: Umgebungsvariable `ZUSATZ_MAX_ALTER_MS`).
