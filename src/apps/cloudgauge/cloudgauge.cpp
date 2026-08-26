// =======================================================
// CloudGauge — probe/tank level monitoring
// =======================================================
// Reads 4-20mA probes, averages over 10 samples, converts to cm and liters.
// Channels A and B read in parallel, one input at a time per channel
// to limit current draw.
//
// For now: 8 probes hardcoded, 4-20mA → 0-300 cm, no liter conversion.
// Future: reads config for probe list, conversion tables, push interval.
// =======================================================

#include "cloudgauge.h"
#include "hardware/adc.h"
#include "hardware/input_config.h"
#include "hardware/voltage_select.h"
#include "platform/task_registry.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// --- Configuration (hardcoded for now) ---
#define NUM_PROBES          8
#define SAMPLE_INTERVAL_MS  1000
#define AVG_WINDOW          10
#define SETTLE_TIME_MS      50
#define PROBE_RANGE_CM      300.0f
#define MA_MIN              4.0f
#define MA_MAX              20.0f

// --- Per-probe state ---
struct ProbeState {
    const char* id;             // probe identifier (from config later)
    const char* input;          // physical input name ("A1", "A2", etc.)
    uint8_t channel;            // ADC channel 0-7
    float samples[AVG_WINDOW];  // ring buffer of mA readings
    uint8_t sample_idx;         // current position in ring buffer
    uint8_t sample_count;       // how many valid samples (0 to AVG_WINDOW)
    float avg_ma;               // current averaged mA value
};

static ProbeState s_probes[NUM_PROBES] = {
    {"A1", "A1", 0, {}, 0, 0, 0.0f},
    {"A2", "A2", 1, {}, 0, 0, 0.0f},
    {"A3", "A3", 2, {}, 0, 0, 0.0f},
    {"A4", "A4", 3, {}, 0, 0, 0.0f},
    {"B1", "B1", 4, {}, 0, 0, 0.0f},
    {"B2", "B2", 5, {}, 0, 0, 0.0f},
    {"B3", "B3", 6, {}, 0, 0, 0.0f},
    {"B4", "B4", 7, {}, 0, 0, 0.0f},
};

static TaskHandle_t s_task_handle = nullptr;

// --- Helpers ---

static float ma_to_cm(float ma) {
    if (ma <= MA_MIN) return 0.0f;
    if (ma >= MA_MAX) return PROBE_RANGE_CM;
    return ((ma - MA_MIN) / (MA_MAX - MA_MIN)) * PROBE_RANGE_CM;
}

static float ma_to_liters(float ma) {
    // TODO: implement when config has conversion table
    // For now just return 0
    (void)ma;
    return 0.0f;
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

// --- Read probes: prepare next, read current ---
// Each second: turn on shunt for NEXT probe, read CURRENT probe (settled for 1s)

static uint8_t s_current_probe = 0;
static bool s_first_round = true;

static void read_all_probes() {
    // Calculate next probe index
    uint8_t next_probe = (s_current_probe + 1) % NUM_PROBES;

    // Turn ON shunt for next probe (will be read next cycle)
    input_config_set((Input)next_probe, SW_SHUNT, true);

    // Read current probe (shunt was turned on last cycle — 1s settle time)
    if (!s_first_round) {
        adc_read_mv((AdcInput)s_current_probe);  // discard (MUX settle)
        int32_t mv = adc_read_mv((AdcInput)s_current_probe);  // keep
        float ma = mv / 200.0f;
        add_sample(s_probes[s_current_probe], ma);
    }

    // Turn OFF shunt on current probe (done reading)
    input_config_set((Input)s_current_probe, SW_SHUNT, false);

    // Advance
    s_current_probe = next_probe;

    // After first full round, we have valid data
    if (s_current_probe == 0) s_first_round = false;
}

// --- Task: one probe per second ---

static void cloudgauge_task(void* param) {
    (void)param;

    // Kick off: turn on shunt for first probe
    input_config_set((Input)0, SW_SHUNT, true);
    s_current_probe = 0;
    s_first_round = true;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
        read_all_probes();
    }
}

// --- Build JSON for a single probe ---

static void probe_to_json(JsonObject obj, const ProbeState& p) {
    obj["id"] = p.id;
    obj["input"] = p.input;
    obj["ma"] = serialized(String(p.avg_ma, 2));
    obj["cm"] = serialized(String(ma_to_cm(p.avg_ma), 1));
    obj["liters"] = (int)ma_to_liters(p.avg_ma);
}

// --- Public API ---

void cloudgauge_init() {
    // Set both channels to 24V (probes need it)
    voltage_select_set_a(VOLTAGE_24V);
    voltage_select_set_b(VOLTAGE_24V);
    vTaskDelay(pdMS_TO_TICKS(20));

    // Configure all 8 inputs: analog ON (always), shunt OFF (toggled per read)
    for (uint8_t i = 0; i < 8; i++) {
        input_config_set((Input)i, SW_ANALOG, true);
        input_config_set((Input)i, SW_PULLUP, false);
        input_config_set((Input)i, SW_SHUNT, false);
        input_config_set((Input)i, SW_DIGITAL, false);
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    Serial.println("[cloudgauge] Initialized, 8 probes @ 24V, shunt toggled per read, 1 Hz");
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
    JsonArray probes = doc["probes"].to<JsonArray>();

    for (uint8_t i = 0; i < NUM_PROBES; i++) {
        JsonObject obj = probes.add<JsonObject>();
        probe_to_json(obj, s_probes[i]);
    }

    String result;
    serializeJson(doc, result);
    return result;
}

String cloudgauge_get(const char* input_name) {
    for (uint8_t i = 0; i < NUM_PROBES; i++) {
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
