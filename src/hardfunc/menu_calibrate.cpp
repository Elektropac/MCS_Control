#include "menu_calibrate.h"
#include "menu.h"
#include "ssd1306.h"
#include "buttons.h"
#include "adc.h"
#include <Arduino.h>

// --- ADC Zero Calibration Screen ---
// Shows warning, OK to start, shows results when done
// LEFT = back to menu at any time

static enum { CAL_CONFIRM, CAL_RUNNING, CAL_DONE } s_state;

static void render_calibrate() {
    oled_begin();

    oled_set_font_bold();
    oled_draw_text(4, 11, "ADC Calibrate");
    oled_draw_line(0, 13, 127, 13);

    oled_set_font_normal();

    switch (s_state) {
        case CAL_CONFIRM:
            oled_draw_text(4, 28, "Disconnect all");
            oled_draw_text(4, 40, "inputs first!");
            oled_set_font_bold();
            oled_draw_text(4, 56, "OK=start  \x1b=cancel");
            break;

        case CAL_RUNNING:
            oled_draw_text(4, 38, "Calibrating...");
            break;

        case CAL_DONE:
            // Show offsets in 2 columns
            {
                char buf[22];
                const char* names[] = {"A1","A2","A3","A4","B1","B2","B3","B4"};
                snprintf(buf, sizeof(buf), "%s:%3d %s:%3d",
                         names[0], adc_get_offset(ADC_A1),
                         names[4], adc_get_offset(ADC_B1));
                oled_draw_text(4, 24, buf);
                snprintf(buf, sizeof(buf), "%s:%3d %s:%3d",
                         names[1], adc_get_offset(ADC_A2),
                         names[5], adc_get_offset(ADC_B2));
                oled_draw_text(4, 34, buf);
                snprintf(buf, sizeof(buf), "%s:%3d %s:%3d",
                         names[2], adc_get_offset(ADC_A3),
                         names[6], adc_get_offset(ADC_B3));
                oled_draw_text(4, 44, buf);
                snprintf(buf, sizeof(buf), "%s:%3d %s:%3d",
                         names[3], adc_get_offset(ADC_A4),
                         names[7], adc_get_offset(ADC_B4));
                oled_draw_text(4, 54, buf);
            }
            oled_draw_text(4, 63, "Saved. \x1b=back");
            break;
    }

    oled_end();
}

static void calibrate_buttons(uint8_t button) {
    switch (s_state) {
        case CAL_CONFIRM:
            if (button == BTN_OK || button == BTN_RIGHT) {
                s_state = CAL_RUNNING;
                // Render the "running" screen immediately
                render_calibrate();
                // Run calibration (blocks ~200ms)
                adc_calibrate_zero();
                s_state = CAL_DONE;
            } else if (button == BTN_LEFT) {
                menu_set_screen(nullptr);
            }
            break;

        case CAL_RUNNING:
            // Ignore buttons during calibration
            break;

        case CAL_DONE:
            if (button == BTN_LEFT || button == BTN_OK) {
                menu_set_screen(nullptr);
            }
            break;
    }
}

// --- Public API ---
void calibrate_menu_show() {
    s_state = CAL_CONFIRM;
    menu_set_screen(render_calibrate);
    menu_set_screen_buttons(calibrate_buttons);
}
