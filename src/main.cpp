#include <Arduino.h>
#include "pins.h"
#include "i2c.h"
#include "logging.h"
#include <ArduinoJson.h>

// Forward-declare file_system + config (header-only libs, defined once via network_task chain)
namespace file_system {
    extern bool is_mounted;
    void init();
}
namespace config {
    extern bool is_loaded;
    extern JsonDocument config;
    void init();
}

#include "hardware/all_drivers_init.h"
#include "platform/sampler.h"
#include "apps/flow_guard/flow_guard.h"
#include "apps/cloudgauge/cloudgauge.h"
#include "apps/poseidon/poseidon.h"
#include "platform/buttons_task.h"
#include "platform/network_task.h"
#include "platform/serial_cmd.h"

void setup() {
    Serial.begin(115200);
    log_init();                     // log system (serial + ring buffer)

    // Filesystem + config (must be before apps that read config)
    file_system::init();
    config::init();

    // Hardware
    i2c_init(I2C_SDA, I2C_SCL);    // I2C bus + mutex
    sampler_init();                 // 4 kHz pulse sampling (ISR, always on)
    flow_guard_init();              // flow guard state (cursor synced to sampler)
    all_drivers_init();             // probe I2C chips + init all peripherals

    // --- Product router ---
    // Read "product" from config to decide which apps to start
    String product = "cloudgauge2";  // default
    if (config::is_loaded && !config::config["product"].isNull()) {
        product = config::config["product"].as<String>();
    }
    log_info("[main] Product: %s", product.c_str());

    if (product == "cloudgauge2") {
        cloudgauge_init();
        flow_guard_start_task();
        cloudgauge_start_task();
    }
    else if (product == "poseidon") {
        poseidon_init();
        poseidon_start_task();
    }
    else if (product == "micro_fms") {
        // TODO: pump_controller_init() + cloudgauge for probes
        cloudgauge_init();
        flow_guard_start_task();
        cloudgauge_start_task();
        log_info("[main] micro_fms: running cloudgauge for now, pump_controller TBD");
    }
    else {
        log_error("[main] Unknown product '%s', defaulting to cloudgauge2", product.c_str());
        cloudgauge_init();
        flow_guard_start_task();
        cloudgauge_start_task();
    }

    // Platform tasks (always run regardless of product)
    buttons_start_task();           // polls button ADC every 50ms
    network_start_task();           // network stack (Ethernet, WiFi, WebSocket, web server)
    serial_cmd_start_task();        // serial debug commands (send ? for help)
}

// All work runs in FreeRTOS tasks. Arduino loop is not used.
void loop() {
    vTaskSuspend(nullptr);
}
