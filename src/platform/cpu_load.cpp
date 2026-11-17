#include "cpu_load.h"
#include <esp_freertos_hooks.h>
#include <esp_timer.h>

// Two idle-loop iterations closer than this = core was idle between them.
static const int64_t IDLE_GAP_US = 12;

struct CoreIdle {
    volatile int64_t last_us;   // timestamp of previous hook call
    volatile int64_t idle_us;   // accumulated idle time
};
static CoreIdle s_core[2];

static int64_t s_win_start_us = 0;
static int64_t s_win_idle0 = 0;
static int64_t s_win_idle1 = 0;

static inline bool IRAM_ATTR idle_tick(CoreIdle &c) {
    int64_t now = esp_timer_get_time();
    int64_t d = now - c.last_us;
    if (c.last_us != 0 && d < IDLE_GAP_US) c.idle_us += d;
    c.last_us = now;
    return true;
}

static bool idle_hook_core0() { return idle_tick(s_core[0]); }
static bool idle_hook_core1() { return idle_tick(s_core[1]); }

void cpu_load_init() {
    s_win_start_us = esp_timer_get_time();
    esp_register_freertos_idle_hook_for_cpu(idle_hook_core0, 0);
    esp_register_freertos_idle_hook_for_cpu(idle_hook_core1, 1);
}

void cpu_load_sample(uint8_t &core0_pct, uint8_t &core1_pct, uint32_t &window_ms) {
    int64_t now   = esp_timer_get_time();
    int64_t i0    = s_core[0].idle_us;
    int64_t i1    = s_core[1].idle_us;
    int64_t span  = now - s_win_start_us;
    if (span <= 0) { core0_pct = core1_pct = 0; window_ms = 0; return; }

    int64_t busy0 = span - (i0 - s_win_idle0);
    int64_t busy1 = span - (i1 - s_win_idle1);
    if (busy0 < 0) busy0 = 0;
    if (busy1 < 0) busy1 = 0;
    core0_pct = (uint8_t)((busy0 * 100) / span);
    core1_pct = (uint8_t)((busy1 * 100) / span);
    window_ms = (uint32_t)(span / 1000);

    s_win_start_us = now;
    s_win_idle0 = i0;
    s_win_idle1 = i1;
}
