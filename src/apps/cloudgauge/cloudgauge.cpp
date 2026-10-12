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
#include "buzzer.h"
#include <strings.h>   // strcasecmp
// Forward-declare config namespace (defined in config.h, included by network_task)
// We only read config::config and config::is_loaded — no need to re-include the header
namespace config {
    extern bool is_loaded;
    extern JsonDocument config;
    bool config_read_groups(JsonDocument& out);
}

#include "logging.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "queues.h"

// Forward declaration — web_socket lives in a header-only namespace
namespace web_socket {
    extern bool is_connected;
    void sendMessage(const String &message);
}

// --- Constants ---
#define MAX_PROBES          8
#define SAMPLE_INTERVAL_MS  1000
#define AVG_WINDOW          3    // rolling average length (was 10 ~10s; 3 = ~3x
                                 // faster response, accepts a little more noise)
#define PUSH_INTERVAL_S     60

// --- Probe type definitions ---
// Maps MCS type names to hardware requirements and conversion

struct ProbeTypeDef {
    const char* type_name;
    uint8_t input_count;        // how many inputs this type uses
    Voltage required_voltage;
    float default_range_mm;     // default measurement range in mm
    float input_min;            // signal floor (e.g. 4.0 mA or 0.0 V)
    float input_max;            // signal ceiling (e.g. 20.0 mA or 5.0 V)
    const char* input_unit;     // "mA" or "V"
    // Hardware setup function called at init
    void (*setup)(uint8_t channel);
};

// mcs_level: 4-20mA, 1 input, needs 24V, analog+shunt
static void setup_mcs_level(uint8_t channel) {
    input_config_set((Input)channel, SW_ANALOG, false);
    input_config_set((Input)channel, SW_SHUNT, false);
    input_config_set((Input)channel, SW_PULLUP, false);
    input_config_set((Input)channel, SW_DIGITAL, false);
}

// mcs_level_temp: 0-5V, 2 inputs, needs 5V, analog only (no shunt)
static void setup_mcs_level_temp(uint8_t channel) {
    input_config_set((Input)channel, SW_ANALOG, false);
    input_config_set((Input)channel, SW_SHUNT, false);
    input_config_set((Input)channel, SW_PULLUP, false);
    input_config_set((Input)channel, SW_DIGITAL, false);
}

static const ProbeTypeDef PROBE_TYPES[] = {
    //  type_name          inputs  voltage       range    min    max    unit   setup
    { "mcs_level",           1,   VOLTAGE_24V,  3000.0f, 4.0f,  20.0f, "mA",  setup_mcs_level },
    { "mcs_level_temp",      2,   VOLTAGE_5V,   3000.0f, 0.0f,   5.0f, "V",   setup_mcs_level_temp },
};
static const uint8_t NUM_PROBE_TYPES = sizeof(PROBE_TYPES) / sizeof(PROBE_TYPES[0]);

// --- Per-probe state ---
#define MAX_LUT_POINTS 10

struct LutPoint {
    float mm;
    float liters;
};

struct ProbeState {
    char id[16];                // from config (e.g. "tank_1")
    char group_id[24];          // config group id (for on-demand rules lookup)
    char input[4];              // physical input name ("A1", "B3")
    char type[20];              // MCS type name
    char media[16];             // liquid name ("Diesel", "AdBlue", "Water", ...)
    uint8_t channel;            // ADC/input channel index 0-7
    const ProbeTypeDef* type_def; // pointer to type definition
    float range_mm;             // measurement range in mm (from config or type default)
    float density;              // liquid density (1.0=water, 0.84=diesel, 0.75=petrol)
    float low_level;            // low-level alarm threshold (in alarm_unit; 0 = off)
    float overfill;             // overfill alarm threshold (in alarm_unit; 0 = off)
    char alarm_unit[4];         // "%", "mm", or "L" — unit the thresholds are in
    LutPoint lut[MAX_LUT_POINTS]; // cm → liter lookup table
    uint8_t lut_count;          // number of valid LUT entries
    float samples[AVG_WINDOW];  // ring buffer of mA readings
    uint8_t sample_idx;
    uint8_t sample_count;
    float avg_ma;
    float raw_ma;               // latest UNsmoothed reading (fast trend detect)
    bool valid;                 // true if config parsed OK
    char last_state[12];        // previous alarm state (for rule edge detection)
    uint32_t last_timer_ms;     // last time interval-timer rules ran (millis)
    // --- Alarm latch: an alarm fires the moment the level crosses a threshold
    // and stays active for ALARM_HOLD_MS, then auto-clears even if still past
    // the threshold (a full tank is normal — no need to nag). It won't re-fire
    // until the level has returned to the normal band and crosses again. ---
    bool     over_armed;        // true when below overfill (ready to fire)
    uint32_t over_alarm_ms;     // millis() when overfill alarm started (0 = off)
    bool     low_armed;         // true when above low (ready to fire)
    uint32_t low_alarm_ms;      // millis() when low alarm started (0 = off)
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

// --- Map media name to density (kg/L) ---
// Mirrors the Setup Engine CG2_KNOWN_DENSITY table. "Custom" (or any
// unknown/empty media) returns <= 0 so the caller falls back to the
// density value from config.
static float media_to_density(const char* media) {
    if (!media || !media[0]) return -1.0f;
    if (strcasecmp(media, "Diesel") == 0) return 0.84f;
    if (strcasecmp(media, "AdBlue") == 0) return 1.09f;
    if (strcasecmp(media, "Water")  == 0) return 1.00f;
    return -1.0f;   // Custom / unknown -> fall back to config density
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

// Convert a given mA reading to a density-compensated mm level.
static float ma_to_mm(const ProbeState& p, float val) {
    if (!p.type_def) return 0.0f;
    float min = p.type_def->input_min;
    float max = p.type_def->input_max;
    if (val <= min) return 0.0f;
    if (val >= max) return p.range_mm;
    float raw_mm = ((val - min) / (max - min)) * p.range_mm;
    // Compensate for liquid density: probe measures pressure (water-calibrated)
    // Lighter liquid (diesel 0.84) produces less pressure → probe reads low
    // Actual mm = raw_mm / density
    return raw_mm / p.density;
}

// Smoothed level (for display + alarm thresholds).
static float input_to_mm(const ProbeState& p) {
    return ma_to_mm(p, p.avg_ma);
}

// Raw/unsmoothed level (for trend direction — reacts immediately, no 10s lag).
static float input_to_mm_raw(const ProbeState& p) {
    return ma_to_mm(p, p.raw_ma);
}

// Linear interpolation in LUT (mm → liters)
static float mm_to_liters(const ProbeState& p, float mm) {
    if (p.lut_count == 0) return 0.0f;
    if (p.lut_count == 1) return p.lut[0].liters;
    
    // Below first point
    if (mm <= p.lut[0].mm) return p.lut[0].liters;
    // Above last point
    if (mm >= p.lut[p.lut_count - 1].mm) return p.lut[p.lut_count - 1].liters;
    
    // Find the two surrounding points and interpolate
    for (uint8_t i = 0; i < p.lut_count - 1; i++) {
        if (mm >= p.lut[i].mm && mm <= p.lut[i + 1].mm) {
            float t = (mm - p.lut[i].mm) / (p.lut[i + 1].mm - p.lut[i].mm);
            return p.lut[i].liters + t * (p.lut[i + 1].liters - p.lut[i].liters);
        }
    }
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
    p.raw_ma = ma;              // keep the unsmoothed value for trend detection
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

static void evaluate_probe_rules(ProbeState& p);   // defined below
static void update_alarm_latch(ProbeState& p, float level); // defined below
static float current_level(const ProbeState& p);  // defined below

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

        // Update the alarm latch for each probe (fires on threshold crossing,
        // holds 5 min, auto-clears, re-arms when back in the normal band).
        // The latch is MEASUREMENT-side state (drives current_state), so it
        // stays here. The rule engine reads that state read-only.
        for (uint8_t i = 0; i < s_num_probes; i++) {
            if (s_probes[i].valid) update_alarm_latch(s_probes[i], current_level(s_probes[i]));
        }

        // NOTE: probe rule evaluation has moved to the shared rule engine
        // (src/apps/rule_engine). The engine reads each probe's alarm state
        // via cloudgauge_group_state() and runs the configured rules there,
        // so probe + pump share ONE engine. Measurement stays in this file.
        // (Was: evaluate_probe_rules(s_probes[i]) for each probe.)

        push_counter++;
        if (web_socket::is_connected && push_counter >= PUSH_INTERVAL_S) {
            push_counter = 0;

            for (uint8_t i = 0; i < s_num_probes; i++) {
                if (!s_probes[i].valid) continue;

                queues::ProbeSample sample;
                sample.id = s_probes[i].id;
                sample.ma = s_probes[i].raw_ma;
                sample.mm = input_to_mm(s_probes[i]);
                queues::send_probe_sample(sample);
            }
        }
    }
}

// --- Level as percentage of the tank (0..100) ---
// Uses liters/capacity when a LUT is present, otherwise mm/range.
static float level_percent(const ProbeState& p, float mm, float liters) {
    float capacity = (p.lut_count > 0) ? p.lut[p.lut_count - 1].liters : 0.0f;
    if (capacity > 0.0f) return (liters / capacity) * 100.0f;
    if (p.range_mm > 0.0f) return (mm / p.range_mm) * 100.0f;
    return 0.0f;
}

// --- Current level expressed in the probe's alarm_unit ---
// "%" -> percentage, "mm" -> millimetres, "L" -> litres.
static float level_in_unit(const ProbeState& p, float mm, float liters, float pct) {
    if (strcmp(p.alarm_unit, "mm") == 0) return mm;
    if (strcmp(p.alarm_unit, "L") == 0)  return liters;
    return pct;   // default "%"
}

// --- Detect a disconnected / faulty probe ---
// A current-loop probe (input_min > 0, e.g. 4-20 mA) reading well below its
// floor means the loop is broken / no probe attached — NOT "tank empty". We
// treat that as a fault so level alarms don't fire on a missing probe.
// Voltage probes (input_min == 0) have no such floor, so never fault here.
static bool probe_is_fault(const ProbeState& p) {
    if (!p.type_def) return false;
    if (p.type_def->input_min <= 0.0f) return false;      // voltage type: no fault floor
    float fault_floor = p.type_def->input_min - 0.5f;     // e.g. 3.5 mA for a 4 mA floor
    return p.avg_ma < fault_floor;
}

// --- Alarm latch -----------------------------------------------------------
// An alarm fires the instant the level crosses a threshold, holds for
// ALARM_HOLD_MS, then auto-clears (a full/empty tank sitting still is normal).
// It re-arms only when the level returns to the normal band, so a full tank
// doesn't re-trigger every few minutes. Called once per cycle with the current
// level (in alarm_unit). Fault is handled separately (always shown).
static const uint32_t ALARM_HOLD_MS = 5UL * 60UL * 1000UL;   // 5 minutes

static void update_alarm_latch(ProbeState& p, float level) {
    uint32_t now = millis();
    if (now == 0) now = 1;   // avoid 0 meaning "off"

    // --- Overfill ---
    if (p.overfill > 0.0f) {
        if (level >= p.overfill) {
            if (p.over_armed) {               // just crossed up -> fire
                p.over_alarm_ms = now;
                p.over_armed = false;
            } else if (p.over_alarm_ms && now - p.over_alarm_ms >= ALARM_HOLD_MS) {
                p.over_alarm_ms = 0;          // held long enough -> auto-clear
            }
        } else {
            p.over_armed = true;              // back in normal band -> re-arm
            p.over_alarm_ms = 0;
        }
    }

    // --- Low level ---
    if (p.low_level > 0.0f) {
        if (level <= p.low_level) {
            if (p.low_armed) {                // just crossed down -> fire
                p.low_alarm_ms = now;
                p.low_armed = false;
            } else if (p.low_alarm_ms && now - p.low_alarm_ms >= ALARM_HOLD_MS) {
                p.low_alarm_ms = 0;
            }
        } else {
            p.low_armed = true;
            p.low_alarm_ms = 0;
        }
    }
}

// --- Derive alarm state from the latch: "fault" | "overfill" | "low_level"
// | "normal". Fault wins, then an active (within-hold) overfill/low latch. ---
static const char* probe_state(const ProbeState& p, float /*level*/) {
    if (probe_is_fault(p))      return "fault";
    if (p.over_alarm_ms != 0)   return "overfill";
    if (p.low_alarm_ms  != 0)   return "low_level";
    return "normal";
}

// --- Build JSON for a single probe ---

static void probe_to_json(JsonObject obj, const ProbeState& p) {
    float mm = input_to_mm(p);
    float liters = mm_to_liters(p, mm);
    float capacity = (p.lut_count > 0) ? p.lut[p.lut_count - 1].liters : 0.0f;
    float pct = level_percent(p, mm, liters);
    float level = level_in_unit(p, mm, liters, pct);
    const char* state = probe_state(p, level);

    obj["id"] = p.id;
    obj["input"] = p.input;
    obj["type"] = p.type;
    obj["media"] = p.media;
    obj["ma"] = serialized(String(p.avg_ma, 2));
    obj["mm"] = (int)mm;
    obj["range_mm"] = (int)p.range_mm;
    obj["liters"] = (int)liters;
    obj["capacity"] = (int)capacity;
    obj["has_lut"] = p.lut_count > 0;
    obj["density"] = serialized(String(p.density, 2));
    obj["pct"] = serialized(String(pct, 1));
    obj["alarm_unit"] = p.alarm_unit;
    obj["low_level"] = serialized(String(p.low_level, 1));
    obj["overfill"] = serialized(String(p.overfill, 1));
    obj["state"] = state;
    obj["low"] = (strcmp(state, "low_level") == 0);
    obj["overfill_alarm"] = (strcmp(state, "overfill") == 0);
    obj["fault"] = (strcmp(state, "fault") == 0);
}

// =======================================================
// Rule engine — per-probe rules from the config groups
// =======================================================
// Triggers:
//   domain "probe" + type low_level/overfill  -> fires when the probe's
//     alarm state ENTERS that state (edge, not every tick)
//   domain "timer" + interval <seconds>       -> fires periodically
// Actions: buzzer (local), read (trigger a fresh sample log), notify
//   (queued/logged — actual delivery needs the server link).
// Rules are read on-demand per group (config_read_group_rules).

namespace config {
    bool config_read_group_rules(const char* group_id, JsonDocument& out);
}

// Current level in the probe's alarm_unit (for the alarm latch).
static float current_level(const ProbeState& p) {
    float mm = input_to_mm(p);
    float liters = mm_to_liters(p, mm);
    float pct = level_percent(p, mm, liters);
    return level_in_unit(p, mm, liters, pct);
}

// Current alarm state string for a probe (same math as probe_to_json).
static const char* current_state(const ProbeState& p) {
    float mm = input_to_mm(p);
    float liters = mm_to_liters(p, mm);
    float pct = level_percent(p, mm, liters);
    float level = level_in_unit(p, mm, liters, pct);
    return probe_state(p, level);
}

// Execute one action object { do: ..., ... } for a probe.
static void run_action(const ProbeState& p, JsonObject action) {
    const char* what = action["do"] | "";

    if (strcmp(what, "buzzer") == 0) {
        const char* sound = action["sound"] | "warning";
        if (strcmp(sound, "ok") == 0)            buzzer_sound_ok();
        else if (strcmp(sound, "error") == 0)    buzzer_sound_error();
        else if (strcmp(sound, "complete") == 0) buzzer_sound_complete();
        else                                     buzzer_sound_warning();
        log_info("[rules] %s: buzzer '%s'", p.id, sound);

    } else if (strcmp(what, "read") == 0) {
        // A fresh reading is already maintained by the task; log current value.
        log_info("[rules] %s: read -> %.2f mA", p.id, p.avg_ma);

    } else if (strcmp(what, "notify") == 0) {
        const char* msg = action["message"] | "";
        const char* level = action["level"] | "info";
        const char* channel = action["channel"] | "";
        // Delivery goes over the server link when connected; otherwise logged.
        if (web_socket::is_connected) {
            JsonDocument n;
            n["subject"] = "notify";
            n["channel"] = channel;
            n["level"] = level;
            n["message"] = msg;
            String out; serializeJson(n, out);
            web_socket::sendMessage(out);
            log_info("[rules] %s: notify sent (%s) '%s'", p.id, level, msg);
        } else {
            log_info("[rules] %s: notify queued (offline, %s) '%s'", p.id, level, msg);
        }

    } else {
        log_info("[rules] %s: unknown action '%s'", p.id, what);
    }
}

// Evaluate all rules for one probe. Called once per task cycle.
static void evaluate_probe_rules(ProbeState& p) {
    const char* state = current_state(p);
    bool state_changed = (strcmp(state, p.last_state) != 0);
    uint32_t now = millis();

    // Load this probe's rules on demand (one group at a time).
    JsonDocument rulesDoc;
    if (config::config_read_group_rules(p.group_id, rulesDoc)) {
        for (JsonObject rule : rulesDoc.as<JsonArray>()) {
            if (!(rule["enabled"] | true)) continue;
            JsonObject trig = rule["trigger"].as<JsonObject>();
            const char* domain = trig["domain"] | "";
            bool fire = false;

            if (strcmp(domain, "probe") == 0) {
                // Fire only on the edge INTO the matching state.
                const char* ttype = trig["type"] | "";
                if (state_changed && strcmp(ttype, state) == 0) fire = true;

            } else if (strcmp(domain, "timer") == 0) {
                uint32_t interval_ms = (uint32_t)(trig["value"] | 0) * 1000UL;
                // Fire on first run and every interval thereafter.
                if (interval_ms > 0 &&
                    (p.last_timer_ms == 0 || (now - p.last_timer_ms) >= interval_ms)) {
                    fire = true;
                    p.last_timer_ms = now;
                }
            }

            if (fire) {
                for (JsonObject a : rule["actions"].as<JsonArray>()) {
                    run_action(p, a);
                }
            }
        }
    }

    // Remember the state for next-cycle edge detection.
    if (state_changed) {
        strncpy(p.last_state, state, sizeof(p.last_state) - 1);
        p.last_state[sizeof(p.last_state) - 1] = '\0';
    }
}

// --- Public API ---

void cloudgauge_init() {
    s_num_probes = 0;

    // Channel voltages are set at device level by channels_setup() before we
    // run. Here we only READ them (to validate each probe's requirement).
    Voltage volt_a = VOLTAGE_24V;  // default
    Voltage volt_b = VOLTAGE_24V;

    if (config::is_loaded) {
        // New format nests channels under "config"; fall back to top-level.
        JsonVariant ch = config::config["config"]["channels"];
        if (ch.isNull()) ch = config::config["channels"];
        int va = ch["A"]["voltage"] | 24;
        int vb = ch["B"]["voltage"] | 24;
        volt_a = voltage_from_config(va);
        volt_b = voltage_from_config(vb);
    }

    // Parse probes from the new grouped config format.
    // Each probe is a group { type:"probe", probe:{ input, probe_type,
    // media, density, low_level, overfill, [lut] } }. rules[] are ignored
    // here (measurement only — alarms/rules handled separately).
    JsonDocument groupsDoc;
    if (config::config_read_groups(groupsDoc) && !groupsDoc["groups"].isNull()) {
        JsonArray groups = groupsDoc["groups"].as<JsonArray>();

        for (JsonObject group : groups) {
            const char* gtype = group["type"] | "";
            if (strcmp(gtype, "probe") != 0) continue;      // skip non-probe groups
            if (group["probe"].isNull()) continue;
            JsonObject probe = group["probe"].as<JsonObject>();

            if (s_num_probes >= MAX_PROBES) {
                log_error("[cloudgauge] Max %d probes, ignoring rest", MAX_PROBES);
                break;
            }

            // New field names: name, input, probe_type (was id, input, type).
            const char* id = probe["name"] | (group["id"] | "unknown");
            const char* type = probe["probe_type"] | "unknown";
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

            // Validate voltage: channel voltage must match the probe requirement
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
            const char* gid = group["id"] | "";
            strncpy(p.group_id, gid, sizeof(p.group_id) - 1);
            p.group_id[sizeof(p.group_id) - 1] = '\0';
            strncpy(p.input, input, sizeof(p.input) - 1);
            p.input[sizeof(p.input) - 1] = '\0';
            strncpy(p.type, type, sizeof(p.type) - 1);
            p.type[sizeof(p.type) - 1] = '\0';
            p.channel = (uint8_t)ch;
            p.type_def = typeDef;
            p.range_mm = probe["range_mm"] | typeDef->default_range_mm;

            // Media drives density (option B): look up density from the media
            // name; fall back to the config "density" value for Custom/unknown.
            const char* media = probe["media"] | "";
            strncpy(p.media, media, sizeof(p.media) - 1);
            p.media[sizeof(p.media) - 1] = '\0';

            float media_d = media_to_density(media);
            float cfg_d   = probe["density"] | 1.0f;
            float d = (media_d > 0.0f) ? media_d : cfg_d;
            p.density = (d > 0.1f && d <= 2.0f) ? d : 1.0f;  // sanity check
            if (media_d > 0.0f)
                log_info("[cloudgauge] Probe '%s' media '%s' -> density %.2f",
                         id, media, p.density);

            // Alarm thresholds (in alarm_unit; default "%"). 0 = alarm off.
            const char* unit = probe["alarm_unit"] | "%";
            strncpy(p.alarm_unit, unit, sizeof(p.alarm_unit) - 1);
            p.alarm_unit[sizeof(p.alarm_unit) - 1] = '\0';
            p.low_level = probe["low_level"] | 0.0f;
            p.overfill  = probe["overfill"]  | 0.0f;
            p.sample_idx = 0;
            p.sample_count = 0;
            p.avg_ma = 0.0f;
            p.raw_ma = 0.0f;
            p.valid = true;
            strcpy(p.last_state, "init");   // forces first evaluation
            p.last_timer_ms = 0;
            // Alarm latch starts armed (ready to fire on first crossing),
            // with no alarm active.
            p.over_armed = true;  p.over_alarm_ms = 0;
            p.low_armed  = true;  p.low_alarm_ms  = 0;

            // Parse LUT (mm vs liter) if present in the probe block
            p.lut_count = 0;
            if (!probe["lut"].isNull()) {
                JsonArray lut = probe["lut"].as<JsonArray>();
                for (JsonArray point : lut) {
                    if (p.lut_count >= MAX_LUT_POINTS) break;
                    p.lut[p.lut_count].mm = point[0].as<float>();
                    p.lut[p.lut_count].liters = point[1].as<float>();
                    p.lut_count++;
                }
                log_info("[cloudgauge] Probe '%s' LUT: %d points", p.id, p.lut_count);

                // When a LUT is present, the tank's full height is the last
                // LUT mm point, so use it as the measurement range (unless the
                // config explicitly set range_mm). This keeps the mA->mm scale
                // aligned with the physical tank instead of a fixed default.
                if (p.lut_count > 0 && probe["range_mm"].isNull()) {
                    float lut_max_mm = p.lut[p.lut_count - 1].mm;
                    if (lut_max_mm > 0.0f) {
                        p.range_mm = lut_max_mm;
                        log_info("[cloudgauge] Probe '%s' range from LUT: %d mm",
                                 p.id, (int)p.range_mm);
                    }
                }
            }

            // Run type-specific hardware setup
            typeDef->setup(p.channel);

            s_num_probes++;
            log_info("[cloudgauge] Probe '%s' (%s) on %s, channel %d", p.id, p.type, p.input, p.channel);
        }
    } else {
        log_error("[cloudgauge] No config or no probe groups — nothing to do");
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

// =======================================================
// Read-only accessors for the shared rule engine
// =======================================================
// The rule engine executes probe rules now (lifted out of this file). It
// needs to READ a probe group's current alarm state + level — WITHOUT
// touching any measurement code. These reuse the exact same math as
// probe_to_json()/evaluate_probe_rules() via current_state()/current_level(),
// so behaviour is identical. Lookup is by config group id.

// Current alarm state of a probe group: "normal"|"low_level"|"overfill"|
// "fault", or "" if the group id is unknown.
const char* cloudgauge_group_state(const char* group_id) {
    for (uint8_t i = 0; i < s_num_probes; i++) {
        if (s_probes[i].valid && strcmp(s_probes[i].group_id, group_id) == 0) {
            return current_state(s_probes[i]);
        }
    }
    return "";
}

// Current level of a probe group in its alarm_unit (%, mm or L).
// Returns 0 if the group id is unknown.
float cloudgauge_group_value(const char* group_id) {
    for (uint8_t i = 0; i < s_num_probes; i++) {
        if (s_probes[i].valid && strcmp(s_probes[i].group_id, group_id) == 0) {
            return current_level(s_probes[i]);
        }
    }
    return 0.0f;
}
