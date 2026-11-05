#pragma once
// =======================================================
// State — global runtime status aggregator (for Nexus / web UI)
// =======================================================
// Niklas wanted ONE "global status" call that says what the device is
// currently DOING (e.g. "a fuelling is in progress"), pulled in a single
// request instead of calling every sub-getter separately.
//
// This is "Variant A" from state-fil-designnotat.md:
//   state_get_all() simply COMBINES the existing _get_all() getters.
//   Each of those getters is already config-dynamic, so the aggregate is
//   too — no config changes, no new data model.
//
// IMPORTANT — this module is strictly READ-ONLY:
//   • It only calls existing getters (pump/cloudgauge/poseidon) and the
//     two activity flags (rule_engine_fueling_active / pump_any_active).
//   • It never writes flash, never changes any runtime state, never drives
//     hardware. It cannot break anything else in the firmware.
//   • State lives in RAM only — this just reflects it at the moment of the
//     call. Nothing is persisted (no flash wear).
//
// Shape (sub-objects are the sub-getters' own JSON, parsed and nested):
//   {
//     "fueling_active": true/false,   // any transaction in progress
//     "pump_active":    true/false,   // any nozzle lifted / relay on
//     "pumps":      { ... },          // == pump_get_all()
//     "probes":     { ... },          // == cloudgauge_get_all()
//     "io":         { ... }           // == poseidon_io_get_all()
//   }
// Sections whose app is not active simply return their empty getter output.
// =======================================================
#include <Arduino.h>

// Returns the aggregated runtime state as a JSON string. Read-only.
String state_get_all();
