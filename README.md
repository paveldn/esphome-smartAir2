# ESPHome Haier component for smartAir2 protocol 

This implementation of the ESPHoime component to control HVAC on the base of the smartAir2 Haier protocol (AC that is controlled by the smartAir2 application). 

# Configuration example

```  
uart:
  baud_rate: 9600
  tx_pin: 17
  rx_pin: 16
  id: ac_port  

climate:
  - platform: haier
    id: ac_port
    name: Haier AC 
    uart_id: ac_port
    visual:                     # Optional, you can use it to limit min and max temperatures in UI (not working for remote!)
      min_temperature: 16 °C
      max_temperature: 30 °C
      temperature_step: 1 °C
    supported_modes:            # Optional, can be used to disable some modes if you don't need them
    - 'OFF'
    - AUTO
    - COOL
    - HEAT
    - DRY
    - FAN_ONLY
    supported_swing_modes:      # Optional, can be used to disable some swing modes if your AC does not support it
    - 'OFF'
    - VERTICAL
    - HORIZONTAL
    - BOTH
```

**Configuration variables**

- **id (Optional, [ID](https://esphome.io/guides/configuration-types.html#config-id)):** Manually specify the ID used for code generation
- **uart_id (Optional, [ID](https://esphome.io/guides/configuration-types.html#config-id)):** ID of the UART port to communicate with AC
- **name (Required, string):** The name of the climate device
- **supported_modes (Optional, list):** Can be used to disable some of AC modes Possible values: OFF (use quotes in opposite case ESPHome will convert it to False), AUTO, COOL, HEAT, DRY, FAN_ONLY
- **supported_swing_modes (Optional, list):** Can be used to disable some swing modes if your AC does not support it. Possible values: OFF (use quotes in opposite case ESPHome will convert it to False), VERTICAL, HORIZONTAL, BOTH
- All other options from [Climate](https://esphome.io/components/climate/index.html#config-climate).

# Automations

Haier climate support some actiuons:

# climate.haier.display_on Action

This action turns the AC display on

```
on_...:
  then:
    climate.haier.display_on: device_id
```

# climate.haier.display_off Action

This action turns the AC display off

```
on_...:
  then:
    climate.haier.display_off: device_id
```
