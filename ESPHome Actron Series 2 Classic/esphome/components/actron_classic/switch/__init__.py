import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID, ENTITY_CATEGORY_CONFIG

from .. import actron_classic_ns, ActronClassic, CONF_ACTRON_CLASSIC_ID

DEPENDENCIES = ["actron_classic"]

ActronZoneSwitch = actron_classic_ns.class_("ActronZoneSwitch", switch.Switch, cg.Component)
# Purely local on/off setting -- see actron_diagnostics_switch.h. Added per
# Claude Feedback (2026-08-15) change request for a diagnostics-publish toggle.
ActronDiagnosticsSwitch = actron_classic_ns.class_("ActronDiagnosticsSwitch", switch.Switch, cg.Component)

CONF_ZONE1 = "zone1"
CONF_ZONE2 = "zone2"
CONF_DIAGNOSTICS_ENABLED = "diagnostics_enabled"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ACTRON_CLASSIC_ID): cv.use_id(ActronClassic),
        cv.Optional(CONF_ZONE1): switch.switch_schema(ActronZoneSwitch),
        cv.Optional(CONF_ZONE2): switch.switch_schema(ActronZoneSwitch),
        cv.Optional(CONF_DIAGNOSTICS_ENABLED): switch.switch_schema(
            ActronDiagnosticsSwitch, entity_category=ENTITY_CATEGORY_CONFIG
        ),
    }
)


async def _setup_zone(hub, conf, zone_number, setter):
    var = cg.new_Pvariable(conf[CONF_ID])
    await cg.register_component(var, conf)
    await switch.register_switch(var, conf)
    cg.add(var.set_parent(hub))
    cg.add(var.set_zone_number(zone_number))
    cg.add(getattr(hub, setter)(var))


async def _setup_diagnostics_switch(hub, conf):
    var = cg.new_Pvariable(conf[CONF_ID])
    await cg.register_component(var, conf)
    await switch.register_switch(var, conf)
    cg.add(var.set_parent(hub))


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ACTRON_CLASSIC_ID])
    if CONF_ZONE1 in config:
        await _setup_zone(hub, config[CONF_ZONE1], 1, "set_zone1_switch")
    if CONF_ZONE2 in config:
        await _setup_zone(hub, config[CONF_ZONE2], 2, "set_zone2_switch")
    if CONF_DIAGNOSTICS_ENABLED in config:
        await _setup_diagnostics_switch(hub, config[CONF_DIAGNOSTICS_ENABLED])
