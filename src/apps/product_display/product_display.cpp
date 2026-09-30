#include "product_display.h"
#include "control_oled_128x64_bitmaps.h"
#include "ssd1306.h"
#include "logging.h"
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

// Map a guide name to its 128x64 bitmap. Returns nullptr if unknown.
// Product names: CloudGauge2, Sapphire Lite Mini, Poseidon, Bridge.
static const unsigned char* bitmap_for_guide(const String& guide) {
    if (guide == "cloudgauge2")        return cloudgauge2_128x64;
    if (guide == "sapphire_lite_mini") return sapphire_lite_mini_128x64;
    if (guide == "poseidon")           return poseidon_128x64;
    if (guide == "bridge")             return bridge_128x64;
    return nullptr;
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
