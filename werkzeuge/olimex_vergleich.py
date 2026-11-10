#!/usr/bin/env python3
"""Vergleicht die Olimex-Variante dieses Repos mit dem Fork von cg089, aus dem sie stammt
(cg089/weishaupt-wem-can-esphome, Zweig olimex-esp32-evb). Verglichen wird, was ESPHome aus der YAML macht
(Ausgabe von `esphome config`), nicht der YAML-Text: im Fork steht alles in einer Datei, hier in Paketen.

  esphome config olimex-weishaupt.yaml > hier.cfg
  python3 werkzeuge/olimex_vergleich.py --soll tests/olimex-fork-soll.txt hier.cfg
      Jede Entitaet des Forks (Art, Name, ID, Plattform, Einheit) muss hier genauso herauskommen.
      Laeuft in der CI. Rueckgabe 1 bei einer Abweichung, die nicht unter AUSNAHMEN steht.

  python3 werkzeuge/olimex_vergleich.py fork.cfg hier.cfg
      Voller Vergleich zweier Ausgaben: jede Entitaet mit allen Feldern samt Lambdas, dazu alle uebrigen
      Abschnitte (Board, Ethernet, CAN, API, OTA, Skripte ...). fork.cfg entsteht genauso im geklonten Fork.

  python3 werkzeuge/olimex_vergleich.py --liste fork.cfg "Fork-Stand ..." > tests/olimex-fork-soll.txt
      Soll-Liste neu schreiben (nur, wenn sich der Fork aendert); der Text landet im Kopf der Liste.
"""

import json
import re
import sys

import yaml

ARTEN = ["sensor", "text_sensor", "binary_sensor", "switch", "button", "number", "select", "text"]

# Was hier mit Absicht anders ist als im Fork: (Art, Name) -> Grund
AUSNAHMEN = {
    ("text_sensor", "Letzte Emergency-Meldung"):
        "ersetzt durch 'Meldung WEM' und 'Meldung Kessel' aus dem Paket kessel (Telegramme 0x081/0x082)",
}
# Abschnitte, die sich unterscheiden duerfen, mit Grund (voller Vergleich)
ABSCHNITTE_ANDERS = {
    "canbus": "Emergency-Telegramme 0x081/0x082 wertet das Paket kessel aus, nicht kraiz-per-can",
    "script": "kc_fehler_lesen: Klartext aus fehlercodes.yaml statt aus den abgetippten Handbuchtexten",
    "esphome": "includes: pakete/kraiz-per-can.h und -texte.h statt weishaupt-helfer.h",
    "substitutions": "can_tx_pin, can_rx_pin, dallas_pin, meldungen_englisch neu; friendly_name heisst anzeigename",
    "web_server": "Secret web_password statt web_server_password",
}


class Lader(yaml.SafeLoader):
    """Liest die Ausgabe von esphome config; !lambda, !secret usw. werden zu ihrem Inhalt."""


def _marke(lader, _suffix, knoten):
    if isinstance(knoten, yaml.ScalarNode):
        return lader.construct_scalar(knoten)
    if isinstance(knoten, yaml.SequenceNode):
        return lader.construct_sequence(knoten, deep=True)
    return lader.construct_mapping(knoten, deep=True)


Lader.add_multi_constructor("!", _marke)


def lade(pfad: str) -> dict:
    with (sys.stdin if pfad == "-" else open(pfad, encoding="utf-8")) as f:
        return yaml.load(f, Loader=Lader)


def entitaeten(cfg: dict) -> dict:
    """(Art, Name) -> Eintrag; Eintraege ohne Namen (interne) unter ihrer ID."""
    aus = {}
    for art in ARTEN:
        for e in cfg.get(art) or []:
            aus[(art, str(e.get("name") or e.get("id")))] = e
    return aus


def zeile(schluessel, e) -> str:
    return " | ".join([schluessel[0], schluessel[1], str(e.get("id", "")), str(e.get("platform", "")),
                       str(e.get("unit_of_measurement", ""))])


def fest(wert) -> str:
    """Vergleichbare Form; der Ordner der Haupt-YAML (absolute Pfade in includes) faellt weg."""
    text = json.dumps(wert, sort_keys=True, ensure_ascii=False, default=str)
    return re.sub(r'"/[^"]*/(components"|pakete/|weishaupt-helfer\.h)', r'"\1', text)


def liste(cfg_pfad: str, stand: str) -> None:
    ent = entitaeten(lade(cfg_pfad))
    print("# Soll-Liste: was der Fork von cg089 (olimex-weishaupt.yaml, Zweig olimex-esp32-evb) an Entitaeten erzeugt.")
    if stand:
        print(f"# {stand}")
    print("# Geschrieben mit: python3 werkzeuge/olimex_vergleich.py --liste fork.cfg   (nicht von Hand aendern)")
    print("# Art | Name | ID | Plattform | Einheit")
    for k in sorted(ent):
        print(zeile(k, ent[k]))


def soll(soll_pfad: str, cfg_pfad: str) -> int:
    with open(soll_pfad, encoding="utf-8") as f:
        erwartet = [z.rstrip("\n") for z in f if z.strip() and not z.startswith("#")]
    ent = entitaeten(lade(cfg_pfad))
    ist = {zeile(k, e) for k, e in ent.items()}
    fehler = ausnahmen = 0
    for z in erwartet:
        if z in ist:
            continue
        art, name = z.split(" | ")[:2]
        if (art, name) in AUSNAHMEN:
            ausnahmen += 1
            print(f"Ausnahme: {art} | {name} – {AUSNAHMEN[(art, name)]}")
            continue
        fehler += 1
        da = ent.get((art, name))
        print(f"FEHLT   : {z}" if da is None else f"ANDERS  : {z}\n   hier : {zeile((art, name), da)}")
    neu = sorted(ist - set(erwartet))
    for z in neu:
        print(f"zusaetzlich hier: {z}")
    print(f"{len(erwartet)} Entitaeten des Forks, {len(erwartet) - fehler - ausnahmen} hier gleich, "
          f"{ausnahmen} Ausnahme(n), {len(neu)} zusaetzlich, {fehler} Fehler")
    return 1 if fehler else 0


def voll(fork_pfad: str, hier_pfad: str) -> int:
    a, b = lade(fork_pfad), lade(hier_pfad)
    ea, eb = entitaeten(a), entitaeten(b)
    gleich = unerwartet = 0
    for k in sorted(set(ea) | set(eb)):
        if k not in eb:
            grund = AUSNAHMEN.get(k)
            unerwartet += grund is None
            print(f"nur im Fork: {k[0]} | {k[1]} – {grund or 'UNERWARTET'}")
        elif k not in ea:
            print(f"nur hier   : {k[0]} | {k[1]}")
        elif fest(ea[k]) == fest(eb[k]):
            gleich += 1
        else:
            unerwartet += 1
            felder = [f for f in sorted(set(ea[k]) | set(eb[k])) if fest(ea[k].get(f)) != fest(eb[k].get(f))]
            print(f"ANDERS     : {k[0]} | {k[1]} | Felder {felder}")
    print(f"Entitaeten: Fork {len(ea)}, hier {len(eb)}, in allen Feldern samt Lambdas gleich: {gleich}")
    for s in sorted((set(a) | set(b)) - set(ARTEN)):
        if fest(a.get(s)) == fest(b.get(s)):
            print(f"Abschnitt {s}: gleich")
        else:
            grund = ABSCHNITTE_ANDERS.get(s)
            unerwartet += grund is None
            print(f"Abschnitt {s}: anders – {grund or 'UNERWARTET'}")
    print("Ergebnis: nur die bekannten Unterschiede" if not unerwartet else f"Ergebnis: {unerwartet} unerwartete Unterschiede")
    return 1 if unerwartet else 0


def main() -> None:
    arg = sys.argv[1:]
    if len(arg) in (2, 3) and arg[0] == "--liste":
        liste(arg[1], arg[2] if len(arg) == 3 else "")
    elif len(arg) == 3 and arg[0] == "--soll":
        sys.exit(soll(arg[1], arg[2]))
    elif len(arg) == 2 and not arg[0].startswith("--"):
        sys.exit(voll(arg[0], arg[1]))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
