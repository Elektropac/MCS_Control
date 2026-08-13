#include "all_drivers_init.h"
#include "i2c.h"
#include "buttons.h"
#include "buzzer.h"
#include "voltage_select.h"
#include "serial_control.h"
#include "relays.h"
#include "input_config.h"
#include "adc.h"
#include "oled.h"
#include "hw_status.h"
#include "logging/logging.h"

void all_drivers_init() {
    // --- Non-I2C drivers ---
    buttons_init();
    hw_set_available(HW_BUTTONS, true);

    buzzer_init();
    hw_set_available(HW_BUZZER, true);

    oled_init();
    hw_set_available(HW_OLED, true);

    log_info("Non-I2C drivers initialized");

    // --- I2C drivers (probe before init) ---
    if (i2c_probe(0x21)) {
        voltage_select_init();
        hw_set_available(HW_VOLTAGE_SELECT, true);
        log_info("Voltage select (0x21) OK");
    } else {
        log_error("Voltage select (0x21) not found");
    }

    if (i2c_probe(0x27)) {
        serial_control_init();
        relays_init();
        hw_set_available(HW_SERIAL_CONTROL, true);
        log_info("Serial control + relays (0x27) OK");
    } else {
        log_error("Serial control + relays (0x27) not found");
    }

    if (i2c_probe(0x25)) {
        input_config_init();
        hw_set_available(HW_INPUT_CONFIG_A, true);
        log_info("Input config A (0x25) OK");
    } else {
        log_error("Input config A (0x25) not found");
    }

    if (i2c_probe(0x23)) {
        hw_set_available(HW_INPUT_CONFIG_B, true);
        log_info("Input config B (0x23) OK");
    } else {
        log_error("Input config B (0x23) not found");
    }

    if (i2c_probe(0x48)) {
        adc_init();
        hw_set_available(HW_ADC_A, true);
        log_info("ADC A (0x48) OK");
    } else {
        log_error("ADC A (0x48) not found");
    }

    if (i2c_probe(0x49)) {
        hw_set_available(HW_ADC_B, true);
        log_info("ADC B (0x49) OK");
    } else {
        log_error("ADC B (0x49) not found");
    }

    // Summary
    uint8_t fails = hw_fail_count();
    if (fails == 0) {
        log_info("All hardware OK");
    } else {
        log_error("%d device(s) not found", fails);
    }
}
