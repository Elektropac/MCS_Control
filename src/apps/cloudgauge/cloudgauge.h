#pragma once
#include <ArduinoJson.h>

// Initialize cloudgauge — configures all probe channels
void cloudgauge_init();

// Start the background reading task (1 Hz sampling, averaging)
void cloudgauge_start_task();

// Pause/resume the background task (e.g. during manual debug reads)
void cloudgauge_stop();
void cloudgauge_start();

// Get all probes as JSON array
String cloudgauge_get_all();

// Get a single probe by input name (e.g. "A1", "B3")
String cloudgauge_get(const char* input_name);
