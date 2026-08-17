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

// Primitives
void oled_draw_line(int x0, int y0, int x1, int y1);
void oled_draw_frame(int x, int y, int w, int h);
void oled_draw_box(int x, int y, int w, int h);

// Enable/disable display
void oled_set_enabled(bool enabled);
bool oled_is_enabled();
