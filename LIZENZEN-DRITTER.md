# Lizenzen Dritter

Der Quelltext dieses Repositorys steht unter der [MIT-Lizenz](LICENSE). Diese Datei nennt, was von anderen stammt
oder beim Bauen dazukommt.

## Fertige Firmware-Dateien (`*.bin` aus den Releases und von der Installationsseite)

Eine gebaute Firmware enthält neben dem Code dieses Repositorys die folgenden Teile. Weil der C++-Teil von ESPHome
unter der GPL-3.0 steht, wird **die Firmware-Datei als Ganzes unter der GPL-3.0 weitergegeben**. Der vollständige
Quelltext dazu ist dieses Repository am Tag des jeweiligen Releases, gebaut mit der in
[`.github/workflows/release.yml`](.github/workflows/release.yml) genannten ESPHome-Version
(`werkzeuge/release_bauen.py`, dann `esphome compile`).

| Teil | Lizenz | Quelle |
|---|---|---|
| ESPHome (C++-Laufzeit, Bauteile; Weboberfläche `web_server`) | GPL-3.0 (C++-Teil), MIT (Python-Teil, Weboberfläche) | <https://github.com/esphome/esphome>, <https://github.com/esphome/esphome-webserver> |
| ESP-IDF mit den darin enthaltenen Bibliotheken (u. a. FreeRTOS, lwIP, mbedTLS) | Apache-2.0; FreeRTOS MIT, lwIP BSD-3-Clause | <https://github.com/espressif/esp-idf> |
| esp_littlefs (Dateisystem, nur mit `dateisystem: true`) | MIT; darin littlefs: BSD-3-Clause | <https://github.com/joltwallet/esp_littlefs>, <https://github.com/littlefs-project/littlefs> |
| ArduinoJson | MIT | <https://github.com/bblanchon/ArduinoJson> |

Die Lizenztexte stehen in den genannten Projekten; der Text der GPL-3.0: <https://www.gnu.org/licenses/gpl-3.0.txt>.

## Nur verlinkt oder beim Bauen geladen (nicht im Repository)

| Teil | Wo | Lizenz |
|---|---|---|
| ESP Web Tools (Knopf „Installieren“) | `installieren/index.html` lädt es von unpkg.com | Apache-2.0 |
| Schriften Share Tech Mono, Barlow Condensed | `app/public/index.html` lädt sie von Google Fonts (der Browser fragt dafür bei Google an) | SIL Open Font License 1.1 |
| express, mqtt | `app/` (Docker-Fassung), per `npm install` | MIT |
| ArduinoJson | `tests/run.sh` lädt es für die Tests | MIT |

## Ohne übernommenen Code

Aus [MenkeC/Weishaupt-C3supermini](https://github.com/MenkeC/Weishaupt-C3supermini),
[geronet1/wem-python](https://github.com/geronet1/wem-python) und
[BorgNumberOne/Weishaupt_CanApiJson](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) (alle ohne Lizenzangabe)
stammen nur Protokollangaben – Adressen, Faktoren, Bedeutungen; Code ist von dort nicht übernommen. Die Herkunft je
Objekt steht in [`register.yaml`](register.yaml) (`quelle:`), die Projekte unter „Quellen“ in der [README](README.md).

Texte und Unterlagen von Weishaupt (Anleitungen, Fehlertexte, Firmware-Dateien) sind **nicht** im Repository; die
eigenen Kurztexte der Warn- und Fehlercodes stehen in [`fehlercodes.yaml`](fehlercodes.yaml).
