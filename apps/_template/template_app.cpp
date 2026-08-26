// =======================================================
// Template App — copy this to start a new app
// =======================================================
// 1. Copy this folder to apps/your_app_name/
// 2. Rename files and functions
// 3. Add your app to apps/app_registry/app_registry.cpp
// 4. Add a config entry in data/config.json under "functions"
// 5. Build and test
// =======================================================

#include "template_app.h"
#include "mcs_api.h"

// --- Your private state ---
static uint8_t s_my_channel = 0;

// --- Callback example: handle incoming message ---
static void on_my_command(const char* topic, const JsonObject& data) {
    // React to a message from another app or the network
    mcs_log(MCS_LOG_INFO, "myapp", "Received message on %s", topic);
}

// --- Init: runs once at boot ---
void template_app_init(const JsonObject& config) {
    // Read your config keys
    s_my_channel = config["input"] | 0;

    // Set up hardware
    mcs_input_mode(s_my_channel, MCS_MODE_VOLTAGE);

    // Subscribe to topics you need
    mcs_subscribe("my_app/command", on_my_command);

    mcs_log(MCS_LOG_INFO, "myapp", "Template app started, channel %d", s_my_channel);
}

// --- Task: your main loop ---
void template_app_task(void* param) {
    for (;;) {
        // 1. Always process inbox first
        mcs_process_inbox();

        // 2. Do your work
        int32_t mv = mcs_adc_read_mv(s_my_channel);

        // 3. Publish results
        JsonDocument doc;
        doc["channel"] = s_my_channel;
        doc["mv"] = mv;
        mcs_publish("my_app/reading", doc);

        // 4. Sleep (MANDATORY — never spin without delay)
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
