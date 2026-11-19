#include "channels_setup.h"
#include "hardware/voltage_select.h"
#include "logging.h"
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// config document (config.h, header-only).
namespace config {
    extern bool is_loaded;
    extern int  channel_voltage_a;
    extern int  channel_voltage_b;
}

static Voltage voltage_from_config(int v) {
    switch (v) {
        case 5:  return VOLTAGE_5V;
        case 12: return VOLTAGE_12V;
        case 24: return VOLTAGE_24V;
        default: return VOLTAGE_OFF;
    }
}

void channels_setup() {
    int va = 0, vb = 0;   // default OFF if config missing

    if (config::is_loaded) {
        // copied out of config.json at init; -1 = key missing -> OFF (as before)
        va = config::channel_voltage_a < 0 ? 0 : config::channel_voltage_a;
        vb = config::channel_voltage_b < 0 ? 0 : config::channel_voltage_b;
    }

    voltage_select_set_a(voltage_from_config(va));
    voltage_select_set_b(voltage_from_config(vb));
    vTaskDelay(pdMS_TO_TICKS(20));   // let supplies settle

    log_info("[channels] supply set: A=%dV B=%dV", va, vb);
}
