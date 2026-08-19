import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from .. import ActronClassic, CONF_ACTRON_CLASSIC_ID

DEPENDENCIES = ["actron_classic"]

CONF_SYSTEM_CLOCK = "system_clock"
CONF_VALIDATION_DETAIL = "validation_detail"

# "System Clock (Not visible to user, only used to check if clock is out of
# sync using an external HA automation, Not actionable by the interface using
# PL4 so it is out of scope)". We surface it as a diagnostic-category text
# sensor: not shown on the main dashboard by default, but still available to
# the API for HA automations, matching the intent as closely as the ESPHome
# entity model allows -- see docs/HOME_ASSISTANT_SETUP.md.
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ACTRON_CLASSIC_ID): cv.use_id(ActronClassic),
        cv.Optional(CONF_SYSTEM_CLOCK): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
        # Claude Feedback (2026-08-15): human-readable description of the
        # most recent PL1 payload-validation mismatch (which field, what raw
        # value was seen), to help diagnose wrong protocol assumptions like
        # the byte 37/38 hardware-status field turned out to be. Pairs with
        # validation_failures (sensor) for a frequency count.
        cv.Optional(CONF_VALIDATION_DETAIL): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ACTRON_CLASSIC_ID])
    if CONF_SYSTEM_CLOCK in config:
        sens = await text_sensor.new_text_sensor(config[CONF_SYSTEM_CLOCK])
        cg.add(hub.set_system_clock_text_sensor(sens))
    if CONF_VALIDATION_DETAIL in config:
        sens = await text_sensor.new_text_sensor(config[CONF_VALIDATION_DETAIL])
        cg.add(hub.set_validation_detail_text_sensor(sens))
