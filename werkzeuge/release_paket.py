#!/usr/bin/env python3
"""Legt die gebauten Release-Dateien zusammen: Firmware (factory = Erstinstallation per USB/Browser,
ota = Update auf der Einstellungsseite), SHA256SUMS und manifest.json fuer ESP Web Tools.

  python3 werkzeuge/release_paket.py <release-ordner> <ausgabe>
  python3 werkzeuge/release_paket.py --pruefen <firmware.bin>   (keine Platzhalter-Zugangsdaten enthalten)
"""

import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

WURZEL = Path(__file__).resolve().parent.parent


def pruefen(bin_pfad: str) -> None:
    daten = Path(bin_pfad).read_bytes()
    beispiel = (WURZEL / "secrets.yaml.example").read_text(encoding="utf-8")
    verboten = []
    for zeile in beispiel.splitlines():
        m = re.match(r'^(\w+):\s*"([^"]{6,})"', zeile)
        if m and m.group(1) not in ("wem_user", "wem_pass", "mqtt_user", "app_benutzer"):
            verboten.append((m.group(1), m.group(2)))
    treffer = [k for k, v in verboten if v.encode() in daten]
    if treffer:
        sys.exit(f"Platzhalter im Image: {', '.join(treffer)}")
    print(f"{bin_pfad}: keine Platzhalter-Zugangsdaten ({len(verboten)} geprueft)")


def main() -> None:
    if sys.argv[1:2] == ["--pruefen"]:
        pruefen(sys.argv[2])
        return
    quelle, ziel = Path(sys.argv[1]), Path(sys.argv[2])
    ziel.mkdir(parents=True, exist_ok=True)
    build = next(quelle.glob(".esphome/build/*/build"))
    y = (WURZEL / "weishaupt-wem-can.yaml").read_text(encoding="utf-8")
    version = re.search(r'firmware: "([^"]+)"', y).group(1)
    for art in ("factory", "ota"):
        shutil.copy(build / f"firmware.{art}.bin", ziel / f"weishaupt-wem-can.{art}.bin")
    summen = []
    for f in sorted(ziel.glob("*.bin")):
        summen.append(f"{hashlib.sha256(f.read_bytes()).hexdigest()}  {f.name}")
    (ziel / "SHA256SUMS").write_text("\n".join(summen) + "\n", encoding="utf-8")
    manifest = {
        "name": "Weishaupt WEM am CAN-Bus",
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": "ESP32", "parts": [{"path": "weishaupt-wem-can.factory.bin", "offset": 0}]}],
    }
    (ziel / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("\n".join(summen))


if __name__ == "__main__":
    main()
