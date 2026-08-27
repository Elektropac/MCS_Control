// =======================================================
// CloudGauge — probe/tank level monitoring (config-driven)
// =======================================================
// Reads probes defined in config.json, averages over 10 samples,
// converts to cm and liters.
// Channels A and B read alternating to limit current draw.
//
// Probe types are defined by MCS type names (e.g. "mcs_level")
// which map to hardware settings (voltage, mode, input count).
// =======================================================

#include "cloudgauge.h"
#include "hardware/adc.h"
#include "hardware/input_config.h"
#include "hardware/voltage_select.h"
#include "platform/task_registry.h"
// Forward-declare config namespace (defined in config.h, included by network_task)
// We only read config::config and config::is_loaded — no need to re-include the header
namespace config {
    extern bool is_loaded;
    extern JsonDocument config;
}

#include "logging.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Forward declaration — web_socket lives in a header-only namespace
namespace web_socket {
    extern bool is_connected;
    void sendMessage(const String &message);
}

// --- Constants ---
#define MAX_PROBES          8
#define SAMPLE_INTERVAL_MS  1000
#define AVG_WINDOW          10
#define PROBE_RANGE_CM      300.0f
#define MA_MIN              4.0f
#define MA_MAX              20.0f
#define PUSH_INTERVAL_S     60

// --- Probe type definitions ---
// Maps MCS type names to hardware requirements

struct ProbeTypeDef {
    const char* type_name;
    uint8_t input_count;        // how many inputs this type uses
    Voltage required_voltage;
    // Hardware setup function called at init
    void (*setup)(uint8_t channel);
};

// mcs_level: 4-20mA, 1 input, needs 24V, analog+shunt
static void setup_mcs_level(uint8_t channel) {
    // analog+shunt toggled per read cycle, not at init
    // just ensure everything is OFF
    input_config_set((Input)channel, SW_ANALOG, false);
    input_config_set((Input)channel, SW_SHUNT, false);
    input_config_set((Input)channel, SW_PULLUP, false);
    input_config_set((Input)channel, SW_DIGITAL, false);
}

static const ProbeTypeDef PROBE_TYPES[] = {
    { "mcs_level", 1, VOLTAGE_24V, setup_mcs_level },
    // Future: { "mcs_level_temp", 2, VOLTAGE_5V, setup_mcs_level_temp },
};
static const uint8_t NUM_PROBE_TYPES = sizeof(PROBE_TYPES) / sizeof(PROBE_TYPES[0]);

// --- Per-probe state ---
struct ProbeState {
    char id[16];                // from config (e.g. "tank_1")
    char input[4];              // physical input name ("A1", "B3")
    char type[20];              // MCS type name
    uint8_t channel;            // ADC/input channel index 0-7
    float samples[AVG_WINDOW];  // ring buffer of mA readings
    uint8_t sample_idx;
    uint8_t sample_count;
    float avg_ma;
    bool valid;                 // true if config parsed OK
};

static ProbeState s_probes[MAX_PROBES];
static uint8_t s_num_probes = 0;
static TaskHandle_t s_task_handle = nullptr;

// --- Parse input name to channel index ---
// "A1"→0, "A2"→1, "A3"→2, "A4"→3, "B1"→4, "B2"→5, "B3"→6, "B4"→7
static int8_t input_name_to_channel(const char* name) {
    if (!name || strlen(name) != 2) return -1;
    char ch = name[0];
    char num = name[1];
    if (num < '1' || num > '4') return -1;
    uint8_t idx = (num - '1');
    if (ch == 'A' || ch == 'a') return idx;
    if (ch == 'B' || ch == 'b') return idx + 4;
    return -1;
}

// --- Find probe type definition ---
static const ProbeTypeDef* find_probe_type(const char* type_name) {
    for (uint8_t i = 0; i < NUM_PROBE_TYPES; i++) {
        if (strcmp(PROBE_TYPES[i].type_name, type_name) == 0) {
            return &PROBE_TYPES[i];
        }
    }
    return nullptr;
}

// --- Map channel voltage from config ---
static Voltage voltage_from_config(int v) {
    switch (v) {
        case 5:  return VOLTAGE_5V;
        case 12: return VOLTAGE_12V;
        case 24: return VOLTAGE_24V;
        default: return VOLTAGE_OFF;
    }
}

// --- Helpers ---

static float ma_to_cm(float ma) {
    if (ma <= MA_MIN) return 0.0f;
    if (ma >= MA_MAX) return PROBE_RANGE_CM;
    return ((ma - MA_MIN) / (MA_MAX - MA_MIN)) * PROBE_RANGE_CM;
}

static float compute_average(ProbeState& p) {
    if (p.sample_count == 0) return 0.0f;
    float sum = 0.0f;
    uint8_t count = (p.sample_count < AVG_WINDOW) ? p.sample_count : AVG_WINDOW;
    for (uint8_t i = 0; i < count; i++) {
        sum += p.samples[i];
    }
    return sum / count;
}

static void add_sample(ProbeState& p, float ma) {
    p.samples[p.sample_idx] = ma;
    p.sample_idx = (p.sample_idx + 1) % AVG_WINDOW;
    if (p.sample_count < AVG_WINDOW) p.sample_count++;
    p.avg_ma = compute_average(p);
}

// --- Build read order: alternate A/B channels ---
// If we have A1,A2,A3,B1,B2 → order: A1,B1,A2,B2,A3
// This ensures we never read two probes on the same DC-DC back-to-back

static uint8_t s_read_order[MAX_PROBES];
static uint8_t s_read_count = 0;

static void build_read_order() {
    // Separate into A-channel and B-channel probes
    uint8_t a_probes[MAX_PROBES], b_probes[MAX_PROBES];
    uint8_t a_count = 0, b_count = 0;

    for (uint8_t i = 0; i < s_num_probes; i++) {
        if (s_probes[i].channel < 4) {
            a_probes[a_count++] = i;
        } else {
            b_probes[b_count++] = i;
        }
    }

    // Interleave: A, B, A, B, ...
    s_read_count = 0;
    uint8_t ai = 0, bi = 0;
    while (ai < a_count || bi < b_count) {
        if (ai < a_count) s_read_order[s_read_count++] = a_probes[ai++];
        if (bi < b_count) s_read_order[s_read_count++] = b_probes[bi++];
    }
}

// --- Read probes: prepare next, read current ---
static uint8_t s_order_idx = 0;
static bool s_first_round = true;

static void read_all_probes() {
    if (s_read_count == 0) return;

    uint8_t current = s_read_order[s_order_idx];
    uint8_t next_idx = (s_order_idx + 1) % s_read_count;
    uint8_t next = s_read_order[next_idx];

    // Turn ON analog+shunt for next probe
    input_config_set((Input)s_probes[next].channel, SW_ANALOG, true);
    input_config_set((Input)s_probes[next].channel, SW_SHUNT, true);

    // Read current probe (was turned on last cycle)
    if (!s_first_round) {
        adc_read_mv((AdcInput)s_probes[current].channel);  // discard (MUX settle)
        int32_t mv = adc_read_mv((AdcInput)s_probes[current].channel);
        float ma = mv / 200.0f;
        add_sample(s_probes[current], ma);
    }

    // Turn OFF current probe
    input_config_set((Input)s_probes[current].channel, SW_ANALOG, false);
    input_config_set((Input)s_probes[current].channel, SW_SHUNT, false);

    s_order_idx = next_idx;
    if (s_order_idx == 0) s_first_round = false;
}

// --- Task ---

static void cloudgauge_task(void* param) {
    (void)param;

    if (s_read_count == 0) {
        log_error("[cloudgauge] No probes configured, task suspending");
        vTaskSuspend(nullptr);
        return;
    }

    // Kick off: turn on first probe
    input_config_set((Input)s_probes[s_read_order[0]].channel, SW_ANALOG, true);
    input_config_set((Input)s_probes[s_read_order[0]].channel, SW_SHUNT, true);
    s_order_idx = 0;
    s_first_round = true;

    uint32_t push_counter = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
        read_all_probes();

        push_counter++;
        if (push_counter >= PUSH_INTERVAL_S) {
            push_counter = 0;
            if (web_socket::is_connected) {
                String json = cloudgauge_get_all();
                web_socket::sendMessage(json);
                log_info("[cloudgauge] Pushed to server");
            }
        }
    }
}

// --- Build JSON for a single probe ---

static void probe_to_json(JsonObject obj, const ProbeState& p) {
    obj["id"] = p.id;
    obj["input"] = p.input;
    obj["type"] = p.type;
    obj["ma"] = serialized(String(p.avg_ma, 2));
    obj["cm"] = serialized(String(ma_to_cm(p.avg_ma), 1));
    obj["liters"] = 0;  // TODO: LUT from config
}

// --- Public API ---

void cloudgauge_init() {
    s_num_probes = 0;

    // Read channel voltages from config
    Voltage volt_a = VOLTAGE_24V;  // default
    Voltage volt_b = VOLTAGE_24V;

    if (config::is_loaded) {
        int va = config::config["channels"]["A"]["voltage"] | 24;
        int vb = config::config["channels"]["B"]["voltage"] | 24;
        volt_a = voltage_from_config(va);
        volt_b = voltage_from_config(vb);
    }

    voltage_select_set_a(volt_a);
    voltage_select_set_b(volt_b);
    vTaskDelay(pdMS_TO_TICKS(20));

    // Parse probes from config
    if (config::is_loaded && !config::config["probes"].isNull()) {
        JsonArray probes = config::config["probes"].as<JsonArray>();

        for (JsonObject probe : probes) {
            if (s_num_probes >= MAX_PROBES) {
                log_error("[cloudgauge] Max %d probes, ignoring rest", MAX_PROBES);
                break;
            }

            const char* id = probe["id"] | "unknown";
            const char* type = probe["type"] | "unknown";
            const char* input = probe["input"] | "";

            const ProbeTypeDef* typeDef = find_probe_type(type);
            if (!typeDef) {
                log_error("[cloudgauge] Unknown probe type '%s' for '%s', skipping", type, id);
                continue;
            }

            int8_t ch = input_name_to_channel(input);
            if (ch < 0) {
                log_error("[cloudgauge] Invalid input '%s' for probe '%s', skipping", input, id);
                continue;
            }

            // Validate voltage: check if channel voltage matches probe requirement
            Voltage chan_volt = (ch < 4) ? volt_a : volt_b;
            if (typeDef->required_voltage != VOLTAGE_OFF && chan_volt != typeDef->required_voltage) {
                log_error("[cloudgauge] Probe '%s' needs %dV but channel %c is set to %dV, skipping",
                    id,
                    (typeDef->required_voltage == VOLTAGE_5V) ? 5 : (typeDef->required_voltage == VOLTAGE_12V) ? 12 : 24,
                    (ch < 4) ? 'A' : 'B',
                    (chan_volt == VOLTAGE_5V) ? 5 : (chan_volt == VOLTAGE_12V) ? 12 : 24);
                continue;
            }

            // Store probe state
            ProbeState& p = s_probes[s_num_probes];
            strncpy(p.id, id, sizeof(p.id) - 1);
            p.id[sizeof(p.id) - 1] = '\0';
            strncpy(p.input, input, sizeof(p.input) - 1);
            p.input[sizeof(p.input) - 1] = '\0';
            strncpy(p.type, type, sizeof(p.type) - 1);
            p.type[sizeof(p.type) - 1] = '\0';
            p.channel = (uint8_t)ch;
            p.sample_idx = 0;
            p.sample_count = 0;
            p.avg_ma = 0.0f;
            p.valid = true;

            // Run type-specific hardware setup
            typeDef->setup(p.channel);

            s_num_probes++;
            log_info("[cloudgauge] Probe '%s' (%s) on %s, channel %d", p.id, p.type, p.input, p.channel);
        }
    } else {
        log_error("[cloudgauge] No config or no probes array — nothing to do");
    }

    // Build alternating read order
    build_read_order();

    Serial.printf("[cloudgauge] Initialized, %d probes, A=%dV B=%dV\n",
        s_num_probes,
        (volt_a == VOLTAGE_5V) ? 5 : (volt_a == VOLTAGE_12V) ? 12 : 24,
        (volt_b == VOLTAGE_5V) ? 5 : (volt_b == VOLTAGE_12V) ? 12 : 24);
}

void cloudgauge_start_task() {
    xTaskCreate(cloudgauge_task, "cloudgauge", 4096, nullptr, 2, &s_task_handle);
    task_register(s_task_handle, "cloudgauge", 2, 4096);
}

void cloudgauge_stop() {
    if (s_task_handle) vTaskSuspend(s_task_handle);
}

void cloudgauge_start() {
    if (s_task_handle) vTaskResume(s_task_handle);
}

String cloudgauge_get_all() {
    JsonDocument doc;
    doc["product"] = "cloudgauge2";
    
    // Add site name from config
    if (config::is_loaded && !config::config["name"].isNull()) {
        doc["name"] = config::config["name"];
    }
    
    JsonArray probes = doc["probes"].to<JsonArray>();

    for (uint8_t i = 0; i < s_num_probes; i++) {
        JsonObject obj = probes.add<JsonObject>();
        probe_to_json(obj, s_probes[i]);
    }

    String result;
    serializeJson(doc, result);
    return result;
}

String cloudgauge_get(const char* input_name) {
    for (uint8_t i = 0; i < s_num_probes; i++) {
        if (strcmp(s_probes[i].input, input_name) == 0) {
            JsonDocument doc;
            JsonObject obj = doc.to<JsonObject>();
            probe_to_json(obj, s_probes[i]);
            String result;
            serializeJson(doc, result);
            return result;
        }
    }
    return "{\"error\":\"probe not found\"}";
}
