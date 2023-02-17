import logging
import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import uart, sensor, climate, logger
from esphome import automation
from esphome.const import (
    CONF_ID,
    CONF_LEVEL,
    CONF_LOGGER,
    CONF_LOGS,
    CONF_MAX_TEMPERATURE, 
    CONF_MIN_TEMPERATURE,
    CONF_VISUAL,
    CONF_SUPPORTED_MODES,
    CONF_SUPPORTED_SWING_MODES,
    CONF_TEMPERATURE_STEP,
    CONF_UART_ID,
    DEVICE_CLASS_TEMPERATURE,
    ICON_THERMOMETER,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
)
from esphome.components.climate import (
    ClimateSwingMode,
    ClimateMode
)

_LOGGER = logging.getLogger(__name__)

PROTOCOL_MIN_TEMPERATURE = 16.0
PROTOCOL_MAX_TEMPERATURE = 30.0
PROTOCOL_TEMPERATURE_STEP = 1.0

CODEOWNERS = ["@paveldn"]
DEPENDENCIES = ["climate", "uart"]

haier_ns = cg.esphome_ns.namespace("haier")
HaierClimate = haier_ns.class_("HaierClimate", climate.Climate, cg.Component)

SUPPORTED_SWING_MODES_OPTIONS = {
    "OFF":        ClimateSwingMode.CLIMATE_SWING_OFF,             # always available
    "VERTICAL":   ClimateSwingMode.CLIMATE_SWING_VERTICAL,        # always available
    "HORIZONTAL": ClimateSwingMode.CLIMATE_SWING_HORIZONTAL,
    "BOTH":       ClimateSwingMode.CLIMATE_SWING_BOTH,
}

SUPPORTED_CLIMATE_MODES_OPTIONS = {
    "OFF":      ClimateMode.CLIMATE_MODE_OFF,       # always available
    "AUTO":     ClimateMode.CLIMATE_MODE_AUTO,      # always available 
    "COOL":     ClimateMode.CLIMATE_MODE_COOL,
    "HEAT":     ClimateMode.CLIMATE_MODE_HEAT,
    "DRY":      ClimateMode.CLIMATE_MODE_DRY,
    "FAN_ONLY": ClimateMode.CLIMATE_MODE_FAN_ONLY,
}

validate_climate_modes = cv.enum(SUPPORTED_CLIMATE_MODES_OPTIONS, upper=True)

CONFIG_SCHEMA = cv.All(
    climate.CLIMATE_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(HaierClimate),
            cv.Optional(CONF_SUPPORTED_MODES): cv.ensure_list(validate_climate_modes),
            cv.Optional(
                CONF_SUPPORTED_SWING_MODES,
                default=[
                "OFF",
                "VERTICAL",
                "HORIZONTAL",
                "BOTH",                
                ],
            ): cv.ensure_list(cv.enum(SUPPORTED_SWING_MODES_OPTIONS, upper=True)),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA),
    # Check minimum esphome version
    cv.require_esphome_version(2022, 1, 0),
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

def _final_validate(config):
    full_config = fv.full_config.get()
    if CONF_LOGGER in full_config:
        _level = "NONE"
        logger_config = full_config[CONF_LOGGER]
        if CONF_LOGS in logger_config:
            if "haier.protocol" in logger_config[CONF_LOGS]:
                _level = logger_config[CONF_LOGS]["haier.protocol"]
            else:
                _level = logger_config[CONF_LEVEL]                
        _LOGGER.info("Detected log level for Haier protocol: %s", _level)
        if _level not in logger.LOG_LEVEL_SEVERITY:
            raise cv.Invalid("Unknown log level for Haier protocol")
        _severity = logger.LOG_LEVEL_SEVERITY.index(_level)
        cg.add_build_flag(f"-DHAIER_LOG_LEVEL={_severity}")
    else:
        _LOGGER.info(
            "No logger component found, logging for Haier protocol is disabled"
        )
        cg.add_build_flag("-DHAIER_LOG_LEVEL=0")
    return config
        

FINAL_VALIDATE_SCHEMA = _final_validate

async def to_code(config):
    cg.add(haier_ns.init_haier_protocol_logging())
    if CONF_VISUAL in config:
        visual_config = config[CONF_VISUAL]
        if CONF_MIN_TEMPERATURE in visual_config:
            min_temp = visual_config[CONF_MIN_TEMPERATURE]
            if min_temp < PROTOCOL_MIN_TEMPERATURE:
                raise cv.Invalid(
                    f"Configured visual minimum temperature {min_temp} is lower than supported by Haier protocol is {PROTOCOL_MIN_TEMPERATURE}"
                )
        else:
            config[CONF_VISUAL][CONF_MIN_TEMPERATURE] = PROTOCOL_MIN_TEMPERATURE
        if CONF_MAX_TEMPERATURE in visual_config:
            max_temp = visual_config[CONF_MAX_TEMPERATURE]
            if max_temp > PROTOCOL_MAX_TEMPERATURE:
                raise cv.Invalid(
                    f"Configured visual maximum temperature {max_temp} is higher than supported by Haier protocol is {PROTOCOL_MAX_TEMPERATURE}"
                )
        else:
            config[CONF_VISUAL][CONF_MAX_TEMPERATURE] = PROTOCOL_MAX_TEMPERATURE
        if CONF_TEMPERATURE_STEP in visual_config:
            temp_step = visual_config[CONF_TEMPERATURE_STEP]
            if temp_step < PROTOCOL_TEMPERATURE_STEP:
                raise cv.Invalid(
                    f"Configured visual temperature step {temp_step} is too small, the minimum step allowed by Haier protocol is {PROTOCOL_TEMPERATURE_STEP}"
                )
            if temp_step % 1 != 0:
                raise cv.Invalid(
                    f"Configured visual temperature step {temp_step} should be integer"
                )
        else:
            config[CONF_VISUAL][CONF_TEMPERATURE_STEP] = PROTOCOL_TEMPERATURE_STEP
    else:
        config[CONF_VISUAL][CONF_MAX_TEMPERATURE] = {
            CONF_MIN_TEMPERATURE: PROTOCOL_MIN_TEMPERATURE,
            CONF_MAX_TEMPERATURE: PROTOCOL_MAX_TEMPERATURE,
            CONF_TEMPERATURE_STEP: PROTOCOL_TEMPERATURE_STEP,
        }       
    uart_component = await cg.get_variable(config[CONF_UART_ID])
    var = cg.new_Pvariable(config[CONF_ID], uart_component)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    await climate.register_climate(var, config)
    if CONF_SUPPORTED_MODES in config: 
        cg.add(var.set_supported_modes(config[CONF_SUPPORTED_MODES]))
    if CONF_SUPPORTED_SWING_MODES in config:
        cg.add(var.set_supported_swing_modes(config[CONF_SUPPORTED_SWING_MODES]))
    cg.add_library("pavlodn/HaierProtocol", "0.9.15")
