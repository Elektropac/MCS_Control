#include <Arduino.h>
#include "scheduler/scheduler.h"
#include "sampler/sampler.h"
#include "sampler/flow_guard.h"
#include "logging/logging.h"
#include "drivers/i2c.h"
#include "drivers/all_drivers_init.h"
#include "drivers/buttons_task.h"

// --- Setup ---

void setup() {
    Serial.begin(115200);
    log_init();               // logging system

    sampler_init();           // start pulse sampling (always on, interrupt-driven)
    flow_guard_init();        // initialize flow guard state
    i2c_init();               // start I2C bus (400 kHz)
    all_drivers_init();       // all hardware drivers (with probe + error logging)

    log_info("Setup complete, starting tasks...");

    // --- Register tasks ---
    task_add("flow_guard",  flow_guard_check,   500);   // TODO: connect alarm to interface event
    task_add("buttons",     buttons_check,      50);    // TODO: connect to menu/display navigation
}

// --- Main loop ---

void loop() {
    task_run();
}
