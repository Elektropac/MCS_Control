#include "display.h"
#include "ssd1306.h"
#include "logging.h"
#include "oled_bitmaps.h"
#include "mcs_logo.h"
#include "mcs_logo_vert.h"
#include "apps/cloudgauge/cloudgauge.h"
#include "apps/pump/pump.h"
#include "hardware/version.h"

// Read-only access to Niklas' network state (same pattern as the OTA app:
// we only READ these, never modify his code).
namespace w5500 { extern bool connected; extern String mac; extern IPAddress local_ip; }
namespace wifi  { extern bool connected; extern String mac; extern IPAddress local_ip; }
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// config_read_string for the guide (product name on HOME).
namespace config {
    String config_read_string(const char* section, const char* key,
                              const char* fallback);
}

// -----------------------------------------------------------------
// Boot splash + panel power (merged from the former product_display app)
// -----------------------------------------------------------------

// How long the MCS brand logo shows at boot.
static const uint32_t LOGO_HOLD_MS = 2000;

// The OLED is a fixed display (no buttons, no inactivity sleep) — always on.
static bool s_panel_on = true;

void display_boot_splash() {
    log_info("[display] showing MCS logo splash");
    oled_begin();
    oled_draw_bitmap(0, 0, mcs_logo_horizontal, 128, 64);
    oled_end();
    vTaskDelay(pdMS_TO_TICKS(LOGO_HOLD_MS));
}

bool display_panel_on() {
    return s_panel_on;
}

void display_wake() {
    // Panel is always on now; ensure it is (safe no-op in normal operation).
    if (!s_panel_on) {
        oled_power(true);
        s_panel_on = true;
    }
}

// -----------------------------------------------------------------
// Carousel state — owned exclusively by display_task. No buttons, no queue:
// the task advances on its own timer. This is the cleaned-up base before the
// config-defined carousel + slide transitions are added.
//
// The carousel is a flat sequence of screens built fresh each cycle from live
// data: HOME, then one screen per tank, then one per pump, then INFO. The
// screen kind + an index (which tank/pump) fully describe what to draw.
// -----------------------------------------------------------------
enum ScreenKind : uint8_t { SCR_HOME, SCR_TANK, SCR_PUMP, SCR_INFO };

// How long each screen is held before advancing (static swap for now).
static const uint32_t DWELL_MS = 4000;

// Alarm cycling period when several probes are alarming at once.
static const uint32_t ALARM_CYCLE_MS = 2500;

// -----------------------------------------------------------------
// Renderers (unchanged from the button era — these are display-only and are
// reused by the carousel). Each draws one full 128x64 frame.
// -----------------------------------------------------------------

// Map the internal guide id to a human-readable display name.
static String guide_display_name() {
    String g = config::config_read_string("config", "guide", "");
    if (g == "cloudgauge2")        return "CloudGauge 2";
    if (g == "sapphire_lite_mini") return "Sapphire Lite Mini";
    if (g == "poseidon")           return "Poseidon";
    if (g == "bridge")             return "Bridge";
    if (g.length() == 0)           return "MCS Control";
    return g;   // custom/unknown: show the raw name
}

// HOME screen: vertical MCS logo down the left edge + the guide name filling
// the rest of the screen (auto-sized, word-wrapped up to 3 lines).
static void render_home() {
    String name = guide_display_name();

    oled_begin();

    oled_draw_bitmap(1, (64 - mcs_logo_vert_h) / 2,
                     mcs_logo_vert, mcs_logo_vert_w, mcs_logo_vert_h);

    const int textX0 = mcs_logo_vert_w + 5;      // left edge of the text region
    const int regionW = 128 - textX0 - 2;        // usable text width
    const uint8_t sizes[] = { 20, 16, 12 };
    for (uint8_t si = 0; si < 3; si++) {
        uint8_t sz = sizes[si];
        bool lastSize = (si == 2);
        int lineH = sz + 3;
        String lines[3]; int nlines = 0; int start = 0; bool fits = true;
        while (start < (int)name.length() && nlines < 3) {
            int end = start, lastSpace = -1;
            while (end < (int)name.length()) {
                int nextSpace = name.indexOf(' ', end);
                int wordEnd = (nextSpace < 0) ? name.length() : nextSpace;
                if (oled_title_width(name.substring(start, wordEnd).c_str(), sz) > regionW) break;
                lastSpace = wordEnd;
                end = wordEnd + 1;
                if (nextSpace < 0) { lastSpace = name.length(); break; }
            }
            int lineEnd;
            if (lastSpace > start) {
                lineEnd = lastSpace;              // break at a fitting space
            } else {
                if (!lastSize) { fits = false; break; }   // word too wide -> smaller font
                int i = start;                    // smallest size: hard break
                while (i < (int)name.length() &&
                       oled_title_width(name.substring(start, i + 1).c_str(), sz) <= regionW) i++;
                lineEnd = (i > start) ? i : start + 1;
            }
            lines[nlines++] = name.substring(start, lineEnd);
            start = lineEnd;
            while (start < (int)name.length() && name[start] == ' ') start++;
        }
        if (start < (int)name.length()) fits = false;
        if (nlines * lineH > 64) fits = false;
        if (!fits && !lastSize) continue;

        int blockH = nlines * lineH;
        int firstBaseline = (64 - blockH) / 2 + sz;
        for (int i = 0; i < nlines; i++) {
            int tw = oled_title_width(lines[i].c_str(), sz);
            int x = textX0 + (regionW - tw) / 2;      // center within text region
            oled_draw_title_at(x, firstBaseline + i * lineH, lines[i].c_str(), sz);
        }
        break;
    }

    oled_end();
}

// TANK screen for probe index `idx`. Shows name, reading, percent + an
// animated horizontal tank graphic.
static void render_tank(uint8_t idx) {
    JsonDocument doc;
    deserializeJson(doc, cloudgauge_get_all());
    JsonArray probes = doc["probes"].as<JsonArray>();
    uint8_t n = probes.size();

    oled_begin();

    if (n == 0) {
        oled_print_center(1, "TANKS");
        oled_print_center(3, "No tanks connected");
        oled_end();
        return;
    }

    if (idx >= n) idx = n - 1;
    JsonObject p = probes[idx];

    const char* name  = p["id"]    | "Tank";
    int liters        = p["liters"] | 0;
    int mm            = p["mm"]      | 0;
    bool has_lut      = p["has_lut"] | false;
    float pctf        = p["pct"]    | 0.0f;
    const char* state = p["state"]  | "normal";

    oled_printf_left(0, "%s", name);
    oled_printf_right(0, "%d/%d", idx + 1, n);

    int fill = (int)(pctf + 0.5f);
    if (fill < 0) fill = 0; if (fill > 100) fill = 100;
    if (has_lut && liters > 0) oled_printf_left(1, "%d L", liters);
    else                       oled_printf_left(1, "%d mm", mm);
    const char* label = nullptr;
    if      (!strcmp(state, "low_level")) label = "LOW";
    else if (!strcmp(state, "overfill"))  label = "OVERFILL";
    else if (!strcmp(state, "fault"))     label = "FAULT";
    if (label) oled_printf_right(1, "%s", label);
    else       oled_printf_right(1, "%d%%", fill);

    // --- Horizontal tank graphic with animated liquid ---
    const int tankX = 5,  tankY = 34, tankW = 118, tankH = 26;
    const int neckW = 18, neckH = 4;
    const int footW = 10, footH = 3;
    const int footY = tankY + tankH + 1;
    const int leftFootX  = tankX + 16;
    const int rightFootX = tankX + tankW - 16 - footW;
    const int neckX = tankX + (tankW - neckW) / 2;
    const int neckY = tankY - neckH + 1;
    const int innerX = tankX + 3, innerY = tankY + 3;
    const int innerW = tankW - 6, innerH = tankH - 6;
    const int fillHeight = innerH * fill / 100;
    const int liquidTop  = innerY + innerH - fillHeight;
    const int wavePhase  = (millis() / 260) % 2;

    oled_draw_rframe(tankX, tankY, tankW, tankH, 7);
    oled_draw_frame(neckX, neckY, neckW, neckH);
    oled_draw_box(leftFootX, footY, footW, footH);
    oled_draw_box(rightFootX, footY, footW, footH);
    oled_draw_line(leftFootX - 6, footY + footH, rightFootX + footW + 6, footY + footH);

    if (fillHeight > 0) {
        const int innerBottom = innerY + innerH - 1;
        for (int y = liquidTop; y <= innerBottom; ++y) {
            int inset = 0;
            const int dTop = y - innerY, dBot = innerBottom - y;
            if      (dTop == 0 || dBot == 0) inset = 4;
            else if (dTop == 1 || dBot == 1) inset = 2;
            else if (dTop == 2 || dBot == 2) inset = 1;
            const int rowW = innerW - inset * 2;
            if (rowW > 0) oled_draw_line(innerX + inset, y, innerX + inset + rowW - 1, y);
        }
        for (int x = 0; x < innerW; x += 4) {
            const int waveY = liquidTop + (((x / 4) + wavePhase) % 2);
            int x2 = innerX + x + 2; if (x2 >= innerX + innerW) x2 = innerX + innerW - 1;
            oled_draw_line(innerX + x, waveY, x2, waveY);
        }
    }

    oled_end();
}

// PUMP screen for pump index `idx`.
static void render_pump(uint8_t idx) {
    JsonDocument doc;
    deserializeJson(doc, pump_get_all());
    JsonArray pumps = doc["pumps"].as<JsonArray>();
    uint8_t n = pumps.size();

    oled_begin();

    if (n == 0) {
        oled_print_center(1, "PUMPS");
        oled_print_center(3, "No pumps connected");
        oled_end();
        return;
    }

    if (idx >= n) idx = n - 1;
    JsonObject p = pumps[idx];

    const char* name   = p["name"]  | "Pump";
    bool nozzle_lifted = p["nozzle_lifted"] | false;
    bool relay_on      = p["relay_on"]      | false;
    float litres       = p["litres"] | 0.0f;

    oled_printf_left(0, "%s", name);
    oled_printf_right(0, "%d/%d", idx + 1, n);

    oled_printf_left(1, "Nozzle: %s", nozzle_lifted ? "LIFTED" : "HUNG");
    oled_printf_left(2, "Pump:   %s", relay_on ? "ON" : "OFF");
    oled_printf_left(3, "%.2f L", litres);

    oled_end();
}

// INFO screen: firmware, hardware + module revision, MAC, IP. All read-only.
static void render_info() {
    uint64_t chip = ESP.getEfuseMac();
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             (uint8_t)(chip >> 40), (uint8_t)(chip >> 32), (uint8_t)(chip >> 24),
             (uint8_t)(chip >> 16), (uint8_t)(chip >> 8),  (uint8_t)chip);

    String ip = "---";
    if      (w5500::connected) ip = w5500::local_ip.toString();
    else if (wifi::connected)  ip = wifi::local_ip.toString();

    oled_begin();
    oled_draw_str(2, 11, "System Info");
    oled_draw_line(0, 15, 127, 15);

    oled_draw_str(2, 27, "FW:");
    // otaCurrentVersion() lives in the OTA app; forward-declare to avoid a
    // heavier include here.
    extern const char* otaCurrentVersion();
    oled_draw_str(40, 27, otaCurrentVersion());
    char hw[20];
    snprintf(hw, sizeof(hw), "HW %u  Mod %u", version_hardware(), version_module());
    oled_draw_str(2, 39, hw);
    char macline[26];
    snprintf(macline, sizeof(macline), "MAC %s", mac);
    oled_draw_str(2, 51, macline);
    char ipline[28];
    snprintf(ipline, sizeof(ipline), "IP  %s", ip.c_str());
    oled_draw_str(2, 63, ipline);
    oled_end();
}

// -----------------------------------------------------------------
// Alarm overlay (unchanged — no input needed). When any probe is in a level
// alarm (low / overfill), this takes over the screen, blinks, and cycles
// through multiple alarms. Clears automatically when readings return to range.
// -----------------------------------------------------------------
struct AlarmItem { char name[20]; char kind[10]; int pct; };
static AlarmItem s_alarms[8];
static uint8_t   s_alarm_count = 0;

static uint8_t collect_alarms() {
    JsonDocument doc;
    deserializeJson(doc, cloudgauge_get_all());
    JsonArray probes = doc["probes"].as<JsonArray>();
    uint8_t n = 0;
    for (JsonObject p : probes) {
        if (n >= 8) break;
        const char* kind = nullptr;
        if      (p["overfill_alarm"] | false) kind = "OVERFILL";
        else if (p["low"]            | false) kind = "LOW LEVEL";
        if (!kind) continue;
        const char* id = p["id"] | "Tank";
        strncpy(s_alarms[n].name, id, sizeof(s_alarms[n].name) - 1);
        s_alarms[n].name[sizeof(s_alarms[n].name) - 1] = '\0';
        strncpy(s_alarms[n].kind, kind, sizeof(s_alarms[n].kind) - 1);
        s_alarms[n].kind[sizeof(s_alarms[n].kind) - 1] = '\0';
        s_alarms[n].pct = (int)((float)(p["pct"] | 0.0f) + 0.5f);
        n++;
    }
    s_alarm_count = n;
    return n;
}

static void render_alarm(const AlarmItem& a, uint8_t idx, uint8_t total) {
    bool inverted = ((millis() / 500) % 2) == 0;

    oled_begin();
    if (inverted) {
        oled_set_draw_color(1);
        oled_fill_screen();
        oled_set_draw_color(0);
    }
    oled_draw_str_center(14, "! ALARM !");
    oled_draw_line(0, 18, 127, 18);
    oled_draw_str_center(32, a.name);
    char line[20]; snprintf(line, sizeof(line), "%s  %d%%", a.kind, a.pct);
    oled_draw_str_center(48, line);
    if (total > 1) {
        char pg[12]; snprintf(pg, sizeof(pg), "%u/%u", idx + 1, total);
        oled_draw_str(2, 63, pg);
    }
    oled_set_draw_color(1);
    oled_end();
}

// -----------------------------------------------------------------
// Carousel sequencing
//
// The sequence is built fresh from live counts each time we need to advance,
// so tanks/pumps appearing or disappearing is handled automatically. A screen
// is identified by (kind, index). step_index walks a virtual flat list:
//   0            -> HOME
//   1..nTanks    -> TANK 0..nTanks-1
//   ..+nPumps    -> PUMP 0..nPumps-1
//   last         -> INFO
// -----------------------------------------------------------------
static uint8_t count_tanks() {
    JsonDocument doc;
    deserializeJson(doc, cloudgauge_get_all());
    return doc["probes"].as<JsonArray>().size();
}
static uint8_t count_pumps() {
    JsonDocument doc;
    deserializeJson(doc, pump_get_all());
    return doc["pumps"].as<JsonArray>().size();
}

// Draw the screen at virtual position `step` for the current live counts.
static void render_step(uint16_t step, uint8_t nTanks, uint8_t nPumps) {
    if (step == 0) { render_home(); return; }
    uint16_t s = step - 1;
    if (s < nTanks) { render_tank((uint8_t)s); return; }
    s -= nTanks;
    if (s < nPumps) { render_pump((uint8_t)s); return; }
    render_info();
}

// Total number of screens in one full cycle.
static uint16_t step_count(uint8_t nTanks, uint8_t nPumps) {
    return 1 /*HOME*/ + nTanks + nPumps + 1 /*INFO*/;
}

static void display_task(void*) {
    uint16_t step = 0;               // current carousel position
    uint32_t dwell_start = millis(); // when the current screen started
    uint8_t  alarm_idx = 0;
    uint32_t alarm_cycle_ms = 0;

    for (;;) {
        // --- Alarm handling first: an alarm takes over regardless of the
        // carousel. It redraws fast (blink) and never advances the carousel. ---
        uint8_t alarms = collect_alarms();
        if (alarms > 0) {
            display_wake();
            if (alarm_idx >= alarms) alarm_idx = 0;
            if (alarms > 1 && millis() - alarm_cycle_ms > ALARM_CYCLE_MS) {
                alarm_cycle_ms = millis();
                alarm_idx = (alarm_idx + 1) % alarms;
            }
            render_alarm(s_alarms[alarm_idx], alarm_idx, alarms);
            // Reset the dwell clock so the current screen gets its full time
            // once the alarm clears (don't skip straight past it).
            dwell_start = millis();
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        alarm_idx = 0;

        // --- Normal carousel: draw the current screen, advance after DWELL. ---
        uint8_t nTanks = count_tanks();
        uint8_t nPumps = count_pumps();
        uint16_t total = step_count(nTanks, nPumps);
        if (step >= total) step = 0;

        render_step(step, nTanks, nPumps);

        if (millis() - dwell_start >= DWELL_MS) {
            step = (step + 1) % total;
            dwell_start = millis();
        }

        // Redraw at ~4 fps so the tank liquid animation stays lively while a
        // screen is held. (Transition animations come in the next step.)
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void display_start() {
    xTaskCreate(display_task, "display", 4096, nullptr, 1, nullptr);
}
