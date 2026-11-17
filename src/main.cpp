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
#include "platform/cpu_load.h"
#include "platform/memmark.h"
#include "bluetooth.h"
#include "ntp.h"
#include "w5500.h"
#include "my_wifi.h"

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include "file_system.h"
#include "config.h"

void setup() {
    Serial.begin(115200);
    memmark("setup start");
    log_init();                     // log system (serial + ring buffer)
    cpu_load_init();                // idle-hook CPU load meter (serial cmd "u")

    // Filesystem + config (must be before apps that read config)
    file_system::init();
    memmark("littlefs");
    config::init();
    memmark("fs+config");
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
    memmark("drivers");

    display_boot_splash();          // guide screen + startup jingle, then system info
    // Boot shows the guide screen (+ jingle) and the system-info screen; the
    // carousel then takes over the OLED, auto-scrolling through tanks + pumps
    // (no buttons — the TopBoard with its 5 buttons + display has been removed;
    // the OLED is now a fixed display driven entirely by config / web / BLE).
    display_start();                // OLED carousel (auto-scrolling)
    memmark("display task");

    channels_setup();               // device-level: set channel supply voltage
    start_apps();           // start the apps for what the config contains
    memmark("apps");
    fueling_auth_init();            // transaction authorization + delivery store
    memmark("fueling_auth");

    // Bring up BLE BEFORE the network stack. BLE's controller needs a large
    // contiguous heap block, which only exists before WiFi/lwIP/Ethernet
    // fragment the heap. (Previously BLE init waited for network_initialized
    // and then aborted the device when the block was too small.)
    bluetooth::init();
    memmark("bluetooth");

    // Platform tasks (always run regardless of product)
    network_start_task();           // network stack (Ethernet, WiFi, WebSocket, web server)
    memmark("net tasks created");
    serial_cmd_start_task();        // serial debug commands (send ? for help)

    // OTA firmware update (web/ota.html). The task only acts on a pending
    // request from the UI and checks the network itself, so it is safe to
    // start here — it won't touch the network until the user triggers it.
    otaInit();
    ota_start_task();
    memmark("ota");

    // Daily clock re-sync: Control has no RTC, so it keeps time via millis()
    // relative to the last known UTC (from NTP or a connecting BLE client).
    // The ESP32 crystal drifts a few seconds/day, so once a day we try NTP
    // again (whichever transport is up) to stay accurate. BLE clients also
    // re-sync on every connect. If no source is available, time just keeps
    // ticking from the last value (events show that until a sync happens).
    //
    // RAM: this used to be a permanent task that slept 24 h with a 4 KB stack.
    // Now a FreeRTOS software timer fires once a day and spawns a short-lived
    // worker (same 4 KB stack) that syncs and deletes itself — 4 KB of heap is
    // free for the other 24 h-minus-a-few-seconds. If the worker cannot be
    // created (very low heap) we just skip today's sync and try again tomorrow.
    TimerHandle_t clockTimer = xTimerCreate(
        "clock_sync", pdMS_TO_TICKS(24UL * 60UL * 60UL * 1000UL), pdTRUE, nullptr,
        [](TimerHandle_t) {
            xTaskCreate([](void*) {
                if (w5500::connected)      ntp_ethernet::sync(3, true);
                else if (wifi::connected)  ntp_wifi::sync(3, true);
                vTaskDelete(nullptr);
            }, "clock_sync", 4096, nullptr, 1, nullptr);
        });
    if (clockTimer) xTimerStart(clockTimer, 0);
}

// All work runs in FreeRTOS tasks. Arduino loop is not used.
//
// RAM: setup() runs inside Arduino's "loopTask" (8 KB stack, allocated from the
// heap). Once setup() has returned that task has nothing left to do, so delete
// it instead of suspending it forever — this hands the 8 KB stack back to the
// heap. (Nothing else refers to loopTask; no global state lives on its stack.)
void loop() {
    vTaskDelete(nullptr);
}
