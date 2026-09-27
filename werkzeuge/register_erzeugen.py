#!/usr/bin/env python3
"""Erzeugt aus register.yaml (der einzigen Registertabelle):

  - pakete/register_gen.h   C++-Konstanten fuer die YAML-Lambdas und regellogik.h
  - die Tabellen in PROTOKOLL.md und README.md zwischen
        <!-- REGISTER:BEGIN name -->  und  <!-- REGISTER:END name -->

Aufruf (aus dem Wurzelverzeichnis oder von ueberall):
  python3 werkzeuge/register_erzeugen.py            # schreibt die Dateien
  python3 werkzeuge/register_erzeugen.py --pruefen  # schreibt nichts, Exitcode 1 wenn veraltet

Prueft ausserdem die Pakete: jeder Name reg::XYZ muss in der Tabelle stehen, und jede
Leseanfrage `canbus.send: … can_id: 0x60N … reg::lese(reg::XYZ)` muss an den Knoten des
Objekts gehen. Braucht PyYAML (pip install pyyaml).
"""
import os
import re
import sys

import yaml

WURZEL = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
TABELLE = os.path.join(WURZEL, "register.yaml")
HEADER = os.path.join(WURZEL, "pakete", "register_gen.h")
DOKU = ["PROTOKOLL.md", "README.md"]

WEGE = {"pdo", "wem_antwort", "eigene_anfrage", "wem_schreibt", "telegramm_6c2", "wem_liest"}
STATUS = {"belegt": "**belegt**", "plausibel": "plausibel", "unbestaetigt": "**unbestätigt**",
          "vermutung": "Vermutung"}
FELDER = {"id", "name", "firmware", "knoten", "index", "sub", "pdo", "typ", "faktor", "einheit",
          "werte", "weg", "takt", "wem_fragt", "klasse", "status", "beleg", "quelle", "json",
          "schreiben", "json_lesen"}


class Fehler(Exception):
    pass


# ---------------------------------------------------------------- Laden und Pruefen

def laden():
    with open(TABELLE, encoding="utf-8") as f:
        daten = yaml.safe_load(f)
    objekte = daten.get("objekte") or []
    ids = set()
    for o in objekte:
        oid = o.get("id")
        if not oid or not re.fullmatch(r"[A-Z][A-Z0-9_]*", oid):
            raise Fehler(f"ungueltige id: {oid!r}")
        if oid in ids:
            raise Fehler(f"id doppelt: {oid}")
        ids.add(oid)
        unbekannt = set(o) - FELDER
        if unbekannt:
            raise Fehler(f"{oid}: unbekannte Felder {sorted(unbekannt)}")
        if not o.get("name"):
            raise Fehler(f"{oid}: name fehlt")
        for w in o.get("weg") or []:
            if w not in WEGE:
                raise Fehler(f"{oid}: unbekannter weg {w!r}")
        if o.get("status") and o["status"] not in STATUS:
            raise Fehler(f"{oid}: unbekannter status {o['status']!r}")
        if "index" in o and not (0 <= o["index"] <= 0xFFFF and 0 <= o.get("sub", 0) <= 0xFF):
            raise Fehler(f"{oid}: index/sub ausserhalb des Bereichs")
        if "knoten" in o and o["knoten"] not in (1, 2):
            raise Fehler(f"{oid}: knoten muss 1 oder 2 sein")
        if "eigene_anfrage" in (o.get("weg") or []) and "knoten" not in o:
            raise Fehler(f"{oid}: eigene_anfrage braucht knoten")
        if "knoten" in o and "index" not in o:
            raise Fehler(f"{oid}: knoten ohne index")
        p = o.get("pdo")
        if p and not (0 < p["can_id"] <= 0x7FF and 0 <= p["byte"] and p["byte"] + p["laenge"] <= 8):
            raise Fehler(f"{oid}: pdo ausserhalb des Bereichs")
        if "pdo" in (o.get("weg") or []) and not p and "index" not in o:
            raise Fehler(f"{oid}: weg pdo ohne pdo-Lage")
        if o.get("schreiben") and not o.get("json"):
            raise Fehler(f"{oid}: schreiben ohne json")
        j = o.get("json")
        if j and (set(j) != {"mi", "mx", "ox", "os", "vs"} or j["vs"] not in (1, 2, 4)):
            raise Fehler(f"{oid}: json braucht mi, mx, ox, os, vs (1/2/4)")
        if o.get("wem_fragt") not in (None, "ja", "selten", "nein"):
            raise Fehler(f"{oid}: wem_fragt muss ja/selten/nein sein")
    return objekte


def pakete_pruefen(objekte):
    """reg::-Namen in den Paketen muessen existieren, Leseanfragen zum richtigen Knoten gehen."""
    nach_id = {o["id"]: o for o in objekte}
    namen = set(nach_id) | {i + "_JSON" for i, o in nach_id.items() if o.get("json")}
    hilfen = {"Sdo", "Pdo", "Json", "lese", "ist", "wert", "pdo_i16", "pdo_wert", "anfrage_id",
              "antwort_id", "json_vg_schreiben"}
    fehler = []
    ordner = os.path.join(WURZEL, "pakete")
    for datei in sorted(os.listdir(ordner)):
        if not datei.endswith((".yaml", ".h")) or datei == "register_gen.h":
            continue
        text = open(os.path.join(ordner, datei), encoding="utf-8").read()
        for m in re.finditer(r"\breg::([A-Za-z_][A-Za-z0-9_]*)", text):
            n = m.group(1)
            if n not in namen and n not in hilfen:
                zeile = text.count("\n", 0, m.start()) + 1
                fehler.append(f"pakete/{datei}:{zeile}: reg::{n} steht nicht in register.yaml")
        for m in re.finditer(r"canbus\.send:\s*\{[^}]*?can_id:\s*(0x[0-9A-Fa-f]+)[^}]*?reg::lese\(reg::(\w+)\)", text):
            can_id, n = int(m.group(1), 16), m.group(2)
            o = nach_id.get(n)
            if o and can_id != 0x600 + o["knoten"]:
                zeile = text.count("\n", 0, m.start()) + 1
                fehler.append(f"pakete/{datei}:{zeile}: can_id 0x{can_id:X} passt nicht zu {n} (Knoten {o['knoten']})")
    if fehler:
        raise Fehler("\n".join(fehler))


# ---------------------------------------------------------------- C++

def f_lit(x):
    return repr(float(x)) + "f"


def header(objekte):
    z = [
        "// ERZEUGT von werkzeuge/register_erzeugen.py aus register.yaml - NICHT VON HAND AENDERN.",
        "// Aenderungen in register.yaml, dann: python3 werkzeuge/register_erzeugen.py",
        "//",
        "// Objekte am CAN-Bus der Weishaupt WTC-GW (Knoten 1 = WEM-Systemgeraet, Knoten 2 = Kessel).",
        "// Reines C++ ohne ESPHome, damit auch die Tests es einbinden koennen.",
        "#pragma once",
        "#include <cstdint>",
        "#include <cstdio>",
        "#include <vector>",
        "",
        "namespace reg {",
        "",
        "// SDO-Objekt: Knoten (0 = nur als Telegramm mitgelesen), Index, Subindex, Faktor",
        "struct Sdo { uint8_t knoten; uint16_t index; uint8_t sub; float faktor; };",
        "// Wert in einem PDO/Telegramm: CAN-ID, erstes Byte, Laenge in Byte, Faktor",
        "struct Pdo { uint16_t can_id; uint8_t byte; uint8_t laenge; float faktor; };",
        "// dasselbe Objekt ueber die WEM-JSON-Schnittstelle",
        "struct Json { uint8_t mi; uint8_t mx; uint16_t ox; uint8_t os; uint8_t vs; };",
        "",
        "constexpr uint16_t anfrage_id(const Sdo &o) { return 0x600 + o.knoten; }",
        "constexpr uint16_t antwort_id(const Sdo &o) { return 0x580 + o.knoten; }",
        "// Lese-Anfrage (SDO-Upload, Kommandobyte 0x40) - das Einzige, was die Firmware auf den Bus schickt",
        "inline std::vector<uint8_t> lese(const Sdo &o) {",
        "  return {0x40, (uint8_t) (o.index & 0xFF), (uint8_t) (o.index >> 8), o.sub, 0, 0, 0, 0};",
        "}",
        "constexpr bool ist(const Sdo &o, uint16_t idx, uint8_t sub) { return idx == o.index && sub == o.sub; }",
        "inline float wert(const Sdo &o, int32_t roh) { return roh * o.faktor; }",
        "// int16 little-endian an der Stelle des PDO-Werts",
        "inline int16_t pdo_i16(const Pdo &p, const std::vector<uint8_t> &x) {",
        "  return int16_t(x[p.byte + 1] << 8 | x[p.byte]);",
        "}",
        "inline float pdo_wert(const Pdo &p, const std::vector<uint8_t> &x) { return pdo_i16(p, x) * p.faktor; }",
        "// VG-Feld eines JSON-Schreibbefehls (CM 03), z.B. 0302002533020001 + Wert",
        "inline void json_vg_schreiben(char *b, size_t n, const Json &j, unsigned wert) {",
        "  snprintf(b, n, \"03%02X%02X%04X%02X%04X%02X\", j.mi, j.mx, j.ox, j.os, j.vs, wert);",
        "}",
        "",
    ]
    for o in objekte:
        kommentar = o["name"].replace("*", "").replace("`", "")
        kommentar = re.sub(r"\s+", " ", kommentar)
        weg = o.get("weg") or []
        if "pdo" in weg and o.get("pdo"):
            p = o["pdo"]
            z.append(f"constexpr Pdo {o['id']}{{0x{p['can_id']:03X}, {p['byte']}, {p['laenge']}, "
                     f"{f_lit(o.get('faktor', 1))}}};  // {kommentar}")
        elif "index" in o:
            z.append(f"constexpr Sdo {o['id']}{{{o.get('knoten', 0)}, 0x{o['index']:04X}, 0x{o.get('sub', 0):02X}, "
                     f"{f_lit(o.get('faktor', 1))}}};  // {kommentar}")
        if o.get("json"):
            j = o["json"]
            z.append(f"constexpr Json {o['id']}_JSON{{0x{j['mi']:02X}, 0x{j['mx']:02X}, 0x{j['ox']:04X}, "
                     f"0x{j['os']:02X}, {j['vs']}}};")
    z += ["", "}  // namespace reg", ""]
    return "\n".join(z)


# ---------------------------------------------------------------- Markdown

def zahl(x):
    s = repr(float(x))
    if s.endswith(".0"):
        s = s[:-2]
    return s.replace(".", ",")


def obj(o):
    s = o.get("sub", 0)
    sub = str(s) if s < 10 else f"{s:02X}"
    return f"`0x{o['index']:04X}`/{sub}"


def faktor(o):
    if "faktor" in o:
        return f"{zahl(o['faktor'])} {o.get('einheit', '')}".strip()
    return "–"


def stand(o, kurz=False):
    st = o.get("status")
    if not st:
        return "–"
    t = STATUS[st]
    if kurz or not o.get("beleg"):
        return t
    if st in ("belegt", "plausibel"):
        return f"{t} ({o['beleg']})"
    return f"{t} – {o['beleg']}"


def fw(o):
    return o.get("firmware") or "nicht ausgewertet"


def tabelle(kopf, zeilen):
    z = ["| " + " | ".join(kopf) + " |", "|" + "---|" * len(kopf)]
    z += ["| " + " | ".join(r) + " |" for r in zeilen]
    return "\n".join(z)


def t_mithoeren(objekte):
    zeilen = []
    for o in objekte:
        weg = o.get("weg") or []
        inhalt = o["name"] + (f": {o['werte']}" if o.get("werte") and "pdo" in weg else "")
        if "pdo" in weg and o.get("pdo"):
            p = o["pdo"]
            b = str(p["byte"]) if p["laenge"] == 1 else f"{p['byte']}–{p['byte'] + p['laenge'] - 1}"
            zeilen.append([f"`0x{p['can_id']:03X}`", b, inhalt, faktor(o), fw(o), stand(o)])
        elif "wem_schreibt" in weg:
            inhalt = o["name"] + (f": {o['werte']}" if o.get("werte") else "")
            zeilen.append(["`0x602`", f"SDO-Schreibtelegramm des WEM an den Kessel, Objekt {obj(o)}",
                           inhalt, faktor(o), fw(o), stand(o)])
        elif "telegramm_6c2" in weg:
            zeilen.append(["`0x6C2`", f"SDO-Schreibtelegramm, Objekt {obj(o)}", o["name"], faktor(o),
                           fw(o), stand(o)])
    return tabelle(["CAN-ID", "Bytes", "Inhalt", "Faktor", "Name in der Firmware", "Stand"], zeilen)


def t_wem_antwort(objekte):
    auswahl = [o for o in objekte if o.get("knoten") == 2 and o.get("wem_fragt")]
    auswahl.sort(key=lambda o: o["wem_fragt"] == "nein")
    zeilen = []
    for o in auswahl:
        wf = o["wem_fragt"]
        eigen = "eigene_anfrage" in (o.get("weg") or [])
        if wf == "nein":
            t = "**nein** – kommt nur mit eigenen Anfragen"
        elif not eigen and o.get("firmware"):
            t = "ja – **nur** so, die Firmware fragt es nie selbst"
        else:
            t = "ja, selten" if wf == "selten" else wf
        name = o.get("firmware") or f"nicht ausgewertet ({o['name']})"
        zeilen.append([obj(o), name, t])
    return tabelle(["Objekt (Knoten 2)", "Name in der Firmware", "vom WEM abgefragt"], zeilen)


def t_eigene(objekte):
    zeilen = []
    for o in objekte:
        if "eigene_anfrage" not in (o.get("weg") or []):
            continue
        f = o["werte"] if o.get("werte") else faktor(o)
        zeilen.append([str(o["knoten"]), obj(o), f, o.get("takt", "–"), o.get("klasse", "–"), fw(o), stand(o)])
    return tabelle(["Knoten", "Objekt", "Faktor", "Takt", "Klasse", "Name in der Firmware", "Stand"], zeilen)


def t_schreiben(objekte):
    zeilen = []
    for o in objekte:
        s = o.get("schreiben")
        if not s:
            continue
        j = o["json"]
        zeilen.append([o["name"], f"`{j['mi']:02X}`", f"`{j['mx']:02X}`", f"`0x{j['ox']:04X}`",
                       f"`{j['os']:02X}`", f"`{j['vs']:04X}`", s["werte"], s["beobachtet"]])
    return tabelle(["Ziel", "MI", "MX", "OX", "OS", "VS", "Werte", "Beobachtet"], zeilen)


def t_json_lesen(objekte):
    zeilen = []
    for o in objekte:
        if not o.get("json_lesen"):
            continue
        zeilen.append([f"`0x{o['json']['ox']:04X}`", o["json_lesen"]["inhalt"], faktor(o),
                       f"Knoten {o['knoten']} {obj(o)}"])
    return tabelle(["OX", "Inhalt", "Faktor", "am Bus"], zeilen)


WEG_TEXT = {"pdo": "PDO", "wem_antwort": "Antwort an den WEM", "eigene_anfrage": "eigene Anfrage",
            "wem_schreibt": "WEM schreibt", "telegramm_6c2": "Telegramm 0x6C2", "wem_liest": "Frage des WEM"}


def t_uebersicht(objekte):
    zeilen = []
    for o in objekte:
        if not o.get("firmware"):
            continue
        weg = o.get("weg") or []
        if "pdo" in weg and o.get("pdo"):
            p = o["pdo"]
            b = str(p["byte"]) if p["laenge"] == 1 else f"{p['byte']}–{p['byte'] + p['laenge'] - 1}"
            stelle = f"PDO `0x{p['can_id']:03X}` B{b}"
        elif "telegramm_6c2" in weg:
            stelle = f"`0x6C2` {obj(o)}"
        else:
            stelle = f"Knoten {o['knoten']} {obj(o)}"
        zeilen.append([stelle, o["firmware"], ", ".join(WEG_TEXT[w] for w in weg), stand(o, kurz=True)])
    return tabelle(["Stelle", "Name in der Firmware", "Weg", "Stand"], zeilen)


BLOECKE = {
    "mithoeren": t_mithoeren,
    "wem-antwort": t_wem_antwort,
    "eigene-anfragen": t_eigene,
    "schreiben": t_schreiben,
    "json-lesen": t_json_lesen,
    "uebersicht": t_uebersicht,
}

HINWEIS = "<!-- erzeugt aus register.yaml von werkzeuge/register_erzeugen.py - nicht von Hand aendern -->"


def doku_ersetzen(text, objekte, datei):
    def ersatz(m):
        name = m.group(1)
        if name not in BLOECKE:
            raise Fehler(f"{datei}: unbekannter Block REGISTER:{name}")
        return f"<!-- REGISTER:BEGIN {name} -->\n{HINWEIS}\n\n{BLOECKE[name](objekte)}\n\n<!-- REGISTER:END {name} -->"
    neu, n = re.subn(r"<!-- REGISTER:BEGIN ([\w-]+) -->.*?<!-- REGISTER:END \1 -->", ersatz, text, flags=re.S)
    if text.count("REGISTER:BEGIN") != n:
        raise Fehler(f"{datei}: BEGIN/END-Marken passen nicht zusammen")
    return neu


# ---------------------------------------------------------------- Hauptprogramm

def main():
    pruefen = "--pruefen" in sys.argv[1:]
    try:
        objekte = laden()
        pakete_pruefen(objekte)
        soll = {HEADER: header(objekte)}
        for d in DOKU:
            p = os.path.join(WURZEL, d)
            soll[p] = doku_ersetzen(open(p, encoding="utf-8").read(), objekte, d)
    except Fehler as e:
        print(f"FEHLER: {e}", file=sys.stderr)
        return 2
    veraltet = []
    for pfad, inhalt in soll.items():
        alt = open(pfad, encoding="utf-8").read() if os.path.exists(pfad) else None
        if alt != inhalt:
            veraltet.append(os.path.relpath(pfad, WURZEL))
            if not pruefen:
                with open(pfad, "w", encoding="utf-8") as f:
                    f.write(inhalt)
    if pruefen:
        if veraltet:
            print("veraltet (python3 werkzeuge/register_erzeugen.py ausfuehren): " + ", ".join(veraltet))
            return 1
        print(f"register.yaml: {len(objekte)} Objekte, erzeugte Dateien aktuell")
        return 0
    print(f"register.yaml: {len(objekte)} Objekte; neu geschrieben: {', '.join(veraltet) or 'nichts'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
