# App Architecture

MCS Control firmware is structured as a **platform with an app layer**. Each application ("app") is a self-contained module that runs as a FreeRTOS task and interacts with hardware exclusively through a defined API.

This makes it possible for anyone to write an app for MCS Control without understanding the full system — they only need the App API.

---

## Layered Design

```
┌─────────────────────────────────────────────────┐
│  Apps                                           │
│  ┌────────────┐ ┌────────────┐ ┌────────────┐  │
│  │pump_ctrl   │ │tank_gauge  │ │ your_app   │  │
│  └─────┬──────┘ └─────┬──────┘ └─────┬──────┘  │
│        │               │              │         │
├────────┴───────────────┴──────────────┴─────────┤
│  App API (mcs_api.h)                            │
│  Reads, writes, events, config — nothing else   │
├─────────────────────────────────────────────────┤
│  System (drivers, sampler, I2C, network, RTOS)  │
└─────────────────────────────────────────────────┘
```

Apps **never** touch hardware directly. No `Wire.begin()`, no `digitalWrite()`, no register writes. Everything goes through the API.

---

## Directory Structure

```
apps/
├── pump_controller/
│   ├── pump_controller.cpp
│   ├── pump_controller.h
│   └── README.md
├── tank_gauge/
│   ├── tank_gauge.cpp
│   ├── tank_gauge.h
│   └── README.md
├── flow_guard/
│   ├── flow_guard.cpp
│   ├── flow_guard.h
│   └── README.md
└── _template/
    ├── template_app.cpp
    ├── template_app.h
    └── README.md
```

Each app folder contains:
- Implementation (`.cpp` + `.h`)
- `README.md` describing purpose, config keys, and which API calls it uses

---

## App Lifecycle

Every app implements the same interface:

```cpp
#include "mcs_api.h"

// Called once at boot. Receives the app's config section.
void app_init(const JsonObject& config);

// The app's main task function (runs in its own FreeRTOS task).
void app_task(void* param);

// Called when system requests clean shutdown (optional).
void app_stop();
```

### Registration

Apps register themselves in a central registry (`apps/app_registry.cpp`):

```cpp
#include "app_registry.h"
#include "pump_controller/pump_controller.h"
#include "tank_gauge/tank_gauge.h"
#include "flow_guard/flow_guard.h"

static const AppEntry s_apps[] = {
    { "pump_controller", pump_controller_init, pump_controller_task, 4096, 3 },
    { "tank_gauge",      tank_gauge_init,      tank_gauge_task,      3072, 2 },
    { "flow_guard",      flow_guard_init,      flow_guard_task,      2048, 1 },
};
```

### Config-driven startup

Only apps referenced in `config.json` are started:

```json
{
  "functions": [
    {
      "id": "pump_1",
      "type": "pump_controller",
      "pulse_input": 0,
      "nozzle_input": 1,
      "relay_output": 0,
      "meter": { "pulses_per_liter": 100.0 }
    },
    {
      "id": "tank_1",
      "type": "tank_gauge",
      "input": 2,
      "conversion": { "type": "linear", "in_min": 4.0, "in_max": 20.0, "out_min": 0, "out_max": 5000, "unit": "L" }
    }
  ]
}
```

At boot, the system iterates `functions[]`, finds the matching app by `type`, and starts a task for each instance with its config section.

---

## App API (`mcs_api.h`)

This is the **only** header an app includes. It provides everything needed.

### Inputs — Reading

```cpp
// Analog
int32_t  mcs_adc_read_mv(uint8_t channel);       // 0-7, returns millivolts
float    mcs_adc_read_ma(uint8_t channel);        // 0-7, returns milliamps (via shunt)

// Digital
bool     mcs_digital_read(uint8_t channel);       // 0-7, true = active

// Pulses
uint32_t mcs_pulse_count(uint8_t channel);        // 0-7, cumulative since last reset
void     mcs_pulse_reset(uint8_t channel);        // reset counter to 0
```

### Inputs — Configuration

```cpp
// Set input mode for a channel
void mcs_input_mode(uint8_t channel, McsInputMode mode);

// Available modes:
enum McsInputMode {
    MCS_MODE_OFF,            // all switches off
    MCS_MODE_VOLTAGE,        // analog 0-5V (via divider)
    MCS_MODE_CURRENT,        // 4-20mA (via 200Ω shunt)
    MCS_MODE_DIGITAL,        // opto-isolated digital
    MCS_MODE_DIGITAL_PULLUP, // digital with internal pullup
    MCS_MODE_PULSE,          // digital + pullup + sampler tracking
};
```

### Outputs

```cpp
void mcs_relay_set(uint8_t relay, bool state);    // 0=A, 1=B
bool mcs_relay_get(uint8_t relay);

void mcs_voltage_set(uint8_t channel, uint8_t voltage);  // channel 0=A, 1=B; voltage 0/5/12/24
```

### Events (callbacks)

```cpp
// Register callback for pulse edges (called from sampler ISR context — keep it fast!)
void mcs_on_pulse(uint8_t channel, void(*callback)(uint8_t ch, uint32_t total_count));

// Register callback for digital state change
void mcs_on_digital_change(uint8_t channel, void(*callback)(uint8_t ch, bool state));
```

### Communication (to network/server layer)

```cpp
// Publish data (goes to web UI + server via Niklas' layer)
void mcs_publish(const char* topic, const JsonObject& data);

// Subscribe to incoming commands
void mcs_subscribe(const char* topic, void(*callback)(const JsonObject& data));
```

### System

```cpp
void     mcs_log(McsLogLevel level, const char* tag, const char* fmt, ...);
uint32_t mcs_uptime_ms();
void     mcs_buzzer(McsBuzzerSound sound);
```

---

## Example: Minimal App

```cpp
// apps/my_counter/my_counter.cpp
#include "mcs_api.h"

static uint8_t s_channel;
static uint32_t s_last_count;

void my_counter_init(const JsonObject& config) {
    s_channel = config["input"] | 0;
    mcs_input_mode(s_channel, MCS_MODE_PULSE);
    mcs_voltage_set(s_channel / 4, 24);  // power the channel
    s_last_count = 0;
}

void my_counter_task(void* param) {
    for (;;) {
        uint32_t count = mcs_pulse_count(s_channel);
        if (count != s_last_count) {
            JsonDocument doc;
            doc["channel"] = s_channel;
            doc["pulses"] = count;
            mcs_publish("counter/update", doc.as<JsonObject>());
            s_last_count = count;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

Config entry:
```json
{ "id": "counter_1", "type": "my_counter", "input": 4 }
```

---

## Rules for Apps

1. **Only use `mcs_api.h`** — no direct hardware access, no `Wire`, no `digitalWrite`
2. **One task per instance** — if config has 2 pumps, 2 tasks are created
3. **No globals shared between apps** — communicate via `mcs_publish` / `mcs_subscribe`
4. **Keep stack usage reasonable** — declare your stack size in the registry
5. **Don't block forever** — always use `vTaskDelay()` or event waits with timeout
6. **Document your config keys** in README.md

---

## What the System Handles (apps don't need to care)

- I2C bus arbitration (mutex)
- ADC calibration (zero + gain + CMOS compensation)
- Sampler ISR (5 kHz, always running)
- Network connectivity (WiFi, Ethernet, WebSocket)
- Config loading from LittleFS
- OTA updates
- Display/menu (separate system task)
- Self-test

---

## Migration Path

The current code in `src/tasks/` (flow_guard, sampler) and `src/hardfunc/` already follows this pattern loosely. Migration steps:

1. Define `mcs_api.h` (wrapper around existing driver functions)
2. Create `apps/` folder
3. Move `flow_guard` → `apps/flow_guard/` (uses sampler API already)
4. Write `pump_controller` as first real app
5. Write `tank_gauge` as second app
6. Add `app_registry.cpp` with config-driven startup
7. Document the API in this file + README per app

Existing driver code (`lib/`, `src/hardfunc/`) stays unchanged — it just gets wrapped by `mcs_api.h`.
