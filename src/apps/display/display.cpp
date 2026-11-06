#include "display.h"
#include "ssd1306.h"
#include "logging.h"
#include "oled_bitmaps.h"
#include "mcs_logo.h"
#include "mcs_logo_vert.h"
#include "apps/probes/probes.h"
#include "apps/pumps/pumps.h"
#include "apps/service_ap/service_ap.h"
#include "qrcodegen.h"
#include "hardware/version.h"
#include "buzzer.h"

// Read-only access to Niklas' network state (same pattern as the OTA app:
// we only READ these, never modify his code).
namespace w5500 { extern bool connected; extern String mac; extern IPAddress local_ip; }
namespace wifi  { extern bool connected; extern String mac; extern IPAddress local_ip; }
// Set true by the network task once the WiFi/Ethernet stack is initialised.
extern bool network_initialized;
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

// How long the guide/product screen shows at boot.
static const uint32_t BOOT_HOLD_MS = 2500;
// How long the system-info screen shows at boot (after the guide screen).
static const uint32_t INFO_HOLD_MS = 2500;

// The OLED is a fixed display (no buttons, no inactivity sleep) — always on.
static bool s_panel_on = true;

// Forward declarations: these renderers live further down with the other
// screen bodies; the boot splash shows them before the carousel starts.
static void render_guide_name_body();
static void render_info();

void display_boot_splash() {
    // Boot sequence = guide/product screen, then the system-info screen. Both
    // are shown ONLY here at boot, not in the carousel: the guide name and the
    // FW/HW/MAC/IP are things a technician checks on startup, not info that
    // needs to keep cycling during normal operation.
    log_info("[display] showing guide splash");
    oled_set_offset(0, 0);
    oled_begin();
    render_guide_name_body();
    oled_end();
    vTaskDelay(pdMS_TO_TICKS(BOOT_HOLD_MS));

    log_info("[display] showing system info");
    oled_set_offset(0, 0);
    oled_begin();
    render_info();
    oled_end();
    // Play the boot jingle while the info screen is up. buzzer_sound_startup()
    // is blocking (~0.6s), so it also counts as part of the info hold; wait the
    // rest of INFO_HOLD_MS afterwards.
    uint32_t t0 = millis();
    buzzer_sound_startup();
    uint32_t spent = millis() - t0;
    if (spent < INFO_HOLD_MS) vTaskDelay(pdMS_TO_TICKS(INFO_HOLD_MS - spent));
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

// Draw the guide name screen body (vertical MCS logo down the left edge + the
// guide name filling the rest, auto-sized + word-wrapped up to 3 lines). No
// begin/end — the caller wraps it. This is the carousel HOME screen.
static void render_guide_name_body() {
    String name = guide_display_name();

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
}

// TANK screen for probe index `idx`. Shows name, reading, percent + an
// animated horizontal tank graphic.
static void render_tank(uint8_t idx) {
    JsonDocument doc;
    deserializeJson(doc, probes_get_all());
    JsonArray probes = doc["probes"].as<JsonArray>();
    uint8_t n = probes.size();

    if (n == 0) {
        oled_print_center(1, "TANKS");
        oled_print_center(3, "No tanks connected");
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
    // (No standalone ground line — as a lone horizontal line far below the
    // tank it read as a stray streak sweeping through during the slide.)

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
}

// PUMP screen for pump index `idx`.
static void render_pump(uint8_t idx) {
    JsonDocument doc;
    deserializeJson(doc, pumps_get_all());
    JsonArray pumps = doc["pumps"].as<JsonArray>();
    uint8_t n = pumps.size();

    if (n == 0) {
        oled_print_center(1, "PUMPS");
        oled_print_center(3, "No pumps connected");
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
}

// SERVICE screen: the WiFi-join QR for the service AP. A technician scans it
// with a phone camera to join the device's AP and reach the web UI — no app,
// no internet. The AP is auto-started at boot (no buttons to toggle it now).
static void __attribute__((unused)) render_service() {
    if (!service_ap::is_active()) {
        oled_draw_str_center(26, "Service AP");
        oled_draw_str_center(42, "starting...");
        return;
    }

    String payload = service_ap::qr_payload();
    const int kMaxVer = 6;
    uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(kMaxVer)];
    uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(kMaxVer)];
    bool ok = qrcodegen_encodeText(
        payload.c_str(), tmp, qr,
        qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, kMaxVer,
        qrcodegen_Mask_AUTO, true);

    if (ok && oled_draw_qr(qr)) {
        // QR drawn centered — maximise scannable area, nothing else.
    } else {
        // Fallback: show credentials as text if the QR won't fit.
        oled_draw_str_center(12, "Service AP");
        oled_draw_line(0, 16, 127, 16);
        char l[26];
        snprintf(l, sizeof(l), "%s", service_ap::ssid().c_str());
        oled_draw_str_center(30, l);
        snprintf(l, sizeof(l), "pw %s", service_ap::password().c_str());
        oled_draw_str_center(44, l);
        snprintf(l, sizeof(l), "%s", service_ap::ip().c_str());
        oled_draw_str_center(58, l);
    }
}

// Shown (carousel paused) while a phone is joined to the service AP: tells the
// user to open their browser at the device IP.
static void render_connected() {
    String ip = service_ap::ip();
    oled_draw_str_center(12, "Connected!");
    oled_draw_line(14, 16, 114, 16);
    oled_draw_str_center(30, "Open your browser at");
    // IP in the big bold font, centered, so it's easy to read/type.
    int w = oled_title_width(ip.c_str(), 14);
    oled_draw_title_at((128 - w) / 2, 50, ip.c_str(), 14);
}

// -----------------------------------------------------------------
// Alarm overlay (no input needed). When any probe is in a level
// alarm (low / overfill), this takes over the screen, blinks, and cycles
// through multiple alarms. Clears automatically when readings return to range.
// -----------------------------------------------------------------
struct AlarmItem { char name[20]; char kind[10]; int pct; };
static AlarmItem s_alarms[8];
static uint8_t   s_alarm_count = 0;

static uint8_t collect_alarms() {
    JsonDocument doc;
    deserializeJson(doc, probes_get_all());
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
    deserializeJson(doc, probes_get_all());
    return doc["probes"].as<JsonArray>().size();
}
static uint8_t count_pumps() {
    JsonDocument doc;
    deserializeJson(doc, pumps_get_all());
    return doc["pumps"].as<JsonArray>().size();
}

// Draw the screen body at virtual position `step` (NO begin/end — the caller
// wraps it, so two screens can be composited into one frame for the slide).
// Sequence: each TANK, each PUMP.
//
// NOT in the carousel (shown only at boot, see display_boot_splash):
//   * The guide/HOME screen.
//   * The system-info (FW/HW/MAC/IP) screen.
// Also not in the carousel: the SERVICE (WiFi-AP QR) screen — the service AP is
// disabled (WiFi softAP + BLE can't coexist on this board). render_service() /
// render_info() are kept for boot + if/when the AP comes back.
static void render_body(uint16_t step, uint8_t nTanks, uint8_t nPumps) {
    if (step < nTanks) { render_tank((uint8_t)step); return; }
    uint16_t s = step - nTanks;
    render_pump((uint8_t)s);
}

// Draw one screen in place (single frame, no slide).
static void render_step(uint16_t step, uint8_t nTanks, uint8_t nPumps) {
    oled_set_offset(0, 0);
    oled_begin();
    render_body(step, nTanks, nPumps);
    oled_end();
}

// Total number of screens in one full cycle (tanks + pumps).
static uint16_t step_count(uint8_t nTanks, uint8_t nPumps) {
    return nTanks + nPumps;
}

// Section id for a step: 1=TANKS, 2=PUMPS. Used to decide the slide direction:
// moving to a DIFFERENT section slides sideways (like switching menu items);
// moving within the TANKS/PUMPS group (sub-item to sub-item) slides UP from the
// bottom (like a sub-menu).
static uint8_t step_section(uint16_t step, uint8_t nTanks, uint8_t nPumps) {
    if (step < nTanks) return 1;                  // TANKS
    return 2;                                     // PUMPS
}

// -----------------------------------------------------------------
// Transition between carousel screens.
//
// TRANSITION_SLIDE: the outgoing screen slides out while the incoming screen
// slides in (both visible at once — a real push). Direction:
//   * different section  -> horizontal (new enters from the right)
//   * same section, next sub-item (tank->tank, pump->pump) -> vertical
//     (new enters from the bottom, like opening a sub-menu)
// After the slide the screen holds still for the dwell (readable).
//
// TRANSITION_NONE: instant swap — kept so it can be compared.
// -----------------------------------------------------------------
enum Transition : uint8_t { TRANSITION_NONE, TRANSITION_SLIDE };
static const Transition TRANSITION = TRANSITION_SLIDE;

static const int      SLIDE_FRAMES   = 14;   // frames from off-screen to settled
static const uint32_t SLIDE_FRAME_MS = 18;
static const int      SLIDE_GAP      = 12;   // black gutter between the two
                                             // screens while they slide

// Animate from `fromStep` to `toStep`: both are drawn into the same frame, the
// old one sliding out and the new one sliding in, along the chosen axis.
static void slide_to(uint16_t fromStep, uint16_t toStep,
                     uint8_t nTanks, uint8_t nPumps) {
    if (TRANSITION == TRANSITION_NONE) {
        render_step(toStep, nTanks, nPumps);
        return;
    }

    // Vertical if staying inside the same TANKS or PUMPS group; else horizontal.
    uint8_t secF = step_section(fromStep, nTanks, nPumps);
    uint8_t secT = step_section(toStep, nTanks, nPumps);
    bool vertical = (secF == secT) && (secF == 1 || secF == 2);
    const int span = vertical ? 64 : 128;       // panel height vs width
    const int travel = span + SLIDE_GAP;         // + gap so there's air between

    for (int f = 1; f <= SLIDE_FRAMES; f++) {
        float t = (float)f / SLIDE_FRAMES;
        float eased = 1.0f - (1.0f - t) * (1.0f - t);   // easeOut quad
        int shift = (int)(travel * eased + 0.5f);        // 0 -> travel

        oled_begin();
        if (vertical) {
            // Old slides up and out, new comes up from below a gap underneath.
            oled_set_offset(0, -shift);
            render_body(fromStep, nTanks, nPumps);
            oled_set_offset(0, travel - shift);
            render_body(toStep, nTanks, nPumps);
            // Mask the gap band (between old bottom and new top) so nothing
            // from either screen bleeds across the seam.
            oled_set_offset(0, 0);
            oled_set_draw_color(0);
            oled_draw_box(0, 64 - shift, 128, SLIDE_GAP);
            oled_set_draw_color(1);
        } else {
            // Old slides left and out, new enters from the right past a gap.
            oled_set_offset(-shift, 0);
            render_body(fromStep, nTanks, nPumps);
            oled_set_offset(travel - shift, 0);
            render_body(toStep, nTanks, nPumps);
            // Mask the gap column between the two screens.
            oled_set_offset(0, 0);
            oled_set_draw_color(0);
            oled_draw_box(128 - shift, 0, SLIDE_GAP, 64);
            oled_set_draw_color(1);
        }
        oled_end();
        vTaskDelay(pdMS_TO_TICKS(SLIDE_FRAME_MS));
    }
    oled_set_offset(0, 0);                       // settle exact
    render_step(toStep, nTanks, nPumps);
}

static void display_task(void*) {
    uint16_t step = 0;               // current carousel position
    uint32_t dwell_start = millis(); // when the current screen started
    uint8_t  alarm_idx = 0;
    uint32_t alarm_cycle_ms = 0;
    bool     ap_started = false;     // one-shot service-AP start
    uint32_t boot_ms = millis();

    for (;;) {
        // Start the service access point ~3s after boot (gives the WiFi radio
        // a moment to settle). We deliberately do NOT wait for
        // network_initialized: on Ethernet-only setups w5500::init() can block
        // on DHCP for a long time, and the AP only needs the WiFi radio, not
        // Ethernet. No button to toggle it anymore, so it auto-starts.
        //
        // DISABLED 2026-11-06: WiFi softAP + BLE cannot run concurrently on
        // this board (shared radio, no PSRAM) — softAP crashes in
        // ieee80211_hostap_attach when BT is up. Plan: driver/tank status moves
        // to a Nexus web page the QR points to, so the device only needs BLE.
        // Re-enable only if we switch to a BLE<->WiFi on-demand model.
        if (!ap_started && millis() - boot_ms > 3000) {
            // service_ap::start();
            ap_started = true;
        }

        // --- Alarm handling first: an alarm takes over regardless of the
        // carousel. It redraws fast (blink) and never advances the carousel. ---
        uint8_t alarms = collect_alarms();
        if (alarms > 0) {
            oled_set_offset(0, 0);   // ensure no leftover slide offset
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

        // --- Phone joined the service AP: pause the carousel and show a
        // "Connected! open your browser at <ip>" screen until they leave. ---
        if (service_ap::client_count() > 0) {
            oled_set_offset(0, 0);
            oled_begin();
            render_connected();
            oled_end();
            dwell_start = millis();   // resume carousel fresh once they leave
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        // --- Normal carousel: hold the current screen (redraw at ~4 fps for
        // live liquid animation), and when the dwell elapses, advance to the
        // next screen with a slide transition (old out + new in). ---
        uint8_t nTanks = count_tanks();
        uint8_t nPumps = count_pumps();
        uint16_t total = step_count(nTanks, nPumps);

        // Nothing to show yet (no tanks, no pumps). This also happens briefly
        // right after boot, before start_apps() has spawned the probe/pump
        // apps, so the counts read 0 for a moment. Just wait quietly without
        // redrawing — redrawing the guide screen here made it flash again just
        // before the carousel began. (Also avoids a divide-by-zero below.)
        if (total == 0) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if (step >= total) step = 0;

        if (millis() - dwell_start >= DWELL_MS) {
            uint16_t next = (step + 1) % total;
            slide_to(step, next, nTanks, nPumps);
            step = next;
            dwell_start = millis();
        } else {
            // Hold: redraw in place so tank liquid animates.
            render_step(step, nTanks, nPumps);
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
}

void display_start() {
    xTaskCreate(display_task, "display", 4096, nullptr, 1, nullptr);
}
