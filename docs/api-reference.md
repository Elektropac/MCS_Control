# MCS Control — API Reference

This document covers all programmatic interfaces to the MCS Control unit: the HTTP JSON API (via function_silo), the serial command interface, and the served web pages.

---

## Web API (HTTP JSON)

All API endpoints accept **POST** requests with a JSON body following the function_silo protocol:

```json
{
  "subject": "command_name",
  "data": { ... }
}
```

Responses are JSON:

```json
{
  "subject": "command_name",
  "status": "ok",
  "data": { ... }
}
```

Or on error:

```json
{
  "subject": "command_name",
  "status": "error",
  "message": "Description of what went wrong"
}
```

### Endpoint

All commands are sent to the same endpoint:

```
POST http://<device-ip>/api
Content-Type: application/json
```

---

### Commands

#### LED Control

| Subject | Data | Description |
|---------|------|-------------|
| `led_on` | — | Turn on the RGB LED |
| `led_off` | — | Turn off the RGB LED |
| `led_toggle` | — | Toggle LED state |
| `led_set` | `{"r": 255, "g": 0, "b": 128}` | Set LED to specific colour |

#### System

| Subject | Data | Description |
|---------|------|-------------|
| `reboot` | — | Restart the device |
| `version` | — | Returns hardware revision and module firmware version |

**Example — version response:**
```json
{
  "subject": "version",
  "status": "ok",
  "data": {
    "hardware_rev": 1,
    "firmware": "1.4.2",
    "build_date": "2026-08-20",
    "module_id": "MCS-CTRL-0042"
  }
}
```

#### I2C Bus

| Subject | Data | Description |
|---------|------|-------------|
| `i2c_scan` | — | Scan I2C bus, returns all expected devices with online/offline status |

**Example response:**
```json
{
  "subject": "i2c_scan",
  "status": "ok",
  "data": {
    "devices": [
      {"address": "0x21", "name": "voltage_version", "online": true},
      {"address": "0x23", "name": "input_b", "online": true},
      {"address": "0x25", "name": "input_a", "online": true},
      {"address": "0x27", "name": "serial_relay", "online": true},
      {"address": "0x48", "name": "adc_a", "online": true},
      {"address": "0x49", "name": "adc_b", "online": true}
    ]
  }
}
```

#### ADC / Analog

| Subject | Data | Description |
|---------|------|-------------|
| `adc_read` | — | Read all 8 ADC channels, returns values in mV and V |

**Example response:**
```json
{
  "subject": "adc_read",
  "status": "ok",
  "data": {
    "channels": {
      "A0": {"mV": 4520.3, "V": 4.520},
      "A1": {"mV": 0.0, "V": 0.000},
      "A2": {"mV": 1245.7, "V": 1.246},
      "A3": {"mV": 0.0, "V": 0.000},
      "B0": {"mV": 3301.2, "V": 3.301},
      "B1": {"mV": 0.0, "V": 0.000},
      "B2": {"mV": 0.0, "V": 0.000},
      "B3": {"mV": 0.0, "V": 0.000}
    }
  }
}
```

#### Relay Control

| Subject | Data | Description |
|---------|------|-------------|
| `relay_toggle` | `{"relay": "A"}` | Toggle relay A or B |

**Data fields:**
- `relay` — `"A"` or `"B"`

#### Voltage Control

| Subject | Data | Description |
|---------|------|-------------|
| `voltage_set` | `{"channel": "A", "voltage": 24}` | Set channel supply voltage |

**Data fields:**
- `channel` — `"A"` or `"B"`
- `voltage` — `0`, `5`, `12`, or `24` (0 = off)

#### Buzzer

| Subject | Data | Description |
|---------|------|-------------|
| `buzzer_test` | `{"sound": 1}` | Play one of the predefined sounds |

**Data fields:**
- `sound` — `1` through `6` (see [Hardware Reference](hardware-reference.md) for sound definitions)

#### Self-Test

| Subject | Data | Description |
|---------|------|-------------|
| `selftest` | `{"section": "all", "verbose": true}` | Run hardware self-test |

**Data fields (all optional):**
- `section` — `"i2c"`, `"analog"`, `"supply"`, `"digital"`, or `"all"` (default: `"all"`)
- `verbose` — `true` for detailed results, `false` for pass/fail summary

**Example response (verbose):**
```json
{
  "subject": "selftest",
  "status": "ok",
  "data": {
    "total_tests": 88,
    "passed": 86,
    "warnings": 2,
    "failures": 0,
    "sections": {
      "i2c": {"passed": 6, "failed": 0, "tests": [...]},
      "analog": {"passed": 64, "failed": 0, "tests": [...]},
      "supply": {"passed": 6, "failed": 0, "tests": [...]},
      "digital": {"passed": 12, "warnings": 2, "tests": [...]}
    }
  }
}
```

#### Probe Testing

| Subject | Data | Description |
|---------|------|-------------|
| `test_probes` | `{"action": "start"}` | Start continuous probe reading |
| `test_probes` | `{"action": "status"}` | Get current probe values |
| `test_probes` | `{"action": "stop"}` | Stop probe test mode |

#### I/O Control

| Subject | Data | Description |
|---------|------|-------------|
| `io_read` | `{"channel": "A", "input": 0}` | Read switch/mode state for an input |
| `io_set` | `{"channel": "A", "input": 0, "mode": "analog"}` | Set input switch mode |

**Mode values:** `"analog"`, `"pullup"`, `"shunt"`, `"digital"`, `"off"`

#### Pulse Test

| Subject | Data | Description |
|---------|------|-------------|
| `pulse_test` | `{"channel": "A", "count": 100, "frequency": 1000}` | Generate pulses and verify count (loopback) |

#### UART Loopback

| Subject | Data | Description |
|---------|------|-------------|
| `uart_loopback` | `{"mode": "rs232"}` | Test UART with loopback (requires crossover cable A↔B) |

**Data fields:**
- `mode` — `"rs232"` or `"rs485"`

---

## Web Pages

The following pages are served from LittleFS and accessible via browser:

| URL | File | Description |
|-----|------|-------------|
| `/` | `index.html` | Dashboard — device status, pump states, tank levels |
| `/test.html` | `test.html` | Hardware test UI — interactive self-test with visual results |
| `/tanks.html` | `tanks.html` | Live 8-channel tank visualization — bar graphs with real-time updates via WebSocket |

### WebSocket

A WebSocket endpoint is available for real-time updates:

```
ws://<device-ip>/ws
```

The WebSocket uses the same JSON message format as the HTTP API. Clients can subscribe to events and receive push updates for:
- ADC value changes
- Pump state transitions
- Tank level updates
- Alarm conditions

---

## Serial Commands (USB)

### Connection Settings

| Parameter | Value |
|-----------|-------|
| Baud rate | 115200 |
| Data bits | 8 |
| Parity | None |
| Stop bits | 1 |
| Flow control | None |
| Line ending | CR+LF (Enter) |

### Command Entry

- All commands require **Enter** to execute
- Exception: Menu navigation keys (`8`, `2`, `4`, `6`, `5`) execute instantly when the input buffer is empty
- Commands are case-sensitive
- Unknown commands are silently ignored

### Command Reference

#### Information Commands

| Command | Description | Output |
|---------|-------------|--------|
| `?` | Show command help | List of all available commands |
| `a` | Read all ADC channels | 8 channels with mV values |
| `d` | Full debug dump | System state, config, network, I/O |
| `t` | FreeRTOS task list | Task names, states, stack high-water marks |
| `r` | Runtime stats | CPU time percentage per task |
| `m` | Memory info | Free SRAM, PSRAM, largest block |
| `i` | I2C bus scan | All 6 devices with online/offline status |
| `v` | Hardware/module version | HW revision, firmware version, build date |

#### Input Mode Commands

| Command | Description |
|---------|-------------|
| `f` | Set all 8 inputs to voltage mode (SW_ANALOG) |
| `n` | Set all 8 inputs to mA mode (SW_SHUNT) |
| `o` | Turn off all inputs |

#### Self-Test & Calibration

| Command | Description |
|---------|-------------|
| `w` | Run full 88-point self-test |
| `c` | ADC zero calibration (inputs must be in shunt mode) |

#### Relay Control

| Command | Description |
|---------|-------------|
| `x` | Toggle relay A |
| `y` | Toggle relay B |

#### Voltage Control

| Command | Description |
|---------|-------------|
| `va5` | Set channel A to 5 V |
| `va12` | Set channel A to 12 V |
| `va24` | Set channel A to 24 V |
| `va0` | Turn off channel A supply |
| `vb5` | Set channel B to 5 V |
| `vb12` | Set channel B to 12 V |
| `vb24` | Set channel B to 24 V |
| `vb0` | Turn off channel B supply |

#### Buzzer

| Command | Description |
|---------|-------------|
| `z` | Default buzzer beep |
| `z1`–`z6` | Play specific sound (1=confirm, 2=complete, 3=warning, 4=power-on, 5=power-off, 6=error) |

#### UART Commands

| Command | Description |
|---------|-------------|
| `u` | Show UART help menu |
| `ua232` | Set channel A to RS-232 mode |
| `ua485` | Set channel A to RS-485 mode |
| `ub232` | Set channel B to RS-232 mode |
| `ub485` | Set channel B to RS-485 mode |
| `uat` | Toggle RS-485 termination on channel A |
| `ubt` | Toggle RS-485 termination on channel B |
| `usa XX` | Send string "XX" on channel A |
| `usb XX` | Send string "XX" on channel B |
| `ul` | Listen on both UART channels for 5 seconds |
| `uloop` | Loopback test: send on A, receive on B (requires crossover) |

#### Menu Navigation

When the serial input buffer is empty, these keys navigate the OLED menu instantly (no Enter required):

| Key | Action |
|-----|--------|
| `8` | Up |
| `2` | Down |
| `4` | Left (back) |
| `6` | Right (enter) |
| `5` | Select (confirm) |

> These correspond to a numeric keypad layout.

---

## Usage Examples

### Check System Health (Serial)

```
> v
HW Rev: 1, FW: 1.4.2, Built: 2026-08-20

> i
I2C Scan:
  0x21 voltage_version ... OK
  0x23 input_b ........... OK
  0x25 input_a ........... OK
  0x27 serial_relay ...... OK
  0x48 adc_a ............. OK
  0x49 adc_b ............. OK
All 6 devices online.

> m
Internal SRAM: 142,384 free (largest: 65,536)
PSRAM: 7,845,120 free (largest: 4,194,304)
LittleFS: 4,231,168 free
```

### Read Sensors (HTTP)

```bash
curl -X POST http://192.168.1.100/api \
  -H "Content-Type: application/json" \
  -d '{"subject": "adc_read"}'
```

### Control Relay (HTTP)

```bash
curl -X POST http://192.168.1.100/api \
  -H "Content-Type: application/json" \
  -d '{"subject": "relay_toggle", "data": {"relay": "A"}}'
```

### Run Self-Test (HTTP)

```bash
curl -X POST http://192.168.1.100/api \
  -H "Content-Type: application/json" \
  -d '{"subject": "selftest", "data": {"section": "all", "verbose": true}}'
```

---

## Related Documentation

- [Firmware Architecture](firmware-architecture.md) — how function_silo works internally
- [Configuration](configuration.md) — config.json format
- [Hardware Reference](hardware-reference.md) — pin mapping and device addresses
- [Troubleshooting](troubleshooting.md) — interpreting self-test results
