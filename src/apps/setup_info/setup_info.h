#pragma once
// =======================================================
// Setup info — device configuration overview (for web UI)
// =======================================================
// Builds a small JSON summary of HOW the device is configured:
//   { guide, channels:{A,B}, probe_count, pump_count,
//     probes:[{id,input,probe_type}], pumps:[{id,input}] }
// Read on-demand from config (guide via filter, groups via the groups
// reader that skips rules[]). Nothing large is held in RAM.
// =======================================================

#include <Arduino.h>

// Returns the setup overview as a JSON string.
String setup_info_get();
