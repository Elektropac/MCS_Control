#pragma once
#include <ArduinoJson.h>

// Initialize poseidon — configure all I/O from config
void io_init();

// Start the background reporting task
void io_start_task();

// Set output pin HIGH/LOW
String io_set(const char* pin, bool state);

// Get single pin status
String io_get(const char* pin);

// Get all pins status
String io_get_all();

// Reset pulse counter for a pin
String io_reset_pulses(const char* pin);
