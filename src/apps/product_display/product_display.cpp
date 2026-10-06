#include "product_display.h"
#include "control_oled_128x64_bitmaps.h"
#include "mcs_logo.h"
#include "ssd1306.h"
#include "logging.h"
#include "apps/menu/menu.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// config_read_string lives in the config namespace (config.h). We only need
// this one function here, so forward-declare it rather than pull in the header.
namespace config {
    String config_read_string(const char* section, const char* key,
                              const char* fallback);
}

// How long the boot splash stays on screen.
static const uint32_t SPLASH_HOLD_MS = 2500;
// How long the MCS brand logo shows before the product splash.
static const uint32_t LOGO_HOLD_MS = 2000;

// Turn the panel off after this much inactivity (10 minutes).
static const uint32_t INACTIVITY_TIMEOUT_MS = 10UL * 60UL * 1000UL; // 10 min

// Shared state for the power manager.
static volatile uint32_t s_last_activity_ms = 0;   // millis() of last activity
static volatile bool     s_panel_on         = true;

// Map a guide name to its 128x64 bitmap. Returns nullptr if unknown.
// Product names: CloudGauge2, Sapphire Lite Mini, Poseidon, Bridge.
static const unsigned char* bitmap_for_guide(const String& guide) {
    if (guide == "cloudgauge2")        return cloudgauge2_128x64;
    if (guide == "sapphire_lite_mini") return sapphire_lite_mini_128x64;
    if (guide == "poseidon")           return poseidon_128x64;
    if (guide == "bridge")             return bridge_128x64;
    return nullptr;
}

// Draw the product splash for the current guide (no delay). Returns true if
// an image was drawn. Shared by boot splash and wake-from-sleep redraw.
static bool draw_product_splash() {
    String guide = config::config_read_string("config", "guide", "");
    const unsigned char* bmp = bitmap_for_guide(guide);
    if (!bmp) return false;
    oled_begin();
    oled_draw_bitmap(0, 0, bmp, 128, 64);
    oled_end();
    return true;
}

void mcs_logo_show_splash() {
    log_info("[product_display] showing MCS logo splash");
    oled_begin();
    oled_draw_bitmap(0, 0, mcs_logo_horizontal, 128, 64);
    oled_end();
    vTaskDelay(pdMS_TO_TICKS(LOGO_HOLD_MS));
}

void product_display_show_splash() {
    String guide = config::config_read_string("config", "guide", "");
    const unsigned char* bmp = bitmap_for_guide(guide);

    if (!bmp) {
        log_info("[product_display] guide '%s' has no splash image", guide.c_str());
        return;
    }

    log_info("[product_display] showing splash for guide '%s'", guide.c_str());
    oled_begin();
    oled_draw_bitmap(0, 0, bmp, 128, 64);
    oled_end();
    vTaskDelay(pdMS_TO_TICKS(SPLASH_HOLD_MS));
}

// -----------------------------------------------------------------
// Power manager
// -----------------------------------------------------------------
bool product_display_panel_on() {
    return s_panel_on;
}

bool product_display_mark_activity() {
    s_last_activity_ms = millis();
    if (!s_panel_on) {
        // Was asleep: wake to the HOME screen. Draw HOME into the hidden buffer
        // first, then power on, so no stale frame flashes. Return true so the
        // caller knows this press was "wake only" (not navigation).
        menu_go_home();
        oled_power(true);
        s_panel_on = true;
        log_info("[product_display] woke panel on activity");
        return true;
    }
    return false;
}

void product_display_wake() {
    // Turn the panel on without changing what's shown (used by the alarm
    // overlay so an alarm lights up a sleeping screen). Also resets the
    // inactivity clock so the panel stays on while the alarm is active.
    s_last_activity_ms = millis();
    if (!s_panel_on) {
        oled_power(true);
        s_panel_on = true;
        log_info("[product_display] woke panel for alarm");
    }
}

static void display_power_task(void* param) {
    (void)param;
    for (;;) {
        if (s_panel_on &&
            (millis() - s_last_activity_ms) >= INACTIVITY_TIMEOUT_MS) {
            oled_power(false);
            s_panel_on = false;
            log_info("[product_display] inactivity timeout — panel off");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void product_display_start_power_manager() {
    s_last_activity_ms = millis();   // start the clock now (after splash)
    s_panel_on = true;
    xTaskCreate(display_power_task, "disp_power", 2048, nullptr, 1, nullptr);
}
