#include "menu_status_channel.h"
#include "menu.h"
#include "ssd1306.h"
#include "buttons.h"
#include "voltage_select.h"
#include "input_config.h"
#include "adc.h"
#include "hw_status.h"
#include <Arduino.h>

// --- Channel status screen ---
// Page 0: Voltage + input modes (4 inputs)
// Page 1: Live ADC readings (4 inputs, mV)
// UP/DOWN = switch page, LEFT = back to menu

static uint8_t s_page = 0;
static uint8_t s_channel = 0;  // 0 = A, 1 = B

#define PAGE_COUNT 2

// --- Helpers ---

static const char* mode_short(Input input) {
    bool sw_a = input_config_get(input, SW_ANALOG);
    bool sw_s = input_config_get(input, SW_SHUNT);
    bool sw_p = input_config_get(input, SW_PULLUP);
    bool sw_d = input_config_get(input, SW_DIGITAL);

    if (!sw_a && !sw_s && !sw_p && !sw_d) return "OFF";
    if (sw_a && !sw_s && !sw_p && !sw_d)  return "VOLT";
    if (sw_a && sw_s && !sw_p && !sw_d)   return "mA";
    if (!sw_a && !sw_s && !sw_p && sw_d)   return "DIG";
    if (!sw_a && !sw_s && sw_p && sw_d)    return "DPU";
    if (!sw_a && !sw_s && sw_p && !sw_d)   return "PU";
    if (!sw_a && sw_s && !sw_p && !sw_d)   return "SHT";
    return "?";
}

static const char* volt_label(Voltage v) {
    switch (v) {
        case VOLTAGE_OFF: return "OFF";
        case VOLTAGE_5V:  return "5V";
        case VOLTAGE_12V: return "12V";
        case VOLTAGE_24V: return "24V";
    }
    return "?";
}

// --- Page 0: Voltage + Input Modes ---
static void render_page_modes() {
    char ch = (s_channel == 0) ? 'A' : 'B';
    Input base = (s_channel == 0) ? INPUT_A1 : INPUT_B1;
    bool hw_ok = (s_channel == 0) ? hw_available(HW_INPUT_CONFIG_A) : hw_available(HW_INPUT_CONFIG_B);

    oled_begin();

    // Header bar
    oled_set_font_bold();
    char title[18];
    snprintf(title, sizeof(title), "Ch %c  Status", ch);
    oled_draw_text(4, 11, title);
    oled_draw_line(0, 13, 127, 13);

    oled_set_font_normal();

    if (!hw_ok) {
        oled_draw_text(4, 34, "OFFLINE");
        oled_draw_text(4, 50, "I2C not responding");
    } else {
        // Voltage
        Voltage v = (s_channel == 0) ? voltage_select_get_a() : voltage_select_get_b();
        char vbuf[22];
        snprintf(vbuf, sizeof(vbuf), "Voltage: %s", volt_label(v));
        oled_draw_text(4, 26, vbuf);

        // Input modes — 2 columns
        char buf[22];
        snprintf(buf, sizeof(buf), "%c1:%-4s  %c3:%-4s", ch, mode_short((Input)(base + 0)), ch, mode_short((Input)(base + 2)));
        oled_draw_text(4, 40, buf);

        snprintf(buf, sizeof(buf), "%c2:%-4s  %c4:%-4s", ch, mode_short((Input)(base + 1)), ch, mode_short((Input)(base + 3)));
        oled_draw_text(4, 52, buf);
    }

    // Footer
    oled_draw_text(4, 62, "\x18\x19 page  \x1b back");
    oled_draw_text(108, 62, "1/2");

    oled_end();
}

// --- Page 1: ADC Readings ---
static void render_page_adc() {
    char ch = (s_channel == 0) ? 'A' : 'B';
    AdcInput base = (s_channel == 0) ? ADC_A1 : ADC_B1;
    bool hw_ok = (s_channel == 0) ? hw_available(HW_ADC_A) : hw_available(HW_ADC_B);

    oled_begin();

    // Header bar
    oled_set_font_bold();
    char title[18];
    snprintf(title, sizeof(title), "Ch %c  ADC", ch);
    oled_draw_text(4, 11, title);
    oled_draw_line(0, 13, 127, 13);

    oled_set_font_normal();

    if (!hw_ok) {
        oled_draw_text(4, 34, "ADC OFFLINE");
    } else {
        char buf[22];
        int32_t mv1 = adc_read_mv((AdcInput)(base + 0));
        int32_t mv2 = adc_read_mv((AdcInput)(base + 1));
        int32_t mv3 = adc_read_mv((AdcInput)(base + 2));
        int32_t mv4 = adc_read_mv((AdcInput)(base + 3));

        snprintf(buf, sizeof(buf), "%c1: %2ld.%02ldV", ch, mv1/1000, (labs(mv1)%1000)/10);
        oled_draw_text(4, 26, buf);
        snprintf(buf, sizeof(buf), "%c2: %2ld.%02ldV", ch, mv2/1000, (labs(mv2)%1000)/10);
        oled_draw_text(4, 37, buf);
        snprintf(buf, sizeof(buf), "%c3: %2ld.%02ldV", ch, mv3/1000, (labs(mv3)%1000)/10);
        oled_draw_text(4, 48, buf);
        snprintf(buf, sizeof(buf), "%c4: %2ld.%02ldV", ch, mv4/1000, (labs(mv4)%1000)/10);
        oled_draw_text(4, 59, buf);
    }

    oled_draw_text(108, 62, "2/2");

    oled_end();
}

// --- Render dispatcher ---
static void render_channel_status() {
    switch (s_page) {
        case 0: render_page_modes(); break;
        case 1: render_page_adc(); break;
    }
}

// --- Button handler ---
static void channel_status_buttons(uint8_t button) {
    switch (button) {
        case BTN_DOWN:
        case BTN_RIGHT:
            s_page = (s_page + 1) % PAGE_COUNT;
            break;
        case BTN_UP:
            if (s_page > 0) s_page--;
            else s_page = PAGE_COUNT - 1;
            break;
        case BTN_LEFT:
            menu_set_screen(nullptr);  // back to menu
            break;
        case BTN_OK:
            s_page = (s_page + 1) % PAGE_COUNT;
            break;
    }
}

// --- Public API ---

void channel_a_status_show() {
    s_channel = 0;
    s_page = 0;
    menu_set_screen(render_channel_status);
    menu_set_screen_buttons(channel_status_buttons);
}

void channel_b_status_show() {
    s_channel = 1;
    s_page = 0;
    menu_set_screen(render_channel_status);
    menu_set_screen_buttons(channel_status_buttons);
}
