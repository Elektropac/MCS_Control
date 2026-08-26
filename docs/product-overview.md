# MCS Control — Product Overview

## What is MCS Control?

MCS Control is a modular fuel management controller designed for unattended and attended fuel dispensing installations. It monitors fuel flow, controls pumps, reads tank probes, and communicates with management systems — all in a compact, rugged unit.

Built on the ESP32-S3 platform with galvanically isolated dual channels, MCS Control handles everything from a single pump with a phone app to a full multi-pump depot with centralised management.

## The MCS Platform

MCS Control is one component in a three-part platform:

| Product | Role |
|---------|------|
| **MCS Control** | Field controller — pump control, metering, I/O, tank monitoring |
| **MCS Console** | User terminal — card/tag authentication, display, driver interaction |
| **MCS Compute** | Cloud/server — transaction processing, reporting, fleet management |

These products can be deployed independently or together depending on site complexity.

## Deployment Configurations

| Configuration | Components | Use Case |
|---------------|-----------|----------|
| Minimal | 1× Control + phone app | Single pump, small fleet, BLE authorisation |
| Standard | 1× Control + 1× Console | Card-based access, one or two pumps |
| Multi-pump | 1× Control + Console + Compute | Multiple pumps, fleet management, reporting |
| Full depot | Multiple Controls + Consoles + Compute | Large depot, many pumps, tank monitoring |

## Key Specifications

### Channels & I/O

- **2× galvanically isolated channels** (A and B)
- Each channel independently selectable: **5V, 12V, or 24V**
- **8× configurable inputs** (4 per channel):
  - 4–20 mA (current loop)
  - 0–5 V or 0–10 V (voltage)
  - Digital (dry contact, opto-isolated)
  - Pulse counting (flow meters)
- **2× relay outputs** (one per channel, dry contact)
- **Up to 8× SSR outputs** (solid-state relay, optional expansion)

### Communication

- **Ethernet** — W5500 hardwired TCP/IP (RJ45)
- **WiFi** — 802.11 b/g/n (2.4 GHz)
- **Bluetooth Low Energy (BLE)** — driver app communication
- **RS-232** — legacy equipment, console link
- **RS-485** — multi-drop bus, tank gauges, PLC integration

### User Interface

- **OLED display** — 128×64 pixels, SPI interface
- **5 navigation buttons** — menu-driven local operation
- **Piezo buzzer** — audible feedback and alarms
- **Web UI** — full configuration and monitoring via browser

### System

- **Processor:** ESP32-S3 dual-core, 240 MHz
- **Memory:** 512 KB SRAM + 8 MB PSRAM
- **Storage:** LittleFS on flash (config, web pages, logs)
- **Firmware updates:** OTA (over-the-air) via WiFi/Ethernet
- **Self-test:** 88-point automated hardware verification
- **Form factor:** 104 × 104 mm (DIN-rail or panel mount)
- **Power:** 12–24 V DC input

### Reliability

- Galvanic isolation between channels and controller
- Hardware watchdog and graceful degradation
- Dual OTA partitions (rollback on failed update)
- All I2C devices probed at boot with status reporting

## Target Applications

- Fleet fuelling depots
- Construction site fuel management
- Agricultural fuel storage
- Marina fuel dispensing
- Generator fuel monitoring
- Any unattended or semi-attended fuel dispensing point

## Related Documentation

- [Installation Guide](installation-guide.md) — physical setup
- [Configuration](configuration.md) — software setup
- [User Guide](user-guide.md) — daily operation
- [Hardware Reference](hardware-reference.md) — detailed specs
