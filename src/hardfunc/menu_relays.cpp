#include "menu_relays.h"
#include "menu.h"
#include "relays.h"
#include "ssd1306.h"
#include "buttons.h"
#include <Arduino.h>

// --- Relay control screen ---
// Shows both relays, UP/DOWN selects, OK toggles, LEFT exits

static uint8_t s_cursor = 0;  // 0 = relay A, 1 = relay B

static void render_relay_screen() {
    bool a_on = relay_get(RELAY_A);
    bool b_on = relay_get(RELAY_B);

    oled_begin();

    // Title
    oled_set_font_bold();
    oled_draw_text(4, 12, "Relay Override");

    // Relay A
    oled_set_font_normal();
    char line_a[24];
    snprintf(line_a, sizeof(line_a), "%s Relay A: %s", (s_cursor == 0) ? ">" : " ", a_on ? "ON " : "OFF");
    oled_draw_text(8, 32, line_a);

    // Relay B
    char line_b[24];
    snprintf(line_b, sizeof(line_b), "%s Relay B: %s", (s_cursor == 1) ? ">" : " ", b_on ? "ON " : "OFF");
    oled_draw_text(8, 48, line_b);

    // Footer
    oled_draw_text(4, 62, "OK=toggle  <=back");

    oled_end();
}

// Handle button input when relay screen is active
static void relay_screen_buttons(uint8_t button) {
    switch (button) {
        case BTN_UP:
            if (s_cursor > 0) s_cursor--;
            break;
        case BTN_DOWN:
            if (s_cursor < 1) s_cursor++;
            break;
        case BTN_OK:
        case BTN_RIGHT:
            if (s_cursor == 0) relay_set(RELAY_A, !relay_get(RELAY_A));
            else               relay_set(RELAY_B, !relay_get(RELAY_B));
            break;
        case BTN_LEFT:
            menu_set_screen(nullptr);  // back to menu
            break;
    }
}

static void action_relays() {
    s_cursor = 0;
    menu_set_screen(render_relay_screen);
    menu_set_screen_buttons(relay_screen_buttons);
}

// --- Submenu entry ---
static MenuItem s_relay_items[1];

void relay_submenu_build() {
    s_relay_items[0] = { "Relays", nullptr, action_relays, nullptr, 0 };
}

const MenuItem* relay_submenu_get_items() {
    return s_relay_items;
}

uint8_t relay_submenu_get_count() {
    return 1;
}
