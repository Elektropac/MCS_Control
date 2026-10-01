#include "ssd1306.h"
#include <U8g2lib.h>
#include <Arduino.h>
#include "logging.h"
#include <stdarg.h>
#include <stdio.h>

// ------------------------------------------
// Display instance — created at init
// ------------------------------------------
static U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI* s_oled = nullptr;

// ------------------------------------------
// Layout constants
// ------------------------------------------
static const uint8_t LINE_HEIGHT = 13;
static const uint8_t TOP_OFFSET  = 8;
static const uint8_t DISPLAY_W   = 128;

// ------------------------------------------
// State
// ------------------------------------------
static bool s_enabled = true;

// ------------------------------------------
// Helpers
// ------------------------------------------
static uint8_t line_y(uint8_t line) {
    return TOP_OFFSET + (line * LINE_HEIGHT);
}

// ------------------------------------------
// Public API
// ------------------------------------------

void oled_init(uint8_t clk, uint8_t din, uint8_t cs, uint8_t dc, uint8_t rst) {
    // Explicit hardware reset BEFORE handing the pin to U8g2.
    //
    // A hard power-cut (pulling the plug) can leave the SSD1306 controller
    // latched in a bad state that U8g2's own begin() does not always clear.
    // Driving RST low -> high with proper settle time guarantees the panel
    // starts from a known state regardless of how power was lost.
    if (rst != U8X8_PIN_NONE) {
        pinMode(rst, OUTPUT);
        digitalWrite(rst, HIGH);
        delay(10);
        digitalWrite(rst, LOW);   // assert reset
        delay(10);
        digitalWrite(rst, HIGH);  // release reset
        delay(10);
    }

    static U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI display(
        U8G2_R2, clk, din, cs, dc, rst
    );
    s_oled = &display;

    bool ok = s_oled->begin();
    if (!ok) {
        log_error("  --  OLED (SSD1306) begin() FAILED");
        s_oled = nullptr;   // so draw calls stay honest no-ops, not silent lies
        return;
    }

    s_oled->setFont(u8g2_font_6x10_tr);
    s_oled->clearBuffer();
    s_oled->sendBuffer();
    log_info("  OK  OLED (SSD1306 128x64)");
}

void oled_begin() {
    if (s_oled) s_oled->clearBuffer();
}

void oled_end() {
    if (s_oled && s_enabled) {
        s_oled->sendBuffer();
    }
}

void oled_print_left(uint8_t line, const char* text) {
    if (s_oled) s_oled->drawStr(0, line_y(line), text);
}

void oled_print_center(uint8_t line, const char* text) {
    if (!s_oled) return;
    int w = s_oled->getStrWidth(text);
    s_oled->drawStr((DISPLAY_W - w) / 2, line_y(line), text);
}

void oled_print_right(uint8_t line, const char* text) {
    if (!s_oled) return;
    int w = s_oled->getStrWidth(text);
    s_oled->drawStr(DISPLAY_W - w, line_y(line), text);
}

void oled_printf_left(uint8_t line, const char* fmt, ...) {
    char buf[32];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    oled_print_left(line, buf);
}

void oled_printf_center(uint8_t line, const char* fmt, ...) {
    char buf[32];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    oled_print_center(line, buf);
}

void oled_printf_right(uint8_t line, const char* fmt, ...) {
    char buf[32];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    oled_print_right(line, buf);
}

void oled_draw_line(int x0, int y0, int x1, int y1) {
    if (s_oled) s_oled->drawLine(x0, y0, x1, y1);
}

void oled_draw_frame(int x, int y, int w, int h) {
    if (s_oled) s_oled->drawFrame(x, y, w, h);
}

void oled_draw_box(int x, int y, int w, int h) {
    if (s_oled) s_oled->drawBox(x, y, w, h);
}

void oled_draw_bitmap(int x, int y, const uint8_t* bitmap, int w, int h) {
    // U8g2 drawBitmap expects MSB-first data (same as Adafruit drawBitmap).
    // cnt = bytes per row = w / 8.
    if (s_oled && bitmap && w > 0 && h > 0) {
        s_oled->drawBitmap(x, y, w / 8, h, bitmap);
    }
}

void oled_set_enabled(bool enabled) {
    s_enabled = enabled;
    if (!s_oled) return;

    if (enabled) {
        // Wake the panel back up. Caller should redraw after this.
        s_oled->setPowerSave(0);   // display ON (SSD1306 0xAF)
    } else {
        // Real power-down: clear what's shown, then turn the panel OFF.
        // setPowerSave(1) stops the panel driving pixels (SSD1306 0xAE) —
        // lower current and a truly dark screen, not just black pixels.
        s_oled->clearBuffer();
        s_oled->sendBuffer();
        s_oled->setPowerSave(1);   // display OFF
    }
}

// Explicit panel on/off without touching the enabled flag's draw-gating.
// true  = panel ON  (pixels driven again; redraw after calling)
// false = panel OFF (dark, low power)
void oled_power(bool on) {
    if (s_oled) s_oled->setPowerSave(on ? 0 : 1);
}

bool oled_is_enabled() {
    return s_enabled;
}
