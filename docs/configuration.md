# MCS Control — Configuration

This guide explains how to configure the MCS Control unit using the local web interface. Configuration defines channel voltages, input modes, pump functions, tank probes, network settings, and serial communication.

---

## Accessing the Web Interface

The MCS Control serves a local web UI accessible from any browser on the same network.

### Via Ethernet

- Default: **DHCP** (obtains IP from your router/switch)
- Check the OLED display for the assigned IP address
- If no DHCP server is available, the unit falls back to a static IP (see Network Settings below)

### Via WiFi

- The unit can operate as a WiFi client (connects to existing network) or AP (creates its own network)
- Default AP SSID: `MCS-Control-XXXX` (last 4 digits of MAC)
- Default AP password: configured at factory
- Connect to the AP, then browse to `192.168.4.1`

### Default Credentials

- No authentication by default on local network
- Authentication can be enabled via config

---

## Channel Configuration

### Voltage Selection

Each channel (A and B) can independently supply 5 V, 12 V, or 24 V to its sensors.

| Setting | Use Case |
|---------|----------|
| 5 V | Logic-level sensors, encoders |
| 12 V | Standard industrial sensors, most flow meters |
| 24 V | Industrial current-loop sensors (4–20 mA), 24 V pulse outputs |

Set via web UI, serial command (`va24`, `vb12`), or config file.

> **Note:** Changing voltage while sensors are connected is safe — the DC-DC converters have soft-start. However, verify sensor voltage ratings first.

### Input Modes

Each of the 4 inputs per channel can be set to one of:

| Mode | Config Value | Description |
|------|-------------|-------------|
| Analog voltage | `analog_voltage` | 0–5 V or 0–10 V measurement |
| Analog current | `analog_current` | 4–20 mA via shunt resistor |
| Digital | `digital` | On/off detection (opto-isolated) |
| Pulse | `pulse` | Frequency/counter input |
| Off | `off` | Input disabled |

Each mode controls the CMOS switches that route the signal through the appropriate conditioning circuit.

---

## Pump Setup

A pump function ties together inputs and outputs into a logical fuel dispensing point.

### Required Parameters

| Parameter | Description | Example |
|-----------|-------------|---------|
| `pulse_input` | Input channel for flow meter pulses | `"A0"` |
| `nozzle_input` | Input for nozzle lift switch | `"A1"` |
| `relay_output` | Relay controlling pump contactor | `"AR"` |
| `pulses_per_liter` | Meter K-factor | `100` |

### Optional Parameters

| Parameter | Description | Default |
|-----------|-------------|---------|
| `max_flow_rate` | Alarm threshold (L/min) | None |
| `idle_timeout` | Stop pump after N seconds with no flow | 60 |
| `pre_auth_volume` | Maximum authorised volume (litres) | Unlimited |

### Operation Logic

1. Nozzle lifted → relay closes → pump runs
2. Pulses counted and converted to litres
3. Nozzle replaced OR pre-auth volume reached → relay opens → pump stops
4. Transaction recorded

---

## Probe / Tank Setup

For tank level monitoring using 4–20 mA probes.

### Linear Conversion

The probe output (4–20 mA) is converted to engineering units using linear mapping:

```
output = output_min + (input - input_min) × (output_max - output_min) / (input_max - input_min)
```

### Configuration Parameters

| Parameter | Description | Example |
|-----------|-------------|---------|
| `input_min` | Probe signal at empty (mA) | `4.0` |
| `input_max` | Probe signal at full (mA) | `20.0` |
| `output_min` | Tank volume at empty (litres) | `0` |
| `output_max` | Tank volume at full (litres) | `10000` |
| `label` | Display name | `"Diesel Tank 1"` |

### Example

A 4–20 mA probe in a 5000 L tank:
- 4 mA = 0 L (empty)
- 20 mA = 5000 L (full)
- Reading of 12 mA = 2500 L (half full)

---

## Network Settings

### WiFi

| Parameter | Description |
|-----------|-------------|
| `wifi_ssid` | Network name to connect to |
| `wifi_password` | Network password |
| `wifi_ap_enabled` | Enable AP mode (true/false) |
| `wifi_ap_ssid` | AP network name |
| `wifi_ap_password` | AP password |

### Ethernet

| Parameter | Description |
|-----------|-------------|
| `eth_dhcp` | Use DHCP (true/false) |
| `eth_ip` | Static IP address |
| `eth_gateway` | Gateway address |
| `eth_subnet` | Subnet mask |
| `eth_dns` | DNS server |

### Server URLs

| Parameter | Description |
|-----------|-------------|
| `server_url` | MCS Compute server endpoint |
| `ws_url` | WebSocket endpoint for real-time data |
| `ntp_server` | Time server (default: pool.ntp.org) |

---

## UART Configuration

Each channel has an independent UART that can operate in RS-232 or RS-485 mode.

### Mode Selection

| Config | Mode | Use Case |
|--------|------|----------|
| `rs232` | RS-232 | Point-to-point, Console link, legacy |
| `rs485` | RS-485 | Multi-drop bus, tank gauges, PLCs |

### Parameters

| Parameter | Description | Default |
|-----------|-------------|---------|
| `mode` | `rs232` or `rs485` | `rs232` |
| `baud` | Baud rate | `9600` |
| `termination` | Enable 120 Ω termination (RS-485) | `false` |

### Setting via Serial Command

```
ua232    → Channel A to RS-232
ua485    → Channel A to RS-485
uat      → Toggle A termination
ub232    → Channel B to RS-232
ub485    → Channel B to RS-485
ubt      → Toggle B termination
```

---

## Config File Format

Configuration is stored as `config.json` on the LittleFS filesystem. A default configuration is embedded in the firmware and used as the base; device-specific overrides are stored on the LittleFS partition.

### Example config.json

```json
{
  "channels": {
    "A": {
      "channel_voltage": 24,
      "inputs": {
        "A0": {
          "mode": "pulse",
          "pullup": true,
          "role": "pump_1_pulse"
        },
        "A1": {
          "mode": "digital",
          "pullup": true,
          "role": "pump_1_nozzle"
        },
        "A2": {
          "mode": "analog_current",
          "role": "probe_1_level",
          "conversion": {
            "input_min": 4.0,
            "input_max": 20.0,
            "output_min": 0,
            "output_max": 10000,
            "unit": "litres"
          }
        },
        "A3": {
          "mode": "off"
        }
      }
    },
    "B": {
      "channel_voltage": 12,
      "inputs": {
        "B0": {
          "mode": "pulse",
          "pullup": true,
          "role": "pump_2_pulse"
        },
        "B1": {
          "mode": "digital",
          "pullup": true,
          "role": "pump_2_nozzle"
        },
        "B2": {
          "mode": "off"
        },
        "B3": {
          "mode": "off"
        }
      }
    }
  },
  "functions": [
    {
      "id": "pump_1",
      "type": "pump_controller",
      "pulse_input": "A0",
      "nozzle_input": "A1",
      "relay_output": "AR",
      "meter": {
        "pulses_per_liter": 100
      }
    },
    {
      "id": "pump_2",
      "type": "pump_controller",
      "pulse_input": "B0",
      "nozzle_input": "B1",
      "relay_output": "BR",
      "meter": {
        "pulses_per_liter": 100
      }
    },
    {
      "id": "tank_1",
      "type": "probe_monitor",
      "input": "A2",
      "label": "Diesel Tank 1"
    }
  ],
  "network": {
    "eth_dhcp": true,
    "wifi_ssid": "",
    "wifi_password": "",
    "server_url": "https://compute.mcs.example/api",
    "ws_url": "wss://compute.mcs.example/ws"
  },
  "uart": {
    "A": {
      "mode": "rs232",
      "baud": 9600
    },
    "B": {
      "mode": "rs485",
      "baud": 9600,
      "termination": true
    }
  }
}
```

### Config Priority

1. Device LittleFS `config.json` (highest priority — user overrides)
2. Firmware-embedded `data/config.json` (defaults)

Changes made via the web UI are saved to LittleFS automatically.

---

## Menu System (OLED Navigation)

The 5-button interface provides local access to key information and settings:

### Button Layout

| Button | Function |
|--------|----------|
| ▲ (Up) | Navigate up / increment value |
| ▼ (Down) | Navigate down / decrement value |
| ◄ (Left) | Back / cancel |
| ► (Right) | Enter submenu / confirm |
| ● (Select) | Toggle / execute action |

### Menu Structure

```
Main Screen (dashboard)
├── Pumps → status, volume, flow rate
├── Tanks → levels, probe readings
├── Network → IP, connection status
├── Config → voltage, inputs, functions
├── Diagnostics → self-test, I2C scan
└── System → version, uptime, reboot
```

---

## Next Steps

- [Installation Guide](installation-guide.md) — wiring and physical setup
- [User Guide](user-guide.md) — daily operation
- [API Reference](api-reference.md) — programmatic access
