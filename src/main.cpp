#include <Arduino.h>
#include <WiFi.h>
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
#include "apps/fueling_auth/fueling_auth.h"
#include "platform/network_task.h"
#include "platform/serial_cmd.h"
#include "bluetooth.h"
#include "ntp.h"
#include "w5500.h"
#include "my_wifi.h"

#include "file_system.h"
#include "config.h"

void setup() {
    Serial.begin(115200);
    log_init();                     // log system (serial + ring buffer)

    // Filesystem + config (must be before apps that read config)
    file_system::init();
    config::init();
    event_log_init();               // flash event log (needs LittleFS mounted)
    event_log_add("BOOT fw=%s", 
#ifdef CONTROL_FW_VERSION
        CONTROL_FW_VERSION
#else
        "dev"
#endif
    );

    // Hardware
    i2c_init(I2C_SDA, I2C_SCL);    // I2C bus + mutex
    sampler_init();                 // 4 kHz pulse sampling (ISR, always on)
    flow_guard_init();              // flow guard state (cursor synced to sampler)
    all_drivers_init();             // probe I2C chips + init all peripherals

    display_boot_splash();          // guide screen + startup jingle, then system info
    // Boot shows the guide screen (+ jingle) and the system-info screen; the
    // carousel then takes over the OLED, auto-scrolling through tanks + pumps
    // (no buttons — the TopBoard with its 5 buttons + display has been removed;
    // the OLED is now a fixed display driven entirely by config / web / BLE).
    display_start();                // OLED carousel (auto-scrolling)

    channels_setup();               // device-level: set channel supply voltage
    start_apps();           // start the apps for what the config contains
    fueling_auth_init();            // transaction authorization + delivery store

    // Bring up BLE BEFORE the network stack. BLE's controller needs a large
    // contiguous heap block, which only exists before WiFi/lwIP/Ethernet
    // fragment the heap. (Previously BLE init waited for network_initialized
    // and then aborted the device when the block was too small.)
    bluetooth::init();

    // Platform tasks (always run regardless of product)
    network_start_task();           // network stack (Ethernet, WiFi, WebSocket, web server)
    serial_cmd_start_task();        // serial debug commands (send ? for help)

    // OTA firmware update (web/ota.html). The task only acts on a pending
    // request from the UI and checks the network itself, so it is safe to
    // start here — it won't touch the network until the user triggers it.
    otaInit();
    ota_start_task();

    // Daily clock re-sync: Control has no RTC, so it keeps time via millis()
    // relative to the last known UTC (from NTP or a connecting BLE client).
    // The ESP32 crystal drifts a few seconds/day, so once a day we try NTP
    // again (whichever transport is up) to stay accurate. BLE clients also
    // re-sync on every connect. If no source is available, time just keeps
    // ticking from the last value (events show that until a sync happens).
    xTaskCreate([](void*) {
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(24UL * 60UL * 60UL * 1000UL));  // 24h
            if (w5500::connected)      ntp_ethernet::sync(3, true);
            else if (wifi::connected)  ntp_wifi::sync(3, true);
        }
    }, "clock_sync", 4096, nullptr, 1, nullptr);
}

// All work runs in FreeRTOS tasks. Arduino loop is not used.
void loop() {
    vTaskSuspend(nullptr);
}
