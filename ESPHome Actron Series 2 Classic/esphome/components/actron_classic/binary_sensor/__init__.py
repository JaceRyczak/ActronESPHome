import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_RUNNING,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from .. import actron_classic_ns, ActronClassic, CONF_ACTRON_CLASSIC_ID

DEPENDENCIES = ["actron_classic"]

CONF_REVERSING_VALVE = "reversing_valve"
CONF_COMPRESSOR_CONTACTOR = "compressor_contactor"
CONF_RUN_REQUEST = "run_request"
CONF_SYSTEM_POWER = "system_power"
CONF_SYSTEM_RUNNING = "system_running"
CONF_ZONE1_STATUS = "zone1_status"
CONF_ZONE2_STATUS = "zone2_status"
CONF_BUS_DATA_STALE = "bus_data_stale"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ACTRON_CLASSIC_ID): cv.use_id(ActronClassic),
        # Claude Feedback (2026-08-15) change #3: categorised as diagnostic.
        cv.Optional(CONF_REVERSING_VALVE): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
        cv.Optional(CONF_COMPRESSOR_CONTACTOR): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_RUNNING,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_RUN_REQUEST): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
        cv.Optional(CONF_SYSTEM_POWER): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_POWER
        ),
        cv.Optional(CONF_SYSTEM_RUNNING): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_RUNNING
        ),
        cv.Optional(CONF_ZONE1_STATUS): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
        cv.Optional(CONF_ZONE2_STATUS): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
        # Claude Feedback (2026-08-15) change #4: no longer diagnostic (main entity).
        cv.Optional(CONF_BUS_DATA_STALE): binary_sensor.binary_sensor_schema(),
    }
)

_SETTERS = {
    CONF_REVERSING_VALVE: "set_reversing_valve_binary_sensor",
    CONF_COMPRESSOR_CONTACTOR: "set_compressor_contactor_binary_sensor",
    CONF_RUN_REQUEST: "set_run_request_binary_sensor",
    CONF_SYSTEM_POWER: "set_system_power_binary_sensor",
    CONF_SYSTEM_RUNNING: "set_system_running_binary_sensor",
    CONF_ZONE1_STATUS: "set_zone1_status_binary_sensor",
    CONF_ZONE2_STATUS: "set_zone2_status_binary_sensor",
    CONF_BUS_DATA_STALE: "set_bus_stale_binary_sensor",
}


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ACTRON_CLASSIC_ID])
    for key, setter in _SETTERS.items():
        if key in config:
            sens = await binary_sensor.new_binary_sensor(config[key])
            cg.add(getattr(hub, setter)(sens))
