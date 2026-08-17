#pragma once
// =======================================================
// BUTTONS — 5-button analog resistor ladder on single pin
// =======================================================
// 5 buttons share one ADC pin via a voltage divider network.
// Each button produces a unique analog value.
//
// Features:
//   - Debounce via multi-sample averaging
//   - Auto-repeat on hold (first after 300ms, then every 150ms)
//   - 5 second startup block (ignore phantom presses at boot)
//
// Pin specified at init. No board dependencies.
//
// Usage:
//   buttons_init(15);  // GPIO 15
//   Button btn = buttons_get_event();
//
// =======================================================
#include <Arduino.h>

enum Button : uint8_t {
    BTN_NONE,
    BTN_UP,
    BTN_DOWN,
    BTN_LEFT,
    BTN_RIGHT,
    BTN_OK
};

// Initialize button ADC pin
void buttons_init(uint8_t pin);

// Get raw button state (no debounce/repeat logic)
Button buttons_read_raw();

// Get debounced button event with auto-repeat
// Returns BTN_NONE if no new press.
Button buttons_get_event();

// Convert button to string (for debug)
const char* buttons_to_text(Button btn);
