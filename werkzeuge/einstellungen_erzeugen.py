#!/usr/bin/env python3
"""Liest die Einstellungen (Schalter und Zahlen) aus den Paketen und haelt die Doku dazu aktuell.

Quelle ist der Code: pakete/*.yaml (restore_mode, initial_value, min/max), die Startwerte in
weishaupt-wem-can.yaml (substitutions) und die Liste der Einstellungsseite in
components/weishaupt_can/app_server.cpp (EINSTELLUNGEN, nur_experte).

Erzeugt in PROTOKOLL.md und PROTOKOLL.en.md
  - die Tabelle zwischen  <!-- EINSTELLUNGEN:BEGIN -->  und  <!-- EINSTELLUNGEN:END -->
und in allen .md-Dateien im Wurzelverzeichnis
  - den Wert zwischen  <!--ab-werk:ID-->  und  <!--/ab-werk-->   (ID = id des Schalters oder der Zahl;
    Schalter: an/aus bzw. on/off in den .en.md, Zahl: ihr Startwert).
So steht eine Werkseinstellung nirgends mehr von Hand in der Doku.

Prueft ausserdem: Jeder Name der Einstellungsseite gibt es in den Paketen, der dort eingetragene
Vorgabewert einer Zahl ist ihr initial_value, jeder restore_mode ist aufloesbar, und die
Olimex-Hauptdatei setzt keinen Startwert anders als die WeAct-Hauptdatei.

Aufruf:
  python3 werkzeuge/einstellungen_erzeugen.py            # schreibt die Dateien
  python3 werkzeuge/einstellungen_erzeugen.py --pruefen  # schreibt nichts, Exitcode 1 wenn veraltet
Braucht PyYAML (pip install pyyaml).
"""
import glob
import os
import re
import sys

import yaml

WURZEL = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
HAUPT = "weishaupt-wem-can.yaml"
HAUPT_WEITERE = ["olimex-weishaupt.yaml"]
APP_SERVER = os.path.join("components", "weishaupt_can", "app_server.cpp")
TABELLE_IN = ["PROTOKOLL.md", "PROTOKOLL.en.md"]

B_AN, B_AUS = "<!-- EINSTELLUNGEN:BEGIN -->", "<!-- EINSTELLUNGEN:END -->"
MARKE = re.compile(r"<!--ab-werk:([a-z0-9_]+)-->(.*?)<!--/ab-werk-->", re.S)
RESTORE = {"RESTORE_DEFAULT_ON": True, "RESTORE_DEFAULT_OFF": False,
           "ALWAYS_ON": True, "ALWAYS_OFF": False,
           "RESTORE_INVERTED_DEFAULT_ON": True, "RESTORE_INVERTED_DEFAULT_OFF": False}


def fehler(text):
    sys.exit("einstellungen_erzeugen: " + text)


class Lader(yaml.SafeLoader):
    """ESPHome-YAML: eigene Marken (!include, !lambda, !extend, !secret ...) als einfache Werte lesen."""


def _marke(lader, suffix, knoten):
    if isinstance(knoten, yaml.ScalarNode):
        return lader.construct_scalar(knoten)
    if isinstance(knoten, yaml.SequenceNode):
        return lader.construct_sequence(knoten, deep=True)
    return lader.construct_mapping(knoten, deep=True)


Lader.add_multi_constructor("!", _marke)


def yaml_lesen(pfad):
    with open(os.path.join(WURZEL, pfad), encoding="utf-8") as f:
        return yaml.load(f, Loader=Lader) or {}


def einsetzen(wert, subst, wo):
    """${name} durch den Startwert ersetzen (Hauptdatei vor Paket)."""
    if not isinstance(wert, str):
        return wert

    def e(m):
        if m.group(1) not in subst:
            fehler(f"{wo}: ${{{m.group(1)}}} ist nirgends festgelegt")
        return str(subst[m.group(1)])
    return re.sub(r"\$\{(\w+)\}", e, wert)


def zahl(x):
    f = float(x)
    return str(int(f)) if f == int(f) else str(f)


def pakete_der_hauptdatei(pfad):
    """Paketdateien, die in der Hauptdatei eingebunden sind (auskommentierte zaehlen nicht)."""
    return {os.path.normpath(v) for v in (yaml_lesen(pfad).get("packages") or {}).values() if isinstance(v, str)}


def sammeln():
    haupt = yaml_lesen(HAUPT)
    haupt_subst = haupt.get("substitutions") or {}
    for weitere in HAUPT_WEITERE:
        for k, v in (yaml_lesen(weitere).get("substitutions") or {}).items():
            if k.endswith("_start") and k in haupt_subst and haupt_subst[k] != v:
                fehler(f"{weitere}: {k} = {v}, in {HAUPT} aber {haupt_subst[k]} - die Doku nennt nur einen Wert")
    eingebunden = pakete_der_hauptdatei(HAUPT)

    gruppen, eintraege, ids = {}, [], {}
    dateien = sorted(glob.glob(os.path.join(WURZEL, "pakete", "*.yaml")))
    geladen = {os.path.relpath(d, WURZEL): yaml_lesen(os.path.relpath(d, WURZEL)) for d in dateien}
    for inhalt in geladen.values():
        for g in ((inhalt.get("web_server") or {}).get("sorting_groups") or []):
            gruppen[g["id"]] = g["name"]
    for pfad, inhalt in geladen.items():
        subst = dict(inhalt.get("substitutions") or {})
        subst.update(haupt_subst)
        for art in ("switch", "number"):
            for e in inhalt.get(art) or []:
                if not isinstance(e, dict) or "name" not in e:
                    continue          # !extend eines Eintrags aus einem anderen Paket
                wo = f"{pfad}: {art} \"{e['name']}\""
                d = {"art": art, "name": e["name"], "id": e.get("id"), "paket": os.path.basename(pfad)[:-5],
                     "eingebunden": os.path.normpath(pfad) in eingebunden,
                     "gruppe": gruppen.get((e.get("web_server") or {}).get("sorting_group_id"), "")}
                if art == "switch":
                    if "restore_mode" not in e:
                        continue      # Schalter ohne gespeicherten Zustand ist keine Einstellung
                    modus = einsetzen(e["restore_mode"], subst, wo)
                    if modus not in RESTORE:
                        fehler(f"{wo}: restore_mode {modus} kenne ich nicht")
                    d["an"] = RESTORE[modus]
                else:
                    if "initial_value" not in e:
                        continue
                    d["wert"] = zahl(einsetzen(e["initial_value"], subst, wo))
                    d["von"], d["bis"] = zahl(e["min_value"]), zahl(e["max_value"])
                if d["id"]:
                    if d["id"] in ids:
                        fehler(f"{wo}: id {d['id']} gibt es schon in {ids[d['id']]}")
                    ids[d["id"]] = pfad
                eintraege.append(d)
    return eintraege


def seite_lesen(eintraege):
    """Einstellungsseite der App: Gruppe und Stufe je Name; prueft die dort eingetragenen Vorgaben."""
    with open(os.path.join(WURZEL, APP_SERVER), encoding="utf-8") as f:
        cpp = f.read()
    m = re.search(r"EINSTELLUNGEN\[\]\[3\] = \{(.*?)\n\};", cpp, re.S)
    if not m:
        fehler(f"{APP_SERVER}: Tabelle EINSTELLUNGEN nicht gefunden")
    zeilen = re.findall(r'\{"([^"]*)",\s*"([^"]*)",\s*"([^"]*)"\}', m.group(1))
    m = re.search(r"static bool nur_experte\(const char \*gruppe\) \{(.*?)\}", cpp, re.S)
    if not m:
        fehler(f"{APP_SERVER}: nur_experte() nicht gefunden")
    experte = set(re.findall(r'strcmp\(gruppe, "([^"]+)"\) == 0', m.group(1)))
    nach_name = {e["name"]: e for e in eintraege}
    for gruppe, name, vorgabe in zeilen:
        e = nach_name.get(name)
        if e is None:
            fehler(f"{APP_SERVER}: Einstellung \"{name}\" gibt es in keinem Paket")
        if e["art"] == "number" and zahl(vorgabe) != e["wert"]:
            fehler(f"{APP_SERVER}: \"{name}\" hat dort die Vorgabe {vorgabe}, im Paket {e['paket']} aber {e['wert']}")
        if e["art"] == "switch" and vorgabe:
            fehler(f"{APP_SERVER}: Schalter \"{name}\" darf dort keine Vorgabe tragen (kommt aus restore_mode)")
        e["seite"], e["experte"] = gruppe, gruppe in experte
    reihenfolge = {name: i for i, (_, name, _) in enumerate(zeilen)}
    return sorted(eintraege, key=lambda e: (reihenfolge.get(e["name"], 999), e["paket"], e["name"]))


def ab_werk(e, en):
    if e["art"] == "switch":
        return ("on" if e["an"] else "off") if en else ("an" if e["an"] else "aus")
    return e["wert"]


def tabelle(eintraege, en):
    kopf = ("| Setting (name on the board) | Default | Range | Package | Group on the settings page | Changing it needs |"
            if en else
            "| Einstellung (Name auf dem Board) | Ab Werk | Bereich | Paket | Gruppe der Einstellungsseite | Ändern braucht |")
    z = [kopf, "|---|---|---|---|---|---|"]
    for e in eintraege:
        wert = ab_werk(e, en)
        if e["art"] == "switch":
            wert = f"**{wert}**"
        bereich = f"{e['von']}–{e['bis']}" if e["art"] == "number" else "–"
        paket = f"`{e['paket']}`" + ("" if e["eingebunden"] else (" (optional)" if en else " (wahlweise)"))
        if "seite" not in e:      # steht nicht auf der Einstellungsseite, nur in der Experten-Oberflaeche
            seite, stufe = "–", ("expert interface" if en else "Experten-Oberfläche")
        elif e["experte"]:
            seite, stufe = e["seite"], ("admin login and expert password" if en else "Admin-Anmeldung und Experten-Passwort")
        else:
            seite, stufe = e["seite"], ("admin login" if en else "Admin-Anmeldung")
        z.append(f"| {e['name']} | {wert} | {bereich} | {paket} | {seite} | {stufe} |")
    return "\n".join(z)


def datei_bearbeiten(pfad, eintraege, pruefen):
    en = pfad.endswith(".en.md")
    with open(os.path.join(WURZEL, pfad), encoding="utf-8") as f:
        alt = f.read()
    neu = alt
    if pfad in TABELLE_IN:
        if neu.count(B_AN) != 1 or neu.count(B_AUS) != 1:
            fehler(f"{pfad}: genau ein Block {B_AN} … {B_AUS} erwartet")
        a, b = neu.index(B_AN) + len(B_AN), neu.index(B_AUS)
        neu = neu[:a] + "\n" + tabelle(eintraege, en) + "\n" + neu[b:]
    nach_id = {e["id"]: e for e in eintraege if e["id"]}

    def marke(m):
        if m.group(1) not in nach_id:
            fehler(f"{pfad}: <!--ab-werk:{m.group(1)}--> - diese id hat kein Schalter und keine Zahl mit Startwert")
        return f"<!--ab-werk:{m.group(1)}-->{ab_werk(nach_id[m.group(1)], en)}<!--/ab-werk-->"
    neu = MARKE.sub(marke, neu)
    if neu == alt:
        return False
    if not pruefen:
        with open(os.path.join(WURZEL, pfad), "w", encoding="utf-8") as f:
            f.write(neu)
    return True


def main():
    pruefen = "--pruefen" in sys.argv[1:]
    eintraege = seite_lesen(sammeln())
    doku = sorted(os.path.basename(p) for p in glob.glob(os.path.join(WURZEL, "*.md")))
    geaendert = [p for p in doku if datei_bearbeiten(p, eintraege, pruefen)]
    marken = sum(len(MARKE.findall(open(os.path.join(WURZEL, p), encoding="utf-8").read())) for p in doku)
    stand = f"{len(eintraege)} Einstellungen, {marken} Marken in {len(doku)} Dateien"
    if pruefen and geaendert:
        sys.exit(f"einstellungen_erzeugen: veraltet: {', '.join(geaendert)} - Generator laufen lassen")
    print(f"einstellungen_erzeugen: {stand}" + (", aktuell" if not geaendert else f", geschrieben: {', '.join(geaendert)}"))


if __name__ == "__main__":
    main()
