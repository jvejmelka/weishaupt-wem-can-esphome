> 🇩🇪 German original: [LIZENZEN-DRITTER.md](LIZENZEN-DRITTER.md). This is a translation; if in doubt, the German version is authoritative.

# Third-party licenses

The source code of this repository is under the [MIT license](LICENSE). This file lists what comes from others
or is added at build time.

## Prebuilt firmware files (`*.bin` from the releases and from the install page)

Besides the code of this repository, a built firmware contains the following parts. Because the C++ part of ESPHome
is under the GPL-3.0, **the firmware file as a whole is distributed under the GPL-3.0**. The complete
corresponding source is this repository as of the day of the respective release, built with the ESPHome version named in
[`.github/workflows/release.yml`](.github/workflows/release.yml)
(`werkzeuge/release_bauen.py`, then `esphome compile`).

| Part | License | Source |
|---|---|---|
| ESPHome (C++ runtime, components; web interface `web_server`) | GPL-3.0 (C++ part), MIT (Python part, web interface) | <https://github.com/esphome/esphome>, <https://github.com/esphome/esphome-webserver> |
| ESP-IDF with the libraries contained in it (among others FreeRTOS, lwIP, mbedTLS) | Apache-2.0; FreeRTOS MIT, lwIP BSD-3-Clause | <https://github.com/espressif/esp-idf> |
| esp_littlefs (file system, only with `dateisystem: true`) | MIT; littlefs contained in it: BSD-3-Clause | <https://github.com/joltwallet/esp_littlefs>, <https://github.com/littlefs-project/littlefs> |
| ArduinoJson | MIT | <https://github.com/bblanchon/ArduinoJson> |

The license texts are in the projects named; the text of the GPL-3.0: <https://www.gnu.org/licenses/gpl-3.0.txt>.

## Included in the repository

| Part | From | License |
|---|---|---|
| `olimex-weishaupt.yaml`, `pakete/netz-lan.yaml`, `pakete/kraiz-per-can.*`, `pakete/dallas.yaml`, `werkzeuge/kraiz-per-can/`, `OLIMEX-ESP32-EVB.md` | cg089 ([fork](https://github.com/cg089/weishaupt-wem-can-esphome), branch `olimex-esp32-evb`), integrated and adapted | MIT, see [`LICENSE`](LICENSE) |
| Object names and value texts in `pakete/kraiz-per-can.yaml` and `pakete/kraiz-per-can-texte.h` (generated from `sensors.py`) | [kraiz/hassio-weishaupt](https://github.com/kraiz/hassio-weishaupt) | MIT, text below |

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

## Only linked or loaded at build time (not in the repository)

| Part | Where | License |
|---|---|---|
| ESP Web Tools (button „Installieren“ ("install")) | `installieren/index.html` loads it from unpkg.com | Apache-2.0 |
| Fonts Share Tech Mono, Barlow Condensed | `app/public/index.html` loads them from Google Fonts (the browser sends a request to Google for this) | SIL Open Font License 1.1 |
| express, mqtt | `app/` (Docker version), via `npm install` | MIT |
| ArduinoJson | `tests/run.sh` loads it for the tests | MIT |

## Without adopted code

From [MenkeC/Weishaupt-C3supermini](https://github.com/MenkeC/Weishaupt-C3supermini) and
[BorgNumberOne/Weishaupt_CanApiJson](https://github.com/BorgNumberOne/Weishaupt_CanApiJson) (both without a stated license)
and from [geronet1/wem-python](https://github.com/geronet1/wem-python) (MIT since 10 Nov 2026)
only protocol details were taken – addresses, factors, meanings; no code was adopted from there. The origin per
object is in [`register.yaml`](register.yaml) (`quelle:`), the projects are listed under "Sources" in the [README](README.en.md).

The same applies to the objects adopted unverified in [`register-uebernommen.yaml`](register-uebernommen.yaml)
([PROTOKOLL, section 7a](PROTOKOLL.en.md#7a-objects-taken-from-other-projects-unverified)): object number,
scaling, unit and meaning with our own designations and the source per object – from the three projects
named, from [kraiz/hassio-weishaupt](https://github.com/kraiz/hassio-weishaupt) (MIT), from
[JS-DE-Tech/hacs-weishaupt-lan](https://github.com/JS-DE-Tech/hacs-weishaupt-lan) (MIT; only the addresses of the
network objects of the WEM, no values) and from
[pull request #10](https://github.com/BorgNumberOne/Weishaupt_CanApiJson/pull/10) by ChristophKomenda.
Tables, texts and code from these sources have not been adopted.

Texts and documents from Weishaupt (manuals, error texts, firmware files) are **not** in the repository; the
project's own short texts for the warning and error codes are in [`fehlercodes.yaml`](fehlercodes.yaml).
