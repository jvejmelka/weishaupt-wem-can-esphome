# Lizenzen Dritter

> 🇬🇧 English version: [LIZENZEN-DRITTER.en.md](LIZENZEN-DRITTER.en.md)

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

## Im Repository enthalten

| Teil | Von | Lizenz |
|---|---|---|
| `olimex-weishaupt.yaml`, `pakete/netz-lan.yaml`, `pakete/kraiz-per-can.*`, `pakete/dallas.yaml`, `werkzeuge/kraiz-per-can/`, `OLIMEX-ESP32-EVB.md` | cg089 ([Fork](https://github.com/cg089/weishaupt-wem-can-esphome), Zweig `olimex-esp32-evb`), eingebaut und angepasst | MIT, siehe [`LICENSE`](LICENSE) |
| Objektnamen und Wertetexte in `pakete/kraiz-per-can.yaml` und `pakete/kraiz-per-can-texte.h` (erzeugt aus `sensors.py`) | [kraiz/hassio-weishaupt](https://github.com/kraiz/hassio-weishaupt) | MIT, Text unten |

### kraiz/hassio-weishaupt

```
MIT License

Copyright (c) 2026 Lars Kreisz

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Nur verlinkt oder beim Bauen geladen (nicht im Repository)

| Teil | Wo | Lizenz |
|---|---|---|
| ESP Web Tools (Knopf „Installieren“) | `installieren/index.html` lädt es von unpkg.com | Apache-2.0 |
| Schriften Share Tech Mono, Barlow Condensed | `app/public/index.html` lädt sie von Google Fonts (der Browser fragt dafür bei Google an) | SIL Open Font License 1.1 |
| express, mqtt | `app/` (Docker-Fassung), per `npm install` | MIT |
| ArduinoJson | `tests/run.sh` lädt es für die Tests | MIT |

## Ohne übernommenen Code

Aus [MenkeC/Weishaupt-C3supermini](https://github.com/MenkeC/Weishaupt-C3supermini) und
[BorgNumberOne/Weishaupt_CanApiJson](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) (beide ohne Lizenzangabe)
sowie aus [geronet1/wem-python](https://github.com/geronet1/wem-python) (MIT seit 10.11.2026)
stammen nur Protokollangaben – Adressen, Faktoren, Bedeutungen; Code ist von dort nicht übernommen. Die Herkunft je
Objekt steht in [`register.yaml`](register.yaml) (`quelle:`), die Projekte unter „Quellen“ in der [README](README.md).

Dasselbe gilt für die ungeprüft übernommenen Objekte in [`register-uebernommen.yaml`](register-uebernommen.yaml)
([PROTOKOLL, Abschnitt 7a](PROTOKOLL.md#7a-übernommene-objekte-aus-anderen-projekten-ungeprüft)): Objektnummer,
Skalierung, Einheit und Bedeutung mit eigenen Bezeichnungen und der Quelle je Objekt – aus den drei genannten
Projekten, aus [kraiz/hassio-weishaupt](https://github.com/kraiz/hassio-weishaupt) (MIT), aus
[JS-DE-Tech/hacs-weishaupt-lan](https://github.com/JS-DE-Tech/hacs-weishaupt-lan) (MIT; nur die Adressen der
Netzwerk-Objekte des WEM, keine Werte) und aus dem
[Pull Request #10](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/pull/10) von ChristophKomenda.
Tabellen, Texte und Code aus diesen Quellen sind nicht übernommen.

Texte und Unterlagen von Weishaupt (Anleitungen, Fehlertexte, Firmware-Dateien) sind **nicht** im Repository; die
eigenen Kurztexte der Warn- und Fehlercodes stehen in [`fehlercodes.yaml`](fehlercodes.yaml).
