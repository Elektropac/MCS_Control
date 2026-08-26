// =======================================================
// Demo Sensor App
// =======================================================
// A simple example app that:
//   1. Reads a 4-20mA sensor every 2 seconds
//   2. Publishes the reading on the message bus
//   3. Listens for "demo/reset" to zero its counter
//
// Config example:
//   { "id": "my_sensor", "type": "demo_sensor", "input": 2, "interval_ms": 2000 }
// =======================================================

#include "demo_sensor.h"
#include "mcs_api.h"

// --- App state (private to this app) ---
static uint8_t  s_channel = 0;
static uint32_t s_interval_ms = 2000;
static uint32_t s_reading_count = 0;

// --- Callback: someone published "demo/reset" ---
static void on_reset(const char* topic, const JsonObject& data) {
    s_reading_count = 0;
    mcs_log(MCS_LOG_INFO, "demo", "Counter reset by message on '%s'", topic);
    mcs_buzzer(MCS_SOUND_CLICK);
}

// --- Init: called once at boot with our config section ---
void demo_sensor_init(const JsonObject& config) {
    // Read config
    s_channel = config["input"] | 0;
    s_interval_ms = config["interval_ms"] | 2000;

    // Configure hardware
    mcs_input_mode(s_channel, MCS_MODE_CURRENT);        // 4-20mA mode
    mcs_voltage_set(s_channel / 4, 24);                 // power the channel at 24V

    // Subscribe to reset command
    mcs_subscribe("demo/reset", on_reset);

    mcs_log(MCS_LOG_INFO, "demo", "Demo sensor started on channel %d, interval %dms",
            s_channel, s_interval_ms);
}

// --- Task: our main loop ---
void demo_sensor_task(void* param) {
    for (;;) {
        // 1. Process incoming messages
        mcs_process_inbox();

        // 2. Read sensor
        float ma = mcs_adc_read_ma(s_channel);
        s_reading_count++;

        // 3. Publish reading
        JsonDocument doc;
        doc["channel"] = s_channel;
        doc["ma"] = ma;
        doc["count"] = s_reading_count;
        mcs_publish("demo/reading", doc);

        // 4. Check threshold — publish alarm if needed
        if (ma < 3.5f) {
            JsonDocument alarm;
            alarm["channel"] = s_channel;
            alarm["ma"] = ma;
            alarm["message"] = "Sensor disconnected or broken wire";
            mcs_publish("demo/alarm", alarm);
        }

        // 5. Sleep until next cycle
        vTaskDelay(pdMS_TO_TICKS(s_interval_ms));
    }
}
