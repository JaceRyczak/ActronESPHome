import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate
from esphome.const import CONF_ID

from .. import actron_classic_ns, ActronClassic, CONF_ACTRON_CLASSIC_ID

DEPENDENCIES = ["actron_classic"]

ActronClimate = actron_classic_ns.class_("ActronClimate", climate.Climate, cg.Component)

CONFIG_SCHEMA = climate.climate_schema(ActronClimate).extend(
    {
        cv.GenerateID(CONF_ACTRON_CLASSIC_ID): cv.use_id(ActronClassic),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)

    parent = await cg.get_variable(config[CONF_ACTRON_CLASSIC_ID])
    cg.add(var.set_parent(parent))
    cg.add(parent.set_climate(var))
