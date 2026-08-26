# Demo Sensor

A simple example app that demonstrates the MCS app framework.

Reads a 4-20mA sensor at a configurable interval and publishes readings on the message bus. Also shows how to subscribe to incoming messages.

## Config

```json
{ "id": "my_sensor", "type": "demo_sensor", "input": 2, "interval_ms": 2000 }
```

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| input | int (0-7) | 0 | ADC channel to read |
| interval_ms | int | 2000 | Reading interval in milliseconds |

## Publishes

| Topic | Data | When |
|-------|------|------|
| `demo/reading` | `{channel, ma, count}` | Every interval |
| `demo/alarm` | `{channel, ma, message}` | When current < 3.5 mA (broken wire) |

## Subscribes

| Topic | Action |
|-------|--------|
| `demo/reset` | Resets the reading counter to 0 |

## Hardware API Used

- `mcs_input_mode()` — sets channel to 4-20mA mode
- `mcs_voltage_set()` — powers the channel at 24V
- `mcs_adc_read_ma()` — reads current in milliamps
- `mcs_buzzer()` — beeps on reset
- `mcs_log()` — logs events
