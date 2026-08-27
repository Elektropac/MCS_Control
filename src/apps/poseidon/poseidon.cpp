// =======================================================
// Poseidon — raw I/O gateway (config-driven)
// =======================================================
// No application logic — server controls everything.
// Each pin is configured from config as input or output.
// Analog/pulse inputs report periodically.
// =======================================================

#include "poseidon.h"
#include "hardware/adc.h"
#include "hardware/input_config.h"
#include "hardware/voltage_select.h"
#include "hardware/relays.h"
#include "platform/task_registry.h"
#include "platform/sampler.h"
#include "logging.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ArduinoJson.h>

// Forward-declare config namespace
namespace config {
    extern bool is_loaded;
    extern JsonDocument config;
}

// Forward-declare web_socket
namespace web_socket {
    extern bool is_connected;
    void sendMessage(const String &message);
}

// --- IO modes ---
enum IoMode : uint8_t {
    IO_DISABLED,
    IO_OUTPUT,
    IO_DIGITAL,
    IO_DIGITAL_PULLUP,    // digital with internal pullup
    IO_ANALOG_CURRENT,    // 4-20mA via shunt
    IO_ANALOG_VOLTAGE,    // 0-5V direct
    IO_PULSE,
    IO_RELAY,             // mechanical relay (A or B)
};

// --- Per-pin state ---
#define MAX_IO_PINS 8

struct IoPin {
    char pin[4];              // "A1", "B3" etc.
    char name[32];            // human-readable name
    IoMode mode;
    bool pullup;              // for digital inputs
    bool inverted;            // invert logic (opto-couplers: HIGH=off, LOW=on)
    uint16_t interval_s;      // reporting interval (0 = no periodic report)
    uint16_t debounce_ms;     // for pulse mode
    uint8_t channel;          // ADC/input channel index 0-7
    // Runtime state
    bool output_state;        // current output state (HIGH/LOW)
    float last_analog;        // last analog reading (mA or V)
    bool last_digital;        // last digital reading
    uint32_t last_report_ms;  // timestamp of last report
    uint32_t pulse_count;     // accumulated pulses
    uint32_t sampler_cursor;  // position in sampler ring buffer (for pulse counting)
    uint8_t prev_bit;         // previous bit state (for edge detection)
    uint32_t last_pulse_count; // pulse count at last rate calc
    uint32_t last_rate_ms;    // timestamp of last rate calc
    float pulse_hz;           // pulses per second
    bool valid;
};

static IoPin s_pins[MAX_IO_PINS];
static uint8_t s_num_pins = 0;
static TaskHandle_t s_task_handle = nullptr;

// --- Parse pin name to channel index ---
static int8_t pin_name_to_channel(const char* name) {
    if (!name || strlen(name) != 2) return -1;
    char ch = name[0];
    char num = name[1];
    if (num < '1' || num > '4') return -1;
    uint8_t idx = (num - '1');
    if (ch == 'A' || ch == 'a') return idx;
    if (ch == 'B' || ch == 'b') return idx + 4;
    return -1;
}

// --- Parse mode string ---
static IoMode parse_mode(const char* mode_str) {
    if (!mode_str) return IO_DISABLED;
    if (strcmp(mode_str, "output") == 0) return IO_OUTPUT;
    if (strcmp(mode_str, "digital") == 0) return IO_DIGITAL;
    if (strcmp(mode_str, "digital_pullup") == 0) return IO_DIGITAL_PULLUP;
    if (strcmp(mode_str, "analog_current") == 0) return IO_ANALOG_CURRENT;
    if (strcmp(mode_str, "analog_voltage") == 0) return IO_ANALOG_VOLTAGE;
    if (strcmp(mode_str, "pulse") == 0) return IO_PULSE;
    if (strcmp(mode_str, "relay") == 0) return IO_RELAY;
    return IO_DISABLED;
}

static const char* mode_to_string(IoMode m) {
    switch (m) {
        case IO_OUTPUT:         return "output";
        case IO_DIGITAL:        return "digital";
        case IO_DIGITAL_PULLUP: return "digital_pullup";
        case IO_ANALOG_CURRENT: return "analog_current";
        case IO_ANALOG_VOLTAGE: return "analog_voltage";
        case IO_PULSE:          return "pulse";
        case IO_RELAY:          return "relay";
        default:                return "disabled";
    }
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

// --- Read a pin's current value ---
static void read_pin(IoPin& p) {
    switch (p.mode) {
        case IO_ANALOG_CURRENT: {
            input_config_set((Input)p.channel, SW_ANALOG, true);
            input_config_set((Input)p.channel, SW_SHUNT, true);
            vTaskDelay(pdMS_TO_TICKS(20));
            adc_read_mv((AdcInput)p.channel);  // discard (MUX settle)
            int32_t mv = adc_read_mv((AdcInput)p.channel);
            p.last_analog = mv / 200.0f;  // mA via 200Ω shunt
            input_config_set((Input)p.channel, SW_ANALOG, false);
            input_config_set((Input)p.channel, SW_SHUNT, false);
            break;
        }
        case IO_ANALOG_VOLTAGE: {
            input_config_set((Input)p.channel, SW_ANALOG, true);
            vTaskDelay(pdMS_TO_TICKS(20));
            adc_read_mv((AdcInput)p.channel);  // discard
            int32_t mv = adc_read_mv((AdcInput)p.channel);
            p.last_analog = mv / 1000.0f;  // V
            input_config_set((Input)p.channel, SW_ANALOG, false);
            break;
        }
        case IO_DIGITAL:
        case IO_DIGITAL_PULLUP: {
            uint8_t state = sampler_current_state();
            bool raw = (state >> p.channel) & 0x01;
            p.last_digital = p.inverted ? !raw : raw;
            break;
        }
        case IO_PULSE: {
            // Count edges from sampler ring buffer
            SamplerEvent evt;
            while (sampler_read_next(p.sampler_cursor, evt)) {
                uint8_t bit = (evt.state >> p.channel) & 0x01;
                // Count falling edges (HIGH→LOW) — typical for pulse sensors
                if (p.prev_bit == 1 && bit == 0) {
                    p.pulse_count++;
                }
                p.prev_bit = bit;
            }
            // Calculate Hz
            uint32_t now = millis();
            uint32_t dt = now - p.last_rate_ms;
            if (dt >= 1000) {
                uint32_t dp = p.pulse_count - p.last_pulse_count;
                p.pulse_hz = (float)dp / ((float)dt / 1000.0f);
                p.last_pulse_count = p.pulse_count;
                p.last_rate_ms = now;
            }
            uint8_t state = sampler_current_state();
            bool raw = (state >> p.channel) & 0x01;
            p.last_digital = p.inverted ? !raw : raw;
            break;
        }
        default:
            break;
    }
}

// --- Build JSON for a single pin ---
static void pin_to_json(JsonObject obj, const IoPin& p) {
    obj["pin"] = p.pin;
    obj["name"] = p.name;
    obj["mode"] = mode_to_string(p.mode);
    // Include channel voltage
    int ch_volt = (p.channel < 4) ? 
        (config::config["channels"]["A"]["voltage"] | 24) :
        (config::config["channels"]["B"]["voltage"] | 24);
    obj["voltage"] = ch_volt;

    switch (p.mode) {
        case IO_OUTPUT:
            obj["state"] = p.output_state ? "HIGH" : "LOW";
            break;
        case IO_ANALOG_CURRENT:
            obj["ma"] = serialized(String(p.last_analog, 2));
            break;
        case IO_ANALOG_VOLTAGE:
            obj["volt"] = serialized(String(p.last_analog, 3));
            break;
        case IO_DIGITAL:
        case IO_DIGITAL_PULLUP:
            obj["state"] = p.last_digital ? "HIGH" : "LOW";
            if (p.inverted) obj["inverted"] = true;
            break;
        case IO_PULSE:
            obj["state"] = p.last_digital ? "HIGH" : "LOW";
            obj["pulses"] = p.pulse_count;
            obj["hz"] = serialized(String(p.pulse_hz, 1));
            if (p.inverted) obj["inverted"] = true;
            break;
        case IO_RELAY:
            obj["state"] = p.output_state ? "ON" : "OFF";
            break;
        default:
            break;
    }
}

// --- IFTTT Rules ---
#define MAX_RULES 10

enum RuleOp : uint8_t { OP_GT, OP_LT, OP_EQ, OP_NEQ, OP_GTE, OP_LTE };
enum RuleAction : uint8_t { ACT_SET_HIGH, ACT_SET_LOW, ACT_RELAY_ON, ACT_RELAY_OFF };

struct Rule {
    char name[32];
    char src_pin[4];        // source pin ("A2", "B1")
    char src_field[8];      // "ma", "volt", "state", "hz", "pulses"
    RuleOp op;
    float threshold;        // numeric threshold (or 1.0=HIGH, 0.0=LOW for state)
    char dst_pin[4];        // target pin or "RA"/"RB"
    RuleAction action;
    bool active;            // currently triggered
    bool enabled;
};

static Rule s_rules[MAX_RULES];
static uint8_t s_num_rules = 0;

static RuleOp parse_op(const char* s) {
    if (!s) return OP_EQ;
    if (strcmp(s, ">") == 0) return OP_GT;
    if (strcmp(s, "<") == 0) return OP_LT;
    if (strcmp(s, "==") == 0) return OP_EQ;
    if (strcmp(s, "!=") == 0) return OP_NEQ;
    if (strcmp(s, ">=") == 0) return OP_GTE;
    if (strcmp(s, "<=") == 0) return OP_LTE;
    return OP_EQ;
}

static RuleAction parse_action(const char* s) {
    if (!s) return ACT_SET_HIGH;
    if (strcmp(s, "high") == 0 || strcmp(s, "HIGH") == 0) return ACT_SET_HIGH;
    if (strcmp(s, "low") == 0 || strcmp(s, "LOW") == 0) return ACT_SET_LOW;
    if (strcmp(s, "on") == 0 || strcmp(s, "ON") == 0) return ACT_RELAY_ON;
    if (strcmp(s, "off") == 0 || strcmp(s, "OFF") == 0) return ACT_RELAY_OFF;
    return ACT_SET_HIGH;
}

static bool eval_op(float val, RuleOp op, float threshold) {
    switch (op) {
        case OP_GT:  return val > threshold;
        case OP_LT:  return val < threshold;
        case OP_EQ:  return fabsf(val - threshold) < 0.01f;
        case OP_NEQ: return fabsf(val - threshold) >= 0.01f;
        case OP_GTE: return val >= threshold;
        case OP_LTE: return val <= threshold;
    }
    return false;
}

// Find pin value by name and field
static bool get_pin_value(const char* pin_name, const char* field, float &out) {
    for (uint8_t i = 0; i < s_num_pins; i++) {
        if (strcmp(s_pins[i].pin, pin_name) != 0) continue;
        if (strcmp(field, "ma") == 0) { out = s_pins[i].last_analog; return true; }
        if (strcmp(field, "volt") == 0) { out = s_pins[i].last_analog; return true; }
        if (strcmp(field, "state") == 0) { out = s_pins[i].last_digital ? 1.0f : 0.0f; return true; }
        if (strcmp(field, "hz") == 0) { out = s_pins[i].pulse_hz; return true; }
        if (strcmp(field, "pulses") == 0) { out = (float)s_pins[i].pulse_count; return true; }
    }
    return false;
}

static void execute_action(const char* pin, RuleAction action) {
    if (strcmp(pin, "RA") == 0) {
        relay_set(RELAY_A, action == ACT_RELAY_ON || action == ACT_SET_HIGH);
    } else if (strcmp(pin, "RB") == 0) {
        relay_set(RELAY_B, action == ACT_RELAY_ON || action == ACT_SET_HIGH);
    } else {
        // Find output pin
        for (uint8_t i = 0; i < s_num_pins; i++) {
            if (strcmp(s_pins[i].pin, pin) == 0 && s_pins[i].mode == IO_OUTPUT) {
                bool state = (action == ACT_SET_HIGH);
                s_pins[i].output_state = state;
                input_config_set((Input)s_pins[i].channel, SW_PULLUP, state);
                break;
            }
        }
    }
}

static void evaluate_rules() {
    for (uint8_t i = 0; i < s_num_rules; i++) {
        Rule& r = s_rules[i];
        if (!r.enabled) continue;

        float val = 0.0f;
        if (!get_pin_value(r.src_pin, r.src_field, val)) continue;

        bool triggered = eval_op(val, r.op, r.threshold);
        if (triggered && !r.active) {
            // Rising edge — execute action
            execute_action(r.dst_pin, r.action);
            r.active = true;
            log_info("[poseidon] Rule '%s' triggered: %s.%s → %s", r.name, r.src_pin, r.src_field, r.dst_pin);
        } else if (!triggered && r.active) {
            r.active = false;
        }
    }
}

static void parse_rules() {
    s_num_rules = 0;
    if (!config::is_loaded || config::config["rules"].isNull()) return;

    JsonArray rules = config::config["rules"].as<JsonArray>();
    for (JsonObject rule : rules) {
        if (s_num_rules >= MAX_RULES) break;

        Rule& r = s_rules[s_num_rules];
        const char* name = rule["name"] | "";
        const char* src = rule["if_pin"] | "";
        const char* field = rule["if_field"] | "state";
        const char* op = rule["if_op"] | "==";
        float threshold = rule["if_value"] | 0.0f;
        const char* dst = rule["then_pin"] | "";
        const char* action = rule["then_action"] | "high";
        bool enabled = rule["enabled"] | true;

        strncpy(r.name, name, sizeof(r.name) - 1); r.name[sizeof(r.name) - 1] = '\0';
        strncpy(r.src_pin, src, sizeof(r.src_pin) - 1); r.src_pin[sizeof(r.src_pin) - 1] = '\0';
        strncpy(r.src_field, field, sizeof(r.src_field) - 1); r.src_field[sizeof(r.src_field) - 1] = '\0';
        r.op = parse_op(op);
        r.threshold = threshold;
        strncpy(r.dst_pin, dst, sizeof(r.dst_pin) - 1); r.dst_pin[sizeof(r.dst_pin) - 1] = '\0';
        r.action = parse_action(action);
        r.active = false;
        r.enabled = enabled;

        s_num_rules++;
        log_info("[poseidon] Rule '%s': %s.%s %s %.1f → %s %s", r.name, r.src_pin, r.src_field, op, r.threshold, r.dst_pin, action);
    }
}

// --- Background task: periodic reads and reporting ---
static void poseidon_task(void* param) {
    (void)param;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));  // 1 Hz base loop

        uint32_t now = millis();
        bool should_push = false;

        for (uint8_t i = 0; i < s_num_pins; i++) {
            IoPin& p = s_pins[i];
            if (p.mode == IO_DISABLED || p.mode == IO_OUTPUT) continue;

            read_pin(p);

            // Check if it's time to report
            if (p.interval_s > 0 && (now - p.last_report_ms) >= (p.interval_s * 1000)) {
                p.last_report_ms = now;
                should_push = true;
            }
        }

        // Push to server if any pin triggered a report
        if (should_push && web_socket::is_connected) {
            String json = poseidon_io_get_all();
            web_socket::sendMessage(json);
        }

        // Evaluate IFTTT rules
        evaluate_rules();
    }
}

// --- Public API ---

void poseidon_init() {
    s_num_pins = 0;

    // Read channel voltages
    Voltage volt_a = VOLTAGE_24V;
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

    // All inputs OFF first
    for (uint8_t i = 0; i < 8; i++) {
        input_config_set((Input)i, SW_ANALOG, false);
        input_config_set((Input)i, SW_PULLUP, false);
        input_config_set((Input)i, SW_SHUNT, false);
        input_config_set((Input)i, SW_DIGITAL, false);
    }

    // Parse io array from config
    if (config::is_loaded && !config::config["io"].isNull()) {
        JsonArray io = config::config["io"].as<JsonArray>();

        for (JsonObject pin : io) {
            if (s_num_pins >= MAX_IO_PINS) {
                log_error("[poseidon] Max %d pins, ignoring rest", MAX_IO_PINS);
                break;
            }

            const char* pin_name = pin["pin"] | "";
            const char* name = pin["name"] | "";
            const char* mode_str = pin["mode"] | "disabled";
            bool pullup = pin["pullup"] | false;
            bool inverted = pin["inverted"] | false;
            int interval = pin["interval_s"] | 0;
            int debounce = pin["debounce_ms"] | 20;

            int8_t ch = pin_name_to_channel(pin_name);
            IoMode mode = parse_mode(mode_str);
            if (ch < 0 && mode != IO_RELAY) {
                log_error("[poseidon] Invalid pin '%s', skipping", pin_name);
                continue;
            }

            IoPin& p = s_pins[s_num_pins];
            strncpy(p.pin, pin_name, sizeof(p.pin) - 1);
            p.pin[sizeof(p.pin) - 1] = '\0';
            strncpy(p.name, name, sizeof(p.name) - 1);
            p.name[sizeof(p.name) - 1] = '\0';
            p.mode = mode;
            p.pullup = pullup;
            p.inverted = inverted;
            p.interval_s = interval;
            p.debounce_ms = debounce;
            p.channel = (ch >= 0) ? (uint8_t)ch : 0;
            p.output_state = false;
            p.last_analog = 0.0f;
            p.last_digital = false;
            p.last_report_ms = 0;
            p.pulse_count = 0;
            p.sampler_cursor = sampler_write_position();
            p.prev_bit = (sampler_current_state() >> ((ch >= 0) ? ch : 0)) & 0x01;
            p.last_pulse_count = 0;
            p.last_rate_ms = millis();
            p.pulse_hz = 0.0f;
            p.valid = true;

            // Configure hardware based on mode
            switch (mode) {
                case IO_RELAY: {
                    // "RA" or "RB" — determine which relay
                    Relay r = (pin_name[1] == 'B' || pin_name[1] == 'b') ? RELAY_B : RELAY_A;
                    relay_set(r, false);
                    break;
                }
                case IO_OUTPUT:
                    // Output uses pullup to drive the line
                    input_config_set((Input)ch, SW_PULLUP, false);
                    break;
                case IO_DIGITAL:
                    input_config_set((Input)ch, SW_DIGITAL, true);
                    if (pullup) input_config_set((Input)ch, SW_PULLUP, true);
                    break;
                case IO_DIGITAL_PULLUP:
                    input_config_set((Input)ch, SW_DIGITAL, true);
                    input_config_set((Input)ch, SW_PULLUP, true);
                    break;
                case IO_ANALOG_CURRENT:
                    // Analog+shunt toggled per read
                    break;
                case IO_ANALOG_VOLTAGE:
                    // Analog toggled per read
                    break;
                case IO_PULSE:
                    input_config_set((Input)ch, SW_DIGITAL, true);
                    if (pullup) input_config_set((Input)ch, SW_PULLUP, true);
                    break;
                default:
                    break;
            }

            s_num_pins++;
            log_info("[poseidon] Pin %s '%s' mode=%s ch=%d", p.pin, p.name, mode_to_string(mode), ch);
        }
    } else {
        log_error("[poseidon] No config or no io array");
    }

    Serial.printf("[poseidon] Initialized, %d pins, A=%dV B=%dV\r\n",
        s_num_pins,
        (volt_a == VOLTAGE_5V) ? 5 : (volt_a == VOLTAGE_12V) ? 12 : 24,
        (volt_b == VOLTAGE_5V) ? 5 : (volt_b == VOLTAGE_12V) ? 12 : 24);

    // Parse IFTTT rules
    parse_rules();
    if (s_num_rules > 0) {
        Serial.printf("[poseidon] %d rules loaded\r\n", s_num_rules);
    }
}

void poseidon_start_task() {
    xTaskCreate(poseidon_task, "poseidon", 4096, nullptr, 2, &s_task_handle);
    task_register(s_task_handle, "poseidon", 2, 4096);
}

String poseidon_io_set(const char* pin_name, bool state) {
    // Handle hardware relays directly
    if (strcmp(pin_name, "RA") == 0) {
        relay_set(RELAY_A, state);
        log_info("[poseidon] Relay A → %s", state ? "ON" : "OFF");
        return state ? "{\"id\":\"RA\",\"state\":\"ON\"}" : "{\"id\":\"RA\",\"state\":\"OFF\"}";
    }
    if (strcmp(pin_name, "RB") == 0) {
        relay_set(RELAY_B, state);
        log_info("[poseidon] Relay B → %s", state ? "ON" : "OFF");
        return state ? "{\"id\":\"RB\",\"state\":\"ON\"}" : "{\"id\":\"RB\",\"state\":\"OFF\"}";
    }

    for (uint8_t i = 0; i < s_num_pins; i++) {
        if (strcmp(s_pins[i].pin, pin_name) == 0) {
            if (s_pins[i].mode == IO_RELAY) {
                s_pins[i].output_state = state;
                Relay r = (s_pins[i].pin[1] == 'B' || s_pins[i].pin[1] == 'b') ? RELAY_B : RELAY_A;
                relay_set(r, state);
                log_info("[poseidon] Relay %s → %s", pin_name, state ? "ON" : "OFF");
            } else if (s_pins[i].mode == IO_OUTPUT) {
                s_pins[i].output_state = state;
                input_config_set((Input)s_pins[i].channel, SW_PULLUP, state);
                log_info("[poseidon] %s → %s", pin_name, state ? "HIGH" : "LOW");
            } else {
                return "{\"error\":\"pin is not output or relay\"}";
            }

            JsonDocument doc;
            doc["pin"] = pin_name;
            doc["state"] = state ? "HIGH" : "LOW";
            doc["result"] = "ok";
            String result;
            serializeJson(doc, result);
            return result;
        }
    }
    return "{\"error\":\"pin not found\"}";
}

String poseidon_io_get(const char* pin_name) {
    for (uint8_t i = 0; i < s_num_pins; i++) {
        if (strcmp(s_pins[i].pin, pin_name) == 0) {
            // Do a fresh read for input pins
            if (s_pins[i].mode != IO_OUTPUT && s_pins[i].mode != IO_DISABLED) {
                read_pin(s_pins[i]);
            }
            JsonDocument doc;
            JsonObject obj = doc.to<JsonObject>();
            pin_to_json(obj, s_pins[i]);
            String result;
            serializeJson(doc, result);
            return result;
        }
    }
    return "{\"error\":\"pin not found\"}";
}

String poseidon_io_get_all() {
    JsonDocument doc;
    doc["product"] = "poseidon";

    if (config::is_loaded && !config::config["name"].isNull()) {
        doc["name"] = config::config["name"];
    }

    JsonArray pins = doc["io"].to<JsonArray>();
    for (uint8_t i = 0; i < s_num_pins; i++) {
        // Fresh read for inputs
        if (s_pins[i].mode != IO_OUTPUT && s_pins[i].mode != IO_DISABLED && s_pins[i].mode != IO_RELAY) {
            read_pin(s_pins[i]);
        }
        JsonObject obj = pins.add<JsonObject>();
        pin_to_json(obj, s_pins[i]);
    }

    // Include hardware relays (from config)
    JsonArray relays = doc["relays"].to<JsonArray>();
    if (config::is_loaded && !config::config["relays"].isNull()) {
        JsonObject rcfg = config::config["relays"].as<JsonObject>();
        if (!rcfg["A"].isNull() && (rcfg["A"]["enabled"] | false)) {
            JsonObject ra = relays.add<JsonObject>();
            ra["id"] = "RA";
            ra["name"] = rcfg["A"]["name"] | "Relay A";
            ra["state"] = relay_get(RELAY_A) ? "ON" : "OFF";
        }
        if (!rcfg["B"].isNull() && (rcfg["B"]["enabled"] | false)) {
            JsonObject rb = relays.add<JsonObject>();
            rb["id"] = "RB";
            rb["name"] = rcfg["B"]["name"] | "Relay B";
            rb["state"] = relay_get(RELAY_B) ? "ON" : "OFF";
        }
    }

    // Rules status
    if (s_num_rules > 0) {
        JsonArray rules = doc["rules"].to<JsonArray>();
        for (uint8_t i = 0; i < s_num_rules; i++) {
            JsonObject r = rules.add<JsonObject>();
            r["name"] = s_rules[i].name;
            r["active"] = s_rules[i].active;
            r["enabled"] = s_rules[i].enabled;
        }
    }

    String result;
    serializeJson(doc, result);
    return result;
}

String poseidon_io_reset_pulses(const char* pin_name) {
    for (uint8_t i = 0; i < s_num_pins; i++) {
        if (strcmp(s_pins[i].pin, pin_name) == 0) {
            if (s_pins[i].mode != IO_PULSE) return "{\"error\":\"pin is not pulse mode\"}";
            uint32_t old = s_pins[i].pulse_count;
            s_pins[i].pulse_count = 0;
            log_info("[poseidon] Reset pulses on %s (was %lu)", pin_name, old);
            JsonDocument doc;
            doc["pin"] = pin_name;
            doc["reset"] = old;
            doc["pulses"] = 0;
            String result;
            serializeJson(doc, result);
            return result;
        }
    }
    return "{\"error\":\"pin not found\"}";
}
