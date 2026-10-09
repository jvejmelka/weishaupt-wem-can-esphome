# Changelog – ESP-CAN-Brücke Weishaupt (WeAct CAN485, ESPHome)


## App – 2026-10-09
- **Fortschrittszeile blieb dauerhaft stehen:** Am 08.10. lief Warmwasser Ein/Aus am Board sauber durch (Schaltprotokoll: „ok“), in der App blieb die Zeile aber auf „Bus liest nach“ stehen statt nach 3 min zu verschwinden. Ursache: Die App liest den Stand eines Befehls aus zwei getrennten Meldungen (`befehl/status` und `status/json`, beide QoS 0). Fehlte in **einer** der beiden der Abschluss, hielt sie sich an den letzten Zwischenstand, und zwar so lange, bis die fehlende Meldung kam, also nie. Jetzt gilt für dieselbe Kennung der Abschluss aus der Quelle, die ihn hat. Als Notbremse verschwindet ein Befehl ohne Abschluss nach 10 min (Board-Zeit). Mit beiden Fehlerbildern (verlorene Meldung, veraltetes `status/json`) nachgestellt: vorher Dauerlauf, jetzt Abschluss ✓ und nach 3 min weg.
- **Neue Version kommt ohne Warten an:** Hinter einem Reverse Proxy mit eigenem Cache-Header (bei uns NPMplus, `max-age=14400`) holte der Service-Worker die Startseite bis zu 4 h aus dem HTTP-Cache. Sie verweist auf `app.js?v=<Version>`, also lief so lange die alte App. Jetzt fragt der Service-Worker immer beim Server nach (`cache: 'no-cache'`, ungeändert nur ein 304), und beim Installieren füllt er seinen Cache am HTTP-Cache vorbei.

## App – 2026-09-27 (nach v30)
- **Knopf-Rahmen am Handy:** der Maus-über-Effekt der Betriebsart-Knöpfe gilt nur noch bei echter Maus (`@media (hover: hover)`). Auf Touch-Geräten blieb sonst der zuletzt berührte Knopf umrandet und sah aus wie eine zweite aktive Betriebsart. Nach dem Update die App einmal neu laden.
- **Live erprobt (Firmware v30 + App):** Warmwasser Ein/Aus und Heizkreis Standby/Zeitprogramm 1 aus der App, einzeln und schnell hintereinander. Der zweite Befehl wurde innerhalb der Sperrminute vorgemerkt und danach automatisch gesendet; alle Befehle liefen angenommen → gesendet → WEM bestätigt → am Bus bestätigt. „Status lesen“ holte den geänderten Warmwasser-Sollwert.

## v30 – 2026-09-27 (intern: R1–R5 als Datenregeln, Verhalten unverändert; Regelformat für eigene Regeln erweitert)
- **Intern umgebaut, Verhalten unverändert.** Die festen Regeln R1–R5 sind jetzt **vorinstallierte Datenregeln** im selben Format wie die eigenen Regeln und laufen durch **einen** Auswerter ([`regelwerk.h`](components/weishaupt_can/regelwerk.h), `rw::frame`). Bisher hatte jede Regel eigene C++-Funktionen und eigene `on_frame`-Zweige in `verdacht.yaml`; jetzt gibt es dort einen Handler für alle Regeln (plus den unveränderten für die Antworten `0x581`).
- **Das Format** ([`verdacht.h`](components/weishaupt_can/verdacht.h), erklärt in [REGELN.md, Abschnitt 5](REGELN.md#5-eigene-regeln-experimentell)): je Regel 1–4 Auslöser – **Frame** (CAN-ID, Mindestlänge, Maske, ein oder mehrere Muster), **Folge** (2–4 Frames mit Zeitfenstern, so ist R1 beschrieben), **Flanke** (Wert aus 1–2 Bytes mit Bitmaske ändert sich, optional nur zu bestimmten Werten – R2, R3, R4, R5) und **Baustein** – dazu je Auslöser eine Bedingung auf den bekannten Stand (`wenn`), auf Regelebene `ruhe_nach` (R1: 5 min nach Neustart des WEM) und `unterdrueckt_durch` (R5 nicht zusätzlich, wenn R3 schon liest). Mindestabstand, Obergrenze, Schattenmodus, Ruhe nach eigenem Schaltbefehl, Protokoll und Ereignisse gelten für alle Regeln einheitlich.
- **Was Baustein bleibt:** nur die Rückrechnung von R4 (`r4_vorlauf`: Raumsoll-Stufe aus Vorlaufsoll `0x241` und Außentemperatur `0x201`, Gleitkomma und Stufen über zwei Frames) – als Byte-Muster nicht sinnvoll ausdrückbar. Sie ist benannt und auch in eigenen Regeln nutzbar. Ebenfalls nur im Code: wo die Vorwerte von R3/R5 gespeichert werden (gleiche Schlüssel wie bisher).
- **Eigene Regeln können das neue Format nutzen.** Alte Regeln (`can_id`/`maske`/`muster`) gelten unverändert und werden byteweise gleich gespeichert, angezeigt und unter `regeln/stand` gemeldet; neue Felder: `muster` als Liste, `laenge_min`, `wenn`, `ausloeser`, `ruhe_nach`, `unterdrueckt_durch`. Beispiel in [`regeln.json.example`](regeln.json.example).
- **Gleich geblieben:** Schalter und Mindestabstände R1–R5, Hauptschalter der eigenen Regeln, Obergrenze (Namen, IDs, gespeicherte Werte), Topics `cmd/regeln`, `cmd/regel` (inkl. Ablehnung von `verdacht`), `regeln/stand`, `verdacht/ereignis`, `verdacht/protokoll`, alle Logtexte. Keine neue CAN-Anfrage, keine periodische Abfrage, nichts an Knoten 2.
- **Nachweis:** die Regel-Logik von v29 liegt eingefroren in [`tests/referenz_v29/`](tests/referenz_v29/). Der Äquivalenztest treibt v29 und die Datenregeln mit denselben Frames (400 Zufallsläufe à 1500 Frames, die Hälfte über den `millis()`-Überlauf, dazu feste Szenarien genau an den Zeitfenstern von R1, R3/R5-Bits, R2/R4, Baustein, eigener Befehl, Abstände, Obergrenze, Schatten) und verlangt nach **jedem** Frame dieselbe Spur (Logzeilen, Aufrufe und ihr Ergebnis, Ereignisse, gespeicherter Zustand); er prüft auch, dass jeder Fall wirklich vorkam. Gegenprobe: neun absichtlich eingebaute Fehler machen ihn rot. Ein weiterer Test schreibt R1–R5 als JSON, liest sie wieder ein und vergleicht auch diese mit v29. **Jetzt 76 Tests.**
- Die bisherigen Einzeltests zu R1–R5 prüfen weiter die eingefrorene v29-Fassung (`rl29::…`); aus `regellogik.h` sind die regel-eigenen Funktionen entfernt.

## v29 – 2026-09-27 (intern: eigenes ESPHome-Bauteil `weishaupt_can`; eine Fehlerbehebung)
- **Intern umgebaut, Verhalten sonst unverändert.** Zustand und Logik stecken jetzt in einem ESPHome-Bauteil (external component) [`components/weishaupt_can/`](components/weishaupt_can/) statt in 68 Globals und langen Lambdas. Die Klassen in [`kern.h`](components/weishaupt_can/kern.h) (reines C++): `Bus` (Lebenszeichen, Anlaufpause, Sendesperre), `Anlass` (wann die Betriebsarten nachgelesen werden), `Lesebefehl`, `Scan`, `Brenner`, `Protokoll`, `Schalten` (Sperrminute, Schreibbefehl), `Verdacht` (Laufzeit der Regeln), `Zusatz`. Die bisherigen Header `regellogik.h`, `befehle.h`, `verdacht.h`, `zusatz.h` und `register_gen.h` sind mit umgezogen (vorher `pakete/`).
- **Die Pakete sind dünn:** Entitäten, CAN-Takte (`interval` + `canbus.send`), `on_frame`-Zuordnung, MQTT-Abos und der WEM-Aufruf (`http_request`) bleiben YAML – das ist Konfiguration; die Lambdas rufen nur noch das Bauteil (`id(wcan)->…`). Lambda-Code insgesamt ein Viertel kürzer, keine `globals:` mehr.
- **Gespeicherte Werte bleiben erhalten:** Brennerstarts, eigene Regeln, Regeln-Datei (Prüfsumme, Version), R3/R5-Vorzustand, Zusatzliste (Speicherform, Prüfsumme, Version) liegen unter **denselben Schlüsseln und Datentypen** wie bis v28 (`1944399030 ^ md5(<alte id>)`, im erzeugten Code geprüft: alle neun Schlüssel gleich). Schalter und Zahlen (Regeln an/aus, Abstände, Wächter, „Eigene CAN-Anfragen“) sind unverändert Entitäten gleichen Namens.
- **Nachweis „gleiches Verhalten“:** Konfiguration v28/v29 nach `esphome config` Feld für Feld gleich (ohne Lambda-Rümpfe, Globals und den neuen Bauteil-Block): 111 Entitäten mit Namen, IDs, Einheiten, Intervallen und Startwerten, 16 CAN-Sendeaktionen (Ziel und Daten unverändert), 11 Intervalle, 23 `on_frame` in gleicher Reihenfolge, alle MQTT-Topics. Die neuen Tests treiben den Code von v28 (im Test als Vergleichsfassung) und die Klassen mit denselben, teils zufälligen Eingaben – auch über den `millis()`-Überlauf – und verlangen gleichen Zustand nach jedem Schritt (**jetzt 67 Tests**).
- **Einbinden für Nachbauer:** `external_components` mit `source: github://jvejmelka/weishaupt-wem-can-esphome@main` bzw. lokal – siehe [INSTALL.md](INSTALL.md#das-bauteil-weishaupt_can). Aufbau in der [README](README.md#aufbau).
- CI: alle Jobs fest auf `ubuntu-24.04` (`ubuntu-latest` wechselt im Oktober 2026 auf 26.04), der Firmware-Job prüft, dass das Bauteil gebaut wurde.
- **Fehlerbehebung (einzige Verhaltensänderung):** bis v28 blieb die Merkvariable „Heizkreis jetzt lesen“ stehen, wenn der Bus im 5-s-Takt nicht bereit war. Fiel eine Heizkreis-Lesung in die Sekunden vor einem Busausfall (≥ 30 s ohne Frame, z. B. Heizung stromlos), ging die Leseanfrage 0x2933/2 danach **alle 5 s erneut** hinaus, bis der Bus wieder bereit war – auch während der 10-min-Anlaufpause nach dem Stromzyklus, in der der WEM Ruhe haben soll. Das war eine periodische Abfrage, die es nicht geben darf. Jetzt wird die Merkvariable in jedem Takt zuerst zurückgesetzt: jede Lesung geht genau einmal hinaus, die gewollte Lesung nach Board-Start bzw. nach der Anlaufpause bleibt. Am Board seit dem Umbau auf den Anlass-Takt nicht beobachtet (braucht einen Busausfall genau nach einer Lesung), aus dem Code belegt; Test `kern_hk_lesung_wiederholt_nie` (wird mit der v28-Reihenfolge rot).

## v28 – 2026-09-27 (intern: eine Registertabelle, Verhalten unverändert)
- **Intern umgebaut, Verhalten unverändert.** Alle CAN-Objekte stehen jetzt in **einer** Tabelle, [`register.yaml`](register.yaml): Knoten, Index, Subindex, Lage im PDO, Datentyp, Faktor, Einheit, Weg (PDO, Antwort auf eine Frage des WEM, eigene Anfrage, Schreibtelegramm), Takt, Aktualisierungsklasse, Stand (belegt/plausibel/unbestätigt/Vermutung), Beleg, Quelle und – wo bekannt – das JSON-Gegenstück (MI/MX/OX/OS/VS).
- **Generator** [`werkzeuge/register_erzeugen.py`](werkzeuge/register_erzeugen.py) erzeugt daraus `pakete/register_gen.h` (C++-Konstanten `reg::…`) und die Tabellen in [PROTOKOLL.md](PROTOKOLL.md) (Abschnitte 3–5) und [README.md](README.md#was-belegt-ist-und-was-nicht). Die YAML-Lambdas, `regellogik.h` und das JSON-Schreiben benutzen nur noch diese Konstanten statt fest eingetippter Zahlen. Er prüft außerdem, dass jedes `reg::…` existiert und jede Leseanfrage an den Knoten ihres Objekts geht.
- **Nachweis „gleiches Verhalten“:** der von ESPHome erzeugte Code (`main.cpp`) von v27 und v28 unterscheidet sich nur in Lambda-Rümpfen (Konstante statt Zahl), den Sendedaten (`reg::lese(…)` statt fester Bytes), der Versionsangabe und dem neuen Include – gleiche Entitäten, Namen, Topics, Takte, Anfragen. Vier neue Tests vergleichen die erzeugte Tabelle Byte für Byte und Faktor für Faktor mit den bisher fest eingetragenen Werten (**jetzt 53 Tests**).
- CI: neuer Job prüft, dass die erzeugten Dateien zu `register.yaml` passen (Generator laufen lassen, `git diff --exit-code`).
- Doku: Tabellen in PROTOKOLL.md jetzt mit Spalte „Klasse“ (Weishaupt-Aktualisierungsklasse), Wärmemengen je Objekt eine Zeile, Rücklauf-Kandidat `0x2533`/2 und Wärmeleistung `0x2731`/2 in der Tabelle der WEM-Fragen. Neue Kurzübersicht in der README. Anleitung „Register ändern“ in [INSTALL.md](INSTALL.md#14-register-ändern).


## v27 – 2026-09-27 (strukturierter Status, Schaltbefehle mit Kennung)
- **Neues Topic `<gerät>/status/json`** (retained, fester Aufbau): Heizkreis {vorgabe, ist, heizt, statusbits, quelle, zeit}, Warmwasser {vorgabe, ladung, soll_aktuell, soll_normal, quelle, zeit}, Kessel {status}, Schalten {laufend, warteschlange, frei_ab, letzte}, Bus {lebt, anlaufpause, letzter_frame_s}, Firmware. Nur bei Änderung, höchstens alle 2 s, nach MQTT-Wiederverbindung einmal. **Reine Buchführung: keine zusätzliche CAN-Anfrage, keine periodische Abfrage der Betriebsarten.** Aufbau und Felder: [TOPICS.md](TOPICS.md).
- **Schaltbefehle mit Kennung und Rückmeldung:** `cmd/heizkreis` und `cmd/warmwasser` nehmen zusätzlich JSON `{"id":17,"wert":"ZP2","quelle":"App"}` an (Kurzform `ZP1`–`ZP3` neu, auch im Klartext). Die alte Klartextform bleibt gültig und bekommt intern eine Kennung (`b1`, `b2` …), ebenso Befehle aus Weboberfläche und Home Assistant. Jede Phase geht als JSON an **`<gerät>/befehl/status`**: angenommen → vorgemerkt (`warten_s`) → gesendet → pruefe_bus (`wem`) → bestaetigt / gescheitert (`cm05`, `nicht_uebernommen`, `keine_rueckmeldung`, `geleert`); dazu `ersetzt` (neuerer Wunsch) und `abgelehnt` (ungültig – nie in der Warteschlange, nie am WEM). Warteschlange, Sperrminute und Kontrolle am Bus sind unverändert.
- **Alte Text-Topics bleiben parallel** („Ergebnis letzter Schaltbefehl“, „Warteschlange“, „Status gelesen“, „Heizkreis Status“, `schaltprotokoll`), gelten aber als veraltet – Gegenüberstellung in TOPICS.md. Die Texte „vorgemerkt …“ nennen jetzt die Quelle (`MQTT`, `Web/HA`, `App` …).
- **Testbar ausgelagert:** Warteschlange, Befehlsphasen, Parser der Schaltbefehle und Status-JSON in [`pakete/befehle.h`](components/weishaupt_can/befehle.h); das Zerlegen von `cmd/regel` („NAME an|aus|loeschen“, bisher ungeprüft im YAML) als `vd::befehl_zerlegen` in `verdacht.h`. **12 neue Tests (jetzt 49).**
- CI: `actions/checkout@v7`, `actions/setup-python@v7`, `actions/cache@v6` (Node 24 statt Node 20).
- **Handy-App:** liest Vorgabe, Zustand, Warteschlange und Fortschritt nur noch aus `status/json` und `befehl/status` (keine Textauswertung mehr), schickt Schaltbefehle als JSON mit eigener Kennung und verfolgt genau diesen Befehl bis „bestätigt ✓“ bzw. „✗ Grund“. Braucht Firmware ab v27.

## v26 – 2026-09-27 (intern umgebaut, Verhalten unverändert, Tests + CI)
- **Intern umgebaut, Verhalten unverändert.** Die Entscheidungslogik der Regeln (R1–R5, eigene Regeln, Schattenmodus, Mindestabstand, Obergrenze pro Stunde, Ruhe nach eigenem Schaltbefehl, Byte-Reihenfolge der Statusbits) steht jetzt in [`pakete/regellogik.h`](components/weishaupt_can/regellogik.h) – reines C++ ohne ESPHome, die Zeit kommt als Parameter. Die Lambdas in `verdacht.yaml` rufen nur noch diese Funktionen auf und kümmern sich um Log, Schalter und MQTT. Log-Meldungen, Ereignisse, Vorgaben und gespeicherte Werte sind dieselben.
- **Automatische Tests** ([`tests/`](tests/)): 37 Tests mit ausgedachten Beispiel-Frames nach PROTOKOLL.md, dazu die Datei-Parser für `cmd/regeln` und `cmd/zusatz`. Einer davon (`r1_erkennt_block_upload_A4`) wird rot, sobald R1 wieder nur `0x40` auswertet – der Fehler aus v22–v24 wäre damit sofort aufgefallen.
- **GitHub Actions:** bei jedem Push laufen die Tests, die Firmware wird mit ESPHome 2026.9.0 und einer Dummy-`secrets.yaml` kompiliert, und die Links in den .md-Dateien werden geprüft.
- `verdacht.h` und `zusatz.h` holen ArduinoJson auf dem Board wie bisher über ESPHome, in den Tests direkt.

## v25 – 2026-09-27 (Regeln: Fehler in R1 behoben, R3/R5, Schattenmodus, Ereignisse)
- **Fehlerbehebung R1: R1 hat in v22–v24 nie ausgelöst.** Der WEM fragt den Kessel mit Kommandobyte **`0xA4`** (SDO Block-Upload) ab, nicht mit `0x40`; R1 hat nur `0x40` ausgewertet. R1 erkennt jetzt beide (Index und Subindex stehen an derselben Stelle). An den Rohmitschnitten nachgeprüft: vorher 0 Treffer, jetzt jede Warmwasser-Umschaltung.
- **R3 reagiert nur noch auf das Standby-Bit `0x1000`** (Heizung ein ↔ Standby). Das bisherige R3-Verhalten (jede Änderung der übrigen Statusbits) ist die neue **R5**, eigener Schalter und Mindestabstand (Vorgabe aus, 5 min). Beide Zustände werden dauerhaft gespeichert.
- **Neue Vorgaben:** R1, R2, R3 an; R4, R5 aus; eigene Regeln aus. Mindestabstand R1 und R3 jetzt **1 min** (vorher 10 bzw. 5) – beide Folgen sind je Umschaltung genau einmal da, und mit dem alten Abstand wären in den Mitschnitten schnelle Umschaltungen verpasst worden.
- **Schattenmodus:** ausgeschaltete Regeln (R1–R5, eigene Regeln einzeln aus oder Hauptschalter aus) prüfen ihre Bedingung weiter. Trifft sie zu, wird **nicht** gelesen, aber im Log (INFO „… waere ausgeloest (Regel aus) - nicht gelesen“) und als Ereignis `waere` gemeldet. Eigener Mindestabstand je Regel, zählt nicht gegen die Obergrenze, steht nicht im Protokoll. R4 und R5 prüfen ihre Bedingung dafür jetzt unabhängig vom Schalter.
- **Ereignisse an `<gerät>/verdacht/ereignis`** (JSON, nicht retained): `ausgeloest`, `waere`, `ergebnis` (mit `wert` und `geaendert` true/false/null), `keine_antwort` – für Telegraf, Grafana und Home Assistant.
- **Log:** jede Auslösung und jedes Ergebnis als INFO, jede unterdrückte Auslösung (eigener Befehl, WEM-Neustart, Mindestabstand, Obergrenze, Bus nicht bereit) als DEBUG, Tag `verdacht`.
- **Eigene Regeln ruhen jetzt ebenfalls 2 min nach einem eigenen Schaltbefehl** (wie R1–R5).
- Datei `cmd/regeln`, `cmd/regel` und `regeln/stand` kennen `R5`; Weboberfläche: Gruppe „Regeln (R1-R5)“, R3 heißt „R3 Heizung ein oder Standby“, neu „R5 uebrige Statusbits geaendert“. Nach dem Update gilt für R3 und R5 der Startwert aus `substitutions` (der Schalter wurde umbenannt) – mit gespeicherter Regeln-Datei `version` hochzählen und erneut senden.
- **Neue Dokumentation [REGELN.md](REGELN.md):** jede Regel mit Auslöser, Begründung, Sicherungen, Grenzen und Prüfung an Mitschnitten; Einstellen, Schalten per MQTT, Protokoll und Ereignisse, Telegraf-Beispiel. PROTOKOLL.md Abschnitt 6a verweist nur noch darauf.

## v24 – 2026-09-27 (Zusatzanzeigen)
- **Neues Paket `zusatz.yaml` (optional, braucht `mqtt`):** Liste von bis zu 6 Zusatzanzeigen für die Handy-App – Name, MQTT-Topic, Feld (bei JSON, auch verschachtelt `a.b`; leer = Payload ist die Zahl), Einheit, Art `wert` oder `laeuft` (mit Schwelle).
- **Das Board liest diese Werte nicht selbst** und erzeugt keinen zusätzlichen Busverkehr oder Broker-Verkehr außer der Konfiguration: es speichert die Liste dauerhaft und veröffentlicht sie retained unter `<gerät>/app/zusatz` (beim Start und bei jeder Änderung).
- Einstellen per MQTT `<gerät>/cmd/zusatz` (JSON `{"version":N,"eintraege":[…]}`, Ungültiges mit Meldung abgelehnt; dieselbe retained Datei wird dank Prüfsumme nicht erneut angewandt) oder in der Weboberfläche (Gruppe „Zusatzanzeigen (Handy-App)“, Feld „Zusatz-Befehl“: `NAME topic=… [feld=…] [einheit=…] [art=wert|laeuft] [schwelle=…]`, `NAME loeschen`, `alle loeschen`). Vorlage `zusatz.json.example`.
- **Handy-App:** abonniert die Liste und die genannten Topics, zeigt unter dem Kessel eine kompakte Zeile („Wohnzimmer 21,3 °C · WP ◉ läuft“); Werte älter als 10 min als „--“, leere Liste = keine Zeile. `/api/status` liefert `zusatz: [{name, wert, einheit, art, laeuft, alter_s}]`. Das App-Konto braucht Leserecht auf jedes eingetragene Topic.

## Handy-App – 2026-09-27 (Ladung, Brenner-Zweck, Schaltfortschritt, ohne Scrollen)
- Warmwasser: **„Ladung: Gas“** (orange pulsierend wie der Brenner, Kessel im Warmwasserbetrieb = Kesselstatus 15) bzw. „Ladung: aus“ ersetzt „Zustand: lädt gerade / keine Ladung“.
- Kessel: hinter der Brennerphase der **Zweck** aus dem Kesselstatus („· Heizung“, „· Warmwasser“, „(Heizung wartet)“ bei Warmwasserbetrieb mit gleichzeitiger Heizanforderung); Kaminfeger und Wartung in Rot.
- **Fortschritt eines Schaltbefehls** wieder sichtbar, jetzt unter den Knöpfen des betroffenen Blocks: vorgemerkt (ab HH:MM) → an WEM gesendet (JSON) → Bus liest nach → bestätigt ✓ bzw. rot ✗ mit Grund; 3 min nach dem Abschluss stehen, sonst keine Zeile.
- **Fit-to-Screen:** die Grundschrift wird gemessen statt geschätzt (größte Schrift, bei der alles ohne Scrollen passt); feste kleine Abstände statt Luft zwischen den Blöcken. `?diag` zeigt Viewport, Pixelverhältnis, Schrift und Scrollhöhe.

## v23 – 2026-09-27
- **Kein gemeinsamer Hauptschalter mehr für R1–R4.** Jede feste Regel ist einzeln schaltbar (Weboberfläche, Datei, `cmd/regel`), mit eigenem Mindestabstand; die gemeinsame Obergrenze pro Stunde bleibt. **Vorgaben im Repository: nur R3 an** (= Verhalten bis v21: Heizkreis nachlesen, wenn sich die Statusbits ändern), R1, R2, R4 aus.
- **Eigene Regeln** sind ein eigener Block „Eigene Regeln (experimentell)“ mit **eigenem Hauptschalter** (Vorgabe aus), der nur für die eigenen Regeln gilt.
- Weboberfläche: Gruppen „Regeln (R1-R4)“ und „Eigene Regeln (experimentell)“ getrennt; „Regeln aktiv“ zeigt zusätzlich, ob die eigenen Regeln an sind. Umbenannt: „Regeln Protokoll“, „Regeln: hoechstens pro Stunde (alle zusammen)“.
- **Datei-Format `cmd/regeln` geändert:** `{"version":N, "R1":{"an":…,"abstand_min":…}, …, "max_pro_stunde":6, "eigene_regeln_an":false, "eigene":[…]}`. Das alte Feld `verdacht` wird **mit Meldung abgelehnt** (nicht umgedeutet – `verdacht: true` hieß „R1–R4 an“ und würde sonst still die eigenen Regeln einschalten). `version` wird im Stand zurückgemeldet.
- `cmd/regel`: Namen `R1`–`R4`, `eigene` (Hauptschalter der eigenen Regeln) und eigene Regeln; `verdacht` wird mit Hinweis abgelehnt. `regeln/stand` im neuen Format.
- `substitutions` umbenannt: `regel_r1_start` … `regel_r4_start`, `regel_abstand_r1` … `_r4`, `regeln_max_h`, `eigene_regeln_start` (bis v22 `verdacht_…`).

## v22 – 2026-09-27
- **Verdachts-Lesen (experimentell), neues Paket `verdacht.yaml`:** die Betriebsarten werden gelesen, wenn das Mithören eine Änderung vermuten lässt – ausschließlich SDO-Leseanfragen an Knoten 1, nie JSON, keine periodische Abfrage, nur bei lebendem Bus und außerhalb der Anlaufpause. **Hauptschalter, Vorgabe AUS.**
- Feste Regeln, einzeln schaltbar mit Mindestabstand: **R1** WEM-Abfragefolge `2101/0A, 2102/0D, 2102/01` (Warmwasser-Wechsel), **R2** Warmwasserbetrieb trotz bekanntem Aus, **R3** Statusbits `0x1C1` geändert (bisheriger Anlass, jetzt schaltbar, Zustand dauerhaft gespeichert), **R4** Heizanforderung passt nicht zur Betriebsart (Vorgabe aus). Obergrenze für alle zusammen pro Stunde (Vorgabe 6). R1–R4 ruhen 2 min nach eigenen Schaltbefehlen.
- **Eigene Regeln** (bis 8): CAN-ID + Maske + Muster → Heizkreis, Warmwasser oder beide lesen; im Flash gespeichert, schaltbar per Datei, per `cmd/regel` und in der Weboberfläche („Regel-Befehl“). `0x601`/`0x581` sind als Auslöser gesperrt.
- **Einstellen per Datei:** Startwerte als `substitutions` in der Hauptdatei; zur Laufzeit JSON an `<gerät>/cmd/regeln` (retained), ungültiges wird mit Meldung abgelehnt; aktiver Stand retained unter `<gerät>/regeln/stand`. Vorlage `regeln.json.example`.
- Weboberfläche: je Regel „was und warum“ und „zuletzt ausgelöst“, Protokoll der letzten zehn Auslösungen (Regel, gelesen, geändert ja/nein), auch retained unter `<gerät>/verdacht/protokoll`.
- **Geändert:** Mit Hauptschalter AUS löst eine Änderung der Statusbits kein Nachlesen mehr aus (bis v21 geschah das immer). Gelesen wird dann nur beim Start, nach eigenen Schaltbefehlen und auf „Status lesen“. Ohne Paket `verdacht` bleibt das alte Verhalten.

## v21 – 2026-09-27
- **Status lesen auf Wunsch:** Knopf „Status lesen“ in der Weboberfläche (Gruppe Betriebsarten) und MQTT-Befehl `<gerät>/cmd/status` (beliebiger Inhalt). Liest einmal die Heizkreis-Betriebsart (Knoten 1 0x2933/2) und – mit Paket warmwasser – Warmwasser-Betriebsart und -Sollwerte (0x2A20/0x2A2C/0x2A39). Nur CAN-Leseanfragen über das vorhandene Anlass-Nachlesen, keine WEM-JSON-Anfrage, keine periodische Abfrage.
- Die 2-min-Grenze gilt für diese eine Lesung nicht; Wünsche werden höchstens alle 10 s angenommen, gesendet wird nur bei lebendem Bus (in der Anlaufpause erst danach).
- Neue Anzeige „Status gelesen“: Zeitpunkt der letzten Lesung der Heizkreis-Betriebsart bzw. Hinweis „angefordert …“ / „Bus schweigt“.
- Handy-App: Knopf „STATUS LESEN“ neben der Überschrift Betriebsart.

## v20 – 2026-09-27
- **Brenner nach Betriebsphase** (0x2541: 0 aus, 1 Vorbelüftung, 2 Steuer-, 3 Regelbetrieb, 4 Nachbelüftung). „Brenner Status“ zeigt jetzt Aus / Vorlüften / Ein / Nachlüften; „Brenner“ (0/1) und der Startzähler zählen nur noch mit Flamme (Phase 2/3), nicht mehr schon beim Vorlüften. Neuer Zahlenwert „Brennerphase“.

## Handy-App – 2026-09-27
- Die Web-App liegt jetzt im Ordner `app/`. Ihre frühere Fassung fragte den WEM direkt über JSON ab und hat ihn damit mehrfach gesperrt; jetzt liest sie nur noch MQTT vom Board und schaltet über dessen `cmd`-Topics – kein Zugriff mehr auf den WEM. Neu: Warmwasser mit Vorgabe/Ist und Ein/Aus, Betriebsarten vierspaltig, echter Rücklauf, Datum statt Build-Nummer.

## v19 – 2026-09-27
- **Anlaufpause unterscheidet zwei Fälle:** nach einem Neustart des Boards (Flashen, Stromausfall am Board) 1 min – der Bus lief ja weiter; kommt der Bus dagegen nach einem Ausfall wieder (Heizung war stromlos, WEM fährt hoch), bleibt es bei 10 min (`anlaufpause_min`).

## v18 – 2026-09-27
- **Betriebsarten werden nicht mehr periodisch gelesen**, sondern nur bei Anlass: einmal nach der Anlaufpause, wenn sich die Statusbits ändern (PDO 0x1C1 – etwa bei Umstellung am Display, in der Weishaupt-App oder im Portal) und nach eigenen Schaltbefehlen. Heizkreis-Betriebsart höchstens alle 2 min. Warmwasser-Betriebsart und -Sollwerte hängen sich an denselben Anlass und werden nach einem Warmwasser-Schaltbefehl nachgelesen.
- Lücke, bewusst in Kauf genommen: ein Wechsel zwischen zwei Zeitprogrammen am Display ändert die Statusbits nicht und bleibt bis zum nächsten Anlass unbemerkt.
- Entfällt: 15-min-Abfrage aus v16, 5-min-Abfrage der Betriebsarten.

## v17 – 2026-09-27
- **Mehr Werte rein passiv**, ohne eine einzige Anfrage:
  - **Rücklauf** (Rücklauftemperatur VPT, 0x2699), **Vorlauf VPT** (0x2697) und **Sollleistung** (0x2698) aus den SDO-Telegrammen, die der Kessel alle ~5 s an Knoten 0x42 schickt (0x6C2).
  - **Kesseltemperatur** zusätzlich aus PDO 0x241 (Bytes 2–3) – alle paar Sekunden statt nur, wenn jemand fragt.
  - **Uhrzeit der Heizung** aus PDO 0x181 (Diagnose).

## v16 – 2026-09-27
- Auch bei ausgeschaltetem Schalter „Eigene CAN-Anfragen“ fragt das Board **Heizkreis- und Warmwasser-Betriebsart alle 15 min** ab (2 Anfragen je Viertelstunde an den WEM). Beide liegen im WEM und kämen sonst nie über den Bus – eine Umstellung am Kesseldisplay oder im Portal bliebe unbemerkt. Sie gehen beim Abschalten deshalb nicht mehr auf „unbekannt“.

## v15 – 2026-09-27
- **Schalter „Eigene CAN-Anfragen“** (Einstellungen, bleibt über Neustarts erhalten). Startzustand in der Hauptdatei über `can_anfragen_start`, Vorgabe **aus** (nur mithören). Aus = nur mithören: ausgewertet wird, was ohnehin auf dem Bus liegt (PDOs des Kessels, Antworten auf Fragen des WEM). Schalten von Heizkreis-Betriebsart und Warmwasser bleibt auch dann erlaubt, samt der einen Kontroll-Leseanfrage; ebenso der Lesebefehl von Hand.
- Beim Abschalten werden alle Werte, die nur durch eigene Abfragen aktuell blieben, auf „unbekannt“ gesetzt – kein veralteter Wert, der aktuell aussieht. Fragt der WEM einen davon selbst ab, kommt er über das Mithören zurück.

## v14 – 2026-09-27
- **Abfragetakt nach Weishaupt-Vorgabe**: kein Wert öfter als alle 40 s (kürzestes Intervall des Datenloggers im WEM-Portal; die Datenpunktliste des WEM-Modbus-Gateways nennt 30 s / 60 s / 10 min).
  Kessel: Temperatur, Abgas, Vorlauf, Leistung, Drehzahl, Brenner alle 40 s; Vorlauf-Soll, Volumenstrom, Druck alle 60 s.
  WEM: Vorlauf Heizkreis, Vorlaufsoll-Anforderung alle 60 s; Heizkreis- und Warmwasser-Betriebsart, Raumsoll, WW-Sollwerte alle 5 min (nach jedem Schaltbefehl wird gezielt nachgelesen).
- Statusbits 0x274D werden nicht mehr abgefragt, sie kommen als PDO 0x1C1 von selbst.
- **Anlaufpause**: nach Board-Start oder wenn der Bus wieder da ist (Heizung war stromlos), 10 min nur mithören. Anzeige „Eigene CAN-Anfragen“ unter Diagnose.
- Last auf den WEM (Knoten 1): von 12 auf etwa 2,6 Anfragen pro Minute.

## v13 – 2026-09-27
- **WEM-Lebenszeichen entfernt** – Automatik alle 6 h, Knopf „jetzt prüfen“ und Anzeige. Jede zusätzliche JSON-Anfrage ist ein Risiko: am 27.09. fiel die JSON-Schnittstelle des WEM nach vier Schaltbefehlen und einem Lebenszeichen binnen zehn Minuten aus (CM=05 auf alle Register).
- **Zähler für WEM-JSON-Fehler und CM=05 entfernt.** CM=05 steht weiterhin im Ergebnis des Schaltbefehls.
- Der WEM wird damit nur noch angesprochen, wenn jemand schaltet.

## v12 – 2026-09-27
- **Vorlauf** (Kessel, 0x2536/00) wird jetzt selbst abgefragt – der WEM fragt es nie ab, deshalb blieb das Feld bisher leer. Deutung bei laufendem Brenner noch zu prüfen.
- Schalten per MQTT (`cmd/heizkreis`, `cmd/warmwasser`) am echten Board getestet: Warteschlange, WEM, Bestätigung am Bus, Protokoll.

## v11 – 2026-09-27
- **Warteschlange sichtbar**: eigenes Feld „Warteschlange" (Ziel, Wert, Quelle, ab wann der nächste Befehl frei ist) und Knopf „Warteschlange leeren". Höchstens zwei Einträge, einer je Ziel (Heizkreis vor Warmwasser); ein neuer Wunsch für dasselbe Ziel ersetzt den alten.

## v10 – 2026-09-27
- **Pakete**: Kessel (Pflicht), Warmwasser, MQTT, Home-Assistant-API, WEM-Schalten, Warmwasser-Schalten, feste IP – einzeln abschaltbar.
- **Warteschlange**: ein Schaltwunsch in der Sperrminute wird vorgemerkt (neuester je Ziel gewinnt) und danach gesendet.
- **Schalten per MQTT**: `cmd/heizkreis` (1–8 oder Name), `cmd/warmwasser` (Ein/Aus).
- **Schaltprotokoll** der letzten zehn Befehle (Web und `schaltprotokoll` retained), mit Quelle und Ergebnis am Bus; Kontrolle erst nach der eigenen Leseanfrage (kein Fehlurteil durch eine zufällige Abfrage).
- **WEM-Lebenszeichen** alle 6 h ± 30 min Zufall, Vergleich mit dem Bus; Knopf zum sofortigen Prüfen.
- **CM=05-Erkennung** mit eigenem Zähler.
- **WLAN wechseln** im Web (Name, Passwort, „WLAN übernehmen"; bei Fehlschlag bleibt das alte WLAN).
- „Warmwasser aktiv" und „Brenner Status" als Text Ein/Aus; die Zahlen stehen unter Zahlencodes.
- Rohmitschnitt `canraw` abschaltbar (Vorgabe aus, beim Scan immer an).
- Text-Topics jetzt ESPHome-Standard (`text_sensor/...`), Zahlen-Topics unverändert (`sensor/...`).

## v9 – 2026-09-26
- **Schreibfeld über WEM** (Experten): „MI MX OX OS WERT" in hex plus Knopf „Schreiben". Nur MI 01/02/03 erlaubt, Kessel gesperrt. Antwort des WEM wird ausgewertet (CM=04 bestätigt, CM=05 abgelehnt).
- **Netzdiagnose**: Gateway, Netzmaske, DNS-Server, BSSID und MAC-Adresse.
- **Zähler für WEM-JSON-Fehler** (bleibt über Neustarts erhalten).
- „Schaltstatus" heißt jetzt „Ergebnis letzter Schaltbefehl".
- Changelog nicht mehr in der Weboberfläche.

## v8 – 2026-09-26
- **Schalten im Web**: Heizkreis-Betriebsart und Warmwasser Ein/Aus über die WEM-JSON-Schnittstelle, Kontrolle am Bus, höchstens ein Befehl pro Minute.
- Web-Passwort; WEM-Adresse und Wächterzeit im Web änderbar; Neustart-Knopf, Laufzeit, Alter des letzten CAN-Frames, IP und WLAN.

## v7 – 2026-09-26 (Endfassung)
- **Senden nur, wenn der Bus lebt** (letzter Frame < 30 s): Poller, Scan und Lesebefehl schweigen bei ausgeschalteter Heizung → kein Bus-off nach Stromzyklus. Wächter (5 min ohne Frame → Neustart) bleibt als zweite Sicherung.
- **Lesebefehl über die Weboberfläche**: Feld „Lesebefehl (KN IDX SUB hex)" plus Knopf „Lesen", Antwort in „Lesebefehl Antwort" (Wert dezimal/hex oder Abbruchcode). Nur Lesen (0x40). Objektscan-Knopf entfernt (Scan weiter per MQTT `cmd/scan`).
- **Oberfläche in Gruppen**: Kessel, Betriebsarten und Sollwerte, Energie, Lesebefehl, Zahlencodes, Diagnose, Firmware. Changelog eine Zeile je Version.
- Textsensoren (Firmware, Changelog, ESPHome-Version) unter `text/`, damit Telegraf sie nicht als Zahl liest. „Betriebsart Heizkreis Code" heißt jetzt „Heizkreis Status Code" (0x274D-Bitfeld).
- **Neue Sensoren (Knoten 1, alle 60 s)**: Heizkreis-Betriebsart (0x2933/2, Text + Code), Warmwasser-Betriebsart (0x2A20/2, Ein/Aus), Raumsoll aktuell (0x2958/2), WW-Soll aktuell (0x2A2C/2), WW-Soll normal (0x2A39/2), Vorlauf Heizkreis (0x2907/2).
- Firmware-Stand, ESPHome-Version und Changelog auf der Weboberfläche.
- ESP-Chiptemperatur entfernt (ESP32-D0WD hat keinen kalibrierten Fühler, Werte wurden verworfen).

## v6 – 2026-09-26
- MQTT-Lesekanal `cmd/lesen`, frei wählbarer Scan `cmd/scan`, `cmd/stop` (Kommandobyte fest 0x40).
- Bus-off-Wächter: 5 min kein Frame → Neustart.

## v5 – 2026-09-26
- 0x274D als Statusbitfeld (0x1000 HK Standby, 0x0040 Heizbetrieb, 0x0010 WW-Ladung).
- Sensor „Vorlaufsoll Anforderung" (0x2640/3). Objektscan-Knopf entfernt.

## v4 – 2026-09-26
- Aktiver Kessel-Poller (Knoten 2, 20 s), Textsensoren unter `text/`, Abgas statt „Rücklauf" für 0x2537.

## Abbildung JSON → CAN (Knoten 1), Grundlage der v7-Sensoren
MI 01 System +0x100 · MI 02 Heizkreis +0x400 · MI 03 Warmwasser +0x500 · Kessel MI 07 = gleich, MI 09/01 +0x100 (Knoten 2).
