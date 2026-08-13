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

void all_drivers_init() {
    // --- Non-I2C drivers ---
    buttons_init();
    buzzer_init();
    oled_init();

    // --- I2C drivers (probe before init) ---
    if (i2c_probe(0x21)) {
        voltage_select_init();
        Serial.println("[OK] Voltage select (0x21)");
    } else {
        Serial.println("[FAIL] Voltage select (0x21) not found");
    }

    if (i2c_probe(0x27)) {
        serial_control_init();
        relays_init();
        Serial.println("[OK] Serial control + relays (0x27)");
    } else {
        Serial.println("[FAIL] Serial control + relays (0x27) not found");
    }

    if (i2c_probe(0x25)) {
        input_config_init();  // this also inits 0x23 internally
        Serial.println("[OK] Input config A (0x25)");
    } else {
        Serial.println("[FAIL] Input config A (0x25) not found");
    }

    if (i2c_probe(0x23)) {
        Serial.println("[OK] Input config B (0x23)");
    } else {
        Serial.println("[FAIL] Input config B (0x23) not found");
    }

    if (i2c_probe(0x48)) {
        adc_init();
        Serial.println("[OK] ADC A (0x48)");
    } else {
        Serial.println("[FAIL] ADC A (0x48) not found");
    }

    if (i2c_probe(0x49)) {
        Serial.println("[OK] ADC B (0x49)");
    } else {
        Serial.println("[FAIL] ADC B (0x49) not found");
    }
}
