"""ESPHome-Bauteil weishaupt_can: Zustand und Logik fuer Weishaupt WEM/WTC am CAN-Bus.

Einbinden (siehe INSTALL.md):

    external_components:
      - source: github://jvejmelka/weishaupt-wem-can-esphome@main
        components: [weishaupt_can]

    weishaupt_can:
      id: wcan
      canbus_id: my_can_bus
      time_id: uhr

Die Pakete unter pakete/ bringen diesen Block schon mit.
"""

import gzip
import hashlib
import os
import re
import zlib

import esphome.codegen as cg
from esphome.components import canbus, time as time_
import esphome.config_validation as cv
from esphome.core import CORE
from esphome.const import CONF_ID, CONF_TIME_ID

CODEOWNERS = ["@jvejmelka"]
DEPENDENCIES = ["canbus"]
AUTO_LOAD = ["json"]

CONF_CANBUS_ID = "canbus_id"
CONF_ANLAUFPAUSE = "anlaufpause"
CONF_STATUSBITS_ANLASS = "statusbits_anlass"
CONF_VERDACHT = "verdacht"
CONF_ZUSATZ = "zusatz"
CONF_APP = "app"
CONF_BENUTZER = "benutzer"
CONF_PASSWORT = "passwort"
CONF_ADMIN_PASSWORT = "admin_passwort"
CONF_EXPERTE_PASSWORT = "experte_passwort"
CONF_PROXY = "proxy"
CONF_SD_KARTE = "sd_karte"
CONF_DATEISYSTEM = "dateisystem"
CONF_FIRMWARE = "firmware"

weishaupt_can_ns = cg.esphome_ns.namespace("weishaupt_can")
WeishauptCan = weishaupt_can_ns.class_("WeishauptCan", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WeishauptCan),
        cv.Required(CONF_CANBUS_ID): cv.use_id(canbus.CanbusComponent),
        cv.Optional(CONF_TIME_ID): cv.use_id(time_.RealTimeClock),
        # Anlaufpause, wenn der Bus nach einem Ausfall wiederkommt (nach dem Board-Start: 1 min)
        cv.Optional(CONF_ANLAUFPAUSE, default="10min"): cv.positive_time_period_milliseconds,
        # geaenderte Statusbits (PDO 0x1C1) loesen das Nachlesen aus; Paket verdacht: false (Regel R3)
        cv.Optional(CONF_STATUSBITS_ANLASS, default=True): cv.boolean,
        # gespeicherte Werte der Pakete verdacht bzw. zusatz anlegen (die Pakete setzen das)
        cv.Optional(CONF_VERDACHT, default=False): cv.boolean,
        cv.Optional(CONF_ZUSATZ, default=False): cv.boolean,
        # microSD-Karte im Steckplatz des WeAct-Boards (SPI: CS 13, SCK 14, MOSI 15, MISO 2), ab v42
        cv.Optional(CONF_SD_KARTE, default=False): cv.boolean,
        # LittleFS in den oberen 4 MB eines 8-MB-Flash (ohne neue Partitionstabelle), ab v42
        cv.Optional(CONF_DATEISYSTEM, default=False): cv.boolean,
        # Heizungs-App auf dem Board (Paket app, ab v38): Vorgaben fuer den Zugang und Firmware-Text.
        # Ab v39 gelten die im Board gespeicherten Zugangsdaten; die Vorgaben nur beim ersten Start.
        # Fehlt ein Passwort, verlangt das Board beim ersten Aufruf von /app eine Ersteinrichtung.
        cv.Optional(CONF_APP): cv.All(
            cv.Schema(
                {
                    cv.Optional(CONF_BENUTZER, default="admin"): cv.string_strict,
                    cv.Optional(CONF_PASSWORT, default=""): cv.string_strict,
                    cv.Optional(CONF_ADMIN_PASSWORT, default=""): cv.string_strict,
                    cv.Optional(CONF_EXPERTE_PASSWORT, default=""): cv.string_strict,
                    cv.Optional(CONF_FIRMWARE, default=""): cv.string,
                    # Reverse Proxy (IPv4-Adresse): nur von ihm wird X-Forwarded-For geglaubt
                    cv.Optional(CONF_PROXY, default=""): cv.Any(cv.one_of(""), cv.ipv4address),
                }
            ),
            cv.requires_component("web_server"),
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


def experten_index() -> bytes | None:
    """ESPHome-Oberflaeche (v3, eingebettet) fuer /experten: dieselbe Datei wie unter "/", nur dass sie
    ihre Daten (/events, /sensor/...) nicht relativ zu ihrem eigenen Pfad sucht, sondern an der Wurzel.
    Gelesen aus der installierten ESPHome-Version - bei einem ESPHome-Update passt sie sich mit an."""
    from esphome.components import web_server

    pfad = os.path.join(os.path.dirname(web_server.__file__), "server_index_v3.h")
    try:
        with open(pfad, encoding="utf-8") as f:
            text = f.read()
    except OSError:
        return None
    m = re.search(r"INDEX_GZ\[\] PROGMEM = \{(.*?)\};", text, re.S)
    if not m:
        return None
    roh = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1)))
    html = zlib.decompress(roh, 16 + 15).decode("utf-8")
    alt = "let e=window.location.pathname;"
    if alt not in html:
        return None
    return gzip.compress(html.replace(alt, 'let e="";', 1).encode("utf-8"), compresslevel=9, mtime=0)


def schluessel(alte_id: str) -> int:
    """Name-Hash wie ESPHome-Globals (globals/__init__.py): md5 der id, erste 8 Hexziffern.

    Die Werte lagen bis v28 in Globals mit diesen ids - mit demselben Hash (und Datentyp)
    findet das Bauteil sie nach dem Update im Flash wieder.
    """
    return int(hashlib.md5(alte_id.encode()).hexdigest()[:8], 16)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    bus = await cg.get_variable(config[CONF_CANBUS_ID])
    cg.add(var.set_canbus(bus))
    if CONF_TIME_ID in config:
        uhr = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_uhr(uhr))
    cg.add(var.set_anlaufpause(config[CONF_ANLAUFPAUSE].total_milliseconds))
    cg.add(var.set_statusbits_anlass(config[CONF_STATUSBITS_ANLASS]))
    cg.add(var.speicher_brenner(schluessel("brenner_starts")))
    if config[CONF_VERDACHT]:
        cg.add(
            var.speicher_verdacht(
                schluessel("vd_speicher"),
                schluessel("vd_datei_hash"),
                schluessel("vd_datei_version"),
                schluessel("vd_stby_vorher"),
                schluessel("vd_rest_vorher"),
            )
        )
    if config[CONF_ZUSATZ]:
        cg.add(
            var.speicher_zusatz(
                schluessel("zs_speicher"),
                schluessel("zs_datei_hash"),
                schluessel("zs_version"),
            )
        )
    if CONF_APP in config:
        app = config[CONF_APP]
        cg.add_define("USE_WC_APP")
        idx = experten_index()
        if idx is not None:
            cg.add_define("USE_WC_EXPERTEN")
            cg.add_global(
                cg.RawStatement(
                    "extern const uint8_t WC_EXPERTEN_GZ[] = {"
                    + ",".join(str(b) for b in idx)
                    + "};\nextern const size_t WC_EXPERTEN_GZ_LEN = sizeof(WC_EXPERTEN_GZ);"
                )
            )
        # Log-Zeilen fuer die Einstellungsseite (ab v41): einen Platz in der Liste der Log-Empfaenger anfordern
        if "logger" in CORE.config:
            from esphome.components.logger import request_log_listener

            request_log_listener()
        # Einrichtungs-Assistent (ab v42): WLAN-Liste auch nach dem Verbinden behalten
        if "wifi" in CORE.config:
            from esphome.components.wifi import request_wifi_scan_results

            request_wifi_scan_results()
        cg.add(
            var.app_einrichten(
                app[CONF_BENUTZER],
                app[CONF_PASSWORT],
                app[CONF_ADMIN_PASSWORT],
                app[CONF_EXPERTE_PASSWORT],
                app[CONF_FIRMWARE],
                str(app[CONF_PROXY]),
            )
        )

    if config[CONF_SD_KARTE]:
        from esphome.components.esp32 import include_builtin_idf_component

        for k in ("fatfs", "sdmmc", "esp_driver_sdspi", "esp_driver_sdmmc", "wear_levelling"):
            include_builtin_idf_component(k)
        cg.add_define("USE_WC_SD")
    if config[CONF_DATEISYSTEM]:
        from esphome.components.esp32 import add_idf_component, require_vfs_dir

        add_idf_component(name="joltwallet/littlefs", ref="1.23.1")
        require_vfs_dir()   # rename/unlink/opendir - ESPHome schaltet das sonst ab
        cg.add_define("USE_WC_DATEIEN")
