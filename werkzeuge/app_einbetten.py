#!/usr/bin/env python3
"""Bettet die Heizungs-App (app/public) gzip-komprimiert in die Firmware ein.

Erzeugt components/weishaupt_can/app_dateien.h. Das Board liefert die Dateien unter
/app (index.html), /app.js, /style.css, /manifest.json, /icon.svg und /sw.js aus.

Fuer das Board werden drei Stellen angepasst (die Docker-App bleibt unveraendert):
  - __VERSION__ = Pruefsumme der Quelldateien (Cache-Bruch nur bei echter Aenderung)
  - Startseite "/" -> "/app" (auf "/" liegt die Weboberflaeche des Boards)
  - der Service Worker faengt nur die eigenen Dateien ab, nie die Weboberflaeche (/events)

Aufruf ohne Argumente; CI prueft mit "git diff --exit-code", dass die Datei aktuell ist.
"""

import gzip
import hashlib
from pathlib import Path

WURZEL = Path(__file__).resolve().parent.parent
QUELLE = WURZEL / "app" / "public"
ZIEL = WURZEL / "components" / "weishaupt_can" / "app_dateien.h"

# (Datei in app/public, Pfad auf dem Board, Inhaltstyp)
DATEIEN = [
    ("index.html", "/app", "text/html; charset=utf-8"),
    ("app.js", "/app.js", "application/javascript; charset=utf-8"),
    ("style.css", "/style.css", "text/css; charset=utf-8"),
    ("manifest.json", "/manifest.json", "application/manifest+json"),
    ("icon.svg", "/icon.svg", "image/svg+xml"),
    ("sw.js", "/sw.js", "application/javascript; charset=utf-8"),
]


def ersetzen(text: str, alt: str, neu: str) -> str:
    # bricht ab, wenn sich die Quelle geaendert hat und die Stelle nicht mehr passt
    if alt not in text:
        raise SystemExit(f"app_einbetten: Stelle nicht gefunden: {alt!r}")
    return text.replace(alt, neu)


def anpassen(name: str, text: str, version: str) -> str:
    text = text.replace("__VERSION__", version)
    if name == "manifest.json":
        text = ersetzen(text, '"start_url": "/"', '"start_url": "/app"')
    if name == "sw.js":
        text = ersetzen(text, "const ASSETS = ['/', '/index.html',", "const ASSETS = ['/app',")
        text = ersetzen(
            text,
            "  if (e.request.url.includes('/api/')) return;",
            "  if (!ASSETS.includes(new URL(e.request.url).pathname)) return;   // Board: nur die App-Dateien",
        )
    return text


def main() -> None:
    roh = {n: (QUELLE / n).read_text(encoding="utf-8") for n, _, _ in DATEIEN}
    version = hashlib.sha256("".join(roh[n] for n, _, _ in DATEIEN).encode()).hexdigest()[:10]
    zeilen = [
        "// ERZEUGT von werkzeuge/app_einbetten.py aus app/public - nicht von Hand aendern.",
        "#pragma once",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace ap {",
        "",
        "struct Datei {",
        "  const char *pfad;",
        "  const char *typ;",
        "  const uint8_t *daten;   // gzip",
        "  size_t laenge;",
        "};",
        "",
        f'static const char APP_VERSION[] = "{version}";',
        "",
    ]
    eintraege = []
    for i, (name, pfad, typ) in enumerate(DATEIEN):
        text = anpassen(name, roh[name], version)
        # mtime=0: gleiche Eingabe -> gleiche Bytes (CI vergleicht)
        daten = gzip.compress(text.encode("utf-8"), compresslevel=9, mtime=0)
        zeilen.append(f"// {name}: {len(text.encode())} Byte, gzip {len(daten)} Byte")
        zeilen.append(f"static const uint8_t DATEI_{i}[] = {{")
        for j in range(0, len(daten), 20):
            zeilen.append("  " + ", ".join(f"0x{b:02x}" for b in daten[j:j + 20]) + ",")
        zeilen.append("};")
        eintraege.append(f'  {{"{pfad}", "{typ}", DATEI_{i}, sizeof(DATEI_{i})}},')
    zeilen += ["", "static const Datei DATEIEN[] = {", *eintraege, "};", "",
               "inline const Datei *datei(const char *pfad, size_t n) {",
               "  for (const auto &d : DATEIEN) {",
               "    size_t l = 0;",
               "    while (d.pfad[l]) l++;",
               "    if (l == n) {",
               "      size_t k = 0;",
               "      while (k < n && d.pfad[k] == pfad[k]) k++;",
               "      if (k == n) return &d;",
               "    }",
               "  }",
               "  return nullptr;",
               "}",
               "",
               "}  // namespace ap", ""]
    ZIEL.write_text("\n".join(zeilen), encoding="utf-8")
    print(f"{ZIEL.relative_to(WURZEL)}: {len(DATEIEN)} Dateien, Version {version}")


if __name__ == "__main__":
    main()
