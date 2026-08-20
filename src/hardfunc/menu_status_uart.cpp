#include "menu_status_uart.h"
#include "menu.h"
#include "ssd1306.h"
#include "buttons.h"
#include "serial_control.h"
#include "hw_status.h"
#include <Arduino.h>

// --- UART status screen ---
// Shows mode + DE/RE/TERM for both channels on one page
// LEFT = back to menu

static const char* mode_label(ComMode m) {
    switch (m) {
        case COM_OFF:   return "OFF";
        case COM_RS232: return "RS232";
        case COM_RS485: return "RS485";
    }
    return "?";
}

static void render_uart_status() {
    bool hw_ok = hw_available(HW_SERIAL_CONTROL);

    oled_begin();

    // Header
    oled_set_font_bold();
    oled_draw_text(4, 11, "UART Status");
    oled_draw_line(0, 13, 127, 13);

    oled_set_font_normal();

    if (!hw_ok) {
        oled_draw_text(4, 34, "OFFLINE");
        oled_draw_text(4, 50, "I2C not responding");
    } else {
        ComMode mode_a = serial_get_mode(CHANNEL_A);
        ComMode mode_b = serial_get_mode(CHANNEL_B);
        bool de_a = serial_get_de(CHANNEL_A);
        bool re_a = serial_get_re(CHANNEL_A);
        bool term_a = serial_get_termination(CHANNEL_A);
        bool de_b = serial_get_de(CHANNEL_B);
        bool re_b = serial_get_re(CHANNEL_B);
        bool term_b = serial_get_termination(CHANNEL_B);

        // Channel A
        char buf[24];
        snprintf(buf, sizeof(buf), "A: %-5s", mode_label(mode_a));
        oled_draw_text(4, 26, buf);

        if (mode_a == COM_RS485) {
            snprintf(buf, sizeof(buf), "  DE:%s RE:%s T:%s",
                     de_a ? "1" : "0", re_a ? "1" : "0", term_a ? "1" : "0");
            oled_draw_text(4, 37, buf);
        }

        // Channel B
        snprintf(buf, sizeof(buf), "B: %-5s", mode_label(mode_b));
        oled_draw_text(4, 50, buf);

        if (mode_b == COM_RS485) {
            snprintf(buf, sizeof(buf), "  DE:%s RE:%s T:%s",
                     de_b ? "1" : "0", re_b ? "1" : "0", term_b ? "1" : "0");
            oled_draw_text(4, 61, buf);
        }
    }

    // Footer only if both modes are not RS485 (there's space)
    // (If RS485 details shown, they take the space)

    oled_end();
}

static void uart_status_buttons(uint8_t button) {
    switch (button) {
        case BTN_LEFT:
            menu_set_screen(nullptr);  // back to menu
            break;
        default:
            break;  // no page switching needed — single page
    }
}

// --- Public API ---
void uart_status_show() {
    menu_set_screen(render_uart_status);
    menu_set_screen_buttons(uart_status_buttons);
}
