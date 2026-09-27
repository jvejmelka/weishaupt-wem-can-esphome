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

import hashlib

import esphome.codegen as cg
from esphome.components import canbus, time as time_
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TIME_ID

CODEOWNERS = ["@jvejmelka"]
DEPENDENCIES = ["canbus"]
AUTO_LOAD = ["json"]

CONF_CANBUS_ID = "canbus_id"
CONF_ANLAUFPAUSE = "anlaufpause"
CONF_STATUSBITS_ANLASS = "statusbits_anlass"
CONF_VERDACHT = "verdacht"
CONF_ZUSATZ = "zusatz"

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
    }
).extend(cv.COMPONENT_SCHEMA)


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
