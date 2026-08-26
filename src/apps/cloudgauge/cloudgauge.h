#pragma once
#include <ArduinoJson.h>

// Initialize cloudgauge — configures all probe channels
void cloudgauge_init();

// Start the background reading task (1 Hz sampling, averaging)
void cloudgauge_start_task();

// Get all probes as JSON array
// Returns: {"probes": [{"id":"A1","input":"A1","ma":12.3,"cm":156.2,"liters":2340}, ...]}
String cloudgauge_get_all();

// Get a single probe by input name (e.g. "A1", "B3")
// Returns: {"id":"A1","input":"A1","ma":12.3,"cm":156.2,"liters":2340}
String cloudgauge_get(const char* input_name);
