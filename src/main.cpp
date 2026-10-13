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
#include "apps/product_display/product_display.h"
#include "apps/product_router/product_router.h"
#include "apps/menu/menu.h"
#include "apps/ota/ota.h"
#include "platform/buttons_task.h"
#include "platform/network_task.h"
#include "platform/serial_cmd.h"

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

    mcs_logo_show_splash();         // MCS brand logo at boot (~2s)
    // Boot shows the MCS logo splash; the guide name then appears via the
    // HOME screen's logo-zoom animation (no separate product image splash).
    product_display_start_power_manager();  // auto-off after inactivity, wake on button
    menu_start();                   // OLED menu (HOME/Tanks/Pumps), driven by buttons

    channels_setup();               // device-level: set channel supply voltage
    start_product_apps();           // start the apps for what the config contains

    // Platform tasks (always run regardless of product)
    buttons_start_task();           // polls button ADC every 50ms
    network_start_task();           // network stack (Ethernet, WiFi, WebSocket, web server)
    serial_cmd_start_task();        // serial debug commands (send ? for help)

    // OTA firmware update (web/ota.html). The task only acts on a pending
    // request from the UI and checks the network itself, so it is safe to
    // start here — it won't touch the network until the user triggers it.
    otaInit();
    ota_start_task();
}

// All work runs in FreeRTOS tasks. Arduino loop is not used.
void loop() {
    vTaskSuspend(nullptr);
}
