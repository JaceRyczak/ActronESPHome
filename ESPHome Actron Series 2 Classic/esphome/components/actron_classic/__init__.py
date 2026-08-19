import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import uart
from esphome.const import CONF_ID

CODEOWNERS = ["@sujith"]
DEPENDENCIES = ["uart"]
MULTI_CONF = False

actron_classic_ns = cg.esphome_ns.namespace("actron_classic")
ActronClassic = actron_classic_ns.class_("ActronClassic", cg.Component, uart.UARTDevice)

CONF_DE_RE_PIN = "de_re_pin"
CONF_ACTRON_CLASSIC_ID = "actron_classic_id"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ActronClassic),
            cv.Required(CONF_DE_RE_PIN): pins.gpio_output_pin_schema,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    de_re_pin = await cg.gpio_pin_expression(config[CONF_DE_RE_PIN])
    cg.add(var.set_de_re_pin(de_re_pin))
