#include "flow_guard.h"
#include "sampler.h"
#include "scheduler/scheduler.h"

// ------------------------------------------
// Configuration
// ------------------------------------------
static const uint8_t  NOISE_THRESHOLD  = 3;     // pulses below this = noise, ignore
static const uint32_t CHECK_INTERVAL   = 500;   // check every 500ms

// ------------------------------------------
// State
// ------------------------------------------
static uint32_t s_cursor = 0;
static uint8_t  s_last_state = 0;
static uint32_t s_pulse_count = 0;

// ------------------------------------------
// Count edges on all channels
// ------------------------------------------
static uint32_t count_edges(uint8_t old_state, uint8_t new_state) {
    uint8_t changed = old_state ^ new_state;
    uint32_t edges = 0;
    for (int i = 0; i < 8; i++) {
        if (changed & (1 << i)) edges++;
    }
    return edges;
}

// ------------------------------------------
// Task function — called by scheduler
// ------------------------------------------
void flow_guard_check() {
    SamplerEvent evt;

    while (sampler_read_next(s_cursor, evt)) {
        uint32_t edges = count_edges(s_last_state, evt.state);
        s_pulse_count += edges;
        s_last_state = evt.state;
    }

    if (s_pulse_count > NOISE_THRESHOLD) {
        // TODO: send alarm event to interface (Niklas)
        // interface_send_event(EVT_ERROR, 0, s_pulse_count, millis());

        s_pulse_count = 0;  // reset after alarm
    }
}

// ------------------------------------------
// Public API
// ------------------------------------------

void flow_guard_init() {
    s_cursor = sampler_write_position();  // start from now, ignore history
    s_last_state = sampler_current_state();
    s_pulse_count = 0;
}

void flow_guard_pause() {
    task_enable(flow_guard_check, false);
}

void flow_guard_resume() {
    // Reset state so we don't count old pulses
    s_cursor = sampler_write_position();
    s_last_state = sampler_current_state();
    s_pulse_count = 0;

    task_enable(flow_guard_check, true);
}
