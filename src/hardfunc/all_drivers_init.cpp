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
    // === ANIMATED SPLASH SCREEN ===

    // Phase 1: Frame draws itself from center out
    oled_init(OLED_CLK, OLED_DIN, OLED_CS, OLED_DC, OLED_RST);
    for (int i = 0; i <= 63; i += 3) {
        oled_begin();
        int x = 63 - i;
        int y = 31 - (i / 2);
        int w = i * 2 + 2;
        int h = i + 2;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (w > 128) w = 128;
        if (h > 64) h = 64;
        oled_draw_frame(x, y, w, h);
        oled_end();
        delay(20);
    }

    // Phase 2: "CONTROL" appears letter by letter (big font)
    const char* title = "CONTROL";
    oled_set_font_bold();
    for (int i = 1; i <= 7; i++) {
        oled_begin();
        oled_draw_frame(0, 0, 128, 64);
        char buf[8] = {0};
        strncpy(buf, title, i);
        oled_set_font_bold();
        // Center the partial text
        oled_draw_text(16, 20, buf);
        oled_set_font_normal();
        oled_draw_text(30, 35, "Micro FMS");
        oled_end();
        delay(80);
    }
    delay(200);

    // Phase 3: Hardware init with live status + progress bar
    struct InitStep {
        const char* label;
        void (*action)();
    };

    int total_steps = 10;
    int step = 0;

    auto draw_progress = [&](const char* label, bool ok) {
        step++;
        oled_begin();
        oled_draw_frame(0, 0, 128, 64);
        oled_set_font_bold();
        oled_draw_text(16, 20, "CONTROL");
        oled_set_font_normal();
        oled_draw_text(30, 35, "Micro FMS");

        // Status line
        char status[32];
        snprintf(status, sizeof(status), "%s %s", label, ok ? "\x80" : "!"); // checkmark or !
        oled_draw_text(4, 52, status);

        // Progress bar
        int bar_w = (step * 120) / total_steps;
        oled_draw_frame(3, 56, 122, 7);
        oled_draw_box(4, 57, bar_w, 5);
        oled_end();
        delay(80);
    };

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
    draw_progress("I2C scan", true);

    // Init drivers with progress
    voltage_select_init();
    draw_progress("Voltage", true);

    serial_control_init();
    draw_progress("Serial", true);

    input_config_init();
    draw_progress("Inputs", true);

    relays_init();
    draw_progress("Relays", true);

    adc_init();
    draw_progress("ADC", true);

    // Non-I2C
    buzzer_init(PIN_BUZZER);
    draw_progress("Buzzer", true);

    buttons_init(PIN_BUTTON);
    draw_progress("Buttons", true);

    buzzer_sound_startup();
    draw_progress("Audio", true);

    draw_progress("Ready!", true);
    delay(500);
}
