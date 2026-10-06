#pragma once
// =======================================================
// Pump / fueling — config-driven pump controller
// =======================================================
// Parses "pump" groups from config (Setup Engine fueling groups):
//   { id, type:"pump", channel, pump:{ name, channel, nozzle, pulse,
//     relay, pulses_per_litre, preset_litres } }
//
// Per pump it:
//   - reads the nozzle switch (digital input: lifted/returned)
//   - counts flow pulses (sampler ring buffer) -> litres via pulses_per_litre
//   - drives the pump relay (relay_a / relay_b)
//
// Mirrors the cloudgauge module's structure (init, task, get_all).
// =======================================================
#include <ArduinoJson.h>

// Parse pump groups from config + set up hardware.
void pump_init();

// Start the background task (nozzle + pulse sampling).
void pump_start_task();

// Pause/resume the background task.
void pump_stop();
void pump_start();

// All pumps as a JSON array (for the web API).
String pump_get_all();

// One pump by group id or name.
String pump_get(const char* id);

// True if ANY pump is currently active (nozzle lifted or relay on) — i.e. a
// fueling transaction is in progress. Used by OTA to avoid interrupting a
// fuelling. Safe to call from another task (reads runtime state only).
bool pump_any_active();
