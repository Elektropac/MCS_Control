#include "menu_input_config.h"
#include "menu.h"
#include "ssd1306.h"
#include "buttons.h"
#include "input_config.h"
#include "hw_status.h"
#include <Arduino.h>

// --- Manual Input Configuration Screen ---
// Page 0: Select input (A1-B4)
// Page 1: Toggle individual switches for selected input
//
// Page 0 layout:
//   > A1  A2  A3  A4
//     B1  B2  B3  B4
//   LEFT/RIGHT = move cursor, OK = enter switch view
//
// Page 1 layout:
//   Input: A1
//   > Analog:  ON
//     Pullup:  OFF
//     Shunt:   OFF
//     Digital: OFF
//   UP/DOWN = select switch, OK = toggle, LEFT = back to input select

static uint8_t s_page = 0;       // 0 = input select, 1 = switch edit
static uint8_t s_input_idx = 0;  // 0-7 (A1-A4, B1-B4)
static uint8_t s_switch_cursor = 0;  // 0-3 (analog, pullup, shunt, digital)

static const char* INPUT_NAMES[] = { "A1", "A2", "A3", "A4", "B1", "B2", "B3", "B4" };
static const char* SWITCH_NAMES[] = { "Analog", "Pullup", "Shunt", "Digital" };
static const InputSwitch SWITCHES[] = { SW_ANALOG, SW_PULLUP, SW_SHUNT, SW_DIGITAL };

static Input idx_to_input(uint8_t idx) {
    return (Input)idx;
}

// --- Page 0: Input Select ---
static void render_input_select() {
    oled_begin();

    oled_set_font_bold();
    oled_draw_text(4, 11, "Input Config");
    oled_draw_line(0, 13, 127, 13);

    oled_set_font_normal();

    // Row A (inputs 0-3)
    for (uint8_t i = 0; i < 4; i++) {
        int x = 8 + i * 30;
        bool selected = (s_input_idx == i);
        if (selected) {
            oled_draw_rframe(x - 3, 18, 26, 16, 2);
        }
        oled_draw_text(x, 30, INPUT_NAMES[i]);
    }

    // Row B (inputs 4-7)
    for (uint8_t i = 0; i < 4; i++) {
        int x = 8 + i * 30;
        bool selected = (s_input_idx == i + 4);
        if (selected) {
            oled_draw_rframe(x - 3, 36, 26, 16, 2);
        }
        oled_draw_text(x, 48, INPUT_NAMES[i + 4]);
    }

    oled_draw_text(4, 62, "\x1a\x1b sel  OK=edit  \x1b=back");

    oled_end();
}

// --- Page 1: Switch Toggle ---
static void render_switch_edit() {
    Input inp = idx_to_input(s_input_idx);
    bool hw_ok = (s_input_idx < 4) ? hw_available(HW_INPUT_CONFIG_A) : hw_available(HW_INPUT_CONFIG_B);

    oled_begin();

    oled_set_font_bold();
    char title[18];
    snprintf(title, sizeof(title), "Input %s", INPUT_NAMES[s_input_idx]);
    oled_draw_text(4, 11, title);
    oled_draw_line(0, 13, 127, 13);

    oled_set_font_normal();

    if (!hw_ok) {
        oled_draw_text(4, 34, "OFFLINE");
    } else {
        for (uint8_t i = 0; i < 4; i++) {
            int y = 26 + i * 11;
            bool on = input_config_get(inp, SWITCHES[i]);
            char buf[22];
            snprintf(buf, sizeof(buf), "%s%-8s %s",
                     (s_switch_cursor == i) ? ">" : " ",
                     SWITCH_NAMES[i],
                     on ? "ON" : "OFF");
            if (s_switch_cursor == i) {
                oled_set_font_bold();
            }
            oled_draw_text(4, y, buf);
            oled_set_font_normal();
        }
    }

    oled_draw_text(4, 62, "OK=toggle  \x1b=back");

    oled_end();
}

// --- Render dispatcher ---
static void render_input_config() {
    switch (s_page) {
        case 0: render_input_select(); break;
        case 1: render_switch_edit(); break;
    }
}

// --- Button handler ---
static void input_config_buttons(uint8_t button) {
    if (s_page == 0) {
        // Input select page
        switch (button) {
            case BTN_RIGHT:
                if (s_input_idx < 7) s_input_idx++;
                break;
            case BTN_LEFT:
                if (s_input_idx > 0) s_input_idx--;
                else menu_set_screen(nullptr);  // back to menu
                break;
            case BTN_DOWN:
                if (s_input_idx < 4) s_input_idx += 4;
                break;
            case BTN_UP:
                if (s_input_idx >= 4) s_input_idx -= 4;
                break;
            case BTN_OK:
                s_page = 1;
                s_switch_cursor = 0;
                break;
        }
    } else {
        // Switch edit page
        switch (button) {
            case BTN_UP:
                if (s_switch_cursor > 0) s_switch_cursor--;
                break;
            case BTN_DOWN:
                if (s_switch_cursor < 3) s_switch_cursor++;
                break;
            case BTN_OK:
            case BTN_RIGHT: {
                Input inp = idx_to_input(s_input_idx);
                bool current = input_config_get(inp, SWITCHES[s_switch_cursor]);
                input_config_set(inp, SWITCHES[s_switch_cursor], !current);
                break;
            }
            case BTN_LEFT:
                s_page = 0;  // back to input select
                break;
        }
    }
}

// --- Public API ---
void input_config_menu_show() {
    s_page = 0;
    s_input_idx = 0;
    s_switch_cursor = 0;
    menu_set_screen(render_input_config);
    menu_set_screen_buttons(input_config_buttons);
}
