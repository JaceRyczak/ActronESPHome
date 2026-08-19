import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_ID

from .. import actron_classic_ns, ActronClassic, CONF_ACTRON_CLASSIC_ID

DEPENDENCIES = ["actron_classic"]

ActronFanModeSelect = actron_classic_ns.class_("ActronFanModeSelect", select.Select, cg.Component)
ActronFanSpeedSelect = actron_classic_ns.class_("ActronFanSpeedSelect", select.Select, cg.Component)

CONF_FAN_MODE = "fan_mode"
CONF_FAN_SPEED = "fan_speed"

FAN_MODE_OPTIONS = ["Normal", "Continuous"]
FAN_SPEED_OPTIONS = ["Low", "Med", "High"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ACTRON_CLASSIC_ID): cv.use_id(ActronClassic),
        cv.Optional(CONF_FAN_MODE): select.select_schema(ActronFanModeSelect),
        cv.Optional(CONF_FAN_SPEED): select.select_schema(ActronFanSpeedSelect),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ACTRON_CLASSIC_ID])

    if CONF_FAN_MODE in config:
        conf = config[CONF_FAN_MODE]
        var = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(var, conf)
        await select.register_select(var, conf, options=FAN_MODE_OPTIONS)
        cg.add(var.set_parent(hub))
        cg.add(hub.set_fan_mode_select(var))

    if CONF_FAN_SPEED in config:
        conf = config[CONF_FAN_SPEED]
        var = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(var, conf)
        await select.register_select(var, conf, options=FAN_SPEED_OPTIONS)
        cg.add(var.set_parent(hub))
        cg.add(hub.set_fan_speed_select(var))
