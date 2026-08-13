#include <Arduino.h>
#include "scheduler/scheduler.h"
#include "sampler/sampler.h"
#include "sampler/flow_guard.h"

// --- Setup ---

void setup() {
    Serial.begin(115200);

    sampler_init();       // start pulse sampling (always on, interrupt-driven)
    flow_guard_init();    // initialize flow guard state

    // --- Register tasks ---
    task_add("flow_guard",  flow_guard_check,   500);
}

// --- Main loop ---

void loop() {
    task_run();
}
