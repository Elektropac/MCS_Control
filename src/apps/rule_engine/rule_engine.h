#pragma once
// =======================================================
// Rule Engine — shared IF/THEN/ELSE rule execution
// =======================================================
// ONE engine executes the rules[] from every config group — both probe
// (CloudGauge) and pump (fueling) domains. It does NOT measure anything:
//   • CloudGauge keeps ALL its measurement code (ADC/mA/mm/litre/LUT/latch).
//     The engine READS a probe's current alarm-state read-only to evaluate
//     probe-domain triggers.
//   • The pump/fueling domain is driven by the sampler (nozzle edge, flow
//     pulse count) + an internal key/value state table (transaction state
//     machine, litres, totalizer) + timers.
//
// Design mirrors rule-engine-reference.md:
//   Rule = trigger (WHEN) + conditions[] (IF) + actions[] (THEN) + else[]
//   Triggers:  io (edge/pulse_count) · probe (state edge) · timer
//              (interval / elapsed-since-state) · comm (message)
//   Actions:   set_relay · set_value · buzzer · notify · read · log
//
// The engine is config-driven and read on-demand (one group's rules at a
// time) to keep peak RAM bounded — per config-arkitektur.md.
// =======================================================
#include <Arduino.h>
#include <ArduinoJson.h>

// Parse all groups from config and prepare per-group runtime state.
// Safe to call once at startup (after config::init()).
void rule_engine_init();

// Start the background evaluation task (~100 ms tick).
void rule_engine_start_task();

// Pause/resume the task.
void rule_engine_stop();
void rule_engine_start();

// True if any fueling transaction is active (nozzle lifted or pump relay on).
// Used by OTA to avoid interrupting a fuelling. Reads runtime state only.
bool rule_engine_fueling_active();

// Feed an incoming comm message (e.g. "AUTH_OK", "STOP") to the engine so
// comm-domain triggers can match it. Called by the network layer on receipt.
// The message is valid for one tick. Safe to call from another task.
void rule_engine_feed_message(const char* msg);

// --- Internal key/value state table (exposed for test/inspection) ---
// Values are stored as strings; numeric helpers parse on demand. Each key
// also records a "last changed" timestamp so timer "elapsed-since-state"
// triggers work without extra hardware.
void   rule_kv_set(const char* key, const char* value);
void   rule_kv_set_num(const char* key, double value);
String rule_kv_get(const char* key, const char* fallback = "");
double rule_kv_get_num(const char* key, double fallback = 0.0);
uint32_t rule_kv_changed_ms(const char* key);   // millis() of last change (0 = never)
