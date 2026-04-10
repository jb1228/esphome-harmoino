from esphome import automation, pins
import esphome.codegen as cg
from esphome.components import button, event, spi, switch, text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ADDRESS, CONF_CHANNEL, CONF_ID, CONF_NAME, CONF_TRIGGER_ID

from .command_data import VALID_HARMONY_CHANNELS, build_event_types, normalize_override

AUTO_LOAD = ["preferences", "button", "event", "switch", "text_sensor"]
DEPENDENCIES = ["spi"]
MULTI_CONF = True

CONF_CE_PIN = "ce_pin"
CONF_CLICK_DURATION = "click_duration"
CONF_COMMAND_OVERRIDES = "command_overrides"
CONF_DISCOVERED_ADDRESS = "discovered_address"
CONF_EFFECTIVE_ADDRESS = "effective_address"
CONF_EVENT_ENTITY = "event"
CONF_FURTHER_REPEAT_DURATION = "further_repeat_duration"
CONF_ON_ADDRESS_DISCOVERED = "on_address_discovered"
CONF_ON_EVENT = "on_event"
CONF_ON_RAW_PACKET = "on_raw_packet"
CONF_PROBE_BUTTON = "probe_button"
CONF_PROBE_ON_STARTUP = "probe_on_startup"
CONF_PROBE_SWITCH = "probe_switch"
CONF_PROBE_TIMEOUT = "probe_timeout"
CONF_SAVE_BUTTON = "save_button"
CONF_SAVED_ADDRESS = "saved_address"
CONF_SECOND_REPEAT_DURATION = "second_repeat_duration"
CONF_TYPE = "type"
CONF_WAIT_DURATION = "wait_duration"

harmoino_ns = cg.esphome_ns.namespace("harmoino")
Harmoino = harmoino_ns.class_("Harmoino", cg.Component, spi.SPIDevice)
RawPacketTrigger = harmoino_ns.class_(
    "RawPacketTrigger", automation.Trigger.template(cg.std_vector.template(cg.uint8), cg.uint8)
)
HarmoinoEventTrigger = harmoino_ns.class_(
    "HarmoinoEventTrigger", automation.Trigger.template(cg.std_string)
)
AddressTrigger = harmoino_ns.class_(
    "AddressTrigger", automation.Trigger.template(cg.std_string)
)
HarmoinoEventEntity = harmoino_ns.class_("HarmoinoEventEntity", event.Event)
HarmoinoAddressTextSensor = harmoino_ns.class_(
    "HarmoinoAddressTextSensor", text_sensor.TextSensor
)
HarmoinoProbeButton = harmoino_ns.class_("HarmoinoProbeButton", button.Button)
HarmoinoProbeSwitch = harmoino_ns.class_("HarmoinoProbeSwitch", switch.Switch)
HarmoinoSaveButton = harmoino_ns.class_("HarmoinoSaveButton", button.Button)


def validate_channel(value):
    return cv.one_of(*VALID_HARMONY_CHANNELS, int=True)(value)


def validate_address(value):
    if value is None or (isinstance(value, str) and value.strip() == ""):
        return None
    value = cv.hex_uint64_t(value)
    if value == 0 or value > 0xFFFFFFFFFF:
        raise cv.Invalid("address must be a 40-bit non-zero value")
    return value


def validate_command_overrides(value):
    raw_entries = []
    if isinstance(value, dict):
        for key, override in value.items():
            command_id = cv.hex_uint32_t(key)
            if not isinstance(override, dict):
                raise cv.Invalid("Each command override must be a mapping")
            raw_entries.append({"id": command_id, **override})
    else:
        entry_schema = cv.Schema(
            {
                cv.Required("id"): cv.hex_uint32_t,
                cv.Optional(CONF_NAME): cv.string_strict,
                cv.Optional(CONF_TYPE): cv.int_range(min=0, max=2),
            }
        )
        raw_entries = cv.ensure_list(entry_schema)(value)

    normalized = []
    seen_ids = set()
    for entry in raw_entries:
        command_id = entry["id"]
        if command_id in seen_ids:
            raise cv.Invalid(f"Duplicate command override for 0x{command_id:08X}")
        seen_ids.add(command_id)

        override = {}
        if CONF_NAME in entry:
            override[CONF_NAME] = cv.string_strict(entry[CONF_NAME])
        if CONF_TYPE in entry:
            override[CONF_TYPE] = cv.int_range(min=0, max=2)(entry[CONF_TYPE])
        try:
            normalized.append(normalize_override(command_id, override))
        except ValueError as err:
            raise cv.Invalid(str(err)) from err

    return normalized


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Harmoino),
            cv.Required(CONF_CE_PIN): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_ADDRESS): validate_address,
            cv.Optional(CONF_CHANNEL): validate_channel,
            cv.Optional(CONF_CLICK_DURATION, default="450ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_WAIT_DURATION, default="225ms"): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_SECOND_REPEAT_DURATION, default="600ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_FURTHER_REPEAT_DURATION, default="150ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_COMMAND_OVERRIDES, default=[]): validate_command_overrides,
            cv.Optional(CONF_EVENT_ENTITY): event.event_schema(
                HarmoinoEventEntity,
                icon="mdi:remote-tv",
                device_class="button",
            ),
            cv.Optional(CONF_DISCOVERED_ADDRESS): text_sensor.text_sensor_schema(
                HarmoinoAddressTextSensor,
                icon="mdi:radar",
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_SAVED_ADDRESS): text_sensor.text_sensor_schema(
                HarmoinoAddressTextSensor,
                icon="mdi:content-save",
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_EFFECTIVE_ADDRESS): text_sensor.text_sensor_schema(
                HarmoinoAddressTextSensor,
                icon="mdi:remote-tv",
                entity_category="diagnostic",
            ),
            cv.Optional(CONF_PROBE_BUTTON): button.button_schema(
                HarmoinoProbeButton,
                icon="mdi:radar",
                entity_category="config",
            ),
            cv.Optional(CONF_PROBE_SWITCH): switch.switch_schema(
                HarmoinoProbeSwitch,
                icon="mdi:radar",
                entity_category="config",
                default_restore_mode="ALWAYS_OFF",
            ),
            cv.Optional(CONF_SAVE_BUTTON): button.button_schema(
                HarmoinoSaveButton,
                icon="mdi:content-save",
                entity_category="config",
            ),
            cv.Optional(CONF_PROBE_ON_STARTUP, default=True): cv.boolean,
            cv.Optional(CONF_PROBE_TIMEOUT, default="120s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_ON_EVENT): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(HarmoinoEventTrigger),
                }
            ),
            cv.Optional(CONF_ON_RAW_PACKET): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(RawPacketTrigger),
                }
            ),
            cv.Optional(CONF_ON_ADDRESS_DISCOVERED): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(AddressTrigger),
                }
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(
        spi.spi_device_schema(
            cs_pin_required=True, default_data_rate=8e6, default_mode="mode0"
        )
    )
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)

    ce_pin = await cg.gpio_pin_expression(config[CONF_CE_PIN])
    cg.add(var.set_ce_pin(ce_pin))
    cg.add(var.set_component_key(str(config[CONF_ID])))
    if CONF_ADDRESS in config and config[CONF_ADDRESS] is not None:
        cg.add(var.set_yaml_address(config[CONF_ADDRESS]))
    if CONF_CHANNEL in config:
        cg.add(var.set_yaml_channel(config[CONF_CHANNEL]))
    cg.add(var.set_probe_on_startup(config[CONF_PROBE_ON_STARTUP]))
    cg.add(var.set_probe_timeout_ms(config[CONF_PROBE_TIMEOUT].total_milliseconds))
    cg.add(var.set_click_duration_ms(config[CONF_CLICK_DURATION].total_milliseconds))
    cg.add(var.set_wait_duration_ms(config[CONF_WAIT_DURATION].total_milliseconds))
    cg.add(
        var.set_second_repeat_duration_ms(
            config[CONF_SECOND_REPEAT_DURATION].total_milliseconds
        )
    )
    cg.add(
        var.set_further_repeat_duration_ms(
            config[CONF_FURTHER_REPEAT_DURATION].total_milliseconds
        )
    )

    for override in config[CONF_COMMAND_OVERRIDES]:
        cg.add(
            var.add_command_override(
                override["id"], override[CONF_NAME], override[CONF_TYPE]
            )
        )

    for automation_conf in config.get(CONF_ON_EVENT, []):
        trigger = cg.new_Pvariable(automation_conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger, [(cg.std_string, "x")], automation_conf
        )
    for automation_conf in config.get(CONF_ON_RAW_PACKET, []):
        trigger = cg.new_Pvariable(automation_conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger,
            [(cg.std_vector.template(cg.uint8), "x"), (cg.uint8, "pipe_num")],
            automation_conf,
        )
    for automation_conf in config.get(CONF_ON_ADDRESS_DISCOVERED, []):
        trigger = cg.new_Pvariable(automation_conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger, [(cg.std_string, "x")], automation_conf
        )

    event_types = build_event_types(config[CONF_COMMAND_OVERRIDES])
    if event_conf := config.get(CONF_EVENT_ENTITY):
        event_var = cg.new_Pvariable(event_conf[CONF_ID])
        await event.register_event(event_var, event_conf, event_types=event_types)
        cg.add(var.set_event_entity(event_var))

    if discovered_conf := config.get(CONF_DISCOVERED_ADDRESS):
        discovered = await text_sensor.new_text_sensor(discovered_conf)
        cg.add(var.set_discovered_address_sensor(discovered))

    if saved_conf := config.get(CONF_SAVED_ADDRESS):
        saved = await text_sensor.new_text_sensor(saved_conf)
        cg.add(var.set_saved_address_sensor(saved))

    if effective_conf := config.get(CONF_EFFECTIVE_ADDRESS):
        effective = await text_sensor.new_text_sensor(effective_conf)
        cg.add(var.set_effective_address_sensor(effective))

    if probe_button_conf := config.get(CONF_PROBE_BUTTON):
        await button.new_button(probe_button_conf, var)

    if probe_switch_conf := config.get(CONF_PROBE_SWITCH):
        probe_switch = await switch.new_switch(probe_switch_conf, var)
        cg.add(var.set_probe_switch(probe_switch))

    if save_button_conf := config.get(CONF_SAVE_BUTTON):
        await button.new_button(save_button_conf, var)
