#include "menu_voltage.h"
#include "menu.h"
#include "voltage_select.h"
#include "ssd1306.h"
#include "buttons.h"
#include <Arduino.h>

// --- Voltage control screen ---
// Shows both channels, UP/DOWN selects channel, LEFT/RIGHT changes voltage, LEFT on channel exits

static uint8_t s_cursor = 0;  // 0 = channel A, 1 = channel B
static Voltage s_volt_a = VOLTAGE_OFF;
static Voltage s_volt_b = VOLTAGE_OFF;

static const char* voltage_label(Voltage v) {
    switch (v) {
        case VOLTAGE_OFF: return "OFF";
        case VOLTAGE_5V:  return " 5V";
        case VOLTAGE_12V: return "12V";
        case VOLTAGE_24V: return "24V";
    }
    return "?";
}

static Voltage voltage_next(Voltage v) {
    switch (v) {
        case VOLTAGE_OFF: return VOLTAGE_5V;
        case VOLTAGE_5V:  return VOLTAGE_12V;
        case VOLTAGE_12V: return VOLTAGE_24V;
        case VOLTAGE_24V: return VOLTAGE_OFF;
    }
    return VOLTAGE_OFF;
}

static Voltage voltage_prev(Voltage v) {
    switch (v) {
        case VOLTAGE_OFF: return VOLTAGE_24V;
        case VOLTAGE_5V:  return VOLTAGE_OFF;
        case VOLTAGE_12V: return VOLTAGE_5V;
        case VOLTAGE_24V: return VOLTAGE_12V;
    }
    return VOLTAGE_OFF;
}

static void apply_voltage() {
    voltage_select_set_a(s_volt_a);
    voltage_select_set_b(s_volt_b);
}

static void render_voltage_screen() {
    oled_begin();

    // Title
    oled_set_font_bold();
    oled_draw_text(4, 12, "Channel Voltage");

    // Channel A
    oled_set_font_normal();
    char line_a[24];
    snprintf(line_a, sizeof(line_a), "%s Ch A: [%s]", (s_cursor == 0) ? ">" : " ", voltage_label(s_volt_a));
    oled_draw_text(8, 32, line_a);

    // Channel B
    char line_b[24];
    snprintf(line_b, sizeof(line_b), "%s Ch B: [%s]", (s_cursor == 1) ? ">" : " ", voltage_label(s_volt_b));
    oled_draw_text(8, 48, line_b);

    // Footer
    oled_draw_text(4, 62, "OK/R=next L=back");

    oled_end();
}

static void voltage_screen_buttons(uint8_t button) {
    switch (button) {
        case BTN_UP:
            if (s_cursor > 0) s_cursor--;
            break;
        case BTN_DOWN:
            if (s_cursor < 1) s_cursor++;
            break;
        case BTN_OK:
        case BTN_RIGHT:
            if (s_cursor == 0) s_volt_a = voltage_next(s_volt_a);
            else               s_volt_b = voltage_next(s_volt_b);
            apply_voltage();
            break;
        case BTN_LEFT:
            menu_set_screen(nullptr);  // back to menu
            break;
    }
}

static void action_voltage() {
    s_cursor = 0;
    menu_set_screen(render_voltage_screen);
    menu_set_screen_buttons(voltage_screen_buttons);
}

// --- Submenu entry ---
static MenuItem s_voltage_items[1];

void voltage_submenu_build() {
    s_voltage_items[0] = { "Voltage", nullptr, action_voltage, nullptr, 0 };
}

const MenuItem* voltage_submenu_get_items() {
    return s_voltage_items;
}

uint8_t voltage_submenu_get_count() {
    return 1;
}
