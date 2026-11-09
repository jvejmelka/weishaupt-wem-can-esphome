#!/usr/bin/env python3
"""fehlercodes.yaml -> components/weishaupt_can/fehlercodes_gen.h (eigene Kurztexte DE/EN).

  python3 werkzeuge/fehlercodes_erzeugen.py            # schreibt die Datei
  python3 werkzeuge/fehlercodes_erzeugen.py --pruefen  # schreibt nichts, Exitcode 1 wenn veraltet
"""
import os
import sys

import yaml

WURZEL = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QUELLE = os.path.join(WURZEL, "fehlercodes.yaml")
ZIEL = os.path.join(WURZEL, "components", "weishaupt_can", "fehlercodes_gen.h")


def c_text(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def erzeugen():
    codes = yaml.safe_load(open(QUELLE, encoding="utf-8"))["codes"]
    z = [
        "// ERZEUGT von werkzeuge/fehlercodes_erzeugen.py aus fehlercodes.yaml - nicht von Hand aendern.",
        "// Eigene Kurztexte (keine Originaltexte der Weishaupt-Anleitung, siehe fehlercodes.yaml).",
        "#pragma once",
        "#include <cstdint>",
        "namespace fehlercodes {",
        "struct Eintrag { char typ; uint16_t nr; uint16_t bis; const char *modul; const char *de; const char *en; };",
        "inline const Eintrag CODES[] = {",
    ]
    for c in codes:
        assert c["typ"] in ("W", "F"), c
        nr = int(c["nr"])
        bis = int(c.get("bis", nr))
        z.append(f"  {{'{c['typ']}', {nr}, {bis}, {c_text(c.get('modul', ''))}, {c_text(c['de'])}, {c_text(c['en'])}}},")
    z += ["};", f"constexpr unsigned ANZAHL = {len(codes)};", "}  // namespace fehlercodes", ""]
    return "\n".join(z)


def main():
    neu = erzeugen()
    alt = open(ZIEL, encoding="utf-8").read() if os.path.exists(ZIEL) else ""
    if "--pruefen" in sys.argv:
        if neu != alt:
            print("fehlercodes_gen.h ist veraltet: python3 werkzeuge/fehlercodes_erzeugen.py")
            return 1
        return 0
    if neu != alt:
        open(ZIEL, "w", encoding="utf-8").write(neu)
        print("geschrieben:", ZIEL)
    return 0


if __name__ == "__main__":
    sys.exit(main())
