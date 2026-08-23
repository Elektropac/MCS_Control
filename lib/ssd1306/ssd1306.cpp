#include "ssd1306.h"
#include <U8g2lib.h>
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
    static U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI display(
        U8G2_R2, clk, din, cs, dc, rst
    );
    s_oled = &display;
    s_oled->begin();
    s_oled->setFont(u8g2_font_6x10_tr);
    s_oled->clearBuffer();
    s_oled->sendBuffer();
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

void oled_draw_rframe(int x, int y, int w, int h, int r) {
    if (s_oled) s_oled->drawRFrame(x, y, w, h, r);
}

void oled_draw_box(int x, int y, int w, int h) {
    if (s_oled) s_oled->drawBox(x, y, w, h);
}

void oled_draw_xbm(int x, int y, int w, int h, const uint8_t* bitmap) {
    if (s_oled) s_oled->drawXBM(x, y, w, h, bitmap);
}

void oled_draw_bitmap(int x, int y, int w, int h, const uint8_t* bitmap) {
    // Draws a PROGMEM bitmap in Adafruit_GFX format (MSB first, row-major)
    // U8g2 drawBitmap uses same format: cnt = bytes per row, h = rows
    if (s_oled) s_oled->drawBitmap(x, y, w / 8, h, bitmap);
}

void oled_draw_text(int x, int y, const char* text) {
    if (s_oled) s_oled->drawStr(x, y, text);
}

void oled_set_font_normal() {
    if (s_oled) s_oled->setFont(u8g2_font_6x10_tr);
}

void oled_set_font_bold() {
    if (s_oled) s_oled->setFont(u8g2_font_7x14B_tr);
}

void oled_set_color(uint8_t color) {
    if (s_oled) s_oled->setDrawColor(color);
}

void oled_set_enabled(bool enabled) {
    s_enabled = enabled;
    if (!enabled && s_oled) {
        s_oled->clearBuffer();
        s_oled->sendBuffer();
    }
}

bool oled_is_enabled() {
    return s_enabled;
}
