# Initialization Functions

This project contains 20 distinct application initialization functions matching `*_init` or `*::init`.

## Startup Order

The direct startup sequence in `setup()` is:

1. `Serial.begin(115200)`
2. `log_init()`
3. `i2c_init(I2C_SDA, I2C_SCL)`
4. `sampler_init()`
5. `flow_guard_init()`
6. `all_drivers_init()`

The network initialization sequence, run by the network task, is:

1. `file_system::init()`
2. `config::init()`
3. `w5500::init()`
4. `wifi::init()`
5. `web_socket::init()`
6. `web_server::init()`
7. `rgb::init()`

## `*_init` Functions

- `buttons_init(...)`
- `buzzer_init(...)`
- `i2c_init(...)`
- `log_init()`
- `oled_init(...)`
- `adc_init()`
- `all_drivers_init()`
- `input_config_init()`
- `relays_init()`
- `serial_control_init()`
- `voltage_select_init()`
- `flow_guard_init()`
- `sampler_init()`

`all_drivers_init()` calls these hardware driver initializers:

- `voltage_select_init()`
- `serial_control_init()`
- `input_config_init()`
- `relays_init()`
- `adc_init()`
- `oled_init(...)`
- `buzzer_init(...)`
- `buttons_init(...)`

## `*::init` Functions

- `file_system::init()`
- `config::init()`
- `w5500::init()`
- `wifi::init()`
- `web_socket::init()`
- `web_server::init()`
- `rgb::init()`

## Excluded Functions

These are task-start functions rather than initialization functions, so they are excluded:

- `flow_guard_start_task()`
- `buttons_start_task()`
- `network_start_task()`
- `serial_cmd_start_task()`

FreeRTOS and Arduino framework initialization are also excluded, except for the explicit application call `Serial.begin(115200)` shown in the startup order.
