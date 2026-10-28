#!/usr/bin/env python3
"""Erzeugt aus weishaupt-wem-can.yaml die Release-Firmware-Konfiguration (ohne persoenliche Daten).

Das Release-Image ist fuer "Board kaufen, im Browser flashen, fertig" gedacht:
  - kein WLAN eingetragen: nach dem Flashen per Improv (Installationsseite) oder ueber den
    offenen Hotspot "Weishaupt-WEM-CAN" mit Anmeldeseite einrichten; kein Neustart, solange kein WLAN da ist
  - keine Passwoerter: die App verlangt beim ersten Aufruf Benutzer, App- und Admin-Passwort
    (Expertenstufe auf der Einstellungsseite setzen)
  - Update nur im Browser (Einstellungsseite, Experten-Passwort) - kein ESPHome-OTA mit festem Passwort
  - ohne MQTT und Zusatzanzeigen (der Broker ist unbekannt; wer MQTT will, baut selbst mit secrets.yaml)
  - Geraetename mit MAC-Endung, damit mehrere Boards im selben Netz gehen

Aufruf: python3 werkzeuge/release_bauen.py <zielordner>   -> <ziel>/weishaupt-wem-can-release.yaml + secrets.yaml
Danach: esphome compile <ziel>/weishaupt-wem-can-release.yaml
"""

import re
import secrets
import shutil
import sys
from pathlib import Path

WURZEL = Path(__file__).resolve().parent.parent


def ersetzen(text: str, alt: str, neu: str) -> str:
    if alt not in text:
        raise SystemExit(f"release_bauen: Stelle nicht gefunden: {alt[:60]!r}")
    return text.replace(alt, neu, 1)


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    ziel = Path(sys.argv[1]).resolve()
    ziel.mkdir(parents=True, exist_ok=True)
    y = (WURZEL / "weishaupt-wem-can.yaml").read_text(encoding="utf-8")
    version = re.search(r'firmware: "([^"]+)"', y).group(1)

    y = ersetzen(y, f'firmware: "{version}"', f'firmware: "{version} (Release)"')
    y = ersetzen(y, "  mqtt: !include pakete/mqtt.yaml\n", "  # mqtt: im Release-Image nicht enthalten (Broker unbekannt)\n")
    y = ersetzen(y, "  zusatz: !include pakete/zusatz.yaml\n", "  # zusatz: braucht MQTT\n")
    y = ersetzen(y, "  name: ${geraet}\n", "  name: ${geraet}\n  name_add_mac_suffix: true\n")
    y = ersetzen(y, "  level: DEBUG\n", "  level: INFO\n")
    y = re.sub(r"ota:\n  - platform: esphome\n    password: !secret ota_password\n", "ota:\n", y)
    y = ersetzen(y, """wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
""", """wifi:
  # Release: kein WLAN eingetragen - Einrichtung per Improv (Installationsseite) oder Hotspot
  reboot_timeout: 0s
""")
    y = ersetzen(y, "    password: !secret ap_password\n", "")
    y = ersetzen(y, "captive_portal:\n", "captive_portal:\n\nimprov_serial:\n")
    # Pfade relativ zum Repo
    y = y.replace("path: components", f"path: {WURZEL / 'components'}")
    y = y.replace("!include pakete/", f"!include {WURZEL / 'pakete'}/")
    (ziel / "weishaupt-wem-can-release.yaml").write_text(y, encoding="utf-8")

    # Platzhalter: nichts davon ist im Betrieb wirksam (App-Ersteinrichtung, Experten-Passwort zur Laufzeit)
    s = (WURZEL / "secrets.yaml.example").read_text(encoding="utf-8")
    s = re.sub(r'^app_benutzer:.*$', 'app_benutzer: ""', s, flags=re.M)
    s = re.sub(r'^app_passwort:.*$', 'app_passwort: ""', s, flags=re.M)
    s = re.sub(r'^app_proxy:.*$', 'app_proxy: ""', s, flags=re.M)
    s = re.sub(r'^wem_ip:.*$', 'wem_ip: ""', s, flags=re.M)
    # Weboberflaeche: zufaellig je Bau (das Board ersetzt es beim Start ohnehin durch das Experten-Passwort
    # bzw. eine Zufallssperre, solange keins gesetzt ist)
    s = re.sub(r'^web_password:.*$', f'web_password: "{secrets.token_hex(16)}"', s, flags=re.M)
    (ziel / "secrets.yaml").write_text(s, encoding="utf-8")
    for p in ("register.yaml",):
        if (WURZEL / p).exists():
            shutil.copy(WURZEL / p, ziel / p)
    print(f"{ziel / 'weishaupt-wem-can-release.yaml'} ({version} Release)")


if __name__ == "__main__":
    main()
