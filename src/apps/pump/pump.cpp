// =======================================================
// Pump / fueling controller (config-driven)
// =======================================================
#include "pump.h"
#include "hardware/relays.h"
#include "hardware/input_config.h"
#include "platform/sampler.h"
#include "platform/task_registry.h"
#include "logging.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

// config namespace (config.h, header-only).
namespace config {
    extern bool is_loaded;
    extern JsonDocument config;
    bool config_read_groups(JsonDocument& out);
}

// web_socket forward-declare (header-only namespace).
namespace web_socket {
    extern bool is_connected;
    void sendMessage(const String &message);
}

// --- Constants ---
#define MAX_PUMPS          4
#define PUMP_SAMPLE_MS     200
#define PUMP_PUSH_S        60

// --- Map input name to channel index (A1..A4 -> 0..3, B1..B4 -> 4..7) ---
static int8_t input_name_to_channel(const char* name) {
    if (!name || strlen(name) != 2) return -1;
    char ch = name[0];
    char n = name[1];
    if (n < '1' || n > '4') return -1;
    int idx = (n - '1');
    if (ch == 'A' || ch == 'a') return idx;
    if (ch == 'B' || ch == 'b') return 4 + idx;
    return -1;
}

// --- Per-pump state ---
struct PumpState {
    char id[24];              // group id (e.g. "fueling_1")
    char name[24];            // display name (e.g. "Pump 1")
    char nozzle_input[4];     // digital nozzle switch input ("A1")
    char pulse_input[4];      // flow pulse input ("A2")
    int8_t nozzle_ch;         // channel index for nozzle (-1 if none)
    int8_t pulse_ch;          // channel index for pulse (-1 if none)
    Relay relay;              // pump relay (RELAY_A / RELAY_B)
    bool has_relay;
    float pulses_per_litre;   // flow calibration
    float preset_litres;      // preset dispense limit (0 = none)

    // Runtime
    bool nozzle_lifted;       // current nozzle state
    bool relay_on;            // current relay state
    uint32_t pulse_count;     // accumulated pulses (this transaction)
    uint32_t sampler_cursor;  // position in sampler ring buffer
    uint8_t prev_bit;         // previous pulse bit (edge detection)
    bool valid;
};

static PumpState s_pumps[MAX_PUMPS];
static uint8_t s_num_pumps = 0;
static TaskHandle_t s_task_handle = nullptr;

// --- Litres dispensed for a pump (pulses / pulses_per_litre) ---
static float pump_litres(const PumpState& p) {
    if (p.pulses_per_litre <= 0.0f) return 0.0f;
    return (float)p.pulse_count / p.pulses_per_litre;
}

// --- Read nozzle + count pulses for one pump from the sampler ---
static void sample_pump(PumpState& p) {
    // Nozzle: current digital level on its channel.
    if (p.nozzle_ch >= 0) {
        uint8_t state = sampler_current_state();
        bool raw = (state >> p.nozzle_ch) & 0x01;
        // Nozzle switch pulled up to 24V; lifted = LOW (active-low). Treat
        // LOW as lifted. (Physical wiring may invert; refine with hardware.)
        p.nozzle_lifted = (raw == 0);
    }

    // Pulse: count falling edges on the pulse channel.
    if (p.pulse_ch >= 0) {
        // The rule engine owns the relay now, so read the ACTUAL relay state
        // for status-display pulse gating (p.relay_on is no longer driven here).
        bool pumping = p.has_relay ? relay_get(p.relay) : false;
        SamplerEvent evt;
        while (sampler_read_next(p.sampler_cursor, evt)) {
            uint8_t bit = (evt.state >> p.pulse_ch) & 0x01;
            if (p.prev_bit == 1 && bit == 0) {
                // Only count flow while the relay is on (pump running).
                if (pumping) p.pulse_count++;
            }
            p.prev_bit = bit;
        }
        p.relay_on = pumping;   // keep status field in sync for the web/OLED UI
    }
}

// --- Set a pump's relay ---
static void pump_set_relay(PumpState& p, bool on) {
    if (!p.has_relay) return;
    p.relay_on = on;
    relay_set(p.relay, on);
}

// --- JSON for one pump ---
static void pump_to_json(JsonObject obj, const PumpState& p) {
    obj["id"] = p.id;
    obj["name"] = p.name;
    obj["nozzle_input"] = p.nozzle_input;
    obj["pulse_input"] = p.pulse_input;
    obj["nozzle_lifted"] = p.nozzle_lifted;
    obj["relay_on"] = p.relay_on;
    obj["pulses"] = p.pulse_count;
    obj["pulses_per_litre"] = serialized(String(p.pulses_per_litre, 2));
    obj["litres"] = serialized(String(pump_litres(p), 2));
    obj["preset_litres"] = serialized(String(p.preset_litres, 1));
}

// --- Task ---
static void pump_task(void* param) {
    (void)param;
    if (s_num_pumps == 0) {
        log_error("[pump] No pumps configured, task suspending");
        vTaskSuspend(nullptr);
        return;
    }

    uint32_t push_counter = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(PUMP_SAMPLE_MS));

        for (uint8_t i = 0; i < s_num_pumps; i++) {
            if (!s_pumps[i].valid) continue;
            sample_pump(s_pumps[i]);

            // NOTE: relay control (start/stop, preset, timeouts, flow guard)
            // is now owned by the shared rule engine (src/apps/rule_engine),
            // which executes the fueling rules[] from config. This task only
            // SAMPLES nozzle + pulse state for the web/OLED status display.
            // (Was: preset auto-stop driving the relay here — removed so the
            // engine is the single owner of the relay.)
        }

        push_counter += PUMP_SAMPLE_MS;
        if (push_counter >= PUMP_PUSH_S * 1000UL) {
            push_counter = 0;
            if (web_socket::is_connected) {
                web_socket::sendMessage(pump_get_all());
            }
        }
    }
}

// --- Public API ---

void pump_init() {
    s_num_pumps = 0;

    JsonDocument groupsDoc;
    if (!config::config_read_groups(groupsDoc) || groupsDoc["groups"].isNull()) {
        log_error("[pump] No config or no groups");
        return;
    }

    for (JsonObject group : groupsDoc["groups"].as<JsonArray>()) {
        const char* gtype = group["type"] | "";
        if (strcmp(gtype, "pump") != 0) continue;
        if (group["pump"].isNull()) continue;
        if (s_num_pumps >= MAX_PUMPS) {
            log_error("[pump] Max %d pumps, ignoring rest", MAX_PUMPS);
            break;
        }
        JsonObject pump = group["pump"].as<JsonObject>();
        PumpState& p = s_pumps[s_num_pumps];
        memset(&p, 0, sizeof(p));

        const char* gid = group["id"] | "pump";
        strncpy(p.id, gid, sizeof(p.id) - 1);
        const char* name = pump["name"] | gid;
        strncpy(p.name, name, sizeof(p.name) - 1);

        const char* nozzle = pump["nozzle"] | "";
        const char* pulse  = pump["pulse"]  | "";
        strncpy(p.nozzle_input, nozzle, sizeof(p.nozzle_input) - 1);
        strncpy(p.pulse_input, pulse, sizeof(p.pulse_input) - 1);
        p.nozzle_ch = input_name_to_channel(nozzle);
        p.pulse_ch  = input_name_to_channel(pulse);

        const char* relay = pump["relay"] | "";
        if (strcmp(relay, "relay_b") == 0) { p.relay = RELAY_B; p.has_relay = true; }
        else if (strcmp(relay, "relay_a") == 0) { p.relay = RELAY_A; p.has_relay = true; }
        else { p.has_relay = false; }

        p.pulses_per_litre = pump["pulses_per_litre"] | 0.0f;
        p.preset_litres    = pump["preset_litres"] | 0.0f;

        // Runtime init
        p.nozzle_lifted = false;
        p.relay_on = false;
        p.pulse_count = 0;
        p.sampler_cursor = sampler_write_position();
        p.prev_bit = 1;
        p.valid = true;

        // Configure inputs: nozzle as digital pull-up, pulse as digital pull-up.
        if (p.nozzle_ch >= 0) {
            input_config_set((Input)p.nozzle_ch, SW_PULLUP, true);
        }
        if (p.pulse_ch >= 0) {
            input_config_set((Input)p.pulse_ch, SW_PULLUP, true);
        }
        // Relay starts off.
        if (p.has_relay) relay_set(p.relay, false);

        s_num_pumps++;
        log_info("[pump] '%s' nozzle=%s pulse=%s relay=%s ppl=%.1f",
                 p.name, nozzle, pulse, relay, p.pulses_per_litre);
    }

    log_info("[pump] Initialized, %d pump(s)", s_num_pumps);
}

void pump_start_task() {
    xTaskCreate(pump_task, "pump", 4096, nullptr, 2, &s_task_handle);
    task_register(s_task_handle, "pump", 2, 4096);
}

void pump_stop()  { if (s_task_handle) vTaskSuspend(s_task_handle); }
void pump_start() { if (s_task_handle) vTaskResume(s_task_handle); }

String pump_get_all() {
    JsonDocument doc;
    JsonArray pumps = doc["pumps"].to<JsonArray>();
    for (uint8_t i = 0; i < s_num_pumps; i++) {
        JsonObject obj = pumps.add<JsonObject>();
        pump_to_json(obj, s_pumps[i]);
    }
    String result;
    serializeJson(doc, result);
    return result;
}

String pump_get(const char* id) {
    for (uint8_t i = 0; i < s_num_pumps; i++) {
        if (strcmp(s_pumps[i].id, id) == 0 || strcmp(s_pumps[i].name, id) == 0) {
            JsonDocument doc;
            JsonObject obj = doc.to<JsonObject>();
            pump_to_json(obj, s_pumps[i]);
            String result;
            serializeJson(doc, result);
            return result;
        }
    }
    return "{\"error\":\"pump not found\"}";
}

// True if any pump has a nozzle lifted or its relay on (fueling in progress).
bool pump_any_active() {
    for (uint8_t i = 0; i < s_num_pumps; i++) {
        if (!s_pumps[i].valid) continue;
        if (s_pumps[i].nozzle_lifted || s_pumps[i].relay_on) return true;
    }
    return false;
}
