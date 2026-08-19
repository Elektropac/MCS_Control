#include "buttons.h"
#include <driver/gpio.h>

// ------------------------------------------
// ADC threshold-based button detection
// Resistor ladder with adjusted values for better spread
// Switches short taps to Buttonpress (ADC pin)
//
// Measured ADC values (12-bit, 3.3V ref):
//   Idle:   ~1867
//   Left:   ~1660
//   Up:     ~1396
//   Right:  ~1052
//   Down:   ~ 615
//   OK:     ~   0
//
// Thresholds (midpoints between adjacent values):
// ------------------------------------------
static const int THRESH_NONE_LEFT  = 1764;
static const int THRESH_LEFT_UP    = 1528;
static const int THRESH_UP_RIGHT   = 1224;
static const int THRESH_RIGHT_DOWN = 834;
static const int THRESH_DOWN_OK    = 308;

// ------------------------------------------
// Repeat timing
// ------------------------------------------
static const unsigned long BTN_FIRST_REPEAT_MS = 300;
static const unsigned long BTN_REPEAT_MS       = 150;
static const unsigned long STARTUP_BLOCK_MS    = 1000;

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
    gpio_set_pull_mode((gpio_num_t)s_pin, GPIO_FLOATING);
    analogReadResolution(12);
    s_last_button = BTN_NONE;
    s_last_event_time = 0;
    s_hold_start = 0;
    s_held = false;
    s_wait_release = true;
}

Button buttons_read_raw() {
    // Average 10 samples for stability (ladder values are close together)
    int samples = 0;
    for (int i = 0; i < 10; i++) {
        samples += analogRead(s_pin);
        delayMicroseconds(500);
    }
    int value = samples / 10;

    // Threshold-based: values spread from idle (highest) to OK (lowest)
    if (value > THRESH_NONE_LEFT)  return BTN_NONE;
    if (value > THRESH_LEFT_UP)    return BTN_LEFT;
    if (value > THRESH_UP_RIGHT)   return BTN_UP;
    if (value > THRESH_RIGHT_DOWN) return BTN_RIGHT;
    if (value > THRESH_DOWN_OK)    return BTN_DOWN;
    return BTN_OK;
}

// Confirmed read: must get same result twice with a gap
static Button buttons_read_confirmed() {
    Button first = buttons_read_raw();
    delay(5);
    Button second = buttons_read_raw();
    if (first == second) return first;
    return BTN_NONE;  // unstable = treat as no press
}

Button buttons_get_event() {
    Button raw = buttons_read_raw();
    unsigned long now = millis();

    // Block input briefly after boot
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

    // Button released — mark as idle
    if (raw == BTN_NONE) {
        s_last_button = BTN_NONE;
        s_held = false;
        return BTN_NONE;
    }

    // Must have been NONE before accepting a new press
    if (s_last_button == BTN_NONE) {
        s_last_button = raw;
        s_last_event_time = now;
        s_hold_start = now;
        s_held = false;
        return raw;
    }

    // Same button still held — auto-repeat
    if (raw == s_last_button) {
        if (!s_held && (now - s_hold_start >= BTN_FIRST_REPEAT_MS)) {
            s_held = true;
            s_last_event_time = now;
            return raw;
        }
        if (s_held && (now - s_last_event_time >= BTN_REPEAT_MS)) {
            s_last_event_time = now;
            return raw;
        }
    }

    // Different button without release — ignore
    return BTN_NONE;
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
