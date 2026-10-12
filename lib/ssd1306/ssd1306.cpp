#include "ssd1306.h"
#include <U8g2lib.h>
#include <Arduino.h>
#include "logging.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdarg.h>
#include <stdio.h>

// ------------------------------------------
// Display instance — created at init
// ------------------------------------------
static U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI* s_oled = nullptr;

// Serialises all access to the single U8g2 frame buffer + panel power, so
// multiple tasks (menu, splash/power manager) can never draw at once.
static SemaphoreHandle_t s_mutex = nullptr;
static inline void oled_lock()   { if (s_mutex) xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY); }
static inline void oled_unlock() { if (s_mutex) xSemaphoreGiveRecursive(s_mutex); }

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
    if (!s_mutex) s_mutex = xSemaphoreCreateRecursiveMutex();
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
    oled_lock();                     // held until oled_end()
    if (s_oled) s_oled->clearBuffer();
}

void oled_end() {
    if (s_oled && s_enabled) {
        s_oled->sendBuffer();
    }
    oled_unlock();
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

void oled_draw_str(int x, int y, const char* text) {
    if (s_oled) s_oled->drawStr(x, y, text);
}

int oled_text_width(const char* text) {
    return s_oled ? s_oled->getStrWidth(text) : 0;
}

void oled_draw_str_center(int y, const char* text) {
    if (!s_oled) return;
    int w = s_oled->getStrWidth(text);
    s_oled->drawStr((DISPLAY_W - w) / 2, y, text);
}

void oled_set_draw_color(uint8_t color) {
    if (s_oled) s_oled->setDrawColor(color);
}

void oled_fill_screen() {
    if (s_oled) s_oled->drawBox(0, 0, 128, 64);
}

static const uint8_t* title_font(uint8_t size) {
    if (size >= 18) return u8g2_font_helvB18_tr;
    if (size >= 14) return u8g2_font_helvB14_tr;
    return u8g2_font_helvB12_tr;
}

int oled_title_width(const char* text, uint8_t size) {
    if (!s_oled) return 0;
    s_oled->setFont(title_font(size));
    int w = s_oled->getStrWidth(text);
    s_oled->setFont(u8g2_font_6x10_tr);   // restore
    return w;
}

int oled_draw_title_center(int y, const char* text, uint8_t size) {
    if (!s_oled) return 0;
    s_oled->setFont(title_font(size));
    int w = s_oled->getStrWidth(text);
    s_oled->drawStr((DISPLAY_W - w) / 2, y, text);
    s_oled->setFont(u8g2_font_6x10_tr);   // restore default for other renderers
    return w;
}

void oled_draw_title_at(int x, int y, const char* text, uint8_t size) {
    if (!s_oled) return;
    s_oled->setFont(title_font(size));
    s_oled->drawStr(x, y, text);
    s_oled->setFont(u8g2_font_6x10_tr);   // restore
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

void oled_draw_rframe(int x, int y, int w, int h, int r) {
    if (s_oled) s_oled->drawRFrame(x, y, w, h, r);
}

void oled_draw_rbox(int x, int y, int w, int h, int r) {
    if (s_oled) s_oled->drawRBox(x, y, w, h, r);
}

void oled_draw_bitmap(int x, int y, const uint8_t* bitmap, int w, int h) {
    // U8g2 drawBitmap expects MSB-first data (same as Adafruit drawBitmap).
    // cnt = bytes per row = w / 8.
    if (s_oled && bitmap && w > 0 && h > 0) {
        s_oled->drawBitmap(x, y, w / 8, h, bitmap);
    }
}

void oled_draw_bitmap_scaled(int x, int y, int dstW, int dstH,
                             const uint8_t* bitmap, int srcW, int srcH) {
    if (!s_oled || !bitmap || dstW <= 0 || dstH <= 0) return;
    const int bytesPerRow = (srcW + 7) / 8;
    // For each destination pixel, sample the nearest source pixel (MSB-first).
    for (int dy = 0; dy < dstH; dy++) {
        int sy = dy * srcH / dstH;
        for (int dx = 0; dx < dstW; dx++) {
            int sx = dx * srcW / dstW;
            uint8_t byte = pgm_read_byte(&bitmap[sy * bytesPerRow + (sx >> 3)]);
            if (byte & (0x80 >> (sx & 7))) {
                s_oled->drawPixel(x + dx, y + dy);
            }
        }
    }
}

// ------------------------------------------
// QR code rendering
// ------------------------------------------
#include "qrcodegen.h"

bool oled_draw_qr(const uint8_t* qrcode) {
    if (!s_oled || !qrcode) return false;

    const int size = qrcodegen_getSize(qrcode);   // modules per side (21..177)
    if (size <= 0) return false;

    // Reserve a 1-module quiet zone on each side (scanners need the margin).
    const int QUIET = 1;
    const int total = size + 2 * QUIET;            // modules incl. quiet zone

    // Largest integer scale that fits the 64px panel height (and 128 width).
    int scale = 64 / total;
    if (128 / total < scale) scale = 128 / total;
    if (scale < 1) return false;                   // too big even at 1px/module

    const int pxCode = total * scale;              // drawn size incl. quiet zone
    const int originX = (DISPLAY_W - pxCode) / 2;  // center horizontally
    const int originY = (64 - pxCode) / 2;         // center vertically

    // White background behind the whole code (quiet zone included) so the QR
    // reads correctly regardless of what was on screen before.
    s_oled->setDrawColor(1);
    s_oled->drawBox(originX, originY, pxCode, pxCode);

    // Draw dark modules in black (draw color 0) on top of the white box.
    s_oled->setDrawColor(0);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (qrcodegen_getModule(qrcode, x, y)) {
                const int px = originX + (x + QUIET) * scale;
                const int py = originY + (y + QUIET) * scale;
                s_oled->drawBox(px, py, scale, scale);
            }
        }
    }
    s_oled->setDrawColor(1);                        // restore default
    return true;
}

void oled_set_enabled(bool enabled) {
    oled_lock();
    s_enabled = enabled;
    if (!s_oled) { oled_unlock(); return; }

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
    oled_unlock();
}

// Explicit panel on/off without touching the enabled flag's draw-gating.
// true  = panel ON  (pixels driven again; redraw after calling)
// false = panel OFF (dark, low power)
void oled_power(bool on) {
    oled_lock();
    if (s_oled) s_oled->setPowerSave(on ? 0 : 1);
    oled_unlock();
}

bool oled_is_enabled() {
    return s_enabled;
}
