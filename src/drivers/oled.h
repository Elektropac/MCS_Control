#pragma once
// =======================================================
// OLED — SSD1306 128x64 display via SPI (U8g2)
// =======================================================
//
// Software SPI on pins from board/pins.h:
//   OLED_CLK, OLED_DIN, OLED_CS, OLED_DC, OLED_RST
//
// Uses full frame buffer (F mode) for flicker-free updates.
// Call oled_begin() before drawing, oled_end() to push to display.
//
// =======================================================
#include <Arduino.h>

// Initialize display
void oled_init();

// Start a new frame (clears buffer)
void oled_begin();

// Push frame buffer to display
void oled_end();

// Text drawing
void oled_print_left(uint8_t line, const char* text);
void oled_print_center(uint8_t line, const char* text);
void oled_print_right(uint8_t line, const char* text);

// Formatted text
void oled_printf_left(uint8_t line, const char* fmt, ...);
void oled_printf_center(uint8_t line, const char* fmt, ...);
void oled_printf_right(uint8_t line, const char* fmt, ...);

// Primitives
void oled_draw_line(int x0, int y0, int x1, int y1);
void oled_draw_frame(int x, int y, int w, int h);
void oled_draw_box(int x, int y, int w, int h);

// Enable/disable display
void oled_set_enabled(bool enabled);
bool oled_is_enabled();
