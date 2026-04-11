# Harmoino ESPHome Component

ESPHome `harmoino` component for capturing key presses events from Logitech Harmony remotes via an nRF24L01+ radio. This code is based on the Harmoino project from [@joakimjalden](https://github.com/joakimjalden/Harmoino) and [@pkscout](https://github.com/pkscout/Harmoino).

This component is very much a "work in progress". 

## Features

- Automatic Harmony RF Address Probing/Discovery
  - Runtime probe control across all 12 Harmony RF channels via switch with automatic timeout
  - Address and channel persistence in ESPHome preferences or provided in YAML config
  - Automatically probe on startup if no existing address has been provided or saved
- Automation hook (`on_event`) for processing key presses from remote
  - Normal presses, repeats, click/double/multiple/long, and sleep
  - Separate `on_press` and `on_release` hooks for physical button transitions
  - Also has raw packet automation hook for protocol debugging
- Optional ESPHome `event` entity for Home Assistant exposure

## Address Workflow

The component chooses its active receiver address in this order:

1. YAML `address`
2. Saved runtime address
3. No active receiver address

When no active address is available, the component starts probing automatically on startup. Set `probe_on_startup: false` if you want it to stay healthy but idle until the probe switch is turned on.

When a probe succeeds, the component captures both the Harmony RF address and the RF channel that answered. If no YAML address is present and no saved runtime address exists yet, the discovered address/channel pair is saved automatically.

Address discovery always scans the Harmony pairing channels:

`5, 8, 14, 17, 32, 35, 41, 44, 62, 65, 71, 74`

Typical setup:

1. Flash the device with the `harmoino:` component configured, and boot it up.
3. Put the Harmony Hub into pairing mode by pressing the rear pair/reset button.
4. Wait for `discovered_address` to populate with `0xAABBCCDDEE`.
5. If you already had a saved address and want to replace it manually, press `Save Harmony Address`.

If you later add `address:` in YAML, that value wins over the saved runtime address.

## Minimal Example

```yaml
external_components:
  - source: github://jb1228/esphome-harmoino
    components: [harmoino]

spi:
  clk_pin: GPIO18
  miso_pin: GPIO19
  mosi_pin: GPIO23

harmoino:
  id: harmoino_receiver
  ce_pin: GPIO16
  cs_pin: GPIO17
  probe_switch:
    name: Probe Harmony Address
  save_button:
    name: Save Harmony Address
  discovered_address:
    name: Harmony Discovered Address
  saved_address:
    name: Harmony Saved Address
  effective_address:
    name: Harmony Effective Address
  on_press:
    - logger.log:
        format: "Harmony press: %s"
        args: ["x.c_str()"]
  on_release:
    - logger.log:
        format: "Harmony release: %s"
        args: ["x.c_str()"]
  on_event:
    - logger.log:
        format: "Harmony event: %s"
        args: ["x.c_str()"]
```

## Configuration

Required keys:

- `ce_pin`
- `cs_pin` through the SPI device schema

Optional keys:

- `address`: 40-bit Harmony RF address such as `0xAABBCCDDEE`
- `channel`: Harmony receive channel override; if omitted, Harmoino uses the saved/discovered channel and otherwise falls back to `5`
- `probe_on_startup`: defaults to `true`; starts scanning automatically when neither YAML nor saved address is available
- `probe_timeout`: defaults to `120s`; automatically stops probing if no address is discovered
- `click_duration`: defaults to `450ms`; minimum hold time before a type `2` command becomes `*_long`
- `wait_duration`: defaults to `225ms`; maximum quiet gap before a click/hold sequence is considered finished
- `second_repeat_duration`: defaults to `600ms`; delay before the first repeated event for a held type `1` command
- `further_repeat_duration`: defaults to `150ms`; repeat interval after held repeats begin for a type `1` command
- `command_overrides`: rename built-in commands or define metadata for unknown command IDs
- `event`
- `discovered_address`
- `saved_address`
- `effective_address`
- `probe_switch`
- `save_button`
- `on_press`
- `on_release`
- `on_event`
- `on_raw_packet`
- `on_address_discovered`

### Timing Tuning

These four settings control how Harmoino groups raw Harmony packets into higher-level events:

- `click_duration`: long-press threshold for type `2` commands such as `music`, `tv`, `movie`, and `off`
- `wait_duration`: gesture gap timeout used to absorb release chatter, keep hold sequences alive, and decide when single/double/multiple clicks are complete
- `second_repeat_duration`: delay between the initial type `1` event and the first held repeat
- `further_repeat_duration`: steady-state repeat interval after the first held repeat fires

Current defaults use a balanced profile:

- `click_duration: 450ms`
- `wait_duration: 225ms`
- `second_repeat_duration: 600ms`
- `further_repeat_duration: 150ms`

Practical tuning hints:

- Increase `click_duration` if long presses trigger too easily.
- Decrease `click_duration` if you need `*_long` to fire sooner.
- Increase `wait_duration` if double-clicks split into single clicks or held gestures stop too early.
- Decrease `wait_duration` if click sequences feel sluggish to finalize.
- Decrease `second_repeat_duration` if held `volume_up` or `channel_down` repeats start too slowly.
- Adjust `further_repeat_duration` to make held repeats faster or slower once they are already repeating.

Alternative profiles:

- Conservative: `click_duration: 500ms`, `wait_duration: 225ms`, `second_repeat_duration: 700ms`, `further_repeat_duration: 175ms`
- Snappy: `click_duration: 400ms`, `wait_duration: 180ms`, `second_repeat_duration: 450ms`, `further_repeat_duration: 125ms`


### Command Overrides

Use `command_overrides` when you want to rename a built-in Harmony command or define the event behavior for an unknown command ID.

Example:

```yaml
harmoino:
  ce_pin: GPIO16
  cs_pin: GPIO17
  command_overrides:
    0x005800C1:
      name: select
    0x0001E9C3:
      name: watch_movie
    0x00ABCDEF:
      name: scene
      type: 2
```

Notes:

- Existing command IDs can override `name`, `type`, or both.
- Unknown command IDs must provide both `name` and `type`.

### Event Semantics

`on_press` emits the physical Harmony command name immediately when a button press starts, for example `volume_up` or `ok`.

`on_release` emits the physical Harmony command name after the release chatter debounce window, using the active command sequence to identify which button was released.

Type `0` commands emit a single payload such as `ok`.

Type `1` commands emit an immediate payload and then repeat while held, for example `volume_up`.

Type `2` commands emit one of:

- `*_clicked`
- `*_double`
- `*_multiple`
- `*_long`

The component also emits `sleep` when an explicit Harmony sleep packet is seen or when the remote becomes idle after the awake ping cadence stops.

## Examples

The `examples/` folder contains:

- `harmoino_local_esp32_arduino.yaml`
- `harmoino_ha_esp32_arduino.yaml`

The first keeps everything local with `on_event` only. The second adds an ESPHome `event` entity for Home Assistant.

