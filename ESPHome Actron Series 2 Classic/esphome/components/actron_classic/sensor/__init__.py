import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    UNIT_CELSIUS,
    UNIT_PERCENT,
    UNIT_SECOND,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from .. import actron_classic_ns, ActronClassic, CONF_ACTRON_CLASSIC_ID

DEPENDENCIES = ["actron_classic"]

UNIT_RPM = "RPM"
UNIT_MILLISECOND = "ms"

CONF_INDOOR_COIL_TEMPERATURE = "indoor_coil_temperature"
CONF_OUTDOOR_COIL_TEMPERATURE = "outdoor_coil_temperature"
CONF_COMPRESSOR_SPEED = "compressor_speed"
CONF_INDOOR_FAN_RPM = "indoor_fan_rpm"
CONF_CHECKSUM_FAILURES = "checksum_failures"
CONF_POLL_RESPONSES = "poll_responses"
CONF_LAST_RESPONSE_LATENCY = "last_response_latency"
CONF_ACTION_QUEUE_DEPTH = "action_queue_depth"
CONF_ACTION_RETRIES = "action_retries"
CONF_SECONDS_SINCE_LAST_PL1 = "seconds_since_last_pl1"
CONF_VALIDATION_FAILURES = "validation_failures"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ACTRON_CLASSIC_ID): cv.use_id(ActronClassic),
        cv.Optional(CONF_INDOOR_COIL_TEMPERATURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_OUTDOOR_COIL_TEMPERATURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_COMPRESSOR_SPEED): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_INDOOR_FAN_RPM): sensor.sensor_schema(
            unit_of_measurement=UNIT_RPM,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_CHECKSUM_FAILURES): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_TOTAL_INCREASING,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_POLL_RESPONSES): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_TOTAL_INCREASING,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_LAST_RESPONSE_LATENCY): sensor.sensor_schema(
            unit_of_measurement=UNIT_MILLISECOND,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_ACTION_QUEUE_DEPTH): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_ACTION_RETRIES): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_TOTAL_INCREASING,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_SECONDS_SINCE_LAST_PL1): sensor.sensor_schema(
            unit_of_measurement=UNIT_SECOND,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        # Claude Feedback (2026-08-15): counts PL1 frames where a monitored
        # byte/byte-pair held a value outside its documented expected set
        # (see protocol.h's "Payload validation" section and
        # ActronClassic::validate_pl1_fields_()). Pairs with
        # validation_detail (text_sensor) for the most recent mismatch.
        cv.Optional(CONF_VALIDATION_FAILURES): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_TOTAL_INCREASING,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)

_SETTERS = {
    CONF_INDOOR_COIL_TEMPERATURE: "set_indoor_coil_temp_sensor",
    CONF_OUTDOOR_COIL_TEMPERATURE: "set_outdoor_coil_temp_sensor",
    CONF_COMPRESSOR_SPEED: "set_compressor_speed_sensor",
    CONF_INDOOR_FAN_RPM: "set_indoor_fan_rpm_sensor",
    CONF_CHECKSUM_FAILURES: "set_checksum_fail_sensor",
    CONF_POLL_RESPONSES: "set_poll_response_count_sensor",
    CONF_LAST_RESPONSE_LATENCY: "set_last_response_latency_sensor",
    CONF_ACTION_QUEUE_DEPTH: "set_action_queue_depth_sensor",
    CONF_ACTION_RETRIES: "set_action_retry_count_sensor",
    CONF_SECONDS_SINCE_LAST_PL1: "set_seconds_since_pl1_sensor",
    CONF_VALIDATION_FAILURES: "set_validation_failures_sensor",
}


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ACTRON_CLASSIC_ID])
    for key, setter in _SETTERS.items():
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(getattr(hub, setter)(sens))
