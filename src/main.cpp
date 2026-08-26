#include <Arduino.h>
#include "pins.h"
#include "i2c.h"
#include "logging.h"
#include "hardware/all_drivers_init.h"
#include "platform/sampler.h"
#include "apps/flow_guard/flow_guard.h"
#include "platform/buttons_task.h"
#include "platform/network_task.h"
#include "platform/serial_cmd.h"

void setup() {
    Serial.begin(115200);
    log_init();                     // log system (serial + ring buffer)

    // Hardware
    i2c_init(I2C_SDA, I2C_SCL);    // I2C bus + mutex
    sampler_init();                 // 4 kHz pulse sampling (ISR, always on)
    flow_guard_init();              // flow guard state (cursor synced to sampler)
    all_drivers_init();             // probe I2C chips + init all peripherals

    // FreeRTOS tasks
    flow_guard_start_task();        // monitors pulses without active transaction
    buttons_start_task();           // polls button ADC every 50ms
    network_start_task();           // network stack (Ethernet, WiFi, WebSocket, web server)
    serial_cmd_start_task();        // serial debug commands (send ? for help)
}

// All work runs in FreeRTOS tasks. Arduino loop is not used.
void loop() {
    vTaskSuspend(nullptr);
}
