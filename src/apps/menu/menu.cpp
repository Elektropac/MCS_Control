#include "menu.h"
#include "ssd1306.h"
#include "logging.h"
#include "apps/product_display/control_oled_128x64_bitmaps.h"
#include "apps/product_display/mcs_logo_crop.h"
#include "apps/product_display/mcs_logo_vert.h"
#include "apps/cloudgauge/cloudgauge.h"
#include "apps/pump/pump.h"
#include "apps/product_display/product_display.h"
#include "apps/ota/ota.h"
#include "hardware/version.h"

// Read-only access to Niklas' network state (same pattern as the OTA app:
// we only READ these, never modify his code).
namespace w5500 { extern bool connected; extern String mac; extern IPAddress local_ip; }
namespace wifi  { extern bool connected; extern String mac; extern IPAddress local_ip; }
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// config_read_string for the guide (product image on HOME).
namespace config {
    String config_read_string(const char* section, const char* key,
                              const char* fallback);
}

// -----------------------------------------------------------------
// State — owned exclusively by menu_task. The buttons task never touches it;
// it posts Button events to s_queue and menu_task applies them. This is the
// canonical RTOS pattern: one owner, no shared mutable state, no races.
// -----------------------------------------------------------------
enum Section : uint8_t { SEC_HOME = 0, SEC_TANKS = 1, SEC_PUMPS = 2, SEC_UPDATE = 3, SEC_INFO = 4, SEC_COUNT = 5 };

static Section s_section = SEC_HOME;
static uint8_t s_index   = 0;      // item within section
static uint8_t s_count   = 0;      // items in current section (set by renderers)
static bool    s_detail  = false;  // detail page open for current item

// UPDATE section: which release in the list is highlighted for install.
static uint8_t s_ota_sel = 0;

// HOME logo-zoom animation: millis() when HOME was last entered. The logo
// shrinks from full-screen into the top-left corner, then the guide name
// appears centered. 0 = not animating (show the settled HOME).
static uint32_t s_home_anim_start = 0;

// Alarm snooze: a button press while an alarm is showing dismisses the overlay
// for this long so the user can use the menu. It re-pops when the time elapses
// (as long as the alarm is still active).
static volatile uint32_t s_alarm_snooze_until = 0;
static const uint32_t ALARM_SNOOZE_MS = 30000;   // 30s

static QueueHandle_t s_queue = nullptr;   // Button events: buttons -> menu
// Signals a forced jump to HOME (e.g. wake from sleep) without a button.
static volatile bool s_want_home = false;

// -----------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------

// -----------------------------------------------------------------
// Renderers
// -----------------------------------------------------------------

// Map the internal guide id to a human-readable display name. Unknown/custom
// guides fall back to showing whatever the config holds (or "MCS Control").
static String guide_display_name() {
    String g = config::config_read_string("config", "guide", "");
    if (g == "cloudgauge2")        return "CloudGauge 2";
    if (g == "sapphire_lite_mini") return "Sapphire Lite Mini";
    if (g == "poseidon")           return "Poseidon";
    if (g == "bridge")             return "Bridge";
    if (g.length() == 0)           return "MCS Control";
    return g;   // custom/unknown: show the raw name
}

// HOME screen: a short animation where the MCS logo zooms down from full
// screen into a small top-left corner mark, then the guide name appears
// centered. After the animation it settles to (corner logo + centered name).
static void render_home() {
    s_count = 0;
    String name = guide_display_name();

    // Animation: shrink through three real logo sizes (crisp, no scaling),
    // easing toward the left edge, then settle to the vertical logo + name.
    const uint32_t T1 = 350, T2 = 650, T3 = 900;
    uint32_t t = (s_home_anim_start == 0) ? T3 : (millis() - s_home_anim_start);

    oled_begin();

    if (t < T1) {
        oled_draw_bitmap((128 - mcs_logo_full_w) / 2, (64 - mcs_logo_full_h) / 2,
                         mcs_logo_full, mcs_logo_full_w, mcs_logo_full_h);
        oled_end(); return;
    }
    if (t < T2) {
        float p = (float)(t - T1) / (T2 - T1);
        int cx = (128 - mcs_logo_mid_w) / 2, cy = (64 - mcs_logo_mid_h) / 2;
        int x = cx + (int)((2 - cx) * p);        // ease toward left
        oled_draw_bitmap(x, cy, mcs_logo_mid, mcs_logo_mid_w, mcs_logo_mid_h);
        oled_end(); return;
    }
    if (t < T3) {
        oled_draw_bitmap(2, (64 - mcs_logo_small_h) / 2,
                         mcs_logo_small, mcs_logo_small_w, mcs_logo_small_h);
        oled_end(); return;
    }

    // --- Settled: vertical MCS logo down the LEFT edge, guide name filling the
    // rest of the screen (full height), centered in that region. The vertical
    // logo frees the whole height so even 3-line names fit comfortably. ---
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
            // Grow line word-by-word while it fits the region width.
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

static void render_tanks() {
    JsonDocument doc;
    deserializeJson(doc, cloudgauge_get_all());
    JsonArray probes = doc["probes"].as<JsonArray>();
    uint8_t n = probes.size();
    s_count = n;

    oled_begin();

    if (n == 0) {
        oled_print_center(1, "TANKS");
        oled_print_center(3, "No tanks connected");
        oled_end();
        return;
    }

    if (s_index >= n) s_index = n - 1;
    JsonObject p = probes[s_index];

    const char* name  = p["id"]    | "Tank";
    int liters        = p["liters"] | 0;
    int mm            = p["mm"]      | 0;
    bool has_lut      = p["has_lut"] | false;
    // pct is emitted as a raw JSON number (serialized), so read it as a float —
    // reading it as a string returns null and silently became 0% before.
    float pctf        = p["pct"]    | 0.0f;
    const char* state = p["state"]  | "normal";

    // Header: name (left) + page counter (right)
    oled_printf_left(0, "%s", name);
    oled_printf_right(0, "%d/%d", s_index + 1, n);

    // Values line: left shows the best available reading, right shows percent.
    // With a LUT we have litres; without one, litres is 0 and meaningless, so
    // show the measured level in mm instead (never a misleading "0 L").
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

    // --- Horizontal tank graphic with animated liquid (ported from old FW) ---
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

    oled_draw_rframe(tankX, tankY, tankW, tankH, 7);  // tank body (rounded)
    oled_draw_frame(neckX, neckY, neckW, neckH);   // neck
    oled_draw_box(leftFootX, footY, footW, footH); // feet
    oled_draw_box(rightFootX, footY, footW, footH);
    oled_draw_line(leftFootX - 6, footY + footH, rightFootX + footW + 6, footY + footH); // ground

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
        // Wave crest on the surface
        for (int x = 0; x < innerW; x += 4) {
            const int waveY = liquidTop + (((x / 4) + wavePhase) % 2);
            int x2 = innerX + x + 2; if (x2 >= innerX + innerW) x2 = innerX + innerW - 1;
            oled_draw_line(innerX + x, waveY, x2, waveY);
        }
    }

    oled_end();
}

static void render_pumps() {
    JsonDocument doc;
    deserializeJson(doc, pump_get_all());
    JsonArray pumps = doc["pumps"].as<JsonArray>();
    uint8_t n = pumps.size();
    s_count = n;

    oled_begin();

    if (n == 0) {
        oled_print_center(1, "PUMPS");
        oled_print_center(3, "No pumps connected");
        oled_end();
        return;
    }

    if (s_index >= n) s_index = n - 1;
    JsonObject p = pumps[s_index];

    const char* name   = p["name"]  | "Pump";
    bool nozzle_lifted = p["nozzle_lifted"] | false;
    bool relay_on      = p["relay_on"]      | false;
    float litres       = p["litres"] | 0.0f;   // serialized number

    oled_printf_left(0, "%s", name);
    oled_printf_right(0, "%d/%d", s_index + 1, n);

    oled_printf_left(1, "Nozzle: %s", nozzle_lifted ? "LIFTED" : "HUNG");
    oled_printf_left(2, "Pump:   %s", relay_on ? "ON" : "OFF");
    oled_printf_left(3, "%.2f L", litres);

    oled_end();
}

// Compare dotted versions: <0 if a<b, 0 equal, >0 if a>b. Mirrors web UI.
static int ota_cmp_ver(const String& a, const String& b) {
    int ia = 0, ib = 0;
    while (ia < (int)a.length() || ib < (int)b.length()) {
        long na = 0, nb = 0;
        while (ia < (int)a.length() && a[ia] != '.') { na = na * 10 + (a[ia] - '0'); ia++; }
        while (ib < (int)b.length() && b[ib] != '.') { nb = nb * 10 + (b[ib] - '0'); ib++; }
        if (na != nb) return na < nb ? -1 : 1;
        if (ia < (int)a.length()) ia++;   // skip '.'
        if (ib < (int)b.length()) ib++;
    }
    return 0;
}

// 20x20 firmware/chip icon (download arrow into a chip). MSB-first, PROGMEM.
static const uint8_t fw_chip_20x20[] PROGMEM = {
    0x03,0x00,0x00, 0x03,0x00,0x00, 0x03,0x00,0x00, 0x03,0x00,0x00,
    0x33,0x30,0x00, 0x1B,0x60,0x00, 0x0F,0xC0,0x00, 0x07,0x80,0x00,
    0x03,0x00,0x00, 0x00,0x00,0x00, 0x7F,0xF0,0x00, 0x40,0x10,0x00,
    0x4E,0xD0,0x00, 0x4E,0xD0,0x00, 0x40,0x10,0x00, 0x7F,0xF0,0x00,
    0x40,0x10,0x00, 0x7F,0xF0,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00
};

// Draw a horizontal progress bar (outline + fill) at x,y of size w,h.
static void draw_progress(int x, int y, int w, int h, int pct) {
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    oled_draw_rframe(x, y, w, h, 2);
    int innerW = w - 4;
    int fillW = innerW * pct / 100;
    if (fillW > 0) oled_draw_rbox(x + 2, y + 2, fillW, h - 4, 1);
}

// Draw the shared header (icon + "Firmware" + current version + divider).
static void draw_update_header(const String& cur) {
    oled_draw_bitmap(4, 2, fw_chip_20x20, 24, 20);  // icon x4-27, y2-21
    oled_draw_str(32, 15, "Firmware");               // baseline y15 (text ~y6-15)
    char vbuf[16]; snprintf(vbuf, sizeof(vbuf), "v%s", cur.c_str());
    int w = oled_text_width(vbuf);
    oled_draw_str(126 - w, 15, vbuf);                // right-aligned
    oled_draw_line(0, 24, 127, 24);                  // divider
}

// OTA / firmware update screen. UP/DOWN picks a release, OK acts in context:
//   no releases loaded  -> Check for updates
//   release highlighted  -> Install / Downgrade / Reinstall (same as web UI)
static void render_update() {
    const size_t rels = otaReleaseCount();
    // Navigable items = all releases + 1 trailing "Check again" entry, so the
    // user can always re-scan the server even after a list is loaded.
    s_count = (uint8_t)(rels + 1);
    if (s_ota_sel >= s_count) s_ota_sel = s_count - 1;

    const String cur = String(otaCurrentVersion());
    const OtaState st = otaState();
    const bool busy  = otaBusy();

    oled_begin();
    draw_update_header(cur);

    // --- Busy: downloading with live progress bar ---
    if (busy || st == OTA_UPDATING) {
        size_t dl = otaDownloadedBytes();
        size_t tot = otaDownloadTotalBytes();
        int pct = (tot > 0) ? (int)((uint64_t)dl * 100 / tot) : 0;
        oled_draw_str_center(38, "Downloading...");
        draw_progress(12, 44, 104, 11, pct);
        char b[24]; snprintf(b, sizeof(b), "%d%%", pct);
        oled_draw_str_center(63, b);
        oled_end();
        return;
    }

    // --- Fueling-wait: warning triangle + text ---
    if (st == OTA_WAITING_FUELING) {
        oled_draw_line(20, 52, 32, 34); oled_draw_line(32, 34, 44, 52);
        oled_draw_line(20, 52, 44, 52); oled_draw_line(32, 44, 32, 48);
        oled_draw_str(52, 42, "Fueling");
        oled_draw_str(52, 54, "active");
        oled_end();
        return;
    }
    // --- Success: checkmark ---
    if (st == OTA_SUCCESS) {
        oled_draw_line(40, 44, 48, 52); oled_draw_line(48, 52, 64, 34);
        oled_draw_str(72, 48, "Done");
        oled_end();
        return;
    }
    // --- Error ---
    if (st == OTA_ERROR) {
        oled_draw_str_center(42, "Update failed");
        oled_draw_str_center(56, "OK = retry");
        oled_end();
        return;
    }

    // --- No releases loaded yet: only the Check item exists ---
    if (rels == 0) {
        oled_draw_str_center(40, "No releases loaded");
        oled_draw_rframe(34, 47, 60, 15, 3);
        oled_draw_str_center(58, "OK: Check");
        oled_end();
        return;
    }

    // --- Trailing "Check again" item selected ---
    if (s_ota_sel >= rels) {
        oled_draw_str_center(38, "Re-scan server");
        oled_draw_rframe(17, 46, 94, 15, 3);
        oled_draw_str_center(57, "OK: Check again");
        oled_end();
        return;
    }

    // --- Idle with a release selected: version transition + action ---
    const String sel = otaReleaseVersionAt(s_ota_sel);
    int c = ota_cmp_ver(sel, cur);
    const char* verb = c < 0 ? "Downgrade" : (c > 0 ? "Install" : "Reinstall");

    // Middle row (y~34-40): "v<cur>  ->  v<sel>"
    char cb[16], sb[16];
    snprintf(cb, sizeof(cb), "v%s", cur.c_str());
    snprintf(sb, sizeof(sb), "v%s", sel.c_str());
    oled_draw_str(4, 39, cb);                        // left
    int sw = oled_text_width(sb);
    oled_draw_str(124 - sw, 39, sb);                 // right
    // arrow in the middle (around x58-70, y36)
    oled_draw_line(56, 36, 70, 36);
    oled_draw_line(70, 36, 66, 32); oled_draw_line(70, 36, 66, 40);
    // page indicator (left) + action button (right-centered)
    char pg[12]; snprintf(pg, sizeof(pg), "%u/%u", (unsigned)(s_ota_sel + 1), (unsigned)rels);
    oled_draw_str(2, 61, pg);
    oled_draw_rframe(30, 48, 94, 15, 3);
    char ab[24]; snprintf(ab, sizeof(ab), "OK: %s", verb);
    int aw = oled_text_width(ab);
    oled_draw_str(30 + (94 - aw) / 2, 59, ab);
    oled_end();
}

// System / device info screen: firmware, hardware + module revision, MAC, IP.
// The MAC is the device's permanent identity (eFuse). IP comes from whichever
// interface is up (Ethernet preferred, else WiFi). All reads are read-only.
static void render_info() {
    s_count = 0;   // single page, UP/DOWN do nothing here

    // Permanent chip MAC (hardware identity), formatted AA:BB:CC:DD:EE:FF.
    uint64_t chip = ESP.getEfuseMac();
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             (uint8_t)(chip >> 40), (uint8_t)(chip >> 32), (uint8_t)(chip >> 24),
             (uint8_t)(chip >> 16), (uint8_t)(chip >> 8),  (uint8_t)chip);

    // Active IP (Ethernet first, then WiFi, else none).
    String ip = "---";
    if      (w5500::connected) ip = w5500::local_ip.toString();
    else if (wifi::connected)  ip = wifi::local_ip.toString();

    oled_begin();
    oled_draw_str(2, 11, "System Info");
    oled_draw_line(0, 15, 127, 15);

    oled_draw_str(2, 27, "FW:");
    oled_draw_str(40, 27, otaCurrentVersion());
    char hw[20];
    snprintf(hw, sizeof(hw), "HW %u  Mod %u", version_hardware(), version_module());
    oled_draw_str(2, 39, hw);
    char macline[26];
    snprintf(macline, sizeof(macline), "MAC %s", mac);
    oled_draw_str(2, 51, macline);         // full width (17 chars + label fits)
    char ipline[28];
    snprintf(ipline, sizeof(ipline), "IP  %s", ip.c_str());
    oled_draw_str(2, 63, ipline);
    oled_end();
}

// -----------------------------------------------------------------
// Alarm overlay
//
// When any probe is in an alarm state (fault / overfill / low), an alarm
// screen takes over the OLED regardless of which section is selected. It
// blinks by inverting the whole screen, and if several probes are alarming it
// cycles through them (~2.5s each). It clears automatically when all readings
// are back in range. A button press still navigates (handled by the task).
// -----------------------------------------------------------------
struct AlarmItem { char name[20]; char kind[10]; int pct; };
static AlarmItem s_alarms[8];
static uint8_t   s_alarm_count = 0;

// Scan live probe data; fill s_alarms. Returns number of active alarms.
// Only real level alarms trigger the overlay (low / overfill). A fault is a
// sensor/wiring issue, not a tank condition — it's shown on the TANKS screen
// as "FAULT" but does not take over the display.
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

// Draw the alarm screen for one item, blinking by inverting every ~500ms.
static void render_alarm(const AlarmItem& a, uint8_t idx, uint8_t total) {
    bool inverted = ((millis() / 500) % 2) == 0;

    oled_begin();
    if (inverted) {
        oled_set_draw_color(1);
        oled_fill_screen();        // white background
        oled_set_draw_color(0);    // black content
    }
    // Banner + tank name + alarm kind + current level %.
    oled_draw_str_center(14, "! ALARM !");
    oled_draw_line(0, 18, 127, 18);
    oled_draw_str_center(32, a.name);
    char line[20]; snprintf(line, sizeof(line), "%s  %d%%", a.kind, a.pct);
    oled_draw_str_center(48, line);
    if (total > 1) {
        char pg[12]; snprintf(pg, sizeof(pg), "%u/%u", idx + 1, total);
        oled_draw_str(2, 63, pg);
    }
    oled_set_draw_color(1);        // restore default for next frame
    oled_end();
}

static void render_tank_detail() {
    JsonDocument doc;
    deserializeJson(doc, cloudgauge_get_all());
    JsonArray probes = doc["probes"].as<JsonArray>();
    uint8_t n = probes.size();
    s_count = n;
    oled_begin();
    if (n == 0) { oled_print_center(2, "No tanks"); oled_end(); return; }
    if (s_index >= n) s_index = n - 1;
    JsonObject p = probes[s_index];

    const char* name  = p["id"]    | "Tank";
    const char* input = p["input"] | "?";
    const char* media = p["media"] | "?";
    float ma          = p["ma"]    | 0.0f;   // serialized number, not a string
    int mm            = p["mm"]      | 0;
    int range_mm      = p["range_mm"] | 0;
    int liters        = p["liters"]  | 0;
    int capacity      = p["capacity"] | 0;
    const char* state = p["state"]  | "normal";

    oled_printf_left(0, "%s  (%s)", name, input);
    oled_printf_left(1, "Media: %s  %.2f mA", media, ma);
    oled_printf_left(2, "%d mm / %d mm", mm, range_mm);
    oled_printf_left(3, "%d L / %d L", liters, capacity);
    oled_printf_left(4, "State: %s", state);
    oled_end();
}

static void render_pump_detail() {
    JsonDocument doc;
    deserializeJson(doc, pump_get_all());
    JsonArray pumps = doc["pumps"].as<JsonArray>();
    uint8_t n = pumps.size();
    s_count = n;
    oled_begin();
    if (n == 0) { oled_print_center(2, "No pumps"); oled_end(); return; }
    if (s_index >= n) s_index = n - 1;
    JsonObject p = pumps[s_index];

    const char* name   = p["name"]         | "Pump";
    const char* nozzle = p["nozzle_input"] | "?";
    const char* pulse  = p["pulse_input"]  | "?";
    bool nozzle_lifted = p["nozzle_lifted"] | false;
    bool relay_on      = p["relay_on"]      | false;
    long pulses        = p["pulses"]        | 0;
    float ppl          = p["pulses_per_litre"] | 0.0f;   // serialized number
    float litres       = p["litres"]        | 0.0f;      // serialized number

    oled_printf_left(0, "%s", name);
    oled_printf_left(1, "Nozzle %s: %s", nozzle, nozzle_lifted ? "LIFT" : "HUNG");
    oled_printf_left(2, "Relay: %s", relay_on ? "ON" : "OFF");
    oled_printf_left(3, "Pulse %s: %ld", pulse, pulses);
    oled_printf_left(4, "%.2f L (%.0f p/L)", litres, ppl);
    oled_end();
}

static void render_current() {
    if (s_detail && s_section == SEC_TANKS) { render_tank_detail(); return; }
    if (s_detail && s_section == SEC_PUMPS) { render_pump_detail(); return; }
    switch (s_section) {
        case SEC_TANKS: render_tanks(); break;
        case SEC_PUMPS: render_pumps(); break;
        case SEC_UPDATE: render_update(); break;
        case SEC_INFO:   render_info();   break;
        default:        render_home();  break;
    }
}

// -----------------------------------------------------------------
// Public API — producers only touch the queue / a flag.
// -----------------------------------------------------------------
void menu_go_home() {
    s_want_home = true;                       // picked up by menu_task
    Button wake = BTN_NONE;
    if (s_queue) xQueueSend(s_queue, &wake, 0); // nudge the task to redraw now
}

void menu_handle_button(Button btn) {
    if (s_queue) xQueueSend(s_queue, &btn, 0);  // menu_task applies it
}

// Apply one navigation event to the (task-owned) state.
static void apply_button(Button btn) {
    switch (btn) {
        case BTN_OK:
            if (s_section == SEC_TANKS || s_section == SEC_PUMPS) {
                s_detail = !s_detail;
            } else if (s_section == SEC_UPDATE) {
                // Context action. Never acts while an action is running.
                if (!otaBusy()) {
                    size_t rels = otaReleaseCount();
                    if (rels == 0 || s_ota_sel >= rels) {
                        // "Check again" item (or empty list): re-scan server.
                        otaRequestCheck();
                    } else {
                        // Select highlighted release, then install. Works for
                        // upgrade AND downgrade (fueling-safe gate still applies
                        // inside the OTA task).
                        String v = otaReleaseVersionAt(s_ota_sel);
                        if (v.length()) { otaSelectVersion(v); otaRequestUpdate(); }
                    }
                }
            }
            break;
        case BTN_RIGHT:
            s_detail = false;
            s_section = (Section)((s_section + 1) % SEC_COUNT);
            s_index = 0;
            break;
        case BTN_LEFT:
            s_detail = false;
            s_section = (Section)((s_section + SEC_COUNT - 1) % SEC_COUNT);
            s_index = 0;
            break;
        case BTN_DOWN:
            if (s_section == SEC_UPDATE) {
                if (s_count > 0) s_ota_sel = (s_ota_sel + 1) % s_count;
            } else if (s_count > 1) {
                s_index = (s_index + 1) % s_count;
            }
            break;
        case BTN_UP:
            if (s_section == SEC_UPDATE) {
                if (s_count > 0) s_ota_sel = (s_ota_sel + s_count - 1) % s_count;
            } else if (s_count > 1) {
                s_index = (s_index + s_count - 1) % s_count;
            }
            break;
        default: break;   // BTN_NONE = wake/redraw nudge
    }
}

static void menu_task(void*) {
    uint8_t  alarm_idx = 0;
    uint32_t alarm_cycle_ms = 0;
    Section prev_section = (Section)255;   // force first HOME to animate
    for (;;) {
        // While the HOME logo-zoom plays, redraw fast (~40ms) for smoothness;
        // otherwise the normal 250ms refresh is plenty.
        bool home_animating = (s_section == SEC_HOME && s_home_anim_start != 0 &&
                               (millis() - s_home_anim_start) < 700);
        TickType_t wait = home_animating ? pdMS_TO_TICKS(40) : pdMS_TO_TICKS(250);

        Button btn;
        if (xQueueReceive(s_queue, &btn, wait) == pdTRUE) {
            if (s_want_home) { s_want_home = false; s_section = SEC_HOME; s_index = 0; s_detail = false; }
            // A real button press while alarming snoozes the overlay so the
            // user can navigate the menu (the alarm re-pops after the snooze).
            if (btn != BTN_NONE && s_alarm_count > 0) {
                s_alarm_snooze_until = millis() + ALARM_SNOOZE_MS;
            }
            apply_button(btn);
        } else if (s_want_home) {
            s_want_home = false; s_section = SEC_HOME; s_index = 0; s_detail = false;
        }

        // Trigger the HOME zoom animation whenever we newly arrive at HOME.
        if (s_section == SEC_HOME && prev_section != SEC_HOME) {
            s_home_anim_start = millis();
        }
        prev_section = s_section;

        // --- Alarm handling (checked BEFORE the panel-sleep guard so an alarm
        // can wake a sleeping screen). Overlay takes over unless the user
        // recently pressed a button (snooze window). ---
        uint8_t alarms = collect_alarms();
        bool snoozed = (int32_t)(s_alarm_snooze_until - millis()) > 0;
        if (alarms == 0) s_alarm_snooze_until = 0;   // reset when cleared
        if (alarms > 0 && !snoozed) {
            product_display_wake();          // light up the panel if it was off
            if (alarm_idx >= alarms) alarm_idx = 0;
            // Advance to the next alarm every ~2.5s when several are active.
            if (alarms > 1 && millis() - alarm_cycle_ms > 2500) {
                alarm_cycle_ms = millis();
                alarm_idx = (alarm_idx + 1) % alarms;
            }
            render_alarm(s_alarms[alarm_idx], alarm_idx, alarms);
            continue;
        }
        alarm_idx = 0;

        // No active alarm: respect the panel-sleep state (don't draw if asleep).
        if (!product_display_panel_on()) continue;

        render_current();     // only menu_task ever draws
    }
}

void menu_start() {
    s_section = SEC_HOME;
    s_index = 0;
    s_count = 0;
    s_detail = false;
    s_queue = xQueueCreate(8, sizeof(Button));
    xTaskCreate(menu_task, "menu", 4096, nullptr, 1, nullptr);
}
