#include "all_drivers_init.h"
#include "hw_status.h"
#include "pins.h"
#include "hal.h"
#include "logging.h"
#include "ssd1306.h"
#include "buzzer.h"
#include "buttons.h"
#include "voltage_select.h"
#include "serial_control.h"
#include "input_config.h"
#include "relays.h"
#include "adc.h"
#include "version.h"

struct ProbeEntry {
    uint8_t addr;
    HwDevice device;
    const char* name;
};

static const ProbeEntry PROBES[] = {
    { ADDR_VOLTAGE_SELECT, HW_VOLTAGE_SELECT, "Voltage Select (0x21)" },
    { ADDR_INPUT_CONFIG_B, HW_INPUT_CONFIG_B, "Input Config B (0x23)" },
    { ADDR_INPUT_CONFIG_A, HW_INPUT_CONFIG_A, "Input Config A (0x25)" },
    { ADDR_SERIAL_CONTROL, HW_SERIAL_CONTROL, "Serial Control (0x27)" },
    { ADDR_ADC_A,          HW_ADC_A,          "ADC A (0x48)" },
    { ADDR_ADC_B,          HW_ADC_B,          "ADC B (0x49)" },
};

void all_drivers_init() {
    // Probe all I2C devices
    for (const auto &p : PROBES) {
        bool found = i2c_probe(p.addr);
        hw_set_available(p.device, found);
        if (found) {
            log_info("  OK  %s", p.name);
        } else {
            log_error("  --  %s NOT FOUND", p.name);
        }
    }

    // Init drivers (each checks hw_available internally)
    voltage_select_init();
    serial_control_init();
    input_config_init();
    relays_init();
    adc_init();

    // Non-I2C peripherals
    oled_init(OLED_CLK, OLED_DIN, OLED_CS, OLED_DC, OLED_RST);
    buzzer_init(PIN_BUZZER);
    buttons_init(PIN_BUTTON);

    // Boot screen
    oled_begin();
    oled_print_center(1, "MCS Control");
    oled_print_center(3, "Starting...");
    oled_end();
}
