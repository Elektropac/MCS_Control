# App Architecture

MCS Control firmware is a **platform**. You write small, self-contained programs ("apps") that run on it. Each app is a FreeRTOS task that talks to hardware through an API and communicates with other apps and the network through a message bus.

---

## The Big Picture

```
┌─────────────────────────────────────────────────────────────┐
│  Apps (each runs as its own FreeRTOS task)                   │
│                                                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │pump_controller│  │ tank_gauge  │  │  your_app   │      │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘      │
│         │                  │                  │              │
│         └──────────────────┼──────────────────┘              │
│                            │                                 │
│                    Message Bus (pub/sub)                      │
│                            │                                 │
├────────────────────────────┼─────────────────────────────────┤
│  System                    │                                 │
│  ┌─────────┐  ┌────────┐  │  ┌──────────┐  ┌────────────┐  │
│  │ mcs_api │  │sampler │  │  │ network  │  │ web server │  │
│  │(hardware)│  │ (ISR)  │  │  │(WiFi/ETH)│  │  (HTTP)    │  │
│  └─────────┘  └────────┘  │  └──────────┘  └────────────┘  │
└────────────────────────────┼─────────────────────────────────┘
                             │
                        JSON messages
                             │
                     ┌───────┴───────┐
                     │  Server/Phone │
                     └───────────────┘
```

---

## Communication: The Bulletin Board

Apps communicate via a **message bus**. Think of it as a bulletin board in a shared room:

- Any app can **post a note** (publish) on a topic
- Any app can say **"tell me when a note about X appears"** (subscribe)
- Apps don't know about each other — they only know about topics

### Topics

A topic is just a string that describes what the message is about:

```
"pump/1/start"      — someone wants pump 1 to start
"pump/1/done"       — pump 1 finished dispensing
"tank/1/level"      — tank 1 has a new level reading
"tank/1/alarm"      — tank 1 level is critically low
"system/heartbeat"  — system is alive
```

Convention: `category/id/action`

### Publish

Post a message to a topic. Everyone who subscribes gets a copy.

```cpp
JsonDocument doc;
doc["liters"] = 142.3;
doc["reason"] = "nozzle_hung_up";
mcs_publish("pump/1/done", doc);
```

The publisher doesn't know (or care) who receives it. Maybe 3 apps are listening. Maybe none. Doesn't matter.

### Subscribe

Register interest in a topic. Your callback runs when someone publishes to it.

```cpp
mcs_subscribe("pump/1/done", [](const JsonObject& data) {
    float liters = data["total_liters"];
    // do something with it
});
```

### Wildcards

```cpp
mcs_subscribe("pump/#", callback);   // all pump messages (any pump, any action)
mcs_subscribe("tank/1/#", callback); // all messages about tank 1
```

---

## How It Works with FreeRTOS

Each app runs in its own FreeRTOS task (like a mini-program running in parallel). The message bus connects them without shared memory.

```
┌─────────────────────────┐     ┌─────────────────────────┐
│  pump_controller task   │     │  tank_gauge task         │
│                         │     │                          │
│  loop:                  │     │  loop:                   │
│    count pulses         │     │    read ADC              │
│    check nozzle         │     │    calculate liters      │
│    publish updates      │     │    publish level         │
│    sleep 100ms          │     │    sleep 1000ms          │
│                         │     │                          │
│  inbox: ←── messages    │     │  inbox: ←── messages     │
│    "pump/1/start"       │     │    "pump/1/done"         │
└─────────────────────────┘     └──────────────────────────┘
         │                                  │
         └────────── Message Bus ───────────┘
```

When app A publishes a message:
1. The bus looks up who subscribes to that topic
2. A copy of the message is placed in each subscriber's **inbox** (a FreeRTOS queue)
3. The subscriber's task picks it up on its next loop iteration

No app ever blocks another app. Messages wait in the inbox until processed.

### Task Loop Pattern

Every app follows the same pattern:

```cpp
void my_app_task(void* param) {
    for (;;) {
        // 1. Process incoming messages (from your subscriptions)
        mcs_process_inbox();

        // 2. Do your work
        //    - read sensors
        //    - check conditions
        //    - control outputs

        // 3. Publish results/events
        mcs_publish("my_topic", my_data);

        // 4. Sleep until next cycle
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

---

## Complete Example: A Fuel Dispensing Scenario

Three apps running. A driver uses their phone to fuel.

### Step 1: Phone authorises

The network layer receives a "start pump" command from the phone via BLE. It publishes:

```cpp
// network_task.cpp (Niklas' code)
mcs_publish("pump/1/start", {"transaction_id": "TX-4821", "max_liters": 500});
```

### Step 2: Pump controller reacts

`pump_controller` subscribed to `"pump/1/start"` at init. Its callback fires:

```cpp
// pump_controller.cpp
void on_pump_start(const JsonObject& data) {
    s_transaction_id = data["transaction_id"].as<String>();
    s_max_liters = data["max_liters"] | 0;
    mcs_relay_set(0, true);       // turn on pump relay
    s_running = true;
    mcs_buzzer(MCS_SOUND_OK);     // beep
}
```

### Step 3: Pump controller counts litres

In its task loop, it publishes updates:

```cpp
void pump_controller_task(void* param) {
    for (;;) {
        mcs_process_inbox();

        if (s_running) {
            uint32_t pulses = mcs_pulse_count(0);
            float liters = pulses / 100.0;

            // Publish progress (network sends to phone, web UI shows it)
            JsonDocument update;
            update["transaction_id"] = s_transaction_id;
            update["liters"] = liters;
            update["state"] = "running";
            mcs_publish("pump/1/update", update);

            // Nozzle hung up? → done
            if (!mcs_digital_read(1)) {
                mcs_relay_set(0, false);
                s_running = false;

                JsonDocument done;
                done["transaction_id"] = s_transaction_id;
                done["total_liters"] = liters;
                done["reason"] = "nozzle_hung_up";
                mcs_publish("pump/1/done", done);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

### Step 4: Tank gauge adjusts

`tank_gauge` subscribed to `"pump/1/done"`:

```cpp
void on_pump_done(const JsonObject& data) {
    float dispensed = data["total_liters"];
    s_tank_level -= dispensed;

    JsonDocument doc;
    doc["tank"] = 1;
    doc["liters"] = s_tank_level;
    mcs_publish("tank/1/level", doc);

    if (s_tank_level < 500) {
        JsonDocument alarm;
        alarm["message"] = "Tank level low";
        alarm["liters"] = s_tank_level;
        mcs_publish("tank/1/alarm", alarm);
    }
}
```

### Step 5: Network sends everything to the server

Network subscribes to all topics:

```cpp
mcs_subscribe("pump/#", send_to_server);
mcs_subscribe("tank/#", send_to_server);
```

It doesn't know about pumps or tanks. It just forwards JSON to the cloud.

### Who knew about whom?

Nobody. `pump_controller` didn't import `tank_gauge`. `tank_gauge` didn't import `network`. They only know about **topic names**. You can add a 4th app tomorrow (e.g. `receipt_printer`) that subscribes to `"pump/#/done"` — and nothing else changes.

---

## Writing a New App

### Step 1: Create a folder

```
apps/my_app/
├── my_app.cpp
├── my_app.h
└── README.md
```

### Step 2: Implement the interface

```cpp
// my_app.h
#pragma once
#include <ArduinoJson.h>

void my_app_init(const JsonObject& config);
void my_app_task(void* param);
```

```cpp
// my_app.cpp
#include "my_app.h"
#include "mcs_api.h"

static uint8_t s_input_channel;

void my_app_init(const JsonObject& config) {
    // Read your config
    s_input_channel = config["input"] | 0;

    // Set up hardware
    mcs_input_mode(s_input_channel, MCS_MODE_CURRENT);
    mcs_voltage_set(s_input_channel / 4, 24);

    // Subscribe to topics you care about
    mcs_subscribe("system/config_changed", on_config_changed);
}

void my_app_task(void* param) {
    for (;;) {
        mcs_process_inbox();

        float ma = mcs_adc_read_ma(s_input_channel);

        // Publish your readings
        JsonDocument doc;
        doc["channel"] = s_input_channel;
        doc["ma"] = ma;
        mcs_publish("my_app/reading", doc);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### Step 3: Register in app registry

```cpp
// apps/app_registry.cpp
#include "my_app/my_app.h"

static const AppEntry s_apps[] = {
    // name              init              task              stack  priority
    { "pump_controller", pump_ctrl_init,   pump_ctrl_task,   4096,  3 },
    { "tank_gauge",      tank_gauge_init,  tank_gauge_task,  3072,  2 },
    { "my_app",          my_app_init,      my_app_task,      3072,  2 },
};
```

### Step 4: Add config entry

```json
{
  "functions": [
    { "id": "my_sensor_1", "type": "my_app", "input": 4 }
  ]
}
```

### Step 5: Document in README.md

```markdown
# My App

Reads a 4-20mA sensor and publishes the reading every second.

## Config keys
| Key | Type | Description |
|-----|------|-------------|
| input | int (0-7) | Which ADC channel to read |

## Publishes
| Topic | Data | When |
|-------|------|------|
| my_app/reading | {channel, ma} | Every 1s |

## Subscribes
| Topic | Action |
|-------|--------|
| system/config_changed | Re-reads config |

## Hardware API used
- mcs_input_mode()
- mcs_voltage_set()
- mcs_adc_read_ma()
```

---

## App API Reference (`mcs_api.h`)

### Inputs — Reading

```cpp
int32_t  mcs_adc_read_mv(uint8_t channel);   // 0-7, returns millivolts
float    mcs_adc_read_ma(uint8_t channel);    // 0-7, returns milliamps
bool     mcs_digital_read(uint8_t channel);   // 0-7, true = active
uint32_t mcs_pulse_count(uint8_t channel);    // 0-7, cumulative count
void     mcs_pulse_reset(uint8_t channel);    // reset counter to 0
```

### Inputs — Configuration

```cpp
void mcs_input_mode(uint8_t channel, McsInputMode mode);

enum McsInputMode {
    MCS_MODE_OFF,
    MCS_MODE_VOLTAGE,         // 0-5V via divider
    MCS_MODE_CURRENT,         // 4-20mA via 200Ω shunt
    MCS_MODE_DIGITAL,         // opto-isolated
    MCS_MODE_DIGITAL_PULLUP,  // digital with pullup
    MCS_MODE_PULSE,           // digital + pullup + sampler tracking
};
```

### Outputs

```cpp
void mcs_relay_set(uint8_t relay, bool state);  // 0=A, 1=B
bool mcs_relay_get(uint8_t relay);
void mcs_voltage_set(uint8_t channel, uint8_t voltage);  // channel 0=A, 1=B; voltage 0/5/12/24
```

### Message Bus

```cpp
void mcs_publish(const char* topic, const JsonDocument& data);
void mcs_subscribe(const char* topic, void(*callback)(const JsonObject& data));
void mcs_process_inbox();  // call once per loop iteration
```

### System

```cpp
void     mcs_log(McsLogLevel level, const char* tag, const char* fmt, ...);
uint32_t mcs_uptime_ms();
void     mcs_buzzer(McsBuzzerSound sound);
```

---

## Topic Naming Convention

```
category/instance_id/action

Examples:
  pump/1/start        — command: start pump 1
  pump/1/update       — event: pump 1 progress
  pump/1/done         — event: pump 1 finished
  tank/1/level        — event: tank 1 reading
  tank/1/alarm        — event: tank 1 problem
  system/heartbeat    — system alive
  system/config_changed — config was updated
```

Use `#` as wildcard:
- `pump/#` — all pump messages
- `tank/1/#` — everything about tank 1

---

## Rules

1. **Only use `mcs_api.h`** — no direct Wire, GPIO, or register access
2. **One task per config instance** — 2 pumps in config = 2 pump_controller tasks
3. **Communicate only via publish/subscribe** — no shared globals between apps
4. **Always call `mcs_process_inbox()`** at the top of your loop
5. **Keep callbacks short** — they run in your task context, don't block
6. **Document your topics** in your README.md
7. **Sleep in your loop** — `vTaskDelay()` is mandatory, never spin

---

## What the System Handles (apps don't touch)

- I2C bus arbitration (mutex)
- ADC calibration (zero + gain + CMOS compensation)
- Sampler ISR (5 kHz, always running, fills ring buffer)
- Network connectivity (WiFi, Ethernet, BLE, WebSocket)
- Config loading from LittleFS
- OTA firmware updates
- Display/menu system
- Self-test
- Message bus routing

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
├── _template/
│   ├── template_app.cpp
│   ├── template_app.h
│   └── README.md
└── app_registry.cpp
```

---

## Migration from Current Code

The existing code already does most of this — it just needs restructuring:

1. ✅ FreeRTOS tasks (already used)
2. ✅ Sampler ring buffer (already ISR-based, apps poll it)
3. ✅ JSON communication (function_silo already does this for web)
4. 🔲 Create `mcs_api.h` (thin wrapper around existing drivers)
5. 🔲 Create message bus (`mcs_publish`/`mcs_subscribe` using FreeRTOS queues)
6. 🔲 Move flow_guard to `apps/flow_guard/`
7. 🔲 Write `pump_controller` as first real app
8. 🔲 Write `tank_gauge` as second app
9. 🔲 Create `app_registry.cpp` with config-driven startup

The existing driver code (`lib/`, `src/hardfunc/`) stays unchanged — `mcs_api.h` wraps it.
