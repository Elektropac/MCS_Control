#pragma once
#include <ArduinoJson.h>

// Initialize cloudgauge — configures all probe channels
void probes_init();

// Start the background reading task (1 Hz sampling, averaging)
void probes_start_task();

// Pause/resume the background task (e.g. during manual debug reads)
void probes_stop();
void probes_start();

// Get all probes as JSON array
String probes_get_all();

// Get a single probe by input name (e.g. "A1", "B3")
String probes_get(const char* input_name);

// Number of probes in probes_get_all() — without building/parsing the JSON.
// (The OLED carousel asks this several times a second.)
uint8_t probes_count();
