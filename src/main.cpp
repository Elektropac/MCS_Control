#include <Arduino.h>
#include "pins.h"
#include "hal.h"
#include "logging.h"
#include "file_system.h"
#include "config.h"
#include "hardfunc/all_drivers_init.h"
#include "tasks/sampler.h"
#include "tasks/flow_guard.h"
#include "tasks/buttons_task.h"
#include "tasks/network_task.h"
#include "tasks/display_task.h"
#include "debug/serial_cmd.h"

void setup() {
    Serial.begin(115200);
    log_init();                     // log system (serial + ring buffer)

    // Filesystem + config (before tasks, so menu can read it)
    file_system::init();
    config::init();

    // Hardware
    i2c_init(I2C_SDA, I2C_SCL);    // I2C bus + mutex
    sampler_init();                 // 4 kHz pulse sampling (ISR, always on)
    flow_guard_init();              // flow guard state (cursor synced to sampler)
    all_drivers_init();             // probe I2C chips + init all peripherals

    // FreeRTOS tasks
    flow_guard_start_task();        // monitors pulses without active transaction
    buttons_start_task();           // polls button ADC every 50ms
    display_start_task();           // renders OLED menu at ~30fps
    network_start_task();           // network stack (Ethernet, WiFi, WebSocket, web server)
    serial_cmd_start_task();        // serial debug commands (send ? for help)
}

// All work runs in FreeRTOS tasks. Arduino loop is not used.
void loop() {
    vTaskSuspend(nullptr);
}
