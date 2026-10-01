#include "buttons.h"

// ------------------------------------------
// ADC center values for each button
// Measured on hardware (2026-10-01), mapping corrected to physical buttons:
// OK~2, DOWN~614, RIGHT~1052, UP~1391, LEFT~1656. Idle rests ~1872.
// ------------------------------------------
static const int BTN_OK_CENTER    = 2;
static const int BTN_DOWN_CENTER  = 614;
static const int BTN_RIGHT_CENTER = 1052;
static const int BTN_UP_CENTER    = 1391;
static const int BTN_LEFT_CENTER  = 1656;
static const int BTN_TOLERANCE    = 120;

// ------------------------------------------
// Repeat timing
// ------------------------------------------
static const unsigned long BTN_FIRST_REPEAT_MS = 300;
static const unsigned long BTN_REPEAT_MS       = 150;
static const unsigned long STARTUP_BLOCK_MS    = 5000;

// ------------------------------------------
// State
// ------------------------------------------
static uint8_t       s_pin = 0;
static Button        s_last_button     = BTN_NONE;
static unsigned long s_last_event_time = 0;
static unsigned long s_hold_start      = 0;
static bool          s_held            = false;
static bool          s_wait_release    = true;

// ------------------------------------------
// Helpers
// ------------------------------------------
static bool in_range(int value, int center, int tolerance) {
    return value > (center - tolerance) && value < (center + tolerance);
}

// ------------------------------------------
// Public API
// ------------------------------------------

void buttons_init(uint8_t pin) {
    s_pin = pin;
    pinMode(s_pin, INPUT);
    analogReadResolution(12);
    s_last_button = BTN_NONE;
    s_last_event_time = 0;
    s_hold_start = 0;
    s_held = false;
    s_wait_release = true;
}

Button buttons_read_raw() {
    // Average 5 samples for stability
    int samples = 0;
    for (int i = 0; i < 5; i++) {
        samples += analogRead(s_pin);
        delayMicroseconds(300);
    }
    int value = samples / 5;

    if (in_range(value, BTN_UP_CENTER,    BTN_TOLERANCE)) return BTN_UP;
    if (in_range(value, BTN_DOWN_CENTER,  BTN_TOLERANCE)) return BTN_DOWN;
    if (in_range(value, BTN_LEFT_CENTER,  BTN_TOLERANCE)) return BTN_LEFT;
    if (in_range(value, BTN_RIGHT_CENTER, BTN_TOLERANCE)) return BTN_RIGHT;
    if (in_range(value, BTN_OK_CENTER,    BTN_TOLERANCE)) return BTN_OK;

    return BTN_NONE;
}

Button buttons_get_event() {
    Button raw = buttons_read_raw();
    unsigned long now = millis();

    // Block input for first 5 seconds after boot
    if (now < STARTUP_BLOCK_MS) {
        s_last_button = BTN_NONE;
        s_held = false;
        return BTN_NONE;
    }

    // Wait for release after startup
    if (s_wait_release) {
        if (raw == BTN_NONE) {
            s_wait_release = false;
        }
        return BTN_NONE;
    }

    // No button pressed
    if (raw == BTN_NONE) {
        s_last_button = BTN_NONE;
        s_held = false;
        return BTN_NONE;
    }

    // New button press
    if (raw != s_last_button) {
        s_last_button = raw;
        s_last_event_time = now;
        s_hold_start = now;
        s_held = false;
        return raw;
    }

    // First repeat
    if (!s_held && (now - s_hold_start >= BTN_FIRST_REPEAT_MS)) {
        s_held = true;
        s_last_event_time = now;
        return raw;
    }

    // Ongoing repeat
    if (s_held && (now - s_last_event_time >= BTN_REPEAT_MS)) {
        s_last_event_time = now;
        return raw;
    }

    return BTN_NONE;
}

int buttons_read_adc() {
    int samples = 0;
    for (int i = 0; i < 5; i++) {
        samples += analogRead(s_pin);
        delayMicroseconds(300);
    }
    return samples / 5;
}

const char* buttons_to_text(Button btn) {
    switch (btn) {
        case BTN_UP:    return "UP";
        case BTN_DOWN:  return "DOWN";
        case BTN_LEFT:  return "LEFT";
        case BTN_RIGHT: return "RIGHT";
        case BTN_OK:    return "OK";
        default:        return "NONE";
    }
}
