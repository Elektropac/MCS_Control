#pragma once
#include <ArduinoJson.h>

// Initialize poseidon — configure all I/O from config
void poseidon_init();

// Start the background reporting task
void poseidon_start_task();

// Set output pin HIGH/LOW
String poseidon_io_set(const char* pin, bool state);

// Get single pin status
String poseidon_io_get(const char* pin);

// Get all pins status
String poseidon_io_get_all();
