#pragma once
// =======================================================
// RELAYS — 2x relay control via TCA9535 (0x27)
// =======================================================
//
// Relay A = P06, Relay B = P07 on the serial expander.
// Shared chip with serial_control — both drivers
// maintain their own bits in the port 0 mirror.
//
// =======================================================
#include <Arduino.h>

enum Relay : uint8_t {
    RELAY_A,
    RELAY_B,
};

// Initialize relays (both off). Call after serial_control_init().
void relays_init();

// Set relay state
void relay_set(Relay relay, bool on);

// Get current relay state
bool relay_get(Relay relay);
