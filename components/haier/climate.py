import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, sensor, climate
from esphome import automation
from esphome.const import (
    CONF_ID,
    CONF_UART_ID,
    DEVICE_CLASS_TEMPERATURE,
    ICON_THERMOMETER,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
)

AUTO_LOAD = ["sensor"]
DEPENDENCIES = ["climate", "uart", "wifi"]

haier_ns = cg.esphome_ns.namespace("haier")
HaierClimate = haier_ns.class_("HaierClimate", climate.Climate, cg.Component)

CONFIG_SCHEMA = cv.All(
    climate.CLIMATE_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(HaierClimate),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA),
)


# Actions
DisplayOnAction = haier_ns.class_("DisplayOnAction", automation.Action)
DisplayOffAction = haier_ns.class_("DisplayOffAction", automation.Action)
# Display on action
@automation.register_action(
    "climate.haier.display_on",
    DisplayOnAction,
    automation.maybe_simple_id(
        {
            cv.Required(CONF_ID): cv.use_id(HaierClimate),
        }
    ),
)
async def haier_set_display_on_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    return var

# Display off action
@automation.register_action(
    "climate.haier.display_off",
    DisplayOffAction,
    automation.maybe_simple_id(
        {
            cv.Required(CONF_ID): cv.use_id(HaierClimate),
        }
    ),
)
async def haier_set_display_off_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    return var

async def to_code(config):
    uart_component = await cg.get_variable(config[CONF_UART_ID])
    var = cg.new_Pvariable(config[CONF_ID], uart_component)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    await climate.register_climate(var, config)
