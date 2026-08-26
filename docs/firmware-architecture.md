# MCS Control — Firmware Architecture

This document describes the firmware design for developers working on the MCS Control codebase. It covers the task model, memory strategy, code organization, interfaces, and build system.

---

## Overview

The firmware runs on an ESP32-S3 using the Arduino framework with FreeRTOS. There is no custom scheduler — all concurrency is managed through standard FreeRTOS tasks, queues, and mutexes.

### Design Principles

- Hardware abstraction through reusable library drivers
- JSON-based command/event system for decoupled communication
- Graceful degradation when hardware faults are detected
- Clear ownership boundary between hardware layer (Jesper) and network layer (Niklas)

---

## Boot Sequence

```
Power On
  │
  ├── setup()
  │     ├── Load config.json from LittleFS (override) or firmware default
  │     ├── Initialise I2C bus (400 kHz, mutex)
  │     ├── Probe all I2C devices → set hw_status flags
  │     ├── Initialise drivers (ADC, GPIO expanders, display, buzzer, relays)
  │     ├── Start FreeRTOS tasks
  │     ├── Run startup self-test (if enabled)
  │     └── Suspend Arduino loop() — all work in tasks
  │
  └── FreeRTOS scheduler takes over
```

After `setup()` completes, the Arduino `loop()` function is suspended. All runtime logic executes within FreeRTOS tasks.

---

## FreeRTOS Task Model

| Task | Priority | Core | Stack | Purpose |
|------|----------|------|-------|---------|
| `sampler` (ISR) | Highest | 0 | — | 5 kHz ADC sampling via hardware timer interrupt |
| `flow_guard` | High | 0 | 4 KB | Monitors pulse counts, detects overflow/underflow |
| `display_task` | Normal | 1 | 4 KB | Updates OLED display at ~10 Hz |
| `buttons_task` | Normal | 1 | 2 KB | Reads analog button ladder, debounce, menu navigation |
| `network_task` | Normal | 1 | 8 KB | Ethernet/WiFi stack, web server, WebSocket |
| `function_silo` | Normal | 1 | 8 KB | JSON command dispatcher, routes messages between subsystems |

### Inter-Task Communication

- **Queues:** Primary mechanism for passing JSON messages between tasks
- **Mutexes:** Protect shared hardware resources (I2C bus, SPI bus)
- **Event groups:** Synchronise startup sequence
- **Direct task notifications:** Lightweight wake-up signals

---

## Memory Strategy

The ESP32-S3 has three distinct memory regions, each used for specific purposes:

| Region | Size | Used For |
|--------|------|----------|
| Internal SRAM | 512 KB | FreeRTOS stacks, ISR buffers, DMA, fast variables |
| PSRAM (external) | 8 MB | Large JSON buffers, web page content, config parsing |
| LittleFS (flash) | 5.8 MB | Persistent config, web UI files, calibration data |

### Allocation Rules

- **ISR context:** Internal SRAM only (PSRAM is too slow for interrupt handlers)
- **JSON parsing/building:** PSRAM (ArduinoJson documents allocated with PSRAM allocator)
- **Config storage:** LittleFS for persistence, loaded into PSRAM at boot
- **Display buffer:** Internal SRAM (SPI DMA requires internal memory)
- **Network buffers:** PSRAM (large TCP/TLS buffers)

---

## Code Organization

### Directory Structure

```
MCS_Control/
├── src/
│   ├── main.cpp              ← setup(), loop() (suspended)
│   ├── hardfunc/             ← Hardware functions (Jesper)
│   │   ├── pump_controller.cpp
│   │   ├── probe_monitor.cpp
│   │   ├── relay_control.cpp
│   │   └── calibration.cpp
│   ├── tasks/                ← FreeRTOS task entry points
│   │   ├── sampler_task.cpp
│   │   ├── display_task.cpp
│   │   ├── buttons_task.cpp
│   │   └── flow_guard.cpp
│   └── debug/                ← Serial debug commands, self-test
│       ├── serial_commands.cpp
│       └── selftest.cpp
├── lib/
│   ├── hal/                  ← Hardware abstraction layer
│   ├── tca9535/              ← I2C GPIO expander driver
│   ├── ads1115/              ← 16-bit ADC driver
│   ├── ssd1306/              ← OLED display driver
│   ├── buzzer/               ← Piezo buzzer driver
│   ├── buttons/              ← Analog button ladder
│   ├── logging/              ← Structured logging
│   ├── 1_w5500/              ← Ethernet driver (Niklas)
│   ├── 1_wifi/               ← WiFi manager (Niklas)
│   ├── 3_ssl_manager/        ← TLS/SSL handling (Niklas)
│   ├── 4_web_server/         ← HTTP server (Niklas)
│   └── 4_web_socket/         ← WebSocket server (Niklas)
├── data/
│   ├── config.json           ← Default config (embedded in firmware)
│   ├── index.html            ← Web dashboard
│   ├── test.html             ← Hardware test UI
│   └── tanks.html            ← Tank visualization
├── include/
│   └── pins.h                ← Pin definitions
└── platformio.ini            ← Build configuration
```

### Library Numbering Convention (Niklas)

Libraries prefixed with numbers indicate dependency layers:
- `0_xxx` — Platform/HAL level
- `1_xxx` — Hardware drivers (Ethernet, WiFi)
- `2_xxx` — Protocol level
- `3_xxx` — Security/crypto
- `4_xxx` — Application services (web server, WebSocket)

---

## Responsibility Split

| Area | Owner | Location |
|------|-------|----------|
| Hardware I/O, sensors, actuators | Jesper | `src/hardfunc/`, `lib/hal`, `lib/tca9535`, `lib/ads1115`, etc. |
| Display, buttons, buzzer | Jesper | `lib/ssd1306`, `lib/buzzer`, `lib/buttons`, `src/tasks/` |
| Self-test, debug, calibration | Jesper | `src/debug/` |
| Ethernet, WiFi, connectivity | Niklas | `lib/1_w5500`, `lib/1_wifi` |
| Web server, WebSocket, TLS | Niklas | `lib/3_ssl_manager`, `lib/4_web_server`, `lib/4_web_socket` |
| function_silo (shared interface) | Both | Interface contract between domains |

### Interface: function_silo

The `function_silo` is the primary interface between Jesper's hardware layer and Niklas's communication layer. It is a JSON-based command dispatcher using a subject → handler pattern.

```cpp
// Registering a handler (Jesper's code)
register_external_handler("relay_toggle", relay_toggle_handler);
register_external_handler("adc_read", adc_read_handler);
register_external_handler("voltage_set", voltage_set_handler);

// Incoming message (from Niklas's web server)
{
  "subject": "relay_toggle",
  "data": { "relay": "A" }
}

// Handler processes command, returns JSON response via queue
```

**Key design decisions:**
- All communication between layers goes through function_silo
- Handlers are registered at boot — no dynamic dispatch
- Messages are JSON (ArduinoJson), allocated in PSRAM
- Async: requests and responses use FreeRTOS queues
- Either side can originate messages (commands from web → hardware, events from hardware → web)

---

## Configuration System

### Config Sources

1. **`data/config.json`** — Default configuration, compiled into the firmware image
2. **LittleFS `/config.json`** — Device-specific overrides, persisted across reboots

At boot, the firmware loads the embedded default, then overlays any LittleFS overrides.

### Config Access Pattern

```cpp
// Load config at boot
JsonDocument config;  // Allocated in PSRAM
loadConfig(config);   // Merges default + LittleFS

// Access values
int voltage = config["channels"]["A"]["channel_voltage"];

// Modify and save
config["channels"]["A"]["channel_voltage"] = 12;
saveConfig(config);   // Writes to LittleFS
```

---

## Error Handling

### Boot Probing

Every I2C device is probed during `setup()`. Results are stored in `hw_status` flags:

```cpp
struct hw_status {
  bool adc_a_ok;        // 0x48
  bool adc_b_ok;        // 0x49
  bool expander_a_ok;   // 0x25
  bool expander_b_ok;   // 0x23
  bool relay_board_ok;  // 0x27
  bool version_board_ok;// 0x21
};
```

### Graceful Degradation

- If a device fails to probe, its features are disabled (not the whole system)
- If ADC-A fails, channel A measurements are unavailable but channel B still works
- If the relay board fails, pumps cannot be controlled but monitoring continues
- The display shows which subsystems are offline

### Runtime Errors

- I2C transactions are retried (3 attempts with backoff)
- Timeout protection on all I2C operations (via mutex with timeout)
- Watchdog timer resets the system if a task hangs

---

## Build System

### PlatformIO Configuration

- **Framework:** Arduino
- **Board:** ESP32-S3 (custom board definition for Waveshare ESP32-S3-ETH)
- **Flash layout:** Dual OTA partitions + LittleFS

### Partition Table

| Partition | Size | Purpose |
|-----------|------|---------|
| app0 (OTA_0) | 5 MB | Active firmware |
| app1 (OTA_1) | 5 MB | OTA update target |
| spiffs (LittleFS) | 5.8 MB | Config, web UI, calibration |

### Build Commands

```bash
pio run                    # Build firmware
pio run --target upload    # Flash via USB
pio run --target uploadfs  # Upload LittleFS filesystem
pio run --target monitor   # Serial monitor (115200 baud)
```

---

## Branching Strategy

- **`main`** — stable, tested, deployable
- **Feature branches** — named descriptively (e.g., `feature/rs485-termination`, `fix/adc-calibration`)
- Merge to main weekly (or when feature is complete and tested)
- No direct commits to main — always via branch + merge

---

## Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| Arduino framework (not ESP-IDF directly) | Faster development, broad library ecosystem, Niklas's existing code |
| FreeRTOS (no custom scheduler) | Well-tested, standard, good tooling, ESP-IDF provides it anyway |
| JSON for inter-module communication | Human-readable, debuggable, flexible schema evolution |
| PSRAM for large allocations | Keeps internal SRAM free for ISRs and DMA |
| LittleFS (not SPIFFS) | Wear leveling, power-loss safety, proper directory support |
| Dual OTA partitions | Rollback on failed updates — critical for field-deployed devices |
| I2C for all peripherals | Reduces pin count, standard protocol, proven in this form factor |
| function_silo as interface contract | Clean separation of concerns, testable, allows independent development |

---

## Related Documentation

- [Hardware Reference](hardware-reference.md) — pin mapping, I2C addresses, electrical specs
- [API Reference](api-reference.md) — function_silo commands, serial interface
- [Configuration](configuration.md) — config.json format and parameters
