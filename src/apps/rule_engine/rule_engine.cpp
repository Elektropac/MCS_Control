// =======================================================
// Rule Engine — shared IF/THEN/ELSE rule execution (implementation)
// =======================================================
#include "rule_engine.h"
#include "hardware/relays.h"
#include "hardware/input_config.h"
#include "platform/sampler.h"
#include "platform/task_registry.h"
#include "buzzer.h"
#include "logging.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <math.h>

// --- config namespace (header-only; defined via network_task chain) ---
namespace config {
    extern bool is_loaded;
    extern JsonDocument config;
    bool config_read_groups(JsonDocument& out);
    bool config_read_group_rules(const char* group_id, JsonDocument& out);
}

// --- web_socket (header-only namespace) ---
namespace web_socket {
    extern bool is_connected;
    void sendMessage(const String &message);
}

// --- CloudGauge read-only accessor: current alarm state of a probe group.
// Returns "normal" | "low_level" | "overfill" | "fault" | "" (unknown).
// Implemented in cloudgauge.cpp — measurement stays there, we only READ. ---
extern const char* cloudgauge_group_state(const char* group_id);
// Current level value of a probe group in its alarm_unit (for conditions).
extern float cloudgauge_group_value(const char* group_id);

// =======================================================
// Tunables
// =======================================================
#define RULE_TICK_MS       100
#define MAX_RULE_GROUPS    8
#define MAX_KV             32
#define KV_KEY_LEN         32
#define KV_VAL_LEN         24

// =======================================================
// Key/value state table (shared across rules)
// =======================================================
// Backs set_value / conditions / timer "elapsed-since-state". String-typed
// with numeric helpers; records last-change timestamp per key.
struct KvEntry {
    char     key[KV_KEY_LEN];
    char     val[KV_VAL_LEN];
    uint32_t changed_ms;
    bool     used;
};
static KvEntry s_kv[MAX_KV];

static KvEntry* kv_find(const char* key) {
    for (uint8_t i = 0; i < MAX_KV; i++)
        if (s_kv[i].used && strcmp(s_kv[i].key, key) == 0) return &s_kv[i];
    return nullptr;
}

static KvEntry* kv_alloc(const char* key) {
    for (uint8_t i = 0; i < MAX_KV; i++) {
        if (!s_kv[i].used) {
            s_kv[i].used = true;
            strncpy(s_kv[i].key, key, KV_KEY_LEN - 1);
            s_kv[i].key[KV_KEY_LEN - 1] = '\0';
            s_kv[i].val[0] = '\0';
            s_kv[i].changed_ms = 0;
            return &s_kv[i];
        }
    }
    log_error("[rules] KV table full (%d) — key '%s' dropped", MAX_KV, key);
    return nullptr;
}

void rule_kv_set(const char* key, const char* value) {
    KvEntry* e = kv_find(key);
    if (!e) e = kv_alloc(key);
    if (!e) return;
    // Only bump the change timestamp if the value actually changed — this is
    // what makes "elapsed since state X" correct (CloudGauge-style edge).
    if (strcmp(e->val, value) != 0) {
        strncpy(e->val, value, KV_VAL_LEN - 1);
        e->val[KV_VAL_LEN - 1] = '\0';
        uint32_t now = millis(); if (now == 0) now = 1;
        e->changed_ms = now;
    }
}

void rule_kv_set_num(const char* key, double value) {
    char buf[KV_VAL_LEN];
    // Integer-ish values print without trailing noise; keep 2 decimals else.
    if (fabs(value - (double)(long)value) < 1e-6) snprintf(buf, sizeof(buf), "%ld", (long)value);
    else snprintf(buf, sizeof(buf), "%.2f", value);
    rule_kv_set(key, buf);
}

String rule_kv_get(const char* key, const char* fallback) {
    KvEntry* e = kv_find(key);
    return e ? String(e->val) : String(fallback);
}

double rule_kv_get_num(const char* key, double fallback) {
    KvEntry* e = kv_find(key);
    if (!e || e->val[0] == '\0') return fallback;
    return atof(e->val);
}

uint32_t rule_kv_changed_ms(const char* key) {
    KvEntry* e = kv_find(key);
    return e ? e->changed_ms : 0;
}

// =======================================================
// Per-group runtime (edge detection for triggers)
// =======================================================
// A group is a probe or pump group from config. We track per-group bits so
// triggers that are edge-based (probe-state change, io digital edge, pulse
// count thresholds, timer intervals) fire exactly once per event — matching
// the existing CloudGauge/pump behaviour.
struct RuleGroup {
    char     id[24];
    char     type[8];                 // "probe" | "pump"
    // probe edge
    char     last_probe_state[12];
    // io digital edge per input channel (bit per A1..B4)
    uint8_t  prev_digital;            // last sampled digital bitmap
    bool     digital_primed;
    // pulse model (per channel):
    //  pulse_total   = monotonic count since the group's pulses were reset
    //                  (reset explicitly by a reset_pulses action / txn start).
    //  pulse_last_ms = millis() of the most recent pulse (for no_activity).
    uint32_t pulse_total[8];
    uint32_t pulse_last_ms[8];
    uint32_t sampler_cursor;
    uint8_t  prev_bit[8];
    // per-rule bookkeeping (keyed by rule index, max 16 rules/group):
    //  timer_last_ms   = interval timer / elapsed "fired" guard
    //  pulse_fired_at  = pulse_total value when a pulse_count rule last fired
    //                    (lets reset:yes fire every N pulses correctly)
    //  rule_fired      = one-shot guard for level/edge rules that must fire once
    uint32_t timer_last_ms[16];
    uint32_t pulse_fired_at[16];
    bool     boot_done;               // recover_on_boot one-shot
    bool     valid;
};
static RuleGroup s_groups[MAX_RULE_GROUPS];
static uint8_t   s_num_groups = 0;
static TaskHandle_t s_task_handle = nullptr;

// --- Incoming comm message (fed by the network layer) ---
// Visible to comm triggers for ONE tick, then cleared. Kept tiny; the network
// layer calls rule_engine_feed_message() when a message arrives (e.g. AUTH_OK,
// STOP). This is the hook — delivery wiring lives in the network code.
#define MSG_BUF_LEN 64
static char s_msg_buf[MSG_BUF_LEN] = {0};
static volatile bool s_msg_pending = false;

static int8_t input_name_to_channel(const char* name) {
    if (!name || strlen(name) != 2) return -1;
    char ch = name[0], n = name[1];
    if (n < '1' || n > '4') return -1;
    int idx = (n - '1');
    if (ch == 'A' || ch == 'a') return idx;
    if (ch == 'B' || ch == 'b') return 4 + idx;
    return -1;
}

// =======================================================
// Condition evaluation (IF-guards; ALL must be true)
// =======================================================
// condition = { source, op, value [, value2] }
// Sources we support now:
//   "<group>.state" / any kv key  -> string or numeric compare
//   "probe:<group>"               -> numeric level in alarm_unit
// Operators: eq neq gt gte lt lte between
static bool cond_cmp_num(double a, const char* op, double b, double b2) {
    if (!strcmp(op, "eq"))  return a == b;
    if (!strcmp(op, "neq")) return a != b;
    if (!strcmp(op, "gt"))  return a >  b;
    if (!strcmp(op, "gte")) return a >= b;
    if (!strcmp(op, "lt"))  return a <  b;
    if (!strcmp(op, "lte")) return a <= b;
    if (!strcmp(op, "between")) return a >= b && a <= b2;
    return false;
}

static bool eval_condition(const RuleGroup& g, JsonObject c) {
    const char* source = c["source"] | "";
    const char* op     = c["op"] | "eq";

    // probe:<group> → numeric level
    if (strncmp(source, "probe:", 6) == 0) {
        const char* grp = source + 6;
        double level = cloudgauge_group_value(grp);
        double v  = c["value"]  | 0.0;
        double v2 = c["value2"] | 0.0;
        return cond_cmp_num(level, op, v, v2);
    }

    // Otherwise treat source as a KV key. Compare as string if the value
    // is a JSON string, else numerically.
    JsonVariant v = c["value"];
    if (v.is<const char*>()) {
        String lhs = rule_kv_get(source, "");
        const char* rhs = v.as<const char*>();
        bool eq = (lhs == rhs);
        if (!strcmp(op, "eq"))  return eq;
        if (!strcmp(op, "neq")) return !eq;
        return eq;
    } else {
        double lhs = rule_kv_get_num(source, 0.0);
        double rhs = v | 0.0;
        double rhs2 = c["value2"] | 0.0;
        return cond_cmp_num(lhs, op, rhs, rhs2);
    }
}

static bool eval_all_conditions(const RuleGroup& g, JsonArray conds) {
    for (JsonObject c : conds) {
        if (!eval_condition(g, c)) return false;
    }
    return true;   // empty = always true
}

// Forward declaration (defined with the sampling helpers below).
static void group_reset_pulses(RuleGroup& g);

// =======================================================
// Action dispatch (THEN / ELSE)
// =======================================================
static void run_action(RuleGroup& g, JsonObject a) {
    const char* what = a["do"] | "";

    if (!strcmp(what, "set_relay")) {
        const char* target = a["target"] | "";
        const char* state  = a["state"]  | "off";
        Relay relay = (!strcmp(target, "relay_b")) ? RELAY_B : RELAY_A;
        if (!strcmp(state, "on"))        relay_set(relay, true);
        else if (!strcmp(state, "off"))  relay_set(relay, false);
        else if (!strcmp(state, "toggle")) relay_set(relay, !relay_get(relay));
        log_info("[rules] %s: set_relay %s=%s", g.id, target, state);

    } else if (!strcmp(what, "set_value")) {
        const char* key = a["key"] | "";
        if (key[0]) {
            const char* op = a["op"] | "";
            if (!strcmp(op, "increment")) {
                double step = a["value"] | 1.0;
                rule_kv_set_num(key, rule_kv_get_num(key, 0.0) + step);
            } else {
                JsonVariant v = a["value"];
                if (v.is<const char*>()) {
                    const char* sval = v.as<const char*>();
                    rule_kv_set(key, sval);
                    // When a transaction ENTERS "pumping", reset this group's
                    // hardware pulse counters so count_litres/preset_reached
                    // start from zero for the new transaction.
                    char stateKey[40];
                    snprintf(stateKey, sizeof(stateKey), "%s.state", g.id);
                    if (!strcmp(key, stateKey) && !strcmp(sval, "pumping")) {
                        group_reset_pulses(g);
                    }
                } else {
                    rule_kv_set_num(key, v | 0.0);
                }
            }
        }

    } else if (!strcmp(what, "reset_pulses")) {
        group_reset_pulses(g);
        log_info("[rules] %s: reset_pulses", g.id);

    } else if (!strcmp(what, "buzzer")) {
        const char* sound = a["sound"] | "warning";
        if (!strcmp(sound, "ok"))            buzzer_sound_ok();
        else if (!strcmp(sound, "error"))    buzzer_sound_error();
        else if (!strcmp(sound, "complete")) buzzer_sound_complete();
        else if (!strcmp(sound, "startup"))  buzzer_sound_startup();
        else if (!strcmp(sound, "click"))    buzzer_sound_click();
        else                                 buzzer_sound_warning();
        log_info("[rules] %s: buzzer '%s'", g.id, sound);

    } else if (!strcmp(what, "read")) {
        log_info("[rules] %s: read", g.id);

    } else if (!strcmp(what, "notify")) {
        const char* msg = a["message"] | "";
        const char* level = a["level"] | "info";
        const char* channel = a["channel"] | "";
        if (web_socket::is_connected) {
            JsonDocument n;
            n["subject"] = "notify";
            n["channel"] = channel;
            n["level"]   = level;
            n["message"] = msg;
            String out; serializeJson(n, out);
            web_socket::sendMessage(out);
            log_info("[rules] %s: notify (%s) '%s'", g.id, level, msg);
        } else {
            log_info("[rules] %s: notify queued (offline, %s) '%s'", g.id, level, msg);
        }

    } else if (!strcmp(what, "log")) {
        // Transaction / event log. For now serialised as JSON over the server
        // link (binary wire-format to Nexus is a later swap, pending Niklas).
        JsonDocument d;
        d["subject"] = "log";
        d["store"]   = a["store"] | "events";
        d["group"]   = g.id;
        // Resolve each field: if the value names a KV key, substitute it.
        JsonObject fields = a["fields"].as<JsonObject>();
        JsonObject out = d["fields"].to<JsonObject>();
        if (!fields.isNull()) {
            for (JsonPair kv : fields) {
                JsonVariant fv = kv.value();
                if (fv.is<const char*>()) {
                    const char* ref = fv.as<const char*>();
                    // If ref is a known KV key, substitute its (numeric) value.
                    if (kv_find(ref)) out[kv.key()] = rule_kv_get_num(ref, 0.0);
                    else out[kv.key()] = ref;
                } else {
                    out[kv.key()] = fv;
                }
            }
        }
        if (web_socket::is_connected) {
            String s; serializeJson(d, s);
            web_socket::sendMessage(s);
            log_info("[rules] %s: log sent", g.id);
        } else {
            log_info("[rules] %s: log queued (offline)", g.id);
        }

    } else {
        log_info("[rules] %s: unknown action '%s'", g.id, what);
    }
}

static void run_actions(RuleGroup& g, JsonArray actions) {
    for (JsonObject a : actions) run_action(g, a);
}

// =======================================================
// Trigger evaluation
// =======================================================
// Returns true if the rule's trigger fires THIS tick. `rule_idx` identifies
// the rule within the group (for per-rule timer bookkeeping).
static bool eval_trigger(RuleGroup& g, JsonObject trig, uint8_t rule_idx,
                         uint8_t sampled_digital) {
    const char* domain = trig["domain"] | "";
    uint32_t now = millis();

    // ---- probe: fire on the edge INTO the matching alarm state ----
    if (!strcmp(domain, "probe")) {
        const char* want = trig["type"] | "";
        const char* state = cloudgauge_group_state(g.id);
        if (!state || !state[0]) return false;
        bool changed = (strcmp(state, g.last_probe_state) != 0);
        bool fire = (changed && !strcmp(want, state));
        return fire;   // last_probe_state updated once per tick by caller
    }

    // ---- io: digital edge, pulse_count threshold, or pulse_rate ----
    if (!strcmp(domain, "io")) {
        const char* input = trig["input"] | "";
        int8_t ch = input_name_to_channel(input);
        if (ch < 0) return false;
        const char* itype = trig["input_type"] | "";
        const char* ttype = trig["type"] | "";

        if (!strcmp(itype, "digital") || !strcmp(ttype, "edge")) {
            bool now_bit  = (sampled_digital >> ch) & 0x01;
            bool prev_bit = (g.prev_digital >> ch) & 0x01;
            if (!g.digital_primed) return false;   // skip first sample
            const char* edge = trig["edge"] | "both";
            if (!strcmp(edge, "rising"))  return (!prev_bit && now_bit);
            if (!strcmp(edge, "falling")) return (prev_bit && !now_bit);
            return (prev_bit != now_bit);          // both
        }

        if (!strcmp(ttype, "pulse_count")) {
            uint32_t target = (uint32_t)(trig["value"] | 0.0);
            if (target == 0 || rule_idx >= 16) return false;
            const char* reset = trig["reset"] | "no";
            if (!strcmp(reset, "yes")) {
                // Fire once per `target` pulses: compare against this rule's
                // own last-fired mark (handles count_litres: 1 L per N pulses).
                if (g.pulse_total[ch] >= g.pulse_fired_at[rule_idx] + target) {
                    g.pulse_fired_at[rule_idx] += target;
                    return true;
                }
                return false;
            } else {
                // reset:no → absolute threshold since pulses were reset
                // (preset_reached: fire once when total crosses value).
                if (g.pulse_total[ch] >= target &&
                    g.pulse_fired_at[rule_idx] == 0) {
                    g.pulse_fired_at[rule_idx] = g.pulse_total[ch];
                    return true;
                }
                return false;
            }
        }

        if (!strcmp(ttype, "pulse_rate")) {
            // Flow guard: any pulse activity (op "above", value small) within
            // the last tick window. We treat "a pulse arrived recently" as
            // active flow. The condition (state != pumping) gates misuse.
            uint32_t now2 = millis();
            uint32_t last = g.pulse_last_ms[ch];
            if (last == 0) return false;
            // Fire if a pulse arrived within ~2 ticks (recent flow).
            return (now2 - last) <= (RULE_TICK_MS * 2);
        }
        return false;
    }

    // ---- timer: interval, elapsed-since-state, or no_activity ----
    if (!strcmp(domain, "timer")) {
        const char* ttype = trig["type"] | "interval";

        if (!strcmp(ttype, "elapsed")) {
            // Fire once when a KV key has held a given state for N seconds.
            const char* since = trig["since"] | "";
            const char* wantState = trig["state"] | "";
            uint32_t need_ms = (uint32_t)(trig["value"] | 0.0) * 1000UL;
            if (!since[0] || need_ms == 0) return false;
            if (wantState[0] && rule_kv_get(since, "") != wantState) return false;
            uint32_t changed = rule_kv_changed_ms(since);
            if (changed == 0) return false;
            if ((now - changed) >= need_ms) {
                if (rule_idx < 16 && g.timer_last_ms[rule_idx] != changed) {
                    g.timer_last_ms[rule_idx] = changed;   // one fire per entry
                    return true;
                }
            }
            return false;
        }

        if (!strcmp(ttype, "no_activity")) {
            // No-flow guard: fire once if, while in `state`, the watched pulse
            // input has seen NO pulse for N seconds. `watch` = pulse input,
            // `since`/`state` gate it to the pumping window.
            const char* watch = trig["watch"] | "";
            const char* since = trig["since"] | "";
            const char* wantState = trig["state"] | "";
            uint32_t need_ms = (uint32_t)(trig["value"] | 0.0) * 1000UL;
            int8_t ch = input_name_to_channel(watch);
            if (ch < 0 || need_ms == 0) return false;
            if (wantState[0] && since[0] && rule_kv_get(since, "") != wantState) return false;
            uint32_t stateChanged = since[0] ? rule_kv_changed_ms(since) : 0;
            // Reference time = later of (last pulse) and (state entry), so we
            // only alarm after N s of silence WITHIN the pumping state.
            uint32_t ref = g.pulse_last_ms[ch];
            if (stateChanged > ref) ref = stateChanged;
            if (ref == 0) return false;
            if ((now - ref) >= need_ms) {
                if (rule_idx < 16 && g.timer_last_ms[rule_idx] != ref) {
                    g.timer_last_ms[rule_idx] = ref;
                    return true;
                }
            }
            return false;
        }

        // interval: fire on first run and every N seconds thereafter.
        uint32_t interval_ms = (uint32_t)(trig["value"] | 0.0) * 1000UL;
        if (interval_ms == 0 || rule_idx >= 16) return false;
        uint32_t last = g.timer_last_ms[rule_idx];
        if (last == 0 || (now - last) >= interval_ms) {
            g.timer_last_ms[rule_idx] = (now == 0) ? 1 : now;
            return true;
        }
        return false;
    }

    // ---- system: boot (recover_on_boot — fire once at startup) ----
    if (!strcmp(domain, "system")) {
        const char* stype = trig["type"] | "";
        if (!strcmp(stype, "boot")) {
            if (!g.boot_done) { g.boot_done = true; return true; }
            return false;
        }
        return false;
    }

    // ---- comm: message match ----
    // The network layer pushes received messages via rule_engine_feed_message().
    // A message is visible to all groups for one tick. Match on "match"
    // substring (empty = any message). `source` (port) is advisory for now.
    if (!strcmp(domain, "comm")) {
        if (!s_msg_pending) return false;
        const char* match = trig["match"] | "";
        if (!match[0]) return true;                 // any message
        return (strstr(s_msg_buf, match) != nullptr);
    }

    return false;
}

// =======================================================
// Per-group pulse + digital sampling (fueling domain)
// =======================================================
// Reads the sampler ring buffer once per tick and updates this group's
// digital bitmap + per-channel pulse counts. Mirrors pump.cpp edge counting.
static void sample_group_io(RuleGroup& g, uint8_t& out_digital) {
    out_digital = sampler_current_state();

    SamplerEvent evt;
    while (sampler_read_next(g.sampler_cursor, evt)) {
        for (uint8_t ch = 0; ch < 8; ch++) {
            uint8_t bit = (evt.state >> ch) & 0x01;
            // Count falling edges as pulses (same as pump.cpp).
            if (g.prev_bit[ch] == 1 && bit == 0) {
                g.pulse_total[ch]++;
                uint32_t now = millis(); if (now == 0) now = 1;
                g.pulse_last_ms[ch] = now;
            }
            g.prev_bit[ch] = bit;
        }
    }
}

// Reset a group's pulse counters + per-rule pulse-fired marks for a channel
// (called when a transaction starts, so count/preset restart cleanly).
static void group_reset_pulses(RuleGroup& g) {
    for (uint8_t ch = 0; ch < 8; ch++) g.pulse_total[ch] = 0;
    for (uint8_t i = 0; i < 16; i++)  g.pulse_fired_at[i] = 0;
}

// =======================================================
// Evaluate all rules for one group (one tick)
// =======================================================
static void evaluate_group(RuleGroup& g) {
    uint8_t sampled_digital = g.prev_digital;
    if (!strcmp(g.type, "pump")) {
        sample_group_io(g, sampled_digital);
    }

    JsonDocument rulesDoc;
    if (config::config_read_group_rules(g.id, rulesDoc)) {
        uint8_t idx = 0;
        for (JsonObject rule : rulesDoc.as<JsonArray>()) {
            if (!(rule["enabled"] | true)) { idx++; continue; }
            JsonObject trig = rule["trigger"].as<JsonObject>();
            if (eval_trigger(g, trig, idx, sampled_digital)) {
                if (eval_all_conditions(g, rule["conditions"].as<JsonArray>())) {
                    run_actions(g, rule["actions"].as<JsonArray>());
                } else {
                    run_actions(g, rule["else"].as<JsonArray>());
                }
            }
            idx++;
        }
    }

    // Update edge-tracking state ONCE per tick (after all rules evaluated).
    if (!strcmp(g.type, "probe")) {
        const char* state = cloudgauge_group_state(g.id);
        if (state && state[0]) {
            strncpy(g.last_probe_state, state, sizeof(g.last_probe_state) - 1);
            g.last_probe_state[sizeof(g.last_probe_state) - 1] = '\0';
        }
    }
    if (!strcmp(g.type, "pump")) {
        g.prev_digital = sampled_digital;
        g.digital_primed = true;
    }
}

// =======================================================
// Task
// =======================================================
static void rule_engine_task(void* param) {
    (void)param;
    if (s_num_groups == 0) {
        log_error("[rules] No rule groups, task suspending");
        vTaskSuspend(nullptr);
        return;
    }
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(RULE_TICK_MS));
        for (uint8_t i = 0; i < s_num_groups; i++) {
            if (s_groups[i].valid) evaluate_group(s_groups[i]);
        }
        // A fed comm message is valid for exactly one tick (all groups saw it).
        if (s_msg_pending) { s_msg_pending = false; s_msg_buf[0] = '\0'; }
    }
}

// =======================================================
// Public API
// =======================================================
void rule_engine_init() {
    s_num_groups = 0;
    memset(s_kv, 0, sizeof(s_kv));

    JsonDocument groupsDoc;
    if (!config::config_read_groups(groupsDoc) || groupsDoc["groups"].isNull()) {
        log_error("[rules] No config or no groups");
        return;
    }

    for (JsonObject group : groupsDoc["groups"].as<JsonArray>()) {
        if (s_num_groups >= MAX_RULE_GROUPS) {
            log_error("[rules] Max %d groups, ignoring rest", MAX_RULE_GROUPS);
            break;
        }
        const char* gtype = group["type"] | "";
        if (strcmp(gtype, "probe") != 0 && strcmp(gtype, "pump") != 0) continue;

        RuleGroup& g = s_groups[s_num_groups];
        memset(&g, 0, sizeof(g));
        const char* gid = group["id"] | "";
        strncpy(g.id, gid, sizeof(g.id) - 1);
        strncpy(g.type, gtype, sizeof(g.type) - 1);
        strcpy(g.last_probe_state, "init");    // forces first probe evaluation
        g.sampler_cursor = sampler_write_position();
        for (uint8_t c = 0; c < 8; c++) g.prev_bit[c] = 1;   // idle-high inputs
        g.digital_primed = false;
        g.valid = true;

        // Pump groups: configure nozzle + pulse inputs as digital pull-ups,
        // seed the transaction state machine. (Measurement/relay primitives
        // are shared; this just prepares the inputs the rules reference.)
        if (!strcmp(gtype, "pump") && !group["pump"].isNull()) {
            JsonObject pump = group["pump"].as<JsonObject>();
            int8_t nz = input_name_to_channel(pump["nozzle"] | "");
            int8_t pl = input_name_to_channel(pump["pulse"]  | "");
            if (nz >= 0) input_config_set((Input)nz, SW_PULLUP, true);
            if (pl >= 0) input_config_set((Input)pl, SW_PULLUP, true);
            // Initial transaction state.
            char key[40];
            snprintf(key, sizeof(key), "%s.state", g.id);
            rule_kv_set(key, "idle");
        }

        s_num_groups++;
        log_info("[rules] group '%s' (%s)", g.id, g.type);
    }
    log_info("[rules] Initialized, %d group(s)", s_num_groups);
}

void rule_engine_start_task() {
    xTaskCreate(rule_engine_task, "rule_engine", 6144, nullptr, 2, &s_task_handle);
    task_register(s_task_handle, "rule_engine", 2, 6144);
}

void rule_engine_stop()  { if (s_task_handle) vTaskSuspend(s_task_handle); }
void rule_engine_start() { if (s_task_handle) vTaskResume(s_task_handle); }

bool rule_engine_fueling_active() {
    for (uint8_t i = 0; i < s_num_groups; i++) {
        if (!s_groups[i].valid || strcmp(s_groups[i].type, "pump") != 0) continue;
        char key[40];
        snprintf(key, sizeof(key), "%s.state", s_groups[i].id);
        String st = rule_kv_get(key, "idle");
        if (st == "pumping" || st == "authorised") return true;
    }
    if (relay_get(RELAY_A) || relay_get(RELAY_B)) return true;
    return false;
}

// Feed an incoming comm message (e.g. "AUTH_OK", "STOP") to the engine.
// Called by the network layer when a message arrives. The message is matched
// by comm-domain triggers on the next tick, then cleared. Safe to call from
// another task (single writer, consumed once).
void rule_engine_feed_message(const char* msg) {
    if (!msg) return;
    strncpy(s_msg_buf, msg, MSG_BUF_LEN - 1);
    s_msg_buf[MSG_BUF_LEN - 1] = '\0';
    s_msg_pending = true;
}
