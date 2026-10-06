#pragma once
// =======================================================
// SSD1306 — 128x64 OLED display driver (SW SPI, U8g2)
// =======================================================
// Generic wrapper around U8g2. Pins specified at init.
// No board-specific code — just a display API.
//
// Usage:
//   oled_init(clk, din, cs, dc, rst);
//   oled_begin();
//   oled_print_left(0, "Hello");
//   oled_end();
//
// =======================================================
#include <Arduino.h>

// Initialize display with pin assignments
void oled_init(uint8_t clk, uint8_t din, uint8_t cs, uint8_t dc, uint8_t rst);

// Start a new frame (clears buffer)
void oled_begin();

// Push frame buffer to display
void oled_end();

// Text drawing (line 0-4)
void oled_print_left(uint8_t line, const char* text);
void oled_print_center(uint8_t line, const char* text);
void oled_print_right(uint8_t line, const char* text);

// Formatted text
void oled_printf_left(uint8_t line, const char* fmt, ...);
void oled_printf_center(uint8_t line, const char* fmt, ...);
void oled_printf_right(uint8_t line, const char* fmt, ...);

// Low-level: draw text at an absolute pixel position (baseline at y).
// For custom layouts that don't fit the 13px line grid. Font is 6x10.
void oled_draw_str(int x, int y, const char* text);
void oled_draw_str_center(int y, const char* text);   // horizontally centered
int  oled_text_width(const char* text);

// Set the active draw color: 1 = pixels on (white), 0 = off (black). Lets a
// caller draw "black on white" after filling the screen (inverted look).
void oled_set_draw_color(uint8_t color);
// Fill the entire 128x64 screen (uses current draw color).
void oled_fill_screen();

// Primitives
void oled_draw_line(int x0, int y0, int x1, int y1);
void oled_draw_frame(int x, int y, int w, int h);
void oled_draw_box(int x, int y, int w, int h);

// Rounded-corner frame / filled box (radius r).
void oled_draw_rframe(int x, int y, int w, int h, int r);
void oled_draw_rbox(int x, int y, int w, int h, int r);

// Draw a monochrome bitmap (MSB-first, e.g. Adafruit drawBitmap format).
// w must be a multiple of 8. Data is in PROGMEM.
void oled_draw_bitmap(int x, int y, const uint8_t* bitmap, int w, int h);

// Enable/disable display
void oled_set_enabled(bool enabled);
bool oled_is_enabled();

// Turn the physical panel on/off (SSD1306 power-save). ON = pixels driven
// (redraw after calling), OFF = dark + low power.
void oled_power(bool on);
