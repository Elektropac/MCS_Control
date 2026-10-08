// =======================================================
// Pump / fueling controller (config-driven)
// =======================================================
#include "pump.h"
#include "hardware/relays.h"
#include "hardware/input_config.h"
#include "platform/sampler.h"
#include "platform/task_registry.h"
#include "apps/rule_engine/rule_engine.h"
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
    bool prev_pumping;        // engine state was "pumping" last tick (for reset)
    bool valid;

    // --- Pulskvalitets-analyse (additivt; beregnes i sample_pump) ---
    // Hvert flanke-tidsstempel (sampler sample_index @ 250µs) bruges til at
    // måle intervallet dt mellem pulser. Fra dt-strømmen udledes 5 fejltyper.
    uint32_t last_pulse_idx;  // sample_index for forrige puls (0 = ingen endnu)
    float    dt_mean;         // løbende middel af dt (i ticks), EWMA
    float    dt_var;          // løbende varians-estimat (til jitter CV)
    uint32_t q_jitter;        // tæller: intervaller med stor afvigelse
    uint32_t q_dropout;       // tæller: huller (dt >> middel)
    uint32_t q_bounce;        // tæller: for korte intervaller (prel)
    uint32_t q_ghost;         // tæller: pulser mens ikke-pumping (uautoriseret)
    uint32_t q_window;        // antal pulser i aktuelt kvalitets-vindue
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
        // Hardware verified (0.3.29, Jesper): with MODE_DIGITAL_PU a FLOATING
        // input reads bit 0, and tying the input to GND reads bit 1. On this
        // board the nozzle switch pulls the input to GND when lifted, so
        // GND = LIFTED = bit 1. (The start rule already fires on the rising
        // edge float->GND, which is why the relay engaged correctly; only this
        // status field was inverted on the web page.)
        p.nozzle_lifted = (raw == 1);
    }

    // Pulse: count falling edges on the pulse channel.
    // Pulse: count on the pulse channel. The input goes through an opto-
    // coupler that inverts the (pull-up, active-low) electrical pulse, so the
    // sampler sees a pulse as a RISING edge (0 -> 1). Count ALWAYS, not gated
    // on the relay: pulses that arrive with the pump OFF are what the flow-
    // guard rule needs to detect (unauthorised flow). Verified on hardware.
    if (p.pulse_ch >= 0) {
        SamplerEvent evt;
        while (sampler_read_next(p.sampler_cursor, evt)) {
            uint8_t bit = (evt.state >> p.pulse_ch) & 0x01;
            if (p.prev_bit == 0 && bit == 1) {
                p.pulse_count++;

                // --- Pulskvalitets-måling på denne stigende flanke ---
                // GHOST: en puls mens pumpen ikke er autoriseret (dyse nede
                // eller relæ fra) = uautoriseret/spøgelses-flow.
                bool authorised = p.nozzle_lifted && p.relay_on;
                if (!authorised) {
                    p.q_ghost++;
                } else if (p.last_pulse_idx != 0) {
                    // dt i ticks (250µs) mellem denne og forrige puls.
                    uint32_t dt = evt.sample_index - p.last_pulse_idx;
                    if (p.dt_mean <= 0.0f) {
                        // Første gyldige interval: initialisér middel.
                        p.dt_mean = (float)dt;
                        p.dt_var  = 0.0f;
                    } else {
                        float m = p.dt_mean;
                        // BOUNCE/PREL: fysisk umuligt kort interval (< 2 ticks
                        // = 500µs) ELLER << halvdelen af middel = dobbelttælling.
                        if (dt < 2 || (float)dt < m * 0.4f) {
                            p.q_bounce++;
                        }
                        // DROPOUT: hul væsentligt større end middel = tabt puls.
                        else if ((float)dt > m * 2.5f) {
                            p.q_dropout++;
                        }
                        // JITTER: moderat afvigelse fra middel (uregelmæssig).
                        else if ((float)dt > m * 1.4f || (float)dt < m * 0.7f) {
                            p.q_jitter++;
                        }
                        // Opdatér løbende middel + varians (EWMA, alpha=1/8).
                        float d = (float)dt - m;
                        p.dt_mean = m + d * 0.125f;
                        p.dt_var  = p.dt_var + (d * d - p.dt_var) * 0.125f;
                    }
                    p.q_window++;
                }
                p.last_pulse_idx = evt.sample_index;
            }
            p.prev_bit = bit;
        }
        // Keep the status relay field in sync with the actual relay.
        if (p.has_relay) p.relay_on = relay_get(p.relay);
    }
}

// --- Set a pump's relay ---
static void pump_set_relay(PumpState& p, bool on) {
    if (!p.has_relay) return;
    p.relay_on = on;
    relay_set(p.relay, on);
}

// --- JSON for one pump ---
// Shows the RAW per-pulse counter (full resolution: every pulse, litres with
// decimals) — this is what worked before. The transaction STATE comes from the
// rule engine (it owns start/stop/timeout), but the live pulse/litre display
// uses pump.cpp's own sampler counter so you see 1,2,3… and 0.1,0.2 L, not the
// engine's whole-litre KV steps.
static void pump_to_json(JsonObject obj, const PumpState& p) {
    char key[40];
    snprintf(key, sizeof(key), "%s.state", p.id);
    String eng_state = rule_kv_get(key, "idle");

    obj["id"] = p.id;
    obj["name"] = p.name;
    obj["nozzle_input"] = p.nozzle_input;
    obj["pulse_input"] = p.pulse_input;
    obj["nozzle_lifted"] = p.nozzle_lifted;
    obj["relay_on"] = p.relay_on;
    obj["state"] = eng_state;                       // idle/pumping/done (engine)
    obj["pulses"] = p.pulse_count;                  // RAW pulses (full resolution)
    obj["pulses_per_litre"] = serialized(String(p.pulses_per_litre, 2));
    obj["litres"] = serialized(String(pump_litres(p), 2));   // raw/ppl, decimals
    obj["preset_litres"] = serialized(String(p.preset_litres, 1));

    // --- Pulskvalitet (Fase 1: kun visning, ingen alarm) ---
    // Jitter som variationskoefficient: std(dt)/middel(dt) i procent.
    float cv = 0.0f;
    if (p.dt_mean > 0.0f && p.dt_var > 0.0f) {
        cv = (sqrtf(p.dt_var) / p.dt_mean) * 100.0f;
    }
    // Samlet dom: "god" medmindre en fejltype har mærkbare tællinger.
    const char* verdict = "god";
    if      (p.q_ghost   > 0)  verdict = "spøgelsespulser";
    else if (p.q_bounce  > 2)  verdict = "prel";
    else if (p.q_dropout > 2)  verdict = "dropout";
    else if (p.q_jitter  > 4 || cv > 20.0f) verdict = "jitter";
    JsonObject q = obj["quality"].to<JsonObject>();
    q["verdict"]  = verdict;
    q["cv_pct"]   = serialized(String(cv, 1));   // jitter-mål
    q["jitter"]   = p.q_jitter;
    q["dropout"]  = p.q_dropout;
    q["bounce"]   = p.q_bounce;
    q["ghost"]    = p.q_ghost;
    q["window"]   = p.q_window;                  // antal målte intervaller
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

            // Reset the display pulse counter at the START of a transaction
            // (engine state enters "pumping"), so each fueling shows from 0.
            // The rule engine owns start/stop/preset/timeouts/flow-guard; this
            // task only SAMPLES pulses for the web/OLED status display.
            PumpState& p = s_pumps[i];
            char key[40];
            snprintf(key, sizeof(key), "%s.state", p.id);
            bool pumping_now = (rule_kv_get(key, "idle") == "pumping");
            if (pumping_now && !p.prev_pumping) {
                p.pulse_count = 0;   // new transaction -> fresh count
                // Nulstil pulskvalitets-analyse for ny transaktion.
                p.last_pulse_idx = 0;
                p.dt_mean = 0.0f; p.dt_var = 0.0f;
                p.q_jitter = p.q_dropout = p.q_bounce = p.q_ghost = p.q_window = 0;
            }
            p.prev_pumping = pumping_now;
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
        // Seed prev_bit from the ACTUAL current pulse-pin level (idle is low on
        // this board via the opto-coupler), so the first real pulse isn't lost
        // or miscounted by assuming the wrong idle level.
        p.prev_bit = (p.pulse_ch >= 0) ? ((sampler_current_state() >> p.pulse_ch) & 0x01) : 0;
        p.valid = true;

        // Configure inputs: nozzle + pulse as DIGITAL with pull-up. Both the
        // digital switch AND the pull-up must be on (MODE_DIGITAL_PU) for the
        // pin to reach the digital read path — a bare pull-up is not enough.
        if (p.nozzle_ch >= 0) {
            input_config_mode((Input)p.nozzle_ch, MODE_DIGITAL_PU);
        }
        if (p.pulse_ch >= 0) {
            input_config_mode((Input)p.pulse_ch, MODE_DIGITAL_PU);
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
