#include "menu_tanks.h"
#include "menu.h"
#include "ssd1306.h"
#include "config.h"
#include <Arduino.h>
#include <ArduinoJson.h>

// --- Tank graphic rendering ---

void render_tank_view(const char* name, int liters, int percent) {
    const int tankX = 5;
    const int tankY = 31;
    const int tankW = 118;
    const int tankH = 28;
    const int tankRadius = 8;
    const int neckW = 18;
    const int neckH = 5;
    const int footW = 10;
    const int footH = 3;
    const int footY = tankY + tankH + 1;
    const int leftFootX = tankX + 16;
    const int rightFootX = tankX + tankW - 16 - footW;
    const int neckX = tankX + (tankW - neckW) / 2;
    const int neckY = tankY - neckH + 1;

    int clampedPercent = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    const int innerX = tankX + 3;
    const int innerY = tankY + 3;
    const int innerW = tankW - 6;
    const int innerH = tankH - 6;
    const int fillHeight = innerH * clampedPercent / 100;
    const int liquidTop = innerY + innerH - fillHeight;
    const int wavePhase = (millis() / 260) % 8;

    // Header
    char title[24];
    snprintf(title, sizeof(title), "Tank: %s", name);
    oled_set_font_bold();
    oled_draw_text(4, 12, title);

    // Stats
    char litersStr[16];
    char percentStr[16];
    snprintf(litersStr, sizeof(litersStr), "%d L", liters);
    snprintf(percentStr, sizeof(percentStr), "%d%%", clampedPercent);
    oled_set_font_normal();
    oled_draw_text(8, 24, litersStr);
    oled_draw_text(82, 24, percentStr);

    // Tank body
    oled_draw_rframe(tankX, tankY, tankW, tankH, tankRadius);
    oled_draw_frame(neckX, neckY, neckW, neckH);
    oled_draw_box(leftFootX, footY, footW, footH);
    oled_draw_box(rightFootX, footY, footW, footH);
    oled_draw_line(leftFootX - 6, footY + footH, rightFootX + footW + 6, footY + footH);

    // Liquid fill
    if (fillHeight > 0) {
        const int innerBottom = innerY + innerH - 1;
        for (int y = liquidTop; y <= innerBottom; ++y) {
            int inset = 0;
            int distTop = y - innerY;
            int distBottom = innerBottom - y;

            if (distTop == 0 || distBottom == 0) inset = 5;
            else if (distTop == 1 || distBottom == 1) inset = 3;
            else if (distTop == 2 || distBottom == 2) inset = 2;
            else if (distTop == 3 || distBottom == 3) inset = 1;

            int rowWidth = innerW - (inset * 2);
            if (rowWidth > 0) {
                oled_draw_line(innerX + inset, y, innerX + inset + rowWidth - 1, y);
            }
        }

        // Wave
        for (int x = 0; x < innerW; x += 4) {
            int waveY = liquidTop + (((x / 4) + wavePhase) % 2);
            int waveX2 = (innerX + x + 2 < innerX + innerW) ? (innerX + x + 2) : (innerX + innerW - 1);
            oled_draw_line(innerX + x, waveY, waveX2, waveY);
        }
    }
}

// --- Dynamic tank submenu built from config ---
#define MAX_TANKS 8

static MenuItem s_tank_items[MAX_TANKS];
static char s_tank_labels[MAX_TANKS][16];
static uint8_t s_tank_count = 0;

// Screen render functions per tank (store index for callback)
static uint8_t s_viewing_tank = 0;

static void render_current_tank() {
    // TODO: read real values from probe/ADC based on s_viewing_tank
    // Demo: show tank ID with fake values
    int demo_liters = 1000 + s_viewing_tank * 1500;
    int demo_percent = 30 + s_viewing_tank * 20;
    render_tank_view(s_tank_labels[s_viewing_tank], demo_liters, demo_percent);
}

// Action functions for each tank slot
static void show_tank_0() { s_viewing_tank = 0; menu_set_screen(render_current_tank); }
static void show_tank_1() { s_viewing_tank = 1; menu_set_screen(render_current_tank); }
static void show_tank_2() { s_viewing_tank = 2; menu_set_screen(render_current_tank); }
static void show_tank_3() { s_viewing_tank = 3; menu_set_screen(render_current_tank); }
static void show_tank_4() { s_viewing_tank = 4; menu_set_screen(render_current_tank); }
static void show_tank_5() { s_viewing_tank = 5; menu_set_screen(render_current_tank); }
static void show_tank_6() { s_viewing_tank = 6; menu_set_screen(render_current_tank); }
static void show_tank_7() { s_viewing_tank = 7; menu_set_screen(render_current_tank); }

static void (*s_tank_actions[MAX_TANKS])() = {
    show_tank_0, show_tank_1, show_tank_2, show_tank_3,
    show_tank_4, show_tank_5, show_tank_6, show_tank_7
};

// Build tank submenu from config "functions" where type == "probe"
void tank_submenu_build() {
    s_tank_count = 0;

    if (!config::is_loaded) {
        // Fallback: 2 default tanks
        snprintf(s_tank_labels[0], 16, "Tank A");
        snprintf(s_tank_labels[1], 16, "Tank B");
        s_tank_items[0] = { s_tank_labels[0], nullptr, show_tank_0, nullptr, 0 };
        s_tank_items[1] = { s_tank_labels[1], nullptr, show_tank_1, nullptr, 0 };
        s_tank_count = 2;
        return;
    }

    JsonArray functions = config::config["functions"].as<JsonArray>();
    if (functions.isNull()) {
        s_tank_count = 0;
        return;
    }

    for (JsonVariant func : functions) {
        if (s_tank_count >= MAX_TANKS) break;

        const char* type = func["type"].as<const char*>();
        if (!type || strcmp(type, "probe") != 0) continue;

        const char* id = func["id"].as<const char*>();
        if (!id) id = "?";

        snprintf(s_tank_labels[s_tank_count], 16, "%s", id);
        s_tank_items[s_tank_count] = {
            s_tank_labels[s_tank_count],
            nullptr,
            s_tank_actions[s_tank_count],
            nullptr,
            0
        };
        s_tank_count++;
    }
}

const MenuItem* tank_submenu_get_items() {
    return s_tank_items;
}

uint8_t tank_submenu_get_count() {
    return s_tank_count;
}
