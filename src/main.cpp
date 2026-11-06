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
#include "platform/channels_setup.h"
#include "apps/flow_guard/flow_guard.h"
#include "apps/display/display.h"
#include "apps/capability_router/capability_router.h"
#include "apps/ota/ota.h"
#include "platform/network_task.h"
#include "platform/serial_cmd.h"
#include "platform/memreport.h"

#include "file_system.h"
#include "config.h"

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

    display_boot_splash();          // MCS brand logo at boot (~2s)
    // Boot shows the MCS logo splash; the carousel then takes over the OLED,
    // auto-scrolling through the config-defined screens (no buttons — the
    // TopBoard with its 5 buttons + display has been removed; the OLED is now
    // a fixed display driven entirely by config / web / BLE).
    display_start();                // OLED carousel (auto-scrolling, config-defined)

    channels_setup();               // device-level: set channel supply voltage
    start_apps();           // start the apps for what the config contains

    // Platform tasks (always run regardless of product)
    network_start_task();           // network stack (Ethernet, WiFi, WebSocket, web server)
    serial_cmd_start_task();        // serial debug commands (send ? for help)

    // OTA firmware update (web/ota.html). The task only acts on a pending
    // request from the UI and checks the network itself, so it is safe to
    // start here — it won't touch the network until the user triggers it.
    otaInit();
    ota_start_task();

    memreport_start();              // TEMP diagnostic: heap + task stacks over serial
}

// All work runs in FreeRTOS tasks. Arduino loop is not used.
void loop() {
    vTaskSuspend(nullptr);
}
